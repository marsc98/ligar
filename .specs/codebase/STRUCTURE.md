# Project Structure

**Analisado:** 2026-06-01
**Raiz:** `/home/marco/projetos/poc-microfone`

## Árvore de Diretórios

```
poc-microfone/
├── CMakeLists.txt              # Boilerplate ESP-IDF (cmake_minimum + project + EXTRA_COMPONENT_DIRS)
├── Makefile                    # Orchestração: firmware-build/flash/monitor, web-dev/build, train, pipeline
├── sdkconfig                   # Configuração gerada pelo menuconfig
├── README.md
│
├── firmware/                   # Componente ESP-IDF do firmware
│   └── main/
│       ├── CMakeLists.txt      # idf_component_register
│       ├── poc-microfone.c     # Todo o código do firmware (~846 linhas)
│       ├── mfcc.h / mfcc.c     # Extração MFCC: Hann window, mel filterbank, DCT
│       ├── dtw.h / dtw.c       # DTW com banda Sakoe-Chiba
│       ├── templates.h         # AUTO-GERADO: templates MFCC por palavra (não editar)
│       ├── wifi_config.h       # Credenciais Wi-Fi (NÃO versionado — .gitignore)
│       └── wifi_config.h.example
│
├── training/                   # Pipeline Python para KWS
│   ├── requirements.txt        # numpy, scipy, librosa, soundfile, websockets
│   ├── firmware_mfcc.py        # Réplica Python exata do mfcc.c (parâmetros idênticos)
│   ├── extract_features.py     # WAV → MFCC alinhado por onset → .npy
│   ├── generate_templates.py   # .npy → templates.h (seleciona 10 mais centrais)
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
│       │   └── useCollection.ts      # WebSocket /record em modo coleta contínua por palavra
│       ├── components/
│       │   ├── ConnectionPanel.tsx
│       │   ├── RecordingList.tsx / RecordingItem.tsx / StatusBadge.tsx
│       │   ├── AudioVisualizer.tsx   # Canvas waveform/FFT para gravações
│       │   ├── LanguageSelect.tsx
│       │   ├── LiveTranscriptPanel.tsx
│       │   ├── StreamVisualizer.tsx
│       │   ├── MonitorTab.tsx        # Aba Monitor: WS /monitor + threshold DTW + log JSONL
│       │   └── CollectionTab.tsx     # Aba Coleta: gravações em série por palavra + export .zip
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
        ├── kws-rejection-model/
        └── speech-recognition/
```

## Responsabilidades por Área

### `firmware/main/` — Firmware

| Arquivo | Responsabilidade |
|---|---|
| `poc-microfone.c` | Toda lógica: I2S, WebSocket, Wi-Fi, FreeRTOS tasks, KWS, LED (~846 linhas) |
| `mfcc.c/h` | Extração MFCC: Hann window, mel filterbank (300-8000 Hz, 26 mels), DCT-II |
| `dtw.c/h` | DTW com banda Sakoe-Chiba (window configurável) |
| `templates.h` | Templates MFCC em C (auto-gerado por `generate_templates.py`) |
| `wifi_config.h` | Credenciais SSID/senha (arquivo local, não versionado) |

**Pinout:**
- GPIO 26 → BCLK, GPIO 25 → WS, GPIO 22 → DATA IN
- GPIO 4 → Botão (pull-up interno), GPIO 2 → LED onboard

### `training/` — Pipeline KWS

| Arquivo | Responsabilidade |
|---|---|
| `firmware_mfcc.py` | Réplica Python do `mfcc.c` (parâmetros idênticos para garantir alinhamento) |
| `extract_features.py` | Carrega WAV, alinha onset pelo final, extrai MFCC, salva .npy |
| `generate_templates.py` | Carrega .npy, seleciona 10 templates mais centrais, gera `templates.h` |
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
| Monitor KWS | `components/MonitorTab.tsx` | WS /monitor, threshold DTW, log JSONL |
| Coleta UI | `components/CollectionTab.tsx` | UI de coleta, download .zip |
| Log monitor | `utils/monitorLogger.ts` | Sessão em memória, stats, export JSONL |

## Ausências Notáveis

- Sem `sdkconfig.defaults` — configuração não versionada de forma reproduzível
- Sem `partitions.csv` — usa tabela de partições padrão do ESP-IDF
- Sem testes (firmware, frontend ou training)
- Sem CI/CD
