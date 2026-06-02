# KWS Rejection Model — Design

## Visão Geral

Dois planos de mudança independentes que se compõem:

```
┌──────────────────────────────────────────────────────────┐
│                    KWS Task (firmware)                    │
│                                                          │
│  I2S chunk → ring buffer → RMS gate                      │
│                                 │                        │
│                                 ▼                        │
│                         [NOVO] Temporal Variance Gate    │
│                           σ_mean < threshold?            │
│                           → rejeita (JSON "var_gate")    │
│                                 │                        │
│                                 ▼                        │
│                          mfcc_compute()                  │
│                                 │                        │
│                                 ▼                        │
│                    DTW vs. keyword templates             │
│                    [NOVO] DTW vs. garbage templates      │
│                                 │                        │
│                                 ▼                        │
│                    [NOVO] Razão keyword/garbage          │
│                    ratio < RATIO_THRESHOLD?              │
│                    → detecta                             │
│                    → ou rejeita (JSON "garbage_ratio")   │
└──────────────────────────────────────────────────────────┘
```

---

## Componente 1 — Bug fix: select_templates

**Arquivo:** `training/generate_templates.py:13-23`

**Problema:** `np.argsort(dists)[-n:]` retorna os índices de maior distância média — os outliers mais afastados do centróide.

**Correção:**

```python
# ANTES (seleciona outliers)
idx = np.argsort(dists)[-n:]

# DEPOIS (seleciona os mais representativos — menor distância média)
idx = np.argsort(dists)[:n]
```

**Log adicional:**

```python
n_total = len(features)
n_discarded = n_total - len(idx)
print(f'  {n_total} amostras → {len(idx)} templates selecionados, {n_discarded} outliers descartados')
```

Sem impacto em outras funções. Sem mudança de interface.

---

## Componente 2 — Temporal Variance Gate

**Arquivos afetados:**
- `firmware/main/poc-microfone.c` — lógica do gate na `kws_task`
- Constante `TEMPORAL_VAR_THRESHOLD` adicionada como `#define` próximo aos demais thresholds

**Matemática:**

```
Para cada coeficiente c ∈ [0, N_COEFS):
  σ[c] = std(mfcc[:, c])       // desvio padrão ao longo dos 48 frames

σ_mean = mean(σ[0..N_COEFS-1])

Se σ_mean < TEMPORAL_VAR_THRESHOLD → rejeita
```

**Valor inicial do threshold:** `0.3`
- Palavras reais têm σ_mean típico ≥ 0.6–1.2 (após z-score, os coeficientes têm escala ~1.0)
- Batidas impulsivas: σ_mean próximo a 0 (poucos frames com energia, resto zerado)
- Calibrar via telemetria `/monitor` antes de fixar

**Implementação no firmware (C):**

```c
// Após mfcc_compute(), antes do DTW
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

**Nota:** O MFCC já é z-score normalizado por `mfcc_compute()`. O desvio padrão pós-normalização tende a 1.0 por coeficiente. Um σ_mean < 0.3 indica que a maioria dos frames está próxima da média — sinal impulsivo ou silêncio com pico isolado.

**JSON de telemetria (extensão do existente):**

```json
// Antes — sem rejeição:
{"rms":450.0,"threshold":3.0,"word":"ligar","dists":{"ligar":2.1}}

// Com var gate — rejeitado:
{"rms":520.0,"threshold":3.0,"word":null,"dists":{},"var":0.12,"rejected":"var_gate"}

// Com var gate — passou:
{"rms":520.0,"threshold":3.0,"word":"ligar","dists":{"ligar":2.1,"garbage":3.8},"var":0.85}
```

---

## Componente 3 — Garbage Model

### 3a. Pipeline de treinamento

O pipeline existente já suporta múltiplas palavras. A classe `garbage` é tratada identicamente a qualquer palavra-chave no `extract_features.py` e `generate_templates.py`.

**Nenhuma mudança de código necessária no pipeline de treinamento** — basta coletar amostras e rodar:

```bash
python extract_features.py --word garbage
python generate_templates.py --words ligar garbage
```

**Amostras a coletar (`samples/garbage_*.wav`):**
- 5x batida na mesa
- 5x palma
- 3x tosse
- 4x fala aleatória (frases curtas que não sejam a palavra-chave)
- 3x ruído ambiente com pico (cadeira arrastando, porta fechando)

Total: ~20 amostras de 0.5s cada.

### 3b. Detecção de classe garbage em templates.h

O `templates.h` gerado já expõe `KWS_WORDS[]` com `name`, `templates`, `n_templates`. Se a coleta incluiu `garbage`, a struct já contém o índice da classe.

No firmware, a presença de garbage é detectada dinamicamente:

```c
static int find_garbage_idx(void) {
    for (int w = 0; w < KWS_N_WORDS; w++) {
        if (strcmp(KWS_WORDS[w].name, "garbage") == 0) return w;
    }
    return -1;  // garbage não presente → skip ratio check
}
```

Chamado uma vez durante init da `kws_task` e armazenado em variável local.

### 3c. Lógica de decisão com razão

**Fluxo atual:**

```c
bool detected = (best_word >= 0 && best_dist < g_dtw_threshold);
```

**Fluxo novo:**

```c
// Calcula DTW contra garbage (se presente)
float garbage_dist = 1e9f;
if (garbage_idx >= 0) {
    for (int t = 0; t < KWS_WORDS[garbage_idx].n_templates; t++) {
        float d = dtw_distance(g_mfcc_out, KWS_WORDS[garbage_idx].templates[t],
                               MFCC_N_FRAMES, MFCC_N_COEFS, DTW_WINDOW);
        if (d < garbage_dist) garbage_dist = d;
    }
}

// Decisão
bool below_threshold = (best_word >= 0 && best_word != garbage_idx && best_dist < g_dtw_threshold);
bool ratio_ok = true;
if (below_threshold && garbage_idx >= 0) {
    float ratio = best_dist / garbage_dist;
    ratio_ok = (ratio < GARBAGE_RATIO_THRESHOLD);
}
bool detected = below_threshold && ratio_ok;
```

**Valor inicial de `GARBAGE_RATIO_THRESHOLD`:** `0.75`
- `ratio < 0.75` significa: keyword está ≥25% mais próxima que garbage
- Calibrar após coleta real das amostras

**Constantes novas em `poc-microfone.c` (junto aos demais `#define`):**

```c
#define TEMPORAL_VAR_THRESHOLD  0.3f
#define GARBAGE_RATIO_THRESHOLD 0.75f
```

---

## Impacto em RAM/CPU

| Item | Impacto |
|------|---------|
| Temporal variance gate | +O(N_FRAMES×N_COEFS) floats na stack da kws_task — ~2.5KB adicionais (48×13×4 bytes) — dentro do stack de 8192 bytes |
| Garbage templates (10 templates) | +10 × 48 × 13 × 4 bytes = **~25KB em flash** (não RAM) |
| DTW extra por garbage | +1 loop DTW por ciclo de detecção — custo idêntico a uma palavra extra |
| find_garbage_idx() | Chamada única no init — custo desprezível |

Flash é abundante no ESP32 (~4MB); RAM não é afetada pelos templates (ficam em flash).

---

## Sequência de implementação

```
1. [T1] Corrigir bug generate_templates.py
2. [T2] Coletar amostras garbage (ação manual do usuário)
3. [T3] Adicionar temporal variance gate no firmware
4. [T4] Adicionar lógica garbage no firmware
5. [T5] Regenerar templates.h com garbage + correção do bug
6. [T6] Calibrar TEMPORAL_VAR_THRESHOLD e GARBAGE_RATIO_THRESHOLD via monitor
```

T3 e T4 são independentes — podem ser implementados em paralelo.
T5 depende de T1 + T2.
T6 depende de T3 + T4 + T5.

---

## Arquivos modificados

| Arquivo | Tipo de mudança |
|---------|-----------------|
| `training/generate_templates.py` | Bug fix — linha 20 + log |
| `firmware/main/poc-microfone.c` | Temporal variance gate + garbage decision logic |
| `firmware/main/templates.h` | Regenerado (output do pipeline) |

Nenhum novo arquivo de código necessário. `mfcc.c/h` e `dtw.c/h` não são alterados.
