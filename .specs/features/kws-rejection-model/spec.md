# KWS Rejection Model — Especificação

## Problem Statement

O pipeline MFCC+DTW atual detecta palavras-chave mas não rejeita ativamente sons que *não são* palavras. Qualquer ruído com energia suficiente (batidas, tosse, fala aleatória) passa pelo VAD gate e tem DTW calculado contra os templates. Se por coincidência espectral o score cai abaixo do threshold (default 3.0), dispara falso positivo. Há também um bug crítico no seletor de templates que escolhe os piores exemplos em vez dos mais representativos.

## Proposed Solution

Duas camadas de rejeição em ordem de custo computacional crescente:

1. **Temporal variance gate** (firmware, antes do DTW): descarta frames com variação temporal insuficiente — ruídos impulsivos têm energia concentrada, palavras reais variam ao longo do tempo.
2. **Garbage model** (training + firmware): classe extra com ~20 amostras de ruído ambiente. O DTW contra garbage serve de piso de referência — a palavra-chave só é aceita se estiver significativamente mais próxima do keyword do que do garbage.

Pré-requisito: corrigir o bug de seleção de templates (`generate_templates.py`) que seleciona outliers em vez de amostras representativas.

## Goals

- [ ] Corrigir bug de seleção de templates em `training/generate_templates.py`
- [ ] Implementar temporal variance gate no firmware antes do DTW
- [ ] Criar pipeline de coleta e treinamento da classe `garbage`
- [ ] Implementar lógica de decisão com razão keyword/garbage no firmware
- [ ] Reduzir falsos positivos sem aumentar falsos negativos

## Out of Scope

| Feature | Razão |
|---------|-------|
| Background model probabilístico (GMM, HMM) | Complexidade de implementação em C no ESP32 |
| Neural network no device (TFLite Micro) | Requer hardware diferente (ESP32-S3) ou Edge Impulse |
| Score de confiança multi-palavra sofisticado | Deferred — depende de mais dados |
| Re-treinamento automático online | Fora do escopo da PoC |

---

## User Stories

### P0: Bug fix — seleção de templates representativos

**User Story:** Como desenvolvedor, quero que os templates gerados representem o centro da distribuição de amostras para que o DTW tenha referências fiéis ao padrão da palavra.

**Why P0:** Bug crítico — `[-n:]` seleciona as amostras *mais distantes* do centróide (outliers), fazendo o DTW ter referências ruins. Todos os demais melhoramentos ficam prejudicados sem essa correção.

**Acceptance Criteria:**

1. WHEN `select_templates` é chamado com `n < len(features)` THEN o sistema SHALL retornar as `n` amostras com *menor* distância média para as demais (mais próximas do centróide)
2. WHEN os templates são gerados com a correção THEN `np.argsort(dists)[:n]` SHALL ser usado em vez de `[-n:]`
3. WHEN `generate_templates.py` é executado THEN o output SHALL incluir log indicando quantas amostras foram descartadas como outliers

---

### P1: Temporal variance gate ⭐ MVP

**User Story:** Como sistema embarcado, quero rejeitar ruídos impulsivos antes de executar o DTW para economizar CPU e evitar falsos positivos de batidas e estampidos.

**Why P1:** Gate barato (O(N×M) sobre a matriz MFCC) que elimina a classe mais comum de falsos positivos sem nenhuma amostra de treino extra.

**Acceptance Criteria:**

1. WHEN a matriz MFCC `(N_FRAMES × N_COEFS)` é computada THEN o firmware SHALL calcular o desvio padrão médio dos coeficientes ao longo do tempo: `σ_mean = mean(std(mfcc, axis=frames))`
2. WHEN `σ_mean < TEMPORAL_VAR_THRESHOLD` THEN o firmware SHALL descartar o frame e NÃO executar DTW
3. WHEN `σ_mean >= TEMPORAL_VAR_THRESHOLD` THEN o firmware SHALL prosseguir para o DTW normalmente
4. WHEN o monitor WebSocket estiver conectado THEN o firmware SHALL incluir `"var"` no JSON de telemetria para calibração do threshold
5. WHEN o threshold é violado THEN o firmware SHALL enviar telemetria com `"rejected":"var_gate"` ao monitor

---

### P1: Pipeline de coleta garbage ⭐ MVP

**User Story:** Como desenvolvedor, quero coletar amostras de ruído rotuladas como `garbage` e incluí-las no pipeline de treinamento para que o firmware possa usá-las como referência de rejeição.

**Why P1:** Sem amostras garbage o modelo de rejeição por razão não funciona.

**Acceptance Criteria:**

1. WHEN o usuário roda `python extract_features.py --word garbage` THEN o sistema SHALL processar WAVs de ruído em `samples/garbage_*.wav` com o mesmo pipeline MFCC
2. WHEN `generate_templates.py --words ligar garbage` é executado THEN `templates.h` SHALL conter templates para ambas as classes
3. WHEN os WAVs de garbage são coletados THEN eles DEVEM incluir: batidas na mesa, palmas, tosse, fala aleatória, silêncio com ruído de fundo — mínimo 20 amostras

---

### P1: Lógica de decisão com razão keyword/garbage ⭐ MVP

**User Story:** Como sistema embarcado, quero rejeitar detecções onde o ruído está tão próximo dos templates quanto a palavra-chave para eliminar falsos positivos por coincidência espectral.

**Why P1:** Ponto central do garbage model — sem a razão de decisão o garbage é inútil.

**Acceptance Criteria:**

1. WHEN o DTW é executado THEN o firmware SHALL calcular `dtw_garbage = min(dtw(query, t) for t in garbage_templates)`
2. WHEN `dtw_keyword < threshold` AND `dtw_keyword / dtw_garbage < RATIO_THRESHOLD` THEN o firmware SHALL declarar detecção
3. WHEN `dtw_keyword < threshold` BUT `dtw_keyword / dtw_garbage >= RATIO_THRESHOLD` THEN o firmware SHALL rejeitar com `"rejected":"garbage_ratio"` na telemetria
4. WHEN o threshold de razão é configurável THEN o firmware SHALL expor `GARBAGE_RATIO_THRESHOLD` como `#define` em `mfcc.h` ou `dtw.h`
5. WHEN nenhum template `garbage` está presente em `templates.h` THEN o firmware SHALL pular a verificação de razão e manter comportamento atual

---

## Edge Cases

- WHEN todas as amostras de treino são similares (< n amostras únicas) THEN `select_templates` SHALL retornar todas sem erro
- WHEN o VAD gate passa mas o temporal variance gate rejeita THEN o cooldown de detecção NÃO SHALL ser ativado
- WHEN o garbage model rejeita uma detecção THEN o cooldown NÃO SHALL ser ativado (permite nova tentativa imediata)
- WHEN somente a palavra `garbage` existe em templates.h THEN o sistema SHALL não detectar nada

---

## Requirement Traceability

| Req ID | Story | Arquivo | Status |
|--------|-------|---------|--------|
| KWS-01 | Bug fix — seleção representativa | training/generate_templates.py:13-23 | Pending |
| KWS-02 | Temporal variance gate — cálculo σ_mean | firmware/main/mfcc.h + poc-microfone.c | Pending |
| KWS-03 | Temporal variance gate — reject path | firmware/main/poc-microfone.c:kws_task | Pending |
| KWS-04 | Temporal variance gate — telemetria | firmware/main/poc-microfone.c:kws_task | Pending |
| KWS-05 | Garbage pipeline — extract_features.py | training/extract_features.py | Pending |
| KWS-06 | Garbage pipeline — generate_templates.py | training/generate_templates.py | Pending |
| KWS-07 | Garbage decision — dtw_garbage | firmware/main/poc-microfone.c:kws_task | Pending |
| KWS-08 | Garbage decision — ratio check | firmware/main/poc-microfone.c:kws_task | Pending |
| KWS-09 | Garbage decision — telemetria ratio | firmware/main/poc-microfone.c:kws_task | Pending |
| KWS-10 | Garbage decision — fallback sem garbage | firmware/main/poc-microfone.c:kws_task | Pending |

---

## Success Criteria

- [ ] Bater palma → não detecta (temporal variance gate ou garbage ratio)
- [ ] Bater na mesa → não detecta
- [ ] Dizer "ligar" claramente → detecta com latência ≤ 600ms após o onset
- [ ] Fala aleatória em português → não detecta
- [ ] Taxa de falso positivo: < 1 por minuto em ambiente de escritório
- [ ] Templates gerados com correção do bug produzem score DTW menor que os templates antigos para amostras válidas
