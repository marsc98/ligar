# Tech Stack

**Analisado:** 2026-06-10

## Firmware (ESP32)

- **SoC:** ESP32-D0WD rev 3.1 — dual-core Xtensa LX6, 160 MHz, **sem PSRAM** (~150KB RAM livre com Wi-Fi)
- **SDK:** ESP-IDF v5.x (CMake + idf.py)
- **Linguagem:** C (C99)
- **RTOS:** FreeRTOS (incluso no ESP-IDF)
- **Microfone:** INMP441 via I2S (Philips mode, 32-bit slot, dados 24-bit MSB, mono, 16kHz)

### Componentes ESP-IDF utilizados

| Componente | Uso |
|---|---|
| `driver/i2s_std` | Captura de áudio (driver v5+, Philips mode) |
| `driver/gpio` | Botão (polling) e LED onboard (GPIO 2) |
| `driver/ledc` | LED RGB PWM: 3 canais (R/G/B), 8-bit, 5kHz |
| `esp_http_server` | HTTP + WebSocket (porta 80) |
| `esp_wifi` | Wi-Fi STA mode, WPA2-PSK |
| `esp_netif` | Abstração de rede |
| `nvs_flash` | Armazenamento NVS (init para Wi-Fi) |
| `freertos` | Tasks, semáforos, filas (`QueueHandle_t`) |

### KWS (Keyword Spotting)

- **Features:** MFCC — 13 coeficientes, 48 frames, janela 0.5s (8000 amostras), hop 10ms
- **Classificador:** MLP quantizado int8 — arquitetura 624→128→64→10, forward pass em `mlp.c`
- **VAD:** RMS threshold (300.0) + mínimo 3 chunks voiced
- **Rejeição:** classe "garbage" na MLP + threshold de confiança (padrão 0.50, ajustável via `/threshold`)
- **Variância temporal:** threshold 0.3 — rejeita silêncio/ruído antes de rodar MLP
- **Vocabulário (10 classes):** amarelo, azul, branco, desligar, garbage, laranja, ligar, roxo, verde, vermelho
- **Pesos:** gerados por `train_mlp.py` → `firmware/main/kws/weights.h`
- **RGB LED:** LEDC/PWM 8-bit em GPIO 18 (R), 19 (G), 21 (B) via `ledc_driver.c`
- **LED indicador KWS_AWAIT_COLOR:** GPIO 23 (LED dedicado ao estado de aguardo de cor)

> `dtw.c/h` e `templates.h` ainda existem no repo mas **não são compilados** (dead code desde migração para MLP).

## Frontend (Web SPA)

- **Framework:** React 19.2.6 (sem Next.js, sem SSR)
- **Linguagem:** TypeScript ~6.0.2
- **Bundler:** Vite 8.0.12 + @vitejs/plugin-react
- **Package managers:** npm + yarn (dois lock files — ver CONCERNS)
- **Estilização:** Inline styles puro (sem Tailwind, MUI, shadcn)
- **State management:** React hooks nativos (`useState`, `useRef`, `useEffect`)
- **Armazenamento:** IndexedDB via API nativa (sem Dexie ou idb wrapper)
- **Comunicação:** WebSocket nativo do browser
- **Reprodução:** `<audio>` element + Object URLs
- **Speech recognition:** `@huggingface/transformers` + Web Worker (Whisper tiny ONNX)
- **Visualização:** Canvas 2D API + FFT customizado
- **Compactação:** JSZip 3.10 (download de amostras de coleta em .zip)

### Dependências de produção

| Pacote | Versão |
|---|---|
| react | ^19.2.6 |
| react-dom | ^19.2.6 |
| @huggingface/transformers | ^4.2.0 |
| jszip | ^3.10.1 |

### Modelo de IA

- `onnx-community/whisper-tiny` — baixado do HuggingFace Hub na primeira execução, rodado no browser via ONNX Runtime (WebAssembly)

## Pipeline de Treinamento

- **Linguagem:** Python 3.12
- **Dependências:** numpy ≥1.24, scipy ≥1.10, librosa ≥0.10, soundfile ≥0.12, websockets ≥12.0
- **Venv:** `training/.venv/`
- **Scripts:** `extract_features.py`, `train_mlp.py`, `firmware_mfcc.py`, `firmware_mlp.py`, `capture_monitor.py`, `generate_templates.py` (deprecated — era para DTW)
- **Artefatos:** `training/features/*.npy` → `train_mlp.py` → `firmware/main/kws/weights.h`

## Tooling

| Ferramenta | Uso |
|---|---|
| ESLint 10 + typescript-eslint | Lint frontend |
| tsc | Type check |
| idf.py flash monitor | Flash e monitor serial |
| Makefile | Orchestração de todos os targets (firmware + web + training) |
