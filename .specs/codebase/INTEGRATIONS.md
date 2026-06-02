# External Integrations

**Analisado:** 2026-06-01

## Hardware

### INMP441 — Microfone I2S

- **Interface:** I2S Standard (Philips mode, `bit_shift=true`, 1-clock delay após WS)
- **Pinout:** BCLK→GPIO26, WS→GPIO25, SD→GPIO22, L/R→GND (Left channel)
- **Formato no fio:** Palavra de 32-bit; dados de 24-bit nos MSBs
- **Conversão:** `(raw32[i] >> 16) * MIC_GAIN` com saturação para int16_t
- **Configuração:** `I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG`, 32-bit slot, mono, 16kHz
- **Gain digital:** `MIC_GAIN = 16` (24 dB) — ajustável via `#define`

### Botão Push

- **GPIO:** 4 (pull-up interno)
- **Detecção:** Polling a 10ms em `button_task`
- **Debounce:** 50ms por software (5 leituras estáveis)
- **Semântica:** Click = falling edge (pressionar); soltar não gera evento
- **Publicação:** `xQueueSend(g_click_queue, &evt, 0)` → consumido por `audio_task`

### LED Onboard

- **GPIO:** 2 (LED azul integrado na placa)
- **Comportamento:** Aceso durante `APP_RECORDING` e `APP_STREAMING`; apagado em `APP_IDLE`

## Rede

### Wi-Fi STA

- **Modo:** Station (cliente), WPA2-PSK
- **Credenciais:** `#define WIFI_SSID / WIFI_PASS` em `main/wifi_config.h` (fora do controle de versão)
- **Reconexão:** Automática via `WIFI_EVENT_STA_DISCONNECTED`
- **Boot sync:** `vTaskDelay(3000ms)` aguarda IP (frágil — ver CONCERNS)

### WebSocket `/record`

- **URL:** `ws://<IP>/record`
- **Fluxo:** connect → 1º click → `"RECORDING_START"` → chunks PCM raw → 2º click → `"RECORDING_END:<n>"`
- **Chunks:** 512 amostras × 2 bytes = 1024 bytes por chunk (sem delay entre chunks)
- **Encerramento:** `<n>` = total de amostras enviadas; cliente monta WAV client-side
- **Limitação:** 1 cliente simultâneo (1 fd global)

### WebSocket `/stream`

- **URL:** `ws://<IP>/stream`
- **Fluxo:** connect → 1º click → PCM raw em tempo real → 2º click encerra
- **Chunk:** 512 amostras × 2 bytes = 1024 bytes (~32ms de áudio a 16kHz)
- **Limitação:** 1 cliente simultâneo (1 fd global)

### WebSocket `/monitor`

- **URL:** `ws://<IP>/monitor`
- **Fluxo:** connect → heartbeats JSON a 1Hz + evento JSON a cada segmento voiced
- **Payload:** `{"rms":float, "threshold":float, "word":string|null, "dists":{word:dist}, "var"?:float, "garbage_dist"?:float, "rejected"?:string}`
- **Limitação:** 1 cliente simultâneo (fd global `g_ws_monitor_fd`)

### HTTP GET `/threshold`

- **URL:** `GET /threshold?v=<float>`
- **Resposta:** `200 OK "OK"`
- **Efeito:** Atualiza `g_dtw_threshold` em runtime
- **Sem autenticação**

## Servidor HTTP Embutido

- **Implementação:** `esp_http_server` do ESP-IDF
- **Porta:** 80 (sem HTTPS)
- **Max sockets:** 7 (configurado explicitamente — 4 WS + margem)
- **WebSocket support:** `CONFIG_HTTPD_WS_SUPPORT=y`
- **Timeouts:** `recv_wait_timeout=120s`, `send_wait_timeout=120s`

## Frontend → IndexedDB

- **DB:** `poc-microfone` v1
- **Object store:** `recordings` (keyPath: `id`)
- **Índice:** `by-timestamp`
- **API:** Nativa (`indexedDB.open`, `IDBRequest`, `IDBTransaction`)
- **Dados armazenados:** Blob WAV completo inline + campo `transcription?: string`

## Frontend → HuggingFace Hub

- **Biblioteca:** `@huggingface/transformers ^4.2.0` (ONNX Runtime WebAssembly)
- **Modelo:** `onnx-community/whisper-tiny` — ~40 MB, baixado na primeira execução, cacheado pelo browser
- **Execução:** `workers/whisper.worker.ts` (Web Worker, fila serial)
- **Modo:** Transcrição (`task: 'transcribe'`) com idioma configurável
- **Uso offline:** Após o primeiro download, funciona sem internet
- **Observação:** `env.allowLocalModels = false` — usa exclusivamente o HuggingFace Hub

## Frontend → IndexedDB (Recording Storage)

- **DB:** `poc-microfone` v1
- **Object store:** `recordings` (keyPath: `id`, índice: `by-timestamp`)
- **Dados:** Blob WAV completo + `transcription?: string` + `collection?: {word, sessionId}`
- **API:** Wrapper em `lib/db.ts` sobre `IDBDatabase` nativo

## Pipeline de Treinamento → Filesystem

- **Input:** `training/samples/<palavra>_NNN.wav` (16kHz, mono)
- **Intermediário:** `training/features/<palavra>.npy` (shape: [N_amostras, 48, 13])
- **Output:** `firmware/main/templates.h` (auto-gerado, 10 templates por palavra)
- **Integração com firmware:** `templates.h` é incluído por `poc-microfone.c` em tempo de compilação
