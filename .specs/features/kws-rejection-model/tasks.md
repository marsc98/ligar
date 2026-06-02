# KWS Rejection Model — Tasks

## Dependências

```
T1 ──────────────────────────────────────────────────────┐
                                                         │
T2 (manual) ──────────────────────────────────────────── T5 ── T6
                                                         │
T3 ──────────────────────────────────────────────────────┤
                                                         │
T4 ──────────────────────────────────────────────────────┘
```

T3 e T4 são paralelos.
T5 depende de T1 e T2 (amostras coletadas).
T6 depende de T3, T4 e T5.

---

## T1 — Bug fix: select_templates

**Escopo:** `training/generate_templates.py`
**Complexidade:** Pequena — 1 linha + log
**Depende de:** nada
**Bloqueia:** T5

### Passos

1. Abrir `generate_templates.py:20`
2. Substituir `idx = np.argsort(dists)[-n:]` por `idx = np.argsort(dists)[:n]`
3. Adicionar print de log após a seleção:
   ```python
   n_discarded = len(features) - len(idx)
   print(f'  {len(features)} amostras → {len(idx)} templates, {n_discarded} outliers descartados')
   ```
4. Rodar `python generate_templates.py --words ligar` com as amostras existentes e verificar que o log aparece sem erro

### Gate de verificação

- [ ] `np.argsort(dists)[:n]` está na linha 20
- [ ] Print de log aparece ao rodar o script
- [ ] Score DTW médio das amostras de validação é menor que com os templates antigos (opcional — verificar manualmente)

---

## T2 — Coleta de amostras garbage (ação manual)

**Escopo:** sistema de arquivos `training/samples/`
**Complexidade:** Manual — não é código
**Depende de:** nada
**Bloqueia:** T5

### Passos

Usando a interface web (`/record`) ou qualquer gravador de 16kHz mono 16-bit:

1. Gravar e salvar em `training/samples/` com naming `garbage_<n>.wav`:
   - `garbage_01.wav` a `garbage_05.wav` — batidas na mesa (variar intensidade)
   - `garbage_06.wav` a `garbage_10.wav` — palmas (variar intensidade e distância)
   - `garbage_11.wav` a `garbage_13.wav` — tosse
   - `garbage_14.wav` a `garbage_17.wav` — fala aleatória curta (~0.5s, não "ligar")
   - `garbage_18.wav` a `garbage_20.wav` — ruídos de ambiente (porta, cadeira, etc.)

2. Verificar que todos os WAVs têm duração ~0.5s e sample rate 16kHz
3. Rodar `python extract_features.py --word garbage` e verificar que 20 arquivos são processados com sucesso

### Gate de verificação

- [ ] `features/garbage.npy` existe com shape `(20, 48, 13)`
- [ ] Nenhum ERR no output do `extract_features.py`

---

## T3 — Temporal Variance Gate (firmware)

**Escopo:** `firmware/main/poc-microfone.c`
**Complexidade:** Média — nova função C + modificação da kws_task
**Depende de:** nada
**Paralelo com:** T4

### Passos

1. Adicionar `#define TEMPORAL_VAR_THRESHOLD 0.3f` próximo a `VAD_RMS_THRESHOLD` (linha ~64)

2. Adicionar função estática `compute_temporal_var()` antes de `kws_task()`:

```c
static float compute_temporal_var(const float *mfcc, int n_frames, int n_coefs) {
    float var_sum = 0.0f;
    for (int c = 0; c < n_coefs; c++) {
        float mean = 0.0f;
        for (int f = 0; f < n_frames; f++) mean += mfcc[f * n_coefs + c];
        mean /= n_frames;
        float sq_sum = 0.0f;
        for (int f = 0; f < n_frames; f++) {
            float d = mfcc[f * n_coefs + c] - mean;
            sq_sum += d * d;
        }
        var_sum += sqrtf(sq_sum / n_frames);
    }
    return var_sum / n_coefs;
}
```

3. Na `kws_task()`, após `mfcc_compute()` (linha ~499) e antes do loop DTW, inserir:

```c
float temporal_var = compute_temporal_var(g_mfcc_out, MFCC_N_FRAMES, MFCC_N_COEFS);

if (temporal_var < TEMPORAL_VAR_THRESHOLD) {
    // Telemetria de rejeição
    if (mon_fd >= 0) {
        char hb[128];
        snprintf(hb, sizeof(hb),
                 "{\"rms\":%.1f,\"threshold\":%.2f,\"word\":null,\"dists\":{},\"var\":%.3f,\"rejected\":\"var_gate\"}",
                 rms, g_dtw_threshold, temporal_var);
        ws_send_text(server, mon_fd, hb);
    }
    continue;
}
```

4. Estender o JSON de telemetria de detecção (no bloco `if (mon_fd >= 0)` final) para incluir `"var":%.3f`:

```c
// Modificar o snprintf existente para incluir temporal_var
int pos = snprintf(json, sizeof(json),
                   "{\"rms\":%.1f,\"threshold\":%.1f,\"var\":%.3f,\"word\":%s%s%s,\"dists\":{",
                   rms, g_dtw_threshold, temporal_var, ...);
```

### Gate de verificação

- [ ] Firmware compila sem warning
- [ ] Bater palma perto do microfone → JSON `"rejected":"var_gate"` aparece no monitor
- [ ] Dizer "ligar" → campo `"var"` > 0.3 no JSON de detecção

---

## T4 — Garbage Decision Logic (firmware)

**Escopo:** `firmware/main/poc-microfone.c`
**Complexidade:** Média — helper function + modificação da decisão
**Depende de:** nada (lógica é no-op se garbage_idx == -1)
**Paralelo com:** T3

### Passos

1. Adicionar `#define GARBAGE_RATIO_THRESHOLD 0.75f` próximo a `TEMPORAL_VAR_THRESHOLD`

2. Adicionar helper `find_word_idx()` antes de `kws_task()`:

```c
static int find_word_idx(const char *name) {
    for (int w = 0; w < KWS_N_WORDS; w++) {
        if (strcmp(KWS_WORDS[w].name, name) == 0) return w;
    }
    return -1;
}
```

3. No início de `kws_task()`, antes do `while(1)`, adicionar:

```c
const int garbage_idx = find_word_idx("garbage");
```

4. No loop DTW existente (linha ~505), substituir o bloco de decisão:

```c
// Cálculo de garbage_dist (após o loop de keywords)
float garbage_dist = 1e9f;
if (garbage_idx >= 0) {
    for (int t = 0; t < KWS_WORDS[garbage_idx].n_templates; t++) {
        float d = dtw_distance(g_mfcc_out, KWS_WORDS[garbage_idx].templates[t],
                               MFCC_N_FRAMES, MFCC_N_COEFS, DTW_WINDOW);
        if (d < garbage_dist) garbage_dist = d;
    }
}

// Decisão com ratio
bool below_threshold = (best_word >= 0 && best_word != garbage_idx && best_dist < g_dtw_threshold);
bool ratio_ok = true;
const char *reject_reason = NULL;
if (below_threshold && garbage_idx >= 0) {
    float ratio = best_dist / garbage_dist;
    ratio_ok = (ratio < GARBAGE_RATIO_THRESHOLD);
    if (!ratio_ok) reject_reason = "garbage_ratio";
}
bool detected = below_threshold && ratio_ok;
```

5. Adicionar `"garbage_dist"` e `"rejected"` ao JSON de telemetria quando aplicável:

```c
// No JSON de telemetria, após "dists", adicionar:
if (garbage_idx >= 0 && garbage_dist < 1e8f) {
    pos += snprintf(json + pos, sizeof(json) - pos, ",\"garbage_dist\":%.1f", garbage_dist);
}
if (reject_reason) {
    pos += snprintf(json + pos, sizeof(json) - pos, ",\"rejected\":\"%s\"", reject_reason);
}
```

6. **Importante:** O loop de cálculo de `word_best[]` (que itera sobre `KWS_N_WORDS`) já inclui o garbage como uma palavra. O `best_word` pode ser o garbage_idx se o garbage ganhar. A verificação `best_word != garbage_idx` no `below_threshold` já trata esse caso.

### Gate de verificação

- [ ] Firmware compila sem warning com e sem templates garbage
- [ ] Sem `garbage` em `templates.h`: comportamento idêntico ao atual
- [ ] Com `garbage` em `templates.h`: JSON de telemetria inclui `"garbage_dist"`

---

## T5 — Regenerar templates.h

**Escopo:** `training/` → `firmware/main/templates.h`
**Complexidade:** Operacional — rodar scripts
**Depende de:** T1 (bug fix) + T2 (amostras coletadas)

### Passos

```bash
cd training
python extract_features.py --word ligar
python extract_features.py --word garbage
python generate_templates.py --words ligar garbage
```

Verificar output:
- Print de amostras por palavra e outliers descartados
- `../firmware/main/templates.h` atualizado com timestamp recente

### Gate de verificação

- [ ] `templates.h` contém `kws_ligar_templates` e `kws_garbage_templates`
- [ ] `KWS_N_WORDS == 2`
- [ ] Log mostra outliers descartados (confirma bug fix ativo)

---

## T6 — Calibração de thresholds

**Escopo:** observação via monitor WebSocket + ajuste de `#define`
**Complexidade:** Iterativa — não é código novo
**Depende de:** T3 + T4 + T5 + flash do firmware

### Passos

1. Flashar firmware compilado com T3 + T4 e templates de T5
2. Conectar monitor WebSocket em `ws://<IP>/monitor`
3. Executar sequência de testes e anotar valores `"var"` e `"garbage_dist"`:

| Teste | Ação | Valores esperados |
|-------|------|-------------------|
| Var gate | Bater palma | `"var"` < 0.3, `"rejected":"var_gate"` |
| Var gate | Dizer "ligar" | `"var"` > 0.5 |
| Garbage ratio | Bater palma (se passar var) | `"garbage_ratio"` |
| Garbage ratio | Dizer "ligar" | `"word":"ligar"`, ratio < 0.75 |
| False negative | Dizer "ligar" suave | Deve detectar — ajustar threshold se não |

4. Se muitos falsos negativos: aumentar `TEMPORAL_VAR_THRESHOLD` (baixar de 0.3) ou aumentar `GARBAGE_RATIO_THRESHOLD` (subir de 0.75)
5. Se ainda há falsos positivos: diminuir `GARBAGE_RATIO_THRESHOLD` (apertar a razão)
6. Registrar valores finais calibrados nos `#define` e commitar

### Gate de verificação

- [ ] Bater palma 10x → 0 detecções
- [ ] Bater na mesa 10x → 0 detecções
- [ ] Dizer "ligar" 10x em distância normal → ≥ 8 detecções
- [ ] Fala aleatória 1 minuto → ≤ 1 detecção

---

## Checklist de entrega

- [ ] T1 — Bug fix commitado
- [ ] T2 — 20 amostras garbage coletadas e features extraídas
- [ ] T3 — Temporal variance gate no firmware
- [ ] T4 — Garbage decision logic no firmware
- [ ] T5 — templates.h regenerado com ligar + garbage
- [ ] T6 — Thresholds calibrados e documentados nos `#define`
- [ ] Todos os gates de verificação passados
