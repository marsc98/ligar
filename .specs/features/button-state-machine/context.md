# Button State Machine — Decisões da Entrevista

**Data:** 2026-05-23
**Escopo:** Substituir modelo "segurar para gravar" por máquina de estados controlada por clicks — 1º click inicia, 2º click encerra (com envio no `/record`, sem envio no `/stream`).
**Fonte:** Discussão informal

---

## Decisões

### Mecanismo de sinalização do click

- `button_task` usa `xQueueSend` para enviar evento de click a `g_click_queue` (capacidade 1)
- `audio_task` faz `xQueueReceive(..., 0)` (não-bloqueante) a cada iteração do loop
- Remove `g_button_pressed` (volatile bool)
- **Rationale:** Queue FreeRTOS evita perda de click durante envio de WAV (race condition real no modelo de flag). Idiomático para o FreeRTOS já em uso no projeto.

### Edge do disparo

- Click é detectado no **falling edge** — momento em que GPIO cai para LOW (botão pressionado, pull-up ativo)
- Rising edge (soltar) é ignorado para fins de disparo de evento
- **Rationale:** Resposta imediata ao toque; comportamento esperado pelo usuário.

### Prioridade de modo (ambos WebSockets conectados)

- Se `/stream` e `/record` estiverem conectados simultaneamente, `/stream` tem prioridade
- **Rationale:** Mantém comportamento atual do código; streaming é mais urgente por natureza.

### Buffer cheio antes do 2º click

- Auto-stop: encerra gravação e envia WAV automaticamente quando `RECORD_BUF_SAMPLES` é atingido
- LED apaga junto com a transição para `APP_IDLE`
- **Rationale:** Evita perda silenciosa de dados; comportamento defensivo.

### Indicação visual (LED)

- GPIO 2 (LED onboard do ESP32 DevKit)
- LED **aceso** durante `APP_RECORDING` e `APP_STREAMING`
- LED **apagado** em `APP_IDLE`

---

## Discrição do Agente

- N/A — todas as áreas foram decididas explicitamente.

---

## Ideias Adiadas

- README explicativo do FreeRTOS e da aplicação — solicitado durante entrevista, implementado em paralelo
