# Project Structure

**Analisado:** 2026-06-10
**Raiz:** `/home/marco/projetos/ligar`

## Árvore de Diretórios

```
poc-microfone/
├── CMakeLists.txt              # Boilerplate ESP-IDF (cmake_minimum + project + EXTRA_COMPONENT_DIRS)
├── Makefile                    # Orchestração: firmware-build/flash/monitor, web-dev/build, train, pipeline
├── sdkconfig                   # Configuração gerada pelo menuconfig
├── README.md
│
├── firmware/                   # Componente ESP-IDF do firmware (modular)
│   └── main/
│       ├── CMakeLists.txt      # idf_component_register com todos os .c + INCLUDE_DIRS
│       ├── app_main.c          # Boot: NVS, Wi-Fi, HTTP server, criação de tasks
│       ├── app_state.h         # Globals compartilhados (enum app_state_t, fds, mutex, queues)
│       ├── wifi_config.h       # Credenciais Wi-Fi (NÃO versionado — .gitignore)
│       ├── wifi_config.h.example
│       ├── drivers/
│       │   ├── i2s_driver.c/h  # I2S: init + i2s_read_16bit (32→16-bit, gain, saturação)
│       │   └── ledc_driver.c/h # RGB LED via LEDC/PWM: init + ledc_set_color(r,g,b)
│       ├── kws/
│       │   ├── mfcc.c/h        # MFCC: Hann, mel filterbank (300-8000 Hz, 26 mels), DCT, CMVN
│       │   ├── mlp.c/h         # MLP forward pass quantizado int8 (624→128→64→10)
│       │   ├── weights.h       # AUTO-GERADO por train_mlp.py — não editar
│       │   ├── color_catalog.h # Lookup table: nome de cor → RGB (9 cores)
│       │   ├── dtw.c/h         # DEAD CODE — não compilado; era DTW Sakoe-Chiba
│       │   └── templates.h     # DEAD CODE — era auto-gerado por generate_templates.py
│       └── tasks/
│           ├── i2s_reader_task.c/h # Leitura exclusiva I2S DMA; distribui para g_audio_queue e g_kws_queue
│           ├── audio_task.c/h  # WS /record e /stream, machine de estados APP_*
│           ├── button_task.c/h # Polling GPIO4, debounce 50ms, publica em g_click_queue
│           ├── kws_task.c/h    # KWS: ring buffer, VAD, MFCC, MLP, WS /monitor
│           └── led_task.c/h    # Consume g_led_queue → ledc_set_color(r,g,b)
│
├── training/                   # Pipeline Python para KWS
│   ├── requirements.txt        # numpy, scipy, librosa, soundfile, websockets
│   ├── firmware_mfcc.py        # Réplica Python exata do mfcc.c (parâmetros idênticos)
│   ├── firmware_mlp.py         # Réplica Python exata do mlp.c (valida paridade Python↔C)
│   ├── extract_features.py     # WAV → MFCC alinhado por onset → .npy
│   ├── train_mlp.py            # .npy → treina MLP → exports weights.h (int8 quantizado)
│   ├── generate_templates.py   # DEPRECATED — era para DTW; não usar
│   ├── capture_monitor.py      # Captura eventos do /monitor via WebSocket
│   ├── samples/                # WAVs de treinamento: <palavra>_NNN.wav
│   └── features/               # MFCCs extraídos: <palavra>.npy
│
├── web/                        # Frontend SPA (React + TypeScript)
│   ├── package.json
│   ├── vite.config.ts
│   ├── tsconfig.json
│   └── src/
│       ├── main.tsx
│       ├── App.tsx             # Composição raiz + 3 abas (Gravações / Monitor / Coleta)
│       ├── types.ts            # Recording, ConnectionState, VisualizerMode
│       ├── hooks/
│       │   ├── useConnection.ts      # WebSocket /record + montagem WAV por chunks
│       │   ├── useRecordings.ts      # CRUD IndexedDB + updateTranscription
│       │   ├── useAudioPlayer.ts     # Reprodução de áudio
│       │   ├── useStream.ts          # WebSocket /stream, buffer + detecção de silêncio
│       │   ├── useStreamVisualizer.ts # Canvas 2D, ring buffer, waveform/FFT em RAF
│       │   ├── useWhisper.ts         # Web Worker + fila, whisper-tiny ONNX
│       │   ├── useCollection.ts      # WebSocket /record em modo coleta contínua por palavra
│       │   └── useLightControl.ts    # Envia GET /led?color=&intensity= para controle direto RGB
│       ├── components/
│       │   ├── ConnectionPanel.tsx
│       │   ├── RecordingList.tsx / RecordingItem.tsx / StatusBadge.tsx
│       │   ├── AudioVisualizer.tsx   # Canvas waveform/FFT para gravações
│       │   ├── LanguageSelect.tsx
│       │   ├── LiveTranscriptPanel.tsx
│       │   ├── StreamVisualizer.tsx
│       │   ├── MonitorTab.tsx        # Aba Monitor: WS /monitor + threshold + log JSONL
│       │   └── CollectionTab.tsx     # Aba Coleta: gravações em série por palavra + export .zip
│           └── LightTab.tsx          # Aba Luz: seletor de cor + intensidade → GET /led
│       ├── lib/
│       │   ├── db.ts           # Wrapper IndexedDB
│       │   ├── wav.ts          # buildWavHeader, assemblePcmToWav
│       │   ├── fft.ts          # FFT Cooley-Tukey (Hann window)
│       │   └── vizUtils.ts     # drawWaveform, drawFFT (Canvas 2D)
│       ├── utils/
│       │   └── monitorLogger.ts  # Sessão de log JSONL em memória, download, stats
│       └── workers/
│           └── whisper.worker.ts
│
├── docs/plans/                 # Planos e análises (pt-BR)
├── build/                      # Artefatos ESP-IDF (não commitar)
└── .specs/                     # Documentação do projeto
    ├── codebase/
    └── features/
        ├── audio-visualization/
        ├── button-state-machine/
        ├── coleta-monitor-integration/
        ├── esp32-audio-recorder/
        ├── kws-alinhamento-fix/
        ├── kws-mlp/
        ├── kws-rejection-model/
        ├── ligar/
        ├── light-control/
        ├── monitor-ws-stability/
        ├── rgb-led-kws/
        └── speech-recognition/
```

## Responsabilidades por Área

### `firmware/main/` — Firmware (modular)

| Arquivo | Responsabilidade |
|---|---|
| `app_main.c` | Boot: NVS init, Wi-Fi, HTTP server (porta 80), criação das 5 tasks |
| `app_state.h` | Globals: `g_app_state`, `g_dtw_threshold` (MLP thr), fds WS, mutex, queues, `i2s_chunk_t` |
| `drivers/i2s_driver.c/h` | I2S Philips 32-bit, 16kHz, mono; `i2s_read_16bit()` com gain 16× e saturação |
| `drivers/ledc_driver.c/h` | LEDC/PWM 8-bit 5kHz: RGB LED em GPIO18/19/21; `ledc_set_color(r,g,b)` |
| `kws/mfcc.c/h` | `mfcc_compute()`: pre-emphasis, FFT, mel filterbank, DCT, CMVN |
| `kws/mlp.c/h` | `mlp_infer()`: forward pass MLP int8 quantizado (624→128→64→10→softmax) |
| `kws/weights.h` | Pesos MLP em C (auto-gerado por `train_mlp.py`) — não editar |
| `kws/color_catalog.h` | Lookup: 9 cores (vermelho…branco) → `{r,g,b}` |
| `kws/dtw.c/h` | **DEAD CODE** — não compilado; era DTW Sakoe-Chiba |
| `kws/templates.h` | **DEAD CODE** — era auto-gerado por `generate_templates.py` |
| `tasks/i2s_reader_task.c/h` | Leitura exclusiva I2S DMA; distribui para `g_audio_queue` e `g_kws_queue` |
| `tasks/audio_task.c/h` | Machine de estados APP_IDLE/RECORDING/STREAMING; WS handlers /record e /stream |
| `tasks/button_task.c/h` | Polling GPIO4, debounce 50ms (5 leituras estáveis), publica em `g_click_queue` |
| `tasks/kws_task.c/h` | KWS 2 estágios (KWS_IDLE/KWS_AWAIT_COLOR), VAD, MFCC, MLP, /monitor, GPIO23 indicador; /threshold e /led handlers |
| `tasks/led_task.c/h` | Consumer de `g_led_queue`; chama `ledc_set_color` para cada `led_command_t {r,g,b}` |
| `wifi_config.h` | Credenciais SSID/senha (arquivo local, não versionado) |

**Pinout completo:**
- GPIO 26 → I2S BCLK, GPIO 25 → I2S WS, GPIO 22 → I2S DATA IN
- GPIO 4 → Botão (pull-up interno)
- GPIO 2 → LED onboard (acende durante RECORDING/STREAMING e detecção KWS)
- GPIO 23 → LED indicador KWS_AWAIT_COLOR (acende enquanto sistema aguarda cor)
- GPIO 18 → LED RGB R (LEDC CH0), GPIO 19 → G (CH1), GPIO 21 → B (CH2)

### `training/` — Pipeline KWS

| Arquivo | Responsabilidade |
|---|---|
| `firmware_mfcc.py` | Réplica Python do `mfcc.c` (parâmetros idênticos para garantir alinhamento) |
| `firmware_mlp.py` | Réplica Python do `mlp.c` com pesos de `weights.h` — valida paridade Python↔C |
| `extract_features.py` | Carrega WAV, alinha onset pelo final, extrai MFCC, salva .npy |
| `train_mlp.py` | Carrega .npy de todas as classes, treina MLP 624→128→64→N com Adam, quantiza int8, gera `weights.h` |
| `generate_templates.py` | **DEPRECATED** — era para DTW; gerava `templates.h` |
| `capture_monitor.py` | Captura e salva eventos do WebSocket /monitor (debug/análise) |

### `web/src/` — Frontend

| Área | Localização | Responsabilidade |
|---|---|---|
| Gravação | `hooks/useConnection.ts` | WebSocket /record, estado de sessão, montagem WAV |
| Stream ao vivo | `hooks/useStream.ts` | WS /stream, buffer, janelas para Whisper |
| Visualização | `hooks/useStreamVisualizer.ts` | Ring buffer, RAF, waveform/FFT |
| Transcrição | `hooks/useWhisper.ts` | Worker, fila serial, whisper-tiny |
| Coleta de amostras | `hooks/useCollection.ts` | WS /record em modo contínuo por palavra |
| Persistência | `hooks/useRecordings.ts` + `lib/db.ts` | IndexedDB CRUD |
| Reprodução | `hooks/useAudioPlayer.ts` | `<audio>` element, Object URLs |
| Monitor KWS | `components/MonitorTab.tsx` | WS /monitor, threshold, log JSONL |
| Coleta UI | `components/CollectionTab.tsx` | UI de coleta, download .zip |
| Controle de Luz | `components/LightTab.tsx` + `hooks/useLightControl.ts` | Seletor de cor + intensidade → GET /led |
| Log monitor | `utils/monitorLogger.ts` | Sessão em memória, stats, export JSONL |

## Ausências Notáveis

- Sem `sdkconfig.defaults` — configuração não versionada de forma reproduzível
- Sem `partitions.csv` — usa tabela de partições padrão do ESP-IDF
- Sem testes (firmware, frontend ou training)
- Sem CI/CD
