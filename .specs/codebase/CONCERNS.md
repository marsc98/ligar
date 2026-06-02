# Codebase Concerns

**Analisado:** 2026-06-01

## Segurança

**Credenciais Wi-Fi em arquivo local (não versionado):**
- Issue: SSID e senha em `main/wifi_config.h` — arquivo excluído pelo `.gitignore`
- Status: Parcialmente resolvido — não vaza via git. Ainda é plaintext em binário no flash.
- Impact residual: Extraível via JTAG/UART ou leitura direta do flash
- Fix completo: `Kconfig.projbuild` + `menuconfig`, ou provisioning via BLE/SmartConfig

**Sem TLS/WSS:**
- Issue: WebSocket em `ws://` — áudio e transcrições transmitidos em plaintext na LAN
- Files: `main/poc-microfone.c` (todo o servidor HTTP)
- Impact: Qualquer dispositivo na rede pode capturar o áudio
- Fix: `esp_tls` + certificado autoassinado para `wss://`

**Modelo Whisper baixado de CDN externo:**
- Issue: `onnx-community/whisper-tiny` baixado do HuggingFace Hub na primeira execução
- Impact: Requer acesso à internet; sujeito a disponibilidade do serviço externo
- Mitigação: Após o primeiro download, fica cacheado no browser
- Fix: Hospedar modelo localmente e servir pelo ESP32 (improvável dado o tamanho ~40 MB) ou via servidor auxiliar

## Known Bugs

**KWS — Zero detecções (bug documentado, em investigação):**
- Symptoms: Nenhuma detecção no monitor, distâncias DTW na faixa de ruído aleatório (~5.1 para 13D)
- Trigger: Qualquer fala durante APP_IDLE
- Files: `firmware/main/poc-microfone.c` (kws_task), `firmware/main/mfcc.c`, `training/extract_features.py`
- Root cause: Possível desalinhamento entre janela de inferência (ring buffer posição arbitrária) e janela de treino (alinhada pelo onset). Três causas investigadas: (1) ring não captura janela correta relativa à palavra, (2) divergência numérica Python↔C, (3) normalização diferente
- Docs: `docs/plans/plano-fix-kws-alinhamento.md`, `docs/plans/analise-matematica-kws.md`

## Tech Debt

**Log de diagnóstico temporário no código de produção:**
- Issue: `static uint32_t dbg_calls` + 5 linhas de `ESP_LOGI` dentro de `i2s_read_16bit` com comentário `/* DIAGNÓSTICO — remover após confirmar funcionamento */`
- Files: `firmware/main/poc-microfone.c:191-201`
- Why: Adicionado durante debug de GPIO errada
- Impact: Spam no serial nos primeiros 5 chunks de qualquer gravação/stream
- Fix: Remover bloco entre as marcações `/* DIAGNÓSTICO */`

**Dois lock files de package manager:**
- Issue: `yarn.lock` e `package-lock.json` coexistem em `web/`
- Impact: Builds potencialmente não-determinísticos dependendo de qual ferramenta for usada
- Fix: Escolher um (npm ou yarn), remover o outro e documentar no README

## Fragilidade

**Sincronização de boot por sleep fixo:**
- Issue: `vTaskDelay(pdMS_TO_TICKS(3000))` aguarda IP antes de iniciar servidor
- Files: `main/poc-microfone.c:542`
- Impact: Falha silenciosa em redes lentas; atraso desnecessário em redes rápidas
- Fix: `EventGroupWaitBits` aguardando `IP_EVENT_STA_GOT_IP`

**Desconexão WebSocket não limpa fd imediatamente:**
- Issue: Se cliente desconectar abruptamente, `g_ws_record_fd` / `g_ws_stream_fd` ficam com fd inválido; são limpos apenas quando o próximo `ws_send_binary` falha
- Files: `firmware/main/poc-microfone.c` — `ws_record_handler`, `ws_stream_handler`
- Fix: Registrar `httpd_sess_err_hapened_cb` para limpar fd ao detectar desconexão

**`useStream` flush por RMS pode descartar áudio:**
- Issue: Flush quando `buf.length >= 8000 && rms < 200`; silêncio detectado pode cortar frase no meio
- Files: `web/src/hooks/useStream.ts`
- Impact: Janela enviada para Whisper pode ser incompleta se o falante fizer pausa curta
- Fix: VAD mais robusto ou buffer maior com overlap

**Fila de click com profundidade 1:**
- Issue: `xQueueCreate(1, sizeof(uint8_t))` — segundo click enquanto o primeiro ainda está na fila é descartado silenciosamente
- Files: `firmware/main/poc-microfone.c` (g_click_queue)
- Impact: Click duplo rápido pode ser ignorado
- Fix: Profundidade 2 ou processar click mais rapidamente

**kws_task e audio_task disputam I2S:**
- Issue: Ambas as tasks chamam `i2s_read_16bit` diretamente; `kws_task` cede com `vTaskDelay(10ms)` quando `g_state != APP_IDLE`, mas não há mutex no I2S
- Files: `firmware/main/poc-microfone.c` (kws_task, audio_task)
- Why fragile: I2S lê de DMA FIFO — reads simultâneos causariam corrupção; a lógica de vTaskDelay funciona na prática mas não é garantida pelo driver
- Safe modification: Adicionar mutex de I2S ou separar canal de captura por task

## Limites de Escalabilidade

**1 cliente por endpoint:**
- `g_ws_record_fd` e `g_ws_stream_fd` são ints globais — sem suporte a múltiplos clientes

**IndexedDB sem limite de quota:**
- Blobs WAV armazenados inline; browsers limitam IndexedDB a ~50% do espaço disponível
- Sem limpeza automática — usuário deve deletar manualmente via UI

**Modelo Whisper bloqueante:**
- `useWhisper` processa uma janela por vez (fila serial); janelas acumulam se o áudio for contínuo
- Whisper tiny pode levar mais tempo que a duração do áudio em hardware lento

## Test Coverage Gaps

**Pipeline KWS end-to-end (crítico — bug ativo):**
- What: Paridade numérica `firmware_mfcc.py` ↔ `mfcc.c`; alinhamento onset no ring buffer
- Risk: Zero detecções; problema atual não reproduzível sem hardware
- Priority: Alto

**Conversão I2S (crítico):**
- What: `i2s_read_16bit` — shift 32→16-bit, gain, saturação
- Risk: Regressão silenciosa ao alterar `MIC_GAIN` ou formato I2S
- Priority: Alto

**Machine de estados firmware:**
- What: Transições IDLE → RECORDING/STREAMING → IDLE
- Risk: Edge cases (click durante transição, desconexão no meio da gravação)
- Priority: Alto

**`useConnection` (hook principal):**
- What: Sequência de mensagens WS, montagem de chunks, `RECORDING_END:<n>`
- Risk: Arquivo WAV incompleto ou corrompido em condições adversas de rede
- Priority: Alto

**`useStream` (lógica de flush):**
- What: Acumulação de buffer, detecção de silêncio por RMS, envio de janelas
- Risk: Janelas incorretas enviadas ao Whisper
- Priority: Alto
