# KWS Alinhamento Fix — Decisões de Interview

**Data:** 2026-05-27
**Escopo:** corrigir o alinhamento temporal MFCC+DTW para produzir as primeiras detecções reais de "ligar" no ESP32
**Fonte:** `docs/plans/plano-fix-kws-alinhamento.md`

---

## Decisões

### Contexto pós-palavra (treinamento vs firmware)

- Manter `offset + SAMPLE_RATE // 20` (50ms) no treinamento.
- **Rationale:** a diferença de 18ms (50ms treino vs ~32ms firmware) está dentro da tolerância do DTW_WINDOW=6 (60ms). A variância dominante é a granularidade do chunk (±16ms) e a qualidade do detector de offset — ajustar para 32ms não elimina esse jitter.

### Duração mínima de voz antes de disparar

- Exigir mínimo de **3 chunks voiced consecutivos (~96ms)** antes de permitir o disparo de MFCC+DTW.
- Implementar com `static int s_voiced_chunks` — incrementado a cada chunk voiced, zerado na transição voz→silêncio.
- Se `s_voiced_chunks < 3` na transição → `continue` sem rodar o pipeline.
- **Rationale:** "ligar" tem ≥200ms de duração acústica. 96ms filtra transientes (palmadas, clicks) sem afetar palavras reais. Custo: uma variável `int`.

### ZCR gate (Fase 2) — chunk correto

- Armazenar o **último chunk voiced** em `static int16_t s_last_voiced_chunk[I2S_READ_CHUNK]` + `static size_t s_last_voiced_n`.
- Copiar via `memcpy` a cada chunk voiced (sobrescreve; só o último importa).
- Na transição, calcular ZCR sobre `s_last_voiced_chunk`, não sobre o chunk silencioso atual.
- **Rationale:** o chunk que dispara a transição é o primeiro silencioso — ZCR ≈ 0, gate seria ineficaz. Custo de RAM: 1KB estático, aceitável no ESP32.

### Fluxo do loop após transição disparar

- Após o bloco de transição (MFCC + DTW + report), usar **`continue` explícito** para voltar ao início do loop.
- Heartbeat de silêncio só sai em silêncio genuíno (sem transição recente).
- **Rationale:** o JSON de detecção já carrega todo o contexto. Um heartbeat imediatamente após seria redundante e polui o log do monitor.

---

## Agente tem Liberdade

- Nenhuma área delegada ao agente; todas as decisões foram tomadas explicitamente.

---

## Ideias Diferidas

- Nenhuma ideia fora do escopo surgiu durante o interview.

---

## Questões em Aberto

- Nenhuma.
