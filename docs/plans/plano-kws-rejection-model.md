# Plano — KWS Rejection Model

**Data:** 2026-05-27
**Refs:** `.specs/features/kws-rejection-model/`

## Objetivo

Reduzir falsos positivos do KWS (MFCC+DTW) no ESP32 aplicando duas camadas de rejeição:
1. **Temporal variance gate** — descarta ruídos impulsivos antes do DTW
2. **Garbage model** — rejeita por razão de distância keyword/ruído

Pré-requisito: corrigir bug em `generate_templates.py:20` que seleciona outliers como templates.

## Resumo do pipeline com rejeição

```
PCM 16kHz
  → RMS gate (existente, VAD_RMS_THRESHOLD=300)
  → [NOVO] Temporal variance gate (σ_mean < 0.3 → rejeita)
  → mfcc_compute()
  → DTW vs. keyword templates
  → [NOVO] DTW vs. garbage templates
  → [NOVO] ratio = dtw_keyword / dtw_garbage < 0.75 → detecta
```

## Tarefas (ordem de execução)

| # | Tarefa | Arquivo | Pode ser paralelo |
|---|--------|---------|-------------------|
| T1 | Bug fix `select_templates` | `training/generate_templates.py:20` | Imediato |
| T2 | Coletar 20 amostras garbage | `training/samples/garbage_*.wav` | Imediato |
| T3 | Temporal variance gate | `firmware/main/poc-microfone.c` | Com T4 |
| T4 | Garbage decision logic | `firmware/main/poc-microfone.c` | Com T3 |
| T5 | Regenerar templates.h | Pipeline `extract + generate` | Após T1+T2 |
| T6 | Calibrar thresholds via monitor | `#define` no firmware | Após T3+T4+T5 |

## Arquivos modificados

- `training/generate_templates.py` — 1 linha + log
- `firmware/main/poc-microfone.c` — ~60 linhas novas (função + gate + decisão)
- `firmware/main/templates.h` — regenerado

## Critérios de sucesso

- Palma / batida na mesa → 0 detecções em 10 tentativas
- "ligar" claramente → ≥ 8 detecções em 10 tentativas
- Fala aleatória 1 minuto → ≤ 1 falso positivo

## Detalhes

Ver spec completo: `.specs/features/kws-rejection-model/spec.md`
Ver design: `.specs/features/kws-rejection-model/design.md`
Ver tasks: `.specs/features/kws-rejection-model/tasks.md`
