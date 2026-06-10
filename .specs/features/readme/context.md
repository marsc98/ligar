# README — Decisões de Revisão

**Data:** 2026-06-10
**Escopo:** revisar o README para corrigir inconsistências com o estado atual do projeto e melhorar a clareza.
**Fonte:** discussão informal

---

## Decisões

### Pipeline KWS

- O diagrama "Pipeline KWS — kws_task" deve manter o mesmo nível de detalhe atual
- Substituir toda referência a DTW por MLP: `DTW vs TRIGGERS[]` → `mlp_infer()`, `DTW vs COLORS[]` → `mlp_infer()`
- O fluxo correto pós-VAD: `mfcc_compute()` → `mlp_infer()` → `best = argmax(probs)` → `valid = (probs[best] >= threshold) && (best != garbage_idx)`
- Remover referências a `garbage_ratio` — não existe no código atual
- MLP classifica 10 classes de uma vez (amarelo, azul, branco, desligar, garbage, laranja, ligar, roxo, verde, vermelho)
- O estado KWS_IDLE/KWS_AWAIT_COLOR continua igual — só a inferência mudou

### Payload /monitor

- Substituir exemplo de JSON: campo `"dists"` + `"garbage_dist"` não existem mais
- Novo formato correto:
  ```json
  {
    "rms": 1234.5,
    "threshold": 0.50,
    "var": 0.85,
    "word": "ligar",
    "probs": { "ligar": 0.92, "desligar": 0.03, "garbage": 0.01, "..." : "..." },
    "kws_mode": "idle"
  }
  ```
- `threshold` agora é probabilidade MLP [0,1], não distância DTW

### Makefile / Pipeline de treinamento

- `make train WORD=<palavra>` não existe — corrigir para `make extract WORD=<palavra>`
- Passo `train-mlp` estava ausente no README — adicionar entre `extract` e `train-templates`
- Remover `train-templates` da sequência do pipeline de treinamento no README (templates.h não é incluído em nenhum .c do firmware — legacy do DTW)
- No Makefile: remover `train-templates` dos targets `pipeline` e `pipeline-all`; manter o target disponível para uso manual
- Pipeline correto documentado:
  1. Coletar amostras (aba Coleta)
  2. `make extract WORD=<palavra>` → `features/<palavra>.npy`
  3. `make train-mlp` → `firmware/main/kws/weights.h`
  4. `make firmware-build` + `make firmware-flash`
  - Atalho: `make pipeline WORD=<palavra>` (após atualização do Makefile)

### Estrutura do projeto

- Seção `kws/`: adicionar `mlp.c/h` e `weights.h`; manter `templates.h` como "(AUTO-GERADO — legacy)"
- Seção `training/`: adicionar `train_mlp.py` e `firmware_mlp.py`

---

## Arbítrio do agente

- Redação do diagrama KWS atualizado (VAD gates → mfcc → mlp → state machine) — manter estilo ASCII atual

---

## Ideias Adiadas

- Nenhuma

---

## Questões em aberto

- Nenhuma
