# Code Conventions

**Analisado:** 2026-06-01

## Firmware (C)

### Naming

| Padrão | Exemplos |
|---|---|
| `g_` prefix | `g_state`, `g_record_len`, `g_ws_mutex`, `g_click_queue`, `g_rx_chan` |
| `snake_case` | `i2s_read_16bit`, `fill_wav_header`, `audio_task`, `button_task` |
| `UPPER_CASE` | `APP_IDLE`, `PIN_BCLK`, `SAMPLE_RATE`, `MIC_GAIN`, `PIN_LED` |
| `_t` suffix | `app_state_t`, `wav_header_t` |
| `TAG` constante | `static const char *TAG = "INMP441"` |

### Estilo

- Todas as funções e variáveis de arquivo são `static` — zero símbolo público além de `app_main`
- `__attribute__((packed))` em structs de protocolo (`wav_header_t`)
- `ESP_ERROR_CHECK()` em todas as chamadas de init — crash-fast
- Comentários em Português (BR)
- Blocos de seção delimitados com `/* ─────────────── NOME ─────────────── */`
- Sem ISR handlers — botão usa polling + debounce por software em task dedicada

### Alocação de memória

```c
// Buffer I2S — deve ser DMA-capable
read_buf = heap_caps_malloc(I2S_READ_BYTES, MALLOC_CAP_DMA);

// Sem buffer de gravação — PCM vai direto I2S → WS sem acumular
```

### Debounce do botão

Polling a 10ms, 5 leituras estáveis = 50ms de debounce. Click disparado no falling edge (pressionar); soltar não gera evento. Evento publicado via `xQueueSend(g_click_queue, &evt, 0)`.

### Logging

```c
ESP_LOGI(TAG, "mensagem normal");
ESP_LOGW(TAG, "aviso");
ESP_LOGE(TAG, "erro crítico");
```

### Ordem de seções no arquivo

1. Includes
2. `#include "wifi_config.h"` (credenciais externas)
3. `#define` de pinos e configuração
4. Estado global (enum + variáveis `static`)
5. Struct WAV + helper
6. I2S init + read
7. WebSocket handlers
8. Audio task
9. Button task
10. Wi-Fi init
11. HTTP server init
12. GPIO init
13. `app_main`

---

## Frontend (TypeScript/React)

### Naming

| Padrão | Exemplos |
|---|---|
| `camelCase` | `addRecording`, `currentId`, `chunksRef`, `liveText` |
| `PascalCase` | `ConnectionPanel`, `RecordingList`, `LiveTranscriptPanel`, `StreamVisualizer` |
| `use` prefix | `useConnection`, `useRecordings`, `useStream`, `useWhisper`, `useStreamVisualizer` |
| Interfaces sem prefixo | `Recording`, `ConnectionState`, `VisualizerMode` (sem prefixo I) |

### Estilo

- Arrow functions em todos os handlers e hooks
- Props tipadas via interface inline ou em `types.ts`
- Sem `any`, sem `as unknown as X`
- Sem comentários — código autoexplicativo por nomes
- Imports organizados: react → hooks → components → types → lib
- Estilização via objeto literal inline (`style={{ ... }}`) — sem classes CSS

### Hooks

- Um hook por responsabilidade (sem god-hooks)
- `useRef` para estado que não deve causar re-render (`chunksRef`, `timerRef`, `stateRef`, `bufferRef`, `wsRef`)
- `stateRef` espelha `state` para uso em closures de callbacks assíncronos
- Workers instanciados dentro de `useEffect` com cleanup via `worker.terminate()`

### Tipos

Centralizados em `src/types.ts`:
```ts
interface Recording { id, name, timestamp, duration, size, blob, transcription?, collection? }
type ConnectionState = 'disconnected' | 'connecting' | 'idle' | 'recording' | 'receiving' | 'saving'
type VisualizerMode = 'waveform' | 'fft' | 'both'
```

---

## Pipeline de Treinamento (Python)

### Naming

- `snake_case` em tudo: funções, variáveis, arquivos
- Constantes em `UPPER_CASE`: `SAMPLE_RATE`, `TEMPLATE_COUNT`, `N_FRAMES`, `N_COEFS`
- Docstrings no topo do arquivo descrevendo uso CLI (não nas funções)

### Invariante crítica

`firmware_mfcc.py` deve ser **numericamente idêntico** a `mfcc.c`. Qualquer alteração nos parâmetros MFCC (frame length, hop, n_mels, n_coefs, fmin, fmax) deve ser espelhada em ambos. O `extract_features.py` usa `firmware_mfcc.py` diretamente; `generate_templates.py` usa apenas os shapes `(N_FRAMES, N_COEFS)`.

### Alinhamento de onset

`extract_features.py` alinha a janela de 0.5s pelo **final da palavra** (onset = última amostra voiced + 50ms de contexto). Treino e inferência precisam usar a mesma convenção; divergências causam zero detecções (ver CONCERNS).
