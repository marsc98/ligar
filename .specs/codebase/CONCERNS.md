# Codebase Concerns

**Analisado:** 2026-06-10

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

**KWS — Zero detecções (RESOLVIDO via migração DTW→MLP):**
- Status anterior: Nenhuma detecção, distâncias DTW em faixa de ruído aleatório
- Resolução: O classificador DTW foi substituído por MLP (624→128→64→10). Com MLP, detecções funcionam normalmente.
- Dead code remanescente: `dtw.c/h` e `templates.h` ainda existem mas não são compilados — podem ser removidos.

**Schema drift /monitor WebSocket:**
- Issue: O firmware ainda envia campo `threshold` no JSON do /monitor (valor do MLP threshold, reutilizando `g_dtw_threshold`), mas `KWS_FLOW_CONTRACT.md` diz para nunca mais enviar `threshold`.
- Files: `firmware/main/tasks/kws_task.c` — todos os 6 pontos de `ws_send_text`
- Impact: Inconsistência entre documentação do contrato e implementação real; o frontend ainda lê e usa `threshold` para sync do slider — remoção quebraria o frontend.
- Fix: Ou remover `threshold` do JSON e adaptar frontend, ou atualizar o contrato para documentar que o campo permanece (com semântica de MLP threshold).

## Tech Debt

**Dead code DTW:**
- Issue: `firmware/main/kws/dtw.c/h` e `firmware/main/kws/templates.h` existem no repo mas não são listados no `CMakeLists.txt` e nunca mais são incluídos
- Impact: Confusão para futuros colaboradores; `templates.h` ainda é auto-gerado mas sem uso
- Fix: Remover `dtw.c`, `dtw.h`, `templates.h` e `generate_templates.py` (ou manter apenas como referência histórica)

**Dois lock files de package manager:**
- Issue: `yarn.lock` e `package-lock.json` coexistem em `web/`
- Impact: Builds potencialmente não-determinísticos dependendo de qual ferramenta for usada
- Fix: Escolher um (npm ou yarn), remover o outro e documentar no README

## Fragilidade

**Sincronização de boot por sleep fixo:**
- Issue: `vTaskDelay(pdMS_TO_TICKS(3000))` aguarda IP antes de iniciar servidor
- Files: `firmware/main/app_main.c`
- Impact: Falha silenciosa em redes lentas; atraso desnecessário em redes rápidas
- Fix: `EventGroupWaitBits` aguardando `IP_EVENT_STA_GOT_IP`

**Desconexão WebSocket não limpa fd imediatamente:**
- Issue: Se cliente desconectar abruptamente, `g_ws_record_fd` / `g_ws_stream_fd` ficam com fd inválido; são limpos apenas quando o próximo `ws_send_binary` falha
- Files: `firmware/main/tasks/audio_task.c` — `ws_record_handler`, `ws_stream_handler`
- Fix: Registrar `httpd_sess_err_hapened_cb` para limpar fd ao detectar desconexão

**`g_ws_monitor_fd` limpeza em send failure (RESOLVIDO):**
- Status anterior: `ws_send_text()` em `kws_task.c` não resetava `g_ws_monitor_fd = -1` em erro
- Resolução: Todos os 6 pontos de `ws_send_text` em `kws_task.c` agora resetam `g_ws_monitor_fd = -1` quando `ws_ret != ESP_OK`.

**`useStream` flush por RMS pode descartar áudio:**
- Issue: Flush quando `buf.length >= 8000 && rms < 200`; silêncio detectado pode cortar frase no meio
- Files: `web/src/hooks/useStream.ts`
- Impact: Janela enviada para Whisper pode ser incompleta se o falante fizer pausa curta
- Fix: VAD mais robusto ou buffer maior com overlap

**KWS_AWAIT_COLOR sem feedback visual ativo (RESOLVIDO):**
- Resolução: GPIO 23 dedicado ao estado `KWS_AWAIT_COLOR` — acende quando sistema aguarda cor, apaga ao sair do estado.
- Files: `firmware/main/tasks/kws_task.c`, commit `dceb543`

**Fila de click com profundidade 1:**
- Issue: `xQueueCreate(1, sizeof(uint8_t))` — segundo click enquanto o primeiro ainda está na fila é descartado silenciosamente
- Files: `firmware/main/app_main.c` (g_click_queue)
- Impact: Click duplo rápido pode ser ignorado
- Fix: Profundidade 2 ou processar click mais rapidamente

**kws_task e audio_task disputavam I2S (RESOLVIDO):**
- Status anterior: Ambas as tasks chamavam `i2s_read_16bit` diretamente, sem mutex
- Resolução: `i2s_reader_task` (prio 12) é agora o único leitor do hardware I2S; distribui chunks via `g_audio_queue` → `audio_task` e `g_kws_queue` → `kws_task`.

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
