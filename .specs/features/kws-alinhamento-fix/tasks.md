# KWS Alinhamento Fix — Tasks

**Spec**: `.specs/features/kws-alinhamento-fix/spec.md`
**Context**: `.specs/features/kws-alinhamento-fix/context.md`
**Status**: Draft

---

## Execution Plan

### Fase 1 — Código (paralelo)
```
T1 [P] ──┐
         ├──→ T3 → T4 (manual) → T5 → T6 (validação)
T2 [P] ──┘
```

### Fases 2–4 — Condicionais (só após T6 revelar problema)
```
T6 ──→ [falsos positivos não-vocais] → T7 → T8
     ──→ [distâncias inconsistentes] → T9 → T10+T11 [P] → T12
     ──→ [FAR > 5%]                  → T13 → T14
```

---

## Fase 1 — Obrigatória

### T1: Treinamento — alinhamento por offset [P]

**What**: Substituir detecção de onset por offset em `extract_mfcc` e corrigir Makefile.
**Where**:
- `training/extract_features.py` — função `extract_mfcc`
- `Makefile` — targets `train` e `train-templates`

**Depends on**: Nada
**Requirement**: KWS-04, KWS-05

**Mudanças em `extract_features.py`:**

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
        voiced_indices = np.where(voiced_mask)[0]
        offset = int(voiced_indices[-1]) if len(voiced_indices) > 0 else len(y_int16)

    # 50ms de contexto pós-palavra para corresponder ao firmware
    end   = min(len(y_int16), offset + SAMPLE_RATE // 20)
    start = max(0, end - N_SAMPLES)

    y_int16 = y_int16[start:end]
    if len(y_int16) < N_SAMPLES:
        # Pad no INÍCIO (silêncio pré-palavra)
        y_int16 = np.pad(y_int16, (N_SAMPLES - len(y_int16), 0))

    mfcc = fw_mfcc(y_int16, N_FRAMES)
    assert mfcc.shape == (N_FRAMES, N_COEFS), f"Shape inesperado: {mfcc.shape}"
    return mfcc
```

**Mudanças no `Makefile`:**
- `train`: corrigir para `python3 training/extract_features.py --word $(WORD)`
- `train-templates`: corrigir para `python3 training/generate_templates.py --words ligar garbage`
- `pipeline`: verificar se `make train WORD=ligar && make train WORD=garbage && make train-templates` encadeia corretamente

**Done when**:
- [ ] `extract_mfcc` usa offset (último sample > 5% pico) em vez de onset
- [ ] Pad é feito no início do array, não no fim
- [ ] `make train WORD=ligar` executa sem erro (requer amostras em `training/samples/`)
- [ ] `make train-templates` executa sem erro `--words not found`
- [ ] Gate: `python3 training/extract_features.py --word ligar` processa amostras existentes sem erro

**Gate**: `python3 training/extract_features.py --word ligar` (se houver amostras) / inspeção de código se não houver

---

### T2: Firmware — trigger na transição voz→silêncio [P]

**What**: Refatorar `kws_task` para disparar MFCC+DTW apenas na transição voz→silêncio, com guard de duração mínima e `continue` explícito pós-detecção.
**Where**: `firmware/main/poc-microfone.c`
- Defines no topo: `DTW_WINDOW`, `DTW_THRESHOLD_DEFAULT`
- Função `kws_task`

**Depends on**: Nada
**Requirement**: KWS-01, KWS-02, KWS-03, KWS-06, KWS-07

**Mudanças nos defines:**
```c
#define DTW_WINDOW            6      // era 12
#define DTW_THRESHOLD_DEFAULT 2.0f   // era 3.0f
```

**Mudanças em `kws_task` — substituir o bloco VAD atual pelo seguinte:**
```c
static bool  s_was_voiced   = false;
static float s_peak_rms     = 0.0f;
static int   s_voiced_chunks = 0;

// [...loop de leitura I2S e atualização do ring buffer permanece igual...]

float rms = compute_rms(chunk, n);
bool  is_voiced = (rms >= VAD_RMS_THRESHOLD);

if (is_voiced) {
    if (rms > s_peak_rms) s_peak_rms = rms;
    s_was_voiced = true;
    s_voiced_chunks++;
    continue;
}

// Transição voz→silêncio
if (s_was_voiced) {
    bool long_enough = (s_voiced_chunks >= 3);
    s_was_voiced   = false;
    s_peak_rms     = 0.0f;
    s_voiced_chunks = 0;

    if (!long_enough) continue;  // transiente curto (<96ms), ignorar

    TickType_t now = xTaskGetTickCount();
    if ((now - last_detection) < pdMS_TO_TICKS(DETECTION_COOLDOWN_MS)) continue;

    mfcc_compute(g_kws_ring, g_kws_ring_pos, MFCC_WIN_SAMPLES, g_mfcc_out);

    // [...bloco temporal_var, DTW, garbage_ratio, report permanece igual...]

    continue;  // não cair no heartbeat de silêncio
}

// Silêncio genuíno — heartbeat
TickType_t now_hb = xTaskGetTickCount();
if ((now_hb - last_heartbeat) >= pdMS_TO_TICKS(1000)) {
    last_heartbeat = now_hb;
    // [...enviar heartbeat ao monitor como antes...]
}
```

**Done when**:
- [ ] `s_was_voiced`, `s_peak_rms`, `s_voiced_chunks` declarados como `static` na função
- [ ] `is_voiced = true` → atualiza estado + `continue` (sem MFCC)
- [ ] Transição → guard de `s_voiced_chunks >= 3` antes de rodar pipeline
- [ ] `continue` explícito após o bloco de detecção (não cai no heartbeat)
- [ ] Heartbeat só envia em silêncio genuíno (sem transição recente)
- [ ] `DTW_WINDOW = 6`, `DTW_THRESHOLD_DEFAULT = 2.0f`
- [ ] Gate: `idf.py build` sem erros

**Gate**: `idf.py build`

---

### T3: Pipeline completo — coletar amostras, gerar templates, build, flash

**What**: Coletar amostras frescas, rodar pipeline de treinamento, flashar firmware.
**Where**: Hardware + comandos de terminal
**Depends on**: T1, T2
**Requirement**: KWS-08, KWS-09

**Passos em ordem:**

1. **Coletar amostras** via aba Coleta na UI (`make web-dev` + abrir browser + conectar):
   - ≥15 gravações de "ligar" → salvar em `training/samples/ligar_001.wav` … `ligar_015.wav`
   - ≥15 gravações de garbage (ruído, palmas, outras palavras) → `garbage_001.wav` … `garbage_015.wav`

2. **Treinar e flashar:**
   ```bash
   cd training
   python extract_features.py --word ligar
   python extract_features.py --word garbage
   python generate_templates.py --words ligar garbage
   cd ..
   idf.py build
   idf.py -p /dev/ttyUSB0 flash
   ```

**Done when**:
- [ ] `training/features/ligar.npy` existe com shape `(≥15, 48, 13)`
- [ ] `training/features/garbage.npy` existe com shape `(≥15, 48, 13)`
- [ ] `firmware/main/templates.h` tem timestamp posterior às mudanças de T1
- [ ] `idf.py build` compila sem erros
- [ ] Firmware flashado com sucesso

**Gate**: `idf.py build`

---

### T4: Validação Fase 1 (manual)

**What**: Verificar critério de sucesso da Fase 1 no hardware.
**Where**: Hardware + monitor serial / WebSocket monitor
**Depends on**: T3
**Requirement**: KWS-01 a KWS-09

**Procedimento:**
1. `make firmware-monitor` (ou monitor WebSocket)
2. Falar "ligar" claramente 5 vezes com intervalo de 2s
3. Verificar: `word: "ligar"` aparece ≥3 vezes
4. Aguardar 30s em silêncio + fazer sons aleatórios (bater mesa, palmas)
5. Verificar: nenhuma detecção espúria

**Done when**:
- [ ] ≥3 em 5 tentativas de "ligar" detectadas
- [ ] 0 falsos positivos em 30s de observação

**Gate**: Observação direta no monitor

---

## Fase 2 — Gate ZCR (condicional)

> **Ativar somente se** T4 revelar falsos positivos com sons não-vocais (batidas, palmas).

### T5: Firmware — ZCR gate com último chunk voiced

**What**: Adicionar buffer `s_last_voiced_chunk` + gate ZCR na transição voz→silêncio.
**Where**: `firmware/main/poc-microfone.c` — `kws_task`
**Depends on**: T4 (falhou com falsos positivos não-vocais)
**Requirement**: KWS-10, KWS-11

**Adições ao estado estático de `kws_task`:**
```c
static int16_t s_last_voiced_chunk[I2S_READ_CHUNK];
static size_t  s_last_voiced_n = 0;
```

**No bloco `if (is_voiced)`** — adicionar antes do `continue`:
```c
memcpy(s_last_voiced_chunk, chunk, n * sizeof(int16_t));
s_last_voiced_n = n;
```

**No bloco de transição** — antes de `mfcc_compute`:
```c
#define ZCR_MAX_THRESHOLD 0.15f
float zcr = 0.0f;
for (size_t i = 1; i < s_last_voiced_n; i++)
    zcr += (float)((s_last_voiced_chunk[i] >= 0) != (s_last_voiced_chunk[i-1] >= 0));
zcr /= (float)s_last_voiced_n;
if (zcr > ZCR_MAX_THRESHOLD) {
    // enviar rejected: zcr_gate ao monitor
    continue;
}
```

**Done when**:
- [ ] `s_last_voiced_chunk` copiado a cada chunk voiced
- [ ] ZCR calculado sobre `s_last_voiced_chunk` (não sobre chunk silencioso)
- [ ] ZCR > 0.15 → `continue` + log `rejected: zcr_gate`
- [ ] Palmas → log mostra `rejected: zcr_gate`
- [ ] "ligar" → ZCR ≤ 0.15, pipeline continua
- [ ] Gate: `idf.py build`

**Gate**: `idf.py build` + validação manual

---

### T6: Build + flash com ZCR gate

**What**: Compilar e flashar firmware com gate ZCR.
**Where**: Hardware
**Depends on**: T5

**Done when**:
- [ ] `idf.py build && idf.py -p /dev/ttyUSB0 flash` sem erros
- [ ] Falsos positivos não-vocais eliminados, "ligar" ainda detectado

---

## Fase 3 — CMVN Global (condicional)

> **Ativar somente se** T4 revelar distâncias DTW inconsistentes entre repetições da mesma palavra (variação > 20% entre gravações similares).

### T7: Treinamento — calcular e emitir CMVN global

**What**: Adicionar cálculo de CMVN global em `generate_templates.py` e emitir arrays C em `templates.h`.
**Where**: `training/generate_templates.py`
**Depends on**: T4 (distâncias inconsistentes)
**Requirement**: KWS-12

**Adições após carregar todos os features:**
```python
all_feat = np.concatenate([features_by_word[w] for w in word_defs], axis=0)
flat = all_feat.reshape(-1, N_COEFS)
global_mean = flat.mean(axis=0)
global_std  = flat.std(axis=0) + 1e-8

# Emitir em templates.h:
# static const float KWS_CMVN_MEAN[13] = { ... };
# static const float KWS_CMVN_STD[13]  = { ... };
```

**Done when**:
- [ ] `global_mean` e `global_std` calculados sobre ligar + garbage
- [ ] `KWS_CMVN_MEAN[13]` e `KWS_CMVN_STD[13]` emitidos em `templates.h`
- [ ] Fallback `1e-8` presente no std para evitar divisão por zero
- [ ] Gate: `python generate_templates.py --words ligar garbage` sem erros

---

### T8 + T9: Aplicar CMVN global (firmware + treinamento) [P]

> Estas duas tasks podem ser executadas em paralelo — tocam arquivos diferentes.

**T8 — Firmware (`mfcc.c`):**
- Substituir bloco CMVN local (linhas 141–161) por aplicação de `KWS_CMVN_MEAN` / `KWS_CMVN_STD` importados de `templates.h`
- Requirement: KWS-13

**T9 — Treinamento (`firmware_mfcc.py`):**
- Substituir `out = (out - mean) / std` no final de `extract` pelo CMVN global
- O caller (`extract_features.py`) deve passar `global_mean` / `global_std` importados de arquivo compartilhado ou calculados inline
- Requirement: KWS-12

**Done when (T8)**:
- [ ] `extern const float KWS_CMVN_MEAN[MFCC_N_COEFS]` e `KWS_CMVN_STD` declarados
- [ ] CMVN local removido das linhas 141–161 de `mfcc.c`
- [ ] Gate: `idf.py build`

**Done when (T9)**:
- [ ] `fw_mfcc` aplica o mesmo CMVN global usado no firmware
- [ ] Gate: `python extract_features.py --word ligar` sem erros

---

### T10: Regravar templates + build + flash (pós CMVN global)

**What**: Regravar templates com CMVN global aplicado e flashar.
**Depends on**: T7, T8, T9

**Done when**:
- [ ] `make train WORD=ligar && make train WORD=garbage && make train-templates` sem erros
- [ ] `idf.py build && idf.py -p /dev/ttyUSB0 flash` sem erros
- [ ] Distâncias DTW para mesma palavra variam < 20% entre repetições

---

## Fase 4 — Delta-MFCC (condicional)

> **Ativar somente se** FAR > 5% após Fases 1–3 (mais de 1 falso positivo em 20 tentativas de sons não-palavras).

### T11: Delta-MFCC — expandir features para 26 coeficientes

**What**: Adicionar derivada temporal (Δ) aos 13 coeficientes MFCC em todos os layers.
**Where**:
- `firmware/main/mfcc.h` — `MFCC_N_COEFS` 13 → 26
- `firmware/main/mfcc.c` — calcular Δ e concatenar
- `training/firmware_mfcc.py` — idem no Python
- `training/extract_features.py` — sem mudança (usa N_COEFS via import)
- `training/generate_templates.py` — sem mudança (usa N_COEFS via import)
- `firmware/main/poc-microfone.c` — `g_mfcc_out` buffer dobra: `MFCC_N_FRAMES * MFCC_N_COEFS * 2`

**Depends on**: T4 (FAR > 5%)
**Requirement**: KWS-14

**Done when**:
- [ ] `MFCC_N_COEFS = 26` em `mfcc.h`
- [ ] Δ calculado: `delta[f][c] = (mfcc[f+1][c] - mfcc[f-1][c]) / 2` (bordas com vizinho único)
- [ ] Output concatenado: `[mfcc_row, delta_row]` por frame
- [ ] `firmware_mfcc.py` produz shape `(N_FRAMES, 26)`
- [ ] `esp_get_free_heap_size()` no boot ≥ 80KB após ativação
- [ ] DTW ainda completa em < 10ms (verificar com log de timestamp)
- [ ] Gate: `idf.py build`

---

### T12: Regravar templates Delta-MFCC + build + flash

**What**: Regravar templates com 26 coefs e flashar.
**Depends on**: T11

**Done when**:
- [ ] Templates gerados com shape correto para 26 coefs
- [ ] `idf.py build && idf.py -p /dev/ttyUSB0 flash` sem erros
- [ ] FAR ≤ 5%

---

## Requirement Traceability Update

| Req ID | Task | Status |
| ------ | ---- | ------ |
| KWS-01 | T2   | Pending |
| KWS-02 | T2   | Pending |
| KWS-03 | T2   | Pending |
| KWS-04 | T1   | Pending |
| KWS-05 | T1   | Pending |
| KWS-06 | T2   | Pending |
| KWS-07 | T2   | Pending |
| KWS-08 | T3   | Pending |
| KWS-09 | T3   | Pending |
| KWS-10 | T5   | Pending (cond.) |
| KWS-11 | T5   | Pending (cond.) |
| KWS-12 | T7, T9 | Pending (cond.) |
| KWS-13 | T8   | Pending (cond.) |
| KWS-14 | T11  | Pending (cond.) |
