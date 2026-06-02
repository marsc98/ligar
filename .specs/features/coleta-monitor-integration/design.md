# Coleta + Monitor Integration — Design

**Data:** 2026-05-24
**Spec:** `spec.md`
**Contexto:** `context.md`

---

## Visão Geral

A implementação tem três camadas independentes que se executam em paralelo:

```
┌─────────────────────────────────────────────────────────────────┐
│  CAMADA 1 — Reestruturação do repo (pré-requisito)              │
│  main/ → firmware/main/  +  training/  +  CMakeLists  +  Makefile│
└──────────────────────────────────┬──────────────────────────────┘
                                   │
              ┌────────────────────┴────────────────────┐
              ▼                                         ▼
┌─────────────────────────┐              ┌──────────────────────────┐
│  CAMADA 2 — Firmware    │              │  CAMADA 3 — Frontend      │
│  kws_task + /monitor    │              │  MonitorTab + CollectionTab│
│  g_kws_paused + timeouts│◀──ws://IP/monitor──▶MonitorTab           │
│  mfcc.c + dtw.c         │◀──ws://IP/record ──▶useCollection        │
└─────────────────────────┘              └──────────────────────────┘
```

---

## Camada 1 — Reestruturação de Arquivos

### Movimentação

```
ANTES                              DEPOIS
─────────────────────────          ─────────────────────────────────
poc-microfone/                     poc-microfone/
├── CMakeLists.txt                 ├── CMakeLists.txt          (mod)
├── main/                          ├── Makefile                (novo)
│   ├── CMakeLists.txt             ├── README.md               (mod)
│   ├── poc-microfone.c            ├── firmware/               (novo)
│   └── wifi_config.h              │   └── main/
├── web/                           │       ├── CMakeLists.txt  (mod)
├── training/  ← não existe        │       ├── poc-microfone.c
└── .specs/                        │       ├── wifi_config.h
                                   │       ├── mfcc.c          (novo)
                                   │       ├── mfcc.h          (novo)
                                   │       ├── dtw.c           (novo)
                                   │       ├── dtw.h           (novo)
                                   │       └── templates.h     (novo)
                                   ├── training/               (novo)
                                   │   ├── firmware_mfcc.py
                                   │   ├── extract_features.py
                                   │   ├── generate_templates.py
                                   │   ├── requirements.txt
                                   │   ├── samples/.gitkeep
                                   │   └── features/.gitkeep
                                   ├── web/
                                   └── .specs/
```

### CMakeLists.txt raiz — diff

```cmake
# ANTES
cmake_minimum_required(VERSION 3.22)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(poc-microfone)

# DEPOIS
cmake_minimum_required(VERSION 3.22)
list(APPEND EXTRA_COMPONENT_DIRS "firmware")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(poc-microfone)
```

### firmware/main/CMakeLists.txt — diff

```cmake
# ANTES
idf_component_register(SRCS "poc-microfone.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver esp_wifi esp_event esp_netif nvs_flash esp_http_server
                    PRIV_REQUIRES esp_driver_gpio esp_driver_i2s)

# DEPOIS — adicionar mfcc.c dtw.c
idf_component_register(SRCS "poc-microfone.c" "mfcc.c" "dtw.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver esp_wifi esp_event esp_netif nvs_flash esp_http_server
                    PRIV_REQUIRES esp_driver_gpio esp_driver_i2s)
```

### training/firmware_mfcc.py — única mudança

```python
# ANTES (poc-identificador)
DURATION_S = 1.0

# DEPOIS
DURATION_S = 1.5
```

Consequência em cascata (automática pelo pipeline):
- `N_SAMPLES = 24000`
- `N_FRAMES = (24000 - 400) / 160 + 1 = 148`
- `templates.h` gerado com `KWS_N_FRAMES = 148`

---

## Camada 2 — Firmware KWS

### Novos globals em poc-microfone.c

```c
// após declaração dos fd existentes:
static int   g_ws_monitor_fd = -1;
static bool  g_kws_paused    = false;

// ring buffer para MFCC (24000 amostras = 1.5s a 16kHz)
#define KWS_RING_SIZE MFCC_WIN_SAMPLES   // definido em mfcc.h
static int16_t g_kws_ring[KWS_RING_SIZE];
static int     g_kws_ring_pos = 0;

// inclui após wifi_config.h:
#include "mfcc.h"
#include "dtw.h"
#include "templates.h"
```

### Modificação em ws_record_handler

```c
// quando conexão estabelecida (HTTP_GET):
g_ws_record_fd = httpd_req_to_sockfd(req);
g_kws_paused   = true;   // ← pausa KWS durante /record

// quando conexão fecha (ret != ESP_OK):
g_ws_record_fd = -1;
g_kws_paused   = false;  // ← retoma KWS
```

### Modificação em start_webserver

```c
httpd_config_t config   = HTTPD_DEFAULT_CONFIG();
config.server_port      = 80;
config.max_open_sockets = 5;           // era 4, +1 para /monitor
config.recv_wait_timeout = 120;        // ← novo: suporta coletas de até 120s
config.send_wait_timeout = 120;        // ← novo

// registrar novo endpoint após /stream:
httpd_uri_t ws_monitor_uri = {
    .uri          = "/monitor",
    .method       = HTTP_GET,
    .handler      = ws_monitor_handler,
    .user_ctx     = NULL,
    .is_websocket = true,
};
httpd_register_uri_handler(server, &ws_monitor_uri);
```

### ws_monitor_handler (novo)

Segue exatamente o padrão dos handlers existentes:
- `HTTP_GET`: registra `g_ws_monitor_fd`
- erro no recv: limpa `g_ws_monitor_fd = -1`
- aceita opcionalmente payload `{"threshold": float}` para ajuste do limiar em runtime

### kws_task (nova FreeRTOS task)

```
kws_task(server):
  LOOP:
    if g_kws_paused OR g_state != APP_IDLE:  // ambos por segurança
        delay 10ms
        continue

    n = i2s_read_16bit(chunk, I2S_CHUNK)     // reutiliza função existente
    if n == 0: continue

    for i in 0..n:                           // atualiza ring buffer circular
        g_kws_ring[g_kws_ring_pos] = chunk[i]
        g_kws_ring_pos = (g_kws_ring_pos + 1) % KWS_RING_SIZE

    rms = compute_rms(chunk, n)
    if rms < VAD_RMS_THRESHOLD: continue     // VAD gate

    cooldown check (DETECTION_COOLDOWN_MS)

    mfcc_compute(g_kws_ring, g_kws_ring_pos, KWS_RING_SIZE, mfcc_out)

    DTW contra todos templates → best_word, best_dist, word_best[]

    detected = (best_dist < g_dtw_threshold)

    if g_ws_monitor_fd >= 0:
        emit JSON: {"rms": rms, "threshold": g_dtw_threshold,
                    "word": nome|null, "dists": {palavra: dist, ...}}
```

**Compartilhamento de I2S:** `kws_task` só chama `i2s_read_16bit` quando `g_state == APP_IDLE`. `audio_task` só lê I2S em RECORDING e STREAMING. Sem conflito por design — mesmo sem mutex, as transições de estado garantem exclusão mútua via `g_kws_paused`.

**Criação da task em app_main:**
```c
xTaskCreate(kws_task, "kws_task", 8192, server, 6, NULL);
// prioridade 6: entre button_task (5) e audio_task (10)
```

### Payload JSON do /monitor

```json
{
  "rms": 1234.5,
  "threshold": 800.0,
  "word": "ligar",
  "dists": { "ligar": 650.2, "luminus": 1100.8 }
}
```
- `word`: `null` quando nenhuma detecção; nome da palavra quando `best_dist < threshold`
- `dists`: distância DTW mínima por palavra (best across templates)

---

## Camada 3 — Frontend

### Diagrama de componentes

```
App.tsx
├── [ip state — compartilhado entre todas as abas]
├── TabBar (Gravações | Monitor | Coleta)
│
├── [activeTab === 'gravacoes'] → conteúdo atual do App.tsx
│   ├── ConnectionPanel
│   ├── LiveTranscriptPanel
│   └── RecordingList (com grupos colapsáveis para sessões)
│
├── [activeTab === 'monitor'] → MonitorTab
│   └── gerencia ws://<IP>/monitor internamente
│
└── [activeTab === 'coleta'] → CollectionTab
    └── usa useCollection hook
```

### types.ts — adição

```typescript
export interface Recording {
  id: string
  name: string
  timestamp: number
  duration: number
  size: number
  blob: Blob
  transcription?: string
  collection?: {          // ← novo
    word: string
    sessionId: string
  }
}
```

Sem migração de IndexedDB necessária — store `recordings` é schemaless.

### useCollection.ts — contrato público

```typescript
type CollectionState = 'idle' | 'connecting' | 'collecting' | 'saving'

interface UseCollectionReturn {
  state: CollectionState
  sampleCount: number
  startCollection(ip: string, word: string): void
  stopCollection(): void
}
```

**Lógica interna:**
```
startCollection(ip, word):
  sessionId = crypto.randomUUID()
  word = word (fechado no closure)
  sampleCount = 0
  sampleIndex = 0
  buffer = Int16Array[0]
  ws = new WebSocket(`ws://${ip}/record`)

  onmessage(binary):
    append chunk to buffer
    while buffer.length >= 24000:
      slice = buffer.slice(0, 24000)
      buffer = buffer.slice(24000)
      sampleIndex++
      blob = assemblePcmToWav([slice.buffer], 24000)
      name = `${word}_${String(sampleIndex).padStart(3,'0')}`
      addRecording(blob, 1.5, { collection: { word, sessionId } })
      sampleCount++

  onmessage("RECORDING_END:<n>"):
    n = parseInt(n)
    if buffer.length >= 8000:
      blob = assemblePcmToWav([buffer.buffer], buffer.length)
      name = `${word}_${String(++sampleIndex).padStart(3,'0')}`
      addRecording(blob, buffer.length/16000, { collection: { word, sessionId } })
      sampleCount++
    ws.close()

stopCollection():
  ws.close()
```

**Nota:** `useCollection` chama `addRecording` de `useRecordings` passado como callback (padrão idêntico ao `useConnection`).

### MonitorTab.tsx — estrutura

```typescript
interface MonitorEvent {
  ts: number          // Date.now()
  rms: number
  threshold: number
  word: string | null
  dists: Record<string, number>
}

// estado interno:
const [events, setEvents] = useState<MonitorEvent[]>([])
const [connected, setConnected] = useState(false)
const wsRef = useRef<WebSocket | null>(null)

// useEffect com cleanup:
// - conecta quando ip disponível
// - desconecta no cleanup (saída da aba via unmount)
// - cada mensagem: parse JSON, prepend, slice(0, 200)

// renderização:
// - lista de eventos (mais recente no topo)
// - linha normal: fundo transparente, texto #94a3b8
// - linha com detecção (word != null):
//     fundo #22c55e18, borda-esquerda 3px #22c55e
//     badge colorido com nome da palavra
```

### CollectionTab.tsx — estrutura

```typescript
// props: ip: string (vem do App.tsx, sem duplicar estado)
// hook: useCollection

// layout:
// 1. Campo: nome da palavra
// 2. Botão "Iniciar Coleta" — disabled se !ip || !word
// 3. Durante coleta:
//    - "Gravando: <palavra> ● N amostras capturadas"
//    - indicador: "✂ corte automático a cada 1.5s"
//    - botão "Encerrar Coleta"
// 4. Lista da sessão: <palavra>_001.wav  1.5s  (vai aparecendo ao vivo)
```

### RecordingList.tsx — agrupamento

```typescript
// separar recordings em dois grupos:
const sessions = new Map<string, Recording[]>()
const individual: Recording[] = []

for (const r of recordings) {
  if (r.collection) {
    const key = r.collection.sessionId
    sessions.set(key, [...(sessions.get(key) ?? []), r])
  } else {
    individual.push(r)
  }
}

// montar lista unificada com timestamp representativo:
type ListItem =
  | { kind: 'individual'; recording: Recording }
  | { kind: 'session'; sessionId: string; word: string; items: Recording[]; ts: number }

// ordenar por ts desc, intercalando grupos e individuais
// grupo colapsável: ▶ <palavra> — N amostras — <data>
// expandido: RecordingItem para cada amostra
```

---

## Makefile raiz — targets

```makefile
# Variáveis com defaults
PORT ?= /dev/ttyUSB0
BAUD ?= 115200

## Firmware
firmware-build:      idf.py build
firmware-flash:      idf.py -p $(PORT) flash
firmware-monitor:    idf.py -p $(PORT) monitor
firmware-clean:      idf.py fullclean

## Web
web-dev:             cd web && npm run dev
web-build:           cd web && npm run build

## Training
train:               python3 training/extract_features.py $(WORD)  # requer WORD=
train-templates:     python3 training/generate_templates.py

## Setup
setup:               pip3 install -r training/requirements.txt && cd web && npm install

## Pipelines combinados
pipeline:            [requer WORD] → train + train-templates + firmware-build
flash:               [requer WORD] → pipeline + firmware-flash

## Help
help:                imprime todos os targets com descrição
```

---

## Fluxo cross-módulo

```
1. Coletar amostras (UI Coleta)
   → WAV salvo no IndexedDB com collection.word = "ligar"

2. Baixar WAVs → training/samples/ligar_001.wav ...

3. make train WORD=ligar
   → training/extract_features.py ligar
   → gera training/features/ligar.npy

4. make train-templates
   → training/generate_templates.py
   → gera firmware/main/templates.h

5. make firmware-build
   → compila firmware com templates atualizados

6. make firmware-flash PORT=/dev/ttyUSB0
   → flasha para ESP32

7. UI Monitor
   → feedback visual em tempo real do KWS com as novas palavras
```

---

## Decisões de design

| Decisão | Escolha | Razionale |
|---|---|---|
| Compartilhamento I2S entre kws_task e audio_task | Exclusão via g_kws_paused + check g_state | Sem mutex adicional; transições de estado garantem segurança |
| Threshold DTW ajustável | Campo `g_dtw_threshold` + payload no /monitor | Permite tuning em runtime sem re-flash |
| WebSocket /monitor no frontend | Gerenciado dentro de MonitorTab (não hook separado) | Uma conexão única, ciclo de vida atado ao mount/unmount da aba |
| useCollection vs reusar useConnection | Hook separado | Lógica de corte periódico é diferente o suficiente para justificar separação |
| max_open_sockets | 5 (era 4) | +1 para /monitor concurrent com /record e /stream |
