# Code Conventions

**Analisado:** 2026-06-10

## Firmware (C)

### Naming

| Padrão | Exemplos |
|---|---|
| `g_` prefix | `g_state`, `g_record_len`, `g_ws_mutex`, `g_click_queue`, `g_rx_chan` |
| `snake_case` | `i2s_read_16bit`, `fill_wav_header`, `audio_task`, `button_task` |
| `UPPER_CASE` | `APP_IDLE`, `PIN_BCLK`, `SAMPLE_RATE`, `MIC_GAIN`, `PIN_LED` |
| `_t` suffix | `app_state_t`, `wav_header_t` |
| `TAG` constante | `static const char *TAG = "INMP441"` |

### Estrutura modular

O firmware é dividido em 4 camadas:

```
app_main.c          — ponto de entrada; orquestra init e criação de tasks
app_state.h         — globals compartilhados (extern): sem lógica, só declarações
drivers/            — abstração de hardware (I2S, LEDC); sem FreeRTOS no header público
kws/                — algoritmos puros (MFCC, DTW, templates, color_catalog); sem deps de tasks
tasks/              — tasks FreeRTOS; dependem de drivers/ e kws/; nunca ao contrário
```

Cada módulo expõe apenas o necessário via header. Funções internas são `static`.

### Estilo

- Funções e variáveis de arquivo são `static`; funções públicas declaradas no `.h` correspondente
- `ESP_ERROR_CHECK()` em todas as chamadas de init — crash-fast
- Comentários em Português (BR)
- Blocos de seção delimitados com `/* ── NOME ── */`
- Sem ISR handlers — botão usa polling + debounce por software em task dedicada
- `TAG` por arquivo: `static const char *TAG = "MODULO"`

### Alocação de memória

```c
// Buffer I2S em audio_task — deve ser DMA-capable
read_buf = heap_caps_malloc(I2S_READ_BYTES, MALLOC_CAP_DMA);

// Ring buffer KWS — stack estático (s_kws_ring[MFCC_WIN_SAMPLES])
// Sem buffer de gravação — PCM vai direto I2S → WS sem acumular
```

### Debounce do botão

Polling a 10ms, 5 leituras estáveis = 50ms de debounce. Click disparado no falling edge (pressionar); soltar não gera evento. Evento publicado via `xQueueSend(g_click_queue, &evt, 0)`.

### LED RGB

Comandos enviados via `xQueueSend(g_led_queue, &cmd, 0)` onde `cmd` é `led_command_t {r, g, b}`. `led_task` consume a fila e chama `ledc_set_color()`. Bloqueio não-urgente — `xQueueSend` com timeout 0.

### Logging

```c
ESP_LOGI(TAG, "mensagem normal");
ESP_LOGW(TAG, "aviso");
ESP_LOGE(TAG, "erro crítico");
```

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

### Invariantes críticas

1. `firmware_mfcc.py` deve ser **numericamente idêntico** a `mfcc.c`. Qualquer alteração nos parâmetros MFCC (frame length, hop, n_mels, n_coefs, fmin, fmax) deve ser espelhada em ambos.
2. `firmware_mlp.py` deve ser **numericamente idêntico** a `mlp.c` com os pesos de `weights.h`. Validação: rodar `firmware_mlp.py <wav>` e comparar com inferência C no firmware.
3. A ordem das classes em `MLP_CLASS_NAMES[]` em `weights.h` deve bater exatamente com a ordem gerada por `train_mlp.py`.

### Alinhamento de onset

`extract_features.py` alinha a janela de 0.5s pelo **final da palavra** (onset = última amostra voiced + 50ms de contexto). Treino e inferência precisam usar a mesma convenção; divergências causam zero detecções (ver CONCERNS).
