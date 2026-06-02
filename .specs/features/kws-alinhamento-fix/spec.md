# KWS Alinhamento Fix — Especificação

## Problem Statement

O pipeline MFCC+DTW não produz nenhuma detecção real de "ligar" porque treinamento e inferência usam alinhamentos temporais incompatíveis: o treinamento alinha pelo onset (início da palavra) enquanto o firmware computa o MFCC a qualquer momento durante a voz. Sem corrigir esse desalinhamento fundamental, ajustes de threshold ou garbage ratio são ineficazes.

## Proposed Solution

Mudar o momento de disparo do MFCC no firmware para a transição voz→silêncio (garantindo que a palavra esteja no final do ring buffer) e mudar o alinhamento do treinamento para offset (fim da palavra), tornando ambos compatíveis. Nas fases condicionais, adicionar gates de qualidade (ZCR, CMVN global, Delta-MFCC) se os falsos positivos ou distâncias inconsistentes persistirem após a Fase 1.

## Goals

- [ ] "ligar" detectado ≥3 em 5 tentativas após a Fase 1
- [ ] Sons aleatórios (batida, palmas, outras palavras) não disparam detecção em 30s de observação
- [ ] Fases 2–4 são condicionais: só executadas se os critérios de falha específicos aparecerem

## Out of Scope

| Feature | Razão |
| ------- | ----- |
| Novas palavras além de "ligar" | Escopo é fazer "ligar" funcionar primeiro |
| Mudança no protocolo WebSocket/Monitor | Infraestrutura de comunicação está fora do escopo |
| Otimização de CPU/RAM além do necessário | Só Delta-MFCC (Fase 4) altera consumo; aceitável conforme plano |

---

## User Stories

### P1: Firmware dispara MFCC na transição voz→silêncio ⭐ MVP

**User Story:** Como desenvolvedor, quero que o MFCC seja computado apenas quando a palavra termina (transição voz→silêncio) para garantir que a palavra esteja alinhada ao final do ring buffer.

**Por que P1:** Causa raiz do problema. Sem isso, nenhuma outra melhoria funciona.

**Acceptance Criteria:**

1. WHEN um chunk de áudio tem RMS ≥ VAD_RMS_THRESHOLD THEN o firmware SHALL atualizar `s_was_voiced = true`, incrementar `s_voiced_chunks`, e fazer `continue` sem computar MFCC
2. WHEN o chunk atual tem RMS < VAD_RMS_THRESHOLD AND `s_was_voiced == true` THEN o firmware SHALL reconhecer transição voz→silêncio e executar o pipeline MFCC+DTW
3. WHEN a transição é detectada AND `s_voiced_chunks < 3` THEN o firmware SHALL ignorar o evento (transiente curto) sem computar MFCC
4. WHEN a transição é detectada AND cooldown não expirou THEN o firmware SHALL ignorar o evento
5. WHEN o pipeline MFCC+DTW completa THEN o firmware SHALL fazer `continue` explícito (não cair no heartbeat de silêncio)
6. WHEN não há transição (silêncio genuíno) THEN o firmware SHALL enviar heartbeat ao monitor a cada 1s como antes

**Variáveis de estado necessárias:**
- `static bool  s_was_voiced  = false`
- `static float s_peak_rms    = 0.0f`
- `static int   s_voiced_chunks = 0`

**Independent Test:** Falar "ligar" claramente → log mostra `word: "ligar"`. Bater na mesa brevemente (<96ms de RMS alto) → nenhum log de detecção.

---

### P1: Treinamento alinha pelo offset (fim da palavra) ⭐ MVP

**User Story:** Como desenvolvedor, quero que `extract_features.py` extraia a janela MFCC a partir do fim da palavra (offset), correspondendo ao comportamento do firmware.

**Por que P1:** Par obrigatório com a mudança de firmware. Templates gerados com alinhamento por onset são inválidos após a mudança.

**Acceptance Criteria:**

1. WHEN `extract_mfcc(wav_path)` é chamado THEN SHALL encontrar o último sample acima de 5% do pico como `offset`
2. WHEN offset é encontrado THEN SHALL calcular `end = min(len, offset + SAMPLE_RATE // 20)` (50ms de contexto pós-palavra)
3. WHEN end é calculado THEN SHALL calcular `start = max(0, end - N_SAMPLES)` (janela de 500ms terminando em `end`)
4. WHEN `len(y_int16) < N_SAMPLES` após o recorte THEN SHALL fazer pad no **início** (silêncio pré-palavra), não no fim
5. WHEN o arquivo de áudio está completamente silencioso (peak == 0) THEN SHALL usar `offset = len(y_int16)` como fallback

**Independent Test:** Rodar `python extract_features.py --word ligar` em amostras existentes → shapes (N, 48, 13) sem erros de assert.

---

### P1: Parâmetros recalibrados pós-alinhamento ⭐ MVP

**User Story:** Como desenvolvedor, quero DTW_WINDOW reduzido e threshold recalibrado para aproveitar o alinhamento consistente.

**Por que P1:** Com alinhamento correto, DTW_WINDOW=12 é conservador demais e aumenta falsos positivos.

**Acceptance Criteria:**

1. WHEN o firmware é compilado THEN `DTW_WINDOW` SHALL ser 6
2. WHEN os templates são gerados THEN `DTW_THRESHOLD_DEFAULT` SHALL começar em 2.0f
3. WHEN os templates antigos existem THEN SHALL ser substituídos por templates gerados com o novo alinhamento

**Independent Test:** Compilar sem erro. `templates.h` com timestamp mais recente que a mudança.

---

### P1: Recoleta de amostras e regravação de templates ⭐ MVP

**User Story:** Como desenvolvedor, quero regravar templates com o novo alinhamento para que o DTW compare features compatíveis.

**Por que P1:** Templates antigos foram gerados com alinhamento por onset — são inválidos.

**Acceptance Criteria:**

1. WHEN `make train WORD=ligar` é executado THEN SHALL processar ≥15 amostras de "ligar" sem erros
2. WHEN `make train WORD=garbage` é executado THEN SHALL processar ≥15 amostras de garbage sem erros
3. WHEN `make train-templates` é executado THEN SHALL gerar `templates.h` com arrays C válidos
4. WHEN `make firmware-build` é executado THEN SHALL compilar sem erros com os novos templates
5. WHEN `make firmware-flash PORT=/dev/ttyUSB0` é executado THEN SHALL flashar com sucesso

**Independent Test:** `make train WORD=ligar && make train WORD=garbage && make train-templates && make firmware-build` completa sem erros.

---

### P2: Gate ZCR para rejeitar impactos não-vocais (Fase 2)

**User Story:** Como desenvolvedor, quero um gate de Zero Crossing Rate para rejeitar sons percussivos (batidas, palmas) que passem pelo VAD.

**Condição de ativação:** Fase 1 gerar falsos positivos com sons não-vocais.

**Acceptance Criteria:**

1. WHEN a transição voz→silêncio é detectada THEN SHALL calcular ZCR sobre `s_last_voiced_chunk` (não sobre o chunk silencioso atual)
2. WHEN ZCR > 0.15 THEN SHALL rejeitar o evento com `continue` sem computar MFCC
3. WHEN ZCR ≤ 0.15 THEN SHALL prosseguir normalmente para MFCC+DTW
4. WHEN um chunk voiced é lido THEN SHALL copiar para `static int16_t s_last_voiced_chunk[I2S_READ_CHUNK]` via `memcpy`
5. WHEN o gate rejeita THEN SHALL enviar evento de rejeição ao monitor com campo `"rejected":"zcr_gate"`

**Independent Test:** Bater palmas → log mostra `rejected: zcr_gate`. Falar "ligar" → ZCR ≤ 0.15, pipeline continua.

---

### P3: CMVN Global (Fase 3)

**User Story:** Como desenvolvedor, quero normalização CMVN global (calculada sobre todo o dataset de treinamento) em vez de por-utterance para reduzir variância entre repetições.

**Condição de ativação:** Fase 1 mostrar distâncias DTW inconsistentes entre repetições da mesma palavra.

**Acceptance Criteria:**

1. WHEN `generate_templates.py` é executado THEN SHALL calcular `global_mean` e `global_std` concatenando todos os features (ligar + garbage)
2. WHEN os stats globais são calculados THEN SHALL emitir `KWS_CMVN_MEAN[13]` e `KWS_CMVN_STD[13]` em `templates.h`
3. WHEN `mfcc_compute` é chamado no firmware THEN SHALL aplicar CMVN global (importado de `templates.h`) em vez do CMVN local atual (linhas 141–161 de `mfcc.c`)
4. WHEN `fw_mfcc` é chamado no treinamento THEN SHALL aplicar o mesmo CMVN global
5. WHEN `global_std[c] == 0` THEN SHALL usar `1e-8` como fallback para evitar divisão por zero

**Independent Test:** Distâncias DTW para a mesma palavra em condições similares variam <20% entre repetições.

---

### P4: Delta-MFCC (Fase 4)

**User Story:** Como desenvolvedor, quero expandir os features de 13 para 26 coeficientes (MFCC + Δ) para melhorar a discriminação entre palavras similares.

**Condição de ativação:** Fases 1–3 não atingirem FAR ≤ 5% (mais de 5% de falsos positivos).

**Acceptance Criteria:**

1. WHEN Delta-MFCC é ativado THEN `MFCC_N_COEFS` SHALL ser 26 (13 MFCC + 13 Δ)
2. WHEN os templates são gerados THEN o consumo adicional de RAM SHALL ser ≤ 30KB (5 templates × 2 palavras × 48 × 13 × 4B)
3. WHEN o DTW é executado THEN o tempo SHALL ser <10ms por inferência
4. WHEN Delta-MFCC é ativado THEN templates e threshold SHALL ser regravados

**Independent Test:** `esp_get_free_heap_size()` no boot mostra heap livre ≥ 80KB após ativação.

---

## Edge Cases

- WHEN a palavra "ligar" tem duração > 500ms (ring buffer) THEN o início será truncado — aceitável, palavra ainda detectável pelos frames finais
- WHEN o dispositivo é reiniciado durante coleta de amostras THEN as amostras gravadas anteriormente persistem em `training/samples/`
- WHEN `s_voiced_chunks` atinge valor muito alto (voz longa > 10s) THEN não overflow — `int` suporta; reset na transição
- WHEN `offset` não é encontrado (arquivo completamente silencioso) THEN fallback para `offset = len(y_int16)`
- WHEN monitor não está conectado (`g_ws_monitor_fd < 0`) THEN log JSON é descartado silenciosamente

---

## Requirement Traceability

| Requirement ID | Story | Fase | Status |
| -------------- | ----- | ---- | ------ |
| KWS-01 | P1: Firmware trigger na transição | Fase 1 | Pending |
| KWS-02 | P1: Mínimo 3 chunks voiced | Fase 1 | Pending |
| KWS-03 | P1: `continue` após transição | Fase 1 | Pending |
| KWS-04 | P1: Treinamento alinha por offset | Fase 1 | Pending |
| KWS-05 | P1: Pad no início, não no fim | Fase 1 | Pending |
| KWS-06 | P1: DTW_WINDOW = 6 | Fase 1 | Pending |
| KWS-07 | P1: DTW_THRESHOLD_DEFAULT = 2.0f | Fase 1 | Pending |
| KWS-08 | P1: Regravar ≥15 amostras ligar | Fase 1 | Pending |
| KWS-09 | P1: Regravar ≥15 amostras garbage | Fase 1 | Pending |
| KWS-10 | P2: ZCR usa s_last_voiced_chunk | Fase 2 (cond.) | Pending |
| KWS-11 | P2: Rejeição ZCR > 0.15 | Fase 2 (cond.) | Pending |
| KWS-12 | P3: CMVN global no treinamento | Fase 3 (cond.) | Pending |
| KWS-13 | P3: CMVN global no firmware | Fase 3 (cond.) | Pending |
| KWS-14 | P4: Delta-MFCC 13→26 coefs | Fase 4 (cond.) | Pending |

**Coverage:** 14 total, 0 mapeados para tasks, 14 pendentes ⚠️

---

## Success Criteria

- [ ] Falar "ligar" claramente → `word: "ligar"` aparece ≥3 em 5 tentativas
- [ ] Sons aleatórios (bater mesa, outras palavras) não disparam em 30s de observação
- [ ] Fases 2–4 não foram necessárias (indica que Fase 1 foi suficiente)
