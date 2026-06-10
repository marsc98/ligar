# External Integrations

**Analisado:** 2026-06-10

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
- **Comportamento:** Aceso durante `APP_RECORDING` e `APP_STREAMING`; pisca 200ms em qualquer detecção KWS

### LED Indicador KWS_AWAIT_COLOR

- **GPIO:** 23
- **Comportamento:** Acende quando `kws_task` entra em modo `KWS_AWAIT_COLOR`; apaga ao sair (por detecção de cor, "desligar" ou timeout 2s)

### LED RGB (LEDC/PWM)

- **GPIOs:** 18 (R), 19 (G), 21 (B)
- **Driver:** `esp_driver_ledc`, LEDC_TIMER_0, 3 canais, 8-bit, 5kHz
- **Controle:** `ledc_driver_init()` no boot; `ledc_set_color(r, g, b)` chamado por `led_task`
- **Acionamento:** Palavra de cor detectada no modo `KWS_AWAIT_COLOR` → cor configurada
- **Desligar:** "desligar" detectado → `send_led(0,0,0)` → apaga RGB
- **Cores suportadas:** vermelho (255,0,0), verde (0,255,0), azul (0,0,255), amarelo (255,255,0), ciano (0,255,255), magenta (255,0,255), laranja (255,165,0), roxo (128,0,128), branco (255,255,255)
- **Quantidade:** 10 LEDs 5mm RGB (através-furo, 4 pinos: R/G/B/GND)

#### Especificações do LED 5mm RGB

| Canal | λ (nm) | Intensidade (mcd) | Vf (V) | Corrente máx |
|---|---|---|---|---|
| Vermelho | 630–640 | 1000–1200 | 1,8–2,0 | 20 mA |
| Verde    | 515–525 | 3000–5000 | 3,2–3,4 | 20 mA |
| Azul     | 465–475 | 2000–3000 | 3,2–3,4 | 20 mA |

#### Resistores recomendados (1 resistor por LED por canal)

| Canal | 5V | 12V |
|---|---|---|
| R | 150 Ω | 510 Ω |
| G | 82 Ω  | 430 Ω |
| B | 82 Ω  | 430 Ω |

> Baseado em: Vf_R = 1,9V, Vf_G = Vf_B = 3,3V, I = 20 mA.

#### Brilho estimado — 10 LEDs (ângulo de visão ~25°, lente difusa)

| Corrente/LED | Duty ESP32 (≈) | R total | G total | B total | Branco (R+G+B) | % de 60W (800 lm) |
|---|---|---|---|---|---|---|
| 5 mA  | ~64/255  | 0,4 lm | 1,5 lm | 0,9 lm | ~2,8 lm  | 0,35% |
| 10 mA | ~128/255 | 0,8 lm | 3,0 lm | 1,9 lm | ~5,7 lm  | 0,71% |
| 15 mA | ~192/255 | 1,2 lm | 4,5 lm | 2,8 lm | ~8,5 lm  | 1,06% |
| 20 mA | 255/255  | 1,6 lm | 6,0 lm | 3,7 lm | ~11,3 lm | 1,41% |

> LEDs 5mm são indicadores visuais — máximo pleno = ~1,4% de uma lâmpada de 60W.
> Canal verde domina (~53% do fluxo total em branco), azul contribui ~33%, vermelho ~14%.
> Relação de intensidade percebida olho nu: G > B > R (mesmo com mcd similares em R e B,
> o olho humano é ~10× mais sensível ao verde que ao vermelho nessa faixa).

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
- **Payload:** `{"rms":float, "threshold":float, "word":string|null, "probs":{classe:prob,...}, "var"?:float, "rejected"?:"too_short"|"cooldown"|"var_gate", "kws_mode":"idle"|"await_color"}`
- **Limitação:** 1 cliente simultâneo (fd global `g_ws_monitor_fd`)
- **Schema completo:** ver `KWS_FLOW_CONTRACT.md`

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
- **Output:** `firmware/main/kws/weights.h` (auto-gerado por `train_mlp.py` — pesos MLP int8)
- **Integração com firmware:** `weights.h` é incluído por `kws/mlp.c` em tempo de compilação
