# Plano de Implementação — Fix KWS Alinhamento Temporal

**Data:** 2026-05-27  
**Base:** `analise-matematica-kws.md`  
**Objetivo:** fazer o pipeline MFCC+DTW produzir as primeiras detecções reais de "ligar"

---

## Contexto

O pipeline atual não detecta nenhuma palavra porque treinamento e inferência usam alinhamentos incompatíveis. O fix principal é mudar o momento em que o MFCC é computado (inferência) e a referência de alinhamento (treinamento). Sem esse fix, qualquer ajuste de threshold ou garbage ratio é ineficaz.

---

## Fase 1 — Alinhamento por Offset (obrigatório)

### Tarefa 1.1 — Firmware: computar MFCC na transição voz→silêncio

**Arquivo:** `firmware/main/poc-microfone.c`  
**Função:** `kws_task`

**Antes:** MFCC é computado a cada chunk enquanto `rms >= VAD_RMS_THRESHOLD`.

**Depois:** MFCC é computado UMA VEZ quando `rms` cai abaixo do threshold (transição voz→silêncio), garantindo que a palavra esteja no final do ring buffer.

```c
// Estado extra na kws_task:
static bool  s_was_voiced  = false;
static float s_peak_rms    = 0.0f;

bool is_voiced = (rms >= VAD_RMS_THRESHOLD);

if (is_voiced) {
    if (rms > s_peak_rms) s_peak_rms = rms;
    s_was_voiced = true;
    // heartbeat se necessário
    continue;
}

// Transição voz→silêncio: palavra acabou de terminar
if (s_was_voiced) {
    s_was_voiced = false;
    float peak = s_peak_rms;
    s_peak_rms = 0.0f;

    // Cooldown
    TickType_t now = xTaskGetTickCount();
    if ((now - last_detection) < pdMS_TO_TICKS(DETECTION_COOLDOWN_MS)) continue;

    // Computar MFCC — palavra está no final do ring buffer
    mfcc_compute(g_kws_ring, g_kws_ring_pos, MFCC_WIN_SAMPLES, g_mfcc_out);

    // ... resto do pipeline (temporal_var, DTW, garbage_ratio) ...
}
```

**Observações:**
- Remover o bloco de heartbeat que enviava RMS enquanto `rms < VAD_RMS_THRESHOLD` — o heartbeat deve continuar funcionando para silêncio real (sem transição recente).
- O `DETECTION_COOLDOWN_MS` ainda se aplica para evitar dupla detecção de um mesmo evento.

---

### Tarefa 1.2 — Treinamento: alinhar pelo offset da palavra

**Arquivo:** `training/extract_features.py`  
**Função:** `extract_mfcc`

**Antes:** alinha pelo onset (início da palavra no frame ≈12).

**Depois:** alinha pelo offset (fim da palavra no final da janela) para corresponder ao comportamento do firmware.

```python
def extract_mfcc(wav_path: Path) -> np.ndarray:
    y, sr = librosa.load(str(wav_path), sr=SAMPLE_RATE, mono=True)
    y_int16 = (y * 32767).clip(-32768, 32767).astype(np.int16)

    abs_y = np.abs(y_int16).astype(np.float32)
    peak  = abs_y.max()

    if peak == 0:
        offset = len(y_int16)
    else:
        threshold = peak * 0.05
        voiced_mask = abs_y > threshold
        # Último sample acima do threshold
        voiced_indices = np.where(voiced_mask)[0]
        offset = int(voiced_indices[-1]) if len(voiced_indices) > 0 else len(y_int16)

    # Pequeno contexto pós-palavra (50ms) para corresponder ao firmware
    end   = min(len(y_int16), offset + SAMPLE_RATE // 20)
    start = max(0, end - N_SAMPLES)

    y_int16 = y_int16[start:end]
    if len(y_int16) < N_SAMPLES:
        # Pad no início (silêncio pré-palavra)
        y_int16 = np.pad(y_int16, (N_SAMPLES - len(y_int16), 0))

    mfcc = fw_mfcc(y_int16, N_FRAMES)
    assert mfcc.shape == (N_FRAMES, N_COEFS), f"Shape inesperado: {mfcc.shape}"
    return mfcc
```

**Observação:** o pad é no início (silêncio antes da palavra), correspondendo ao que o firmware tem no ring buffer quando a palavra acaba de terminar.

---

### Tarefa 1.3 — Ajuste de parâmetros pós-alinhamento

Com alinhamento correto, o DTW window pode ser reduzido (não precisa compensar deslocamentos grandes):

**Arquivo:** `firmware/main/poc-microfone.c`

```c
// Antes
#define DTW_WINDOW 12

// Depois (apertado — alinhamento agora é consistente)
#define DTW_WINDOW 6
```

O threshold DTW precisa ser recalibrado após regerar os templates. Começar com `DTW_THRESHOLD_DEFAULT = 2.0f` e ajustar via UI.

---

### Tarefa 1.4 — Regravar amostras e regravar templates

Após as mudanças acima, os templates atuais são inválidos (foram gerados com alinhamento por onset). Necessário:

1. Coletar 15+ amostras de "ligar" via aba Coleta na UI
2. Coletar 15+ amostras de "garbage" (ruído, palmas, outras palavras)
3. Rodar pipeline completo:

```bash
make train WORD=ligar
make train WORD=garbage
make train-templates
make firmware-build
make firmware-flash PORT=/dev/ttyUSB0
```

---

### Critério de sucesso da Fase 1

Abrir monitor e falar "ligar" claramente:
- `word: "ligar"` aparece no log pelo menos 3 vezes em 5 tentativas
- Sons aleatórios (bater mesa, falar outra palavra) não disparam detecção

---

## Fase 2 — Gate de Variância Temporal Funcional

**Condição:** executar somente se Fase 1 gerar falsos positivos com sons não-vocais (batidas, palmas).

### Tarefa 2.1 — Substituir gate de var por ZCR

**Arquivo:** `firmware/main/poc-microfone.c`  
**Ponto de inserção:** dentro do bloco de transição voz→silêncio, antes de `mfcc_compute`.

```c
// ZCR do chunk que triggerou a transição
// Voz: ZCR ≈ 0.01–0.10; impactos/ruído: ZCR > 0.15
#define ZCR_MAX_THRESHOLD 0.15f

float zcr = 0.0f;
for (size_t i = 1; i < n; i++)
    zcr += (float)((chunk[i] >= 0) != (chunk[i-1] >= 0));
zcr /= (float)n;

if (zcr > ZCR_MAX_THRESHOLD) {
    // Envia evento de rejeição ao monitor
    continue;
}
```

O campo `var` no JSON do monitor pode passar a reportar o ZCR para facilitar calibração.

---

## Fase 3 — CMVN Global (opcional)

**Condição:** executar somente se Fase 1 mostrar distâncias inconsistentes entre repetições da mesma palavra.

### Tarefa 3.1 — Calcular e emitir CMVN global no treinamento

**Arquivo:** `training/generate_templates.py`

Após carregar todos os features:
```python
# Concatenar todos os features (ligar + garbage)
all_feat = np.concatenate([features_by_word[w] for w in word_defs], axis=0)
# shape: (N_total_samples, N_FRAMES, N_COEFS)
flat = all_feat.reshape(-1, N_COEFS)
global_mean = flat.mean(axis=0)
global_std  = flat.std(axis=0) + 1e-8
```

Emitir em `templates.h`:
```c
static const float KWS_CMVN_MEAN[13] = { ... };
static const float KWS_CMVN_STD[13]  = { ... };
```

### Tarefa 3.2 — Aplicar CMVN global no firmware

**Arquivo:** `firmware/main/mfcc.c`

Substituir o bloco CMVN local (linhas 141–161) por:
```c
extern const float KWS_CMVN_MEAN[MFCC_N_COEFS];
extern const float KWS_CMVN_STD[MFCC_N_COEFS];

for (int f = 0; f < MFCC_N_FRAMES; f++)
    for (int c = 0; c < MFCC_N_COEFS; c++)
        out[f * MFCC_N_COEFS + c] =
            (out[f * MFCC_N_COEFS + c] - KWS_CMVN_MEAN[c]) / KWS_CMVN_STD[c];
```

**Custo:** 26 floats = 104 bytes.

### Tarefa 3.3 — Aplicar mesmo CMVN global no treinamento

**Arquivo:** `training/firmware_mfcc.py`

Substituir o CMVN local no final de `extract`:
```python
out = (out - global_mean) / global_std  # importar de arquivo compartilhado
```

---

## Fase 4 — Delta-MFCC (melhoria de discriminação)

**Condição:** executar somente se Fases 1-3 não atingirem FAR suficientemente baixo (>5% de falsos positivos).

### Resumo

- Expandir feature de 13 → 26 coeficientes (MFCC + Δ)
- Memória: +30KB (5 templates × 2 palavras × 48 × 13 × 4B)
- CPU: 2× mais lento no DTW (ainda <10ms)
- Exige regerar templates e recalibrar threshold

Ver detalhes em `analise-matematica-kws.md`.

---

## Ordem de Execução

```
[ ] Tarefa 1.1 — firmware: MFCC na transição voz→silêncio
[ ] Tarefa 1.2 — treinamento: alinhamento por offset
[ ] Tarefa 1.3 — reduzir DTW_WINDOW para 6
[ ] Tarefa 1.4 — regravar amostras + templates + flash

[ ] Validação Fase 1: 3/5 detecções corretas, <1 falso positivo em 30s

[ ] (condicional) Tarefa 2.1 — gate ZCR
[ ] (condicional) Tarefa 3.1–3.3 — CMVN global
[ ] (condicional) Fase 4 — Delta-MFCC
```
