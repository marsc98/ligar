# Architecture

**Analisado:** 2026-06-01
**Padrão:** Firmware monolítico + SPA standalone (sem backend intermediário)

## Visão Geral

Dois sistemas independentes que se comunicam via WebSocket sobre LAN:

```
┌──────────────────────────────────────┐      LAN (Wi-Fi)        ┌──────────────────────────────────────┐
│              ESP32                   │◀───────────────────────▶│   Browser (SPA — 3 abas)             │
│                                      │   ws://IP/record        │                                      │
│  INMP441 ──I2S──▶ audio_task         │   ws://IP/stream        │ aba Gravações: useConnection         │
│  Botão GPIO4 ──▶ button_task         │   ws://IP/monitor       │ aba Gravações: useStream             │
│  LED GPIO2 ◀── audio_task/kws_task   │   GET /threshold?v=     │ aba Monitor:   MonitorTab + WS       │
│  esp_http_server (porta 80)          │                         │ aba Coleta:    CollectionTab         │
│  kws_task (MFCC+DTW)                 │                         │ useWhisper (Web Worker)              │
└──────────────────────────────────────┘                         │ useRecordings (IndexedDB)            │
                                                                 └──────────────────────────────────────┘

       training/ (Python)
       ┌─────────────────────────────────────────────────────┐
       │ samples/*.wav → extract_features.py → features/*.npy │
       │ features/*.npy → generate_templates.py → templates.h  │
       └────────────────────────────┬────────────────────────┘
                                    │ (commitar templates.h)
                                    ▼
                              firmware/main/templates.h → idf.py build
```

## Firmware — Componentes e Fluxo

### Tasks FreeRTOS

| Task | Stack | Prioridade | Responsabilidade |
|---|---|---|---|
| `audio_task` | 8192 bytes | 10 | Machine de estados, leitura I2S, envio WS |
| `kws_task` | 8192 bytes | 6 | KWS contínuo: ring buffer → VAD → MFCC → DTW → /monitor |
| `button_task` | 2048 bytes | 5 | Polling GPIO + debounce, publica em `g_click_queue` |
| (main_task) | padrão IDF | — | Init, cria tasks e termina |

**Nota:** `kws_task` lê I2S em paralelo com `audio_task`. Quando `audio_task` está em RECORDING ou STREAMING, `kws_task` faz vTaskDelay(10ms) e cede o I2S exclusivamente para `audio_task`.

### Machine de Estados (audio_task)

```
      ┌─────────┐
      │  IDLE   │◀──────────────────────────────────┐
      └────┬────┘                                   │
           │ click + stream_fd≥0    click + record_fd≥0
           ▼                               ▼
      ┌──────────┐               ┌──────────────┐
      │STREAMING │               │  RECORDING   │
      │ LED on   │               │  LED on      │
      └──────┬───┘               └──────┬───────┘
             │ click                    │ click
             ▼                          ▼
          IDLE (LED off)    "RECORDING_END:<n>" → IDLE (LED off)
```

**Nota:** IDLE dá prioridade a `/stream` se ambos os fds estiverem conectados. Nos estados RECORDING e STREAMING, `kws_task` cede o I2S.

### Fluxo KWS (kws_task)

```
I2S read (chunk 512 amostras)
  → ring buffer circular (8000 amostras / 0.5s) [posição g_kws_ring_pos]
  → VAD: RMS ≥ 300 → acumula; RMS < 300 → fim de segmento voiced
  → filtros: min 3 chunks voiced; cooldown 1000ms após detecção
  → mfcc_compute(ring, pos, size, out) → g_mfcc_out [48×13]
  → compute_temporal_var() ≥ 0.3 (filtra silêncio/ruído)
  → DTW vs todos os templates (melhor distância por palavra)
  → rejeição garbage: best_dist/garbage_dist < 0.75
  → resultado → JSON → ws_send_text(server, monitor_fd, json)
```

**Alinhamento no ring buffer:** O ring captura amostras continuamente; quando `s_was_voiced` cai para falso, o ring contém os 0.5s terminando na posição da última amostra silenciosa. A palavra alinhada ao final é a premissa do pipeline — se a palavra ocupar menos que 0.5s, haverá silêncio inicial (padding natural).

### Sincronização

- `g_ws_mutex` — Mutex que protege `g_ws_record_fd` e `g_ws_stream_fd`
- `g_click_queue` — `QueueHandle_t(depth=1)` entre `button_task` e `audio_task`; substitui o antigo `volatile bool g_button_pressed` + ISR (race condition eliminada)

### I2S

- **Modo:** Philips Standard (1-bit delay após WS, `bit_shift=true`)
- **Formato:** 32-bit por slot, mono, Left channel, 16kHz
- **INMP441:** envia 24-bit nos MSBs da palavra de 32-bit
- **Conversão:** `raw32[i] >> 16` → 16-bit base, depois `× MIC_GAIN (16)` com saturação

### Protocolo WAV sem buffer

O ESP32 **não** mantém buffer de áudio em memória. Em `APP_RECORDING`:
1. Lê I2S → envia chunk PCM raw (512 amostras = 1024 bytes) diretamente via WS
2. Mantém contador `g_record_len` (amostras enviadas)
3. No 2º click: envia `RECORDING_END:<g_record_len>` e volta ao IDLE
4. O cliente monta o WAV: header calculado a partir de `<n>` + chunks recebidos

**Eliminação do limite de 3s:** sem buffer de gravação na heap, a gravação é limitada apenas pela duração da sessão WS — não há mais limite rígido de memória.

## Frontend — Componentes

### Hooks (lógica de negócio)

| Hook | Responsabilidade |
|---|---|
| `useConnection` | WebSocket `/record`, estado da sessão, coleta de chunks PCM, montagem WAV ao receber `RECORDING_END` |
| `useRecordings` | CRUD IndexedDB, lista de gravações em memória, `updateTranscription` |
| `useAudioPlayer` | Reprodução via `<audio>`, gestão de Object URLs |
| `useStream` | WebSocket `/stream`, buffer acumulado, detecção de silêncio por RMS, janelas para Whisper |
| `useStreamVisualizer` | Canvas 2D, ring buffer 4096 samples, waveform/FFT em requestAnimationFrame |
| `useWhisper` | Web Worker + fila de jobs, modelo `whisper-tiny` ONNX, transcrição de gravações e stream ao vivo |
| `useCollection` | WebSocket `/record` em modo coleta contínua, auto-gravar por palavra, exportar .zip |

### Fluxo de dados — Gravação

```
WebSocket /record
  → onmessage (binary) → chunksRef (ArrayBuffer[])
  → onmessage "RECORDING_START" → limpa chunks
  → onmessage "RECORDING_END:<n>" → assemblePcmToWav(chunks, n) → Blob WAV
  → addRecording() → IndexedDB
  → transcribe(id, blob) → useWhisper queue
  → updateTranscription(id, text) → re-render
```

### Fluxo de dados — Stream ao Vivo

```
WebSocket /stream
  → onmessage (binary Int16Array)
  → useStream.onChunk → useStreamVisualizer.pushChunk (ring buffer)
  → RMS threshold → useStream.onWindow (Float32Array janela)
  → useWhisper.transcribeLive → worker → onLive(text) → liveText state
  → Canvas requestAnimationFrame → waveform + FFT
```

### Persistência

- **IndexedDB** `poc-microfone` v1 — object store `recordings` com índice `by-timestamp`
- Blobs WAV armazenados inline + campo `transcription?: string`
- Ordenação: mais recente primeiro

## Protocolo WebSocket

### `/monitor` — Stream de eventos KWS

```
cliente              ESP32
  │──── connect ────▶│
  │◀── JSON heartbeat (1 Hz) ──│  {"rms":..., "threshold":..., "word":null, "dists":{}}
  │◀── JSON evento voiced ──────│  {"rms":..., "threshold":..., "var":..., "word":"ligar"|null, "dists":{"ligar":3.2,"garbage":4.1}, "rejected":"garbage_ratio"|null}
```

**Format JSON completo:** `rms`, `threshold`, `var`, `word` (string ou null), `dists` (objeto word→dist), `garbage_dist?`, `rejected?` (string motivo), `ts` (adicionado client-side).

### `/threshold` — Controle de sensibilidade

```
GET /threshold?v=2.5  →  HTTP 200 "OK"
```

Atualiza `g_dtw_threshold` em tempo real. Enviado pelo MonitorTab via `fetch()` com debounce 150ms.

### `/record` — Gravação por chunks sem buffer

```
cliente         ESP32
  │──── connect ────▶│
  │◀── "RECORDING_START" (text) ──│  (1º click com /record conectado)
  │◀── PCM chunk 1024 bytes ──────│  (loop: I2S → WS direto)
  │◀── PCM chunk 1024 bytes ──────│
  │      ...
  │◀── "RECORDING_END:<n>" (text) │  (2º click — n = total de amostras)
```

**Cliente monta WAV:** `buildWavHeader(n)` + todos os chunks → `Blob audio/wav`

### `/stream` — Stream em tempo real

```
cliente         ESP32
  │──── connect ────▶│
  │◀── PCM raw 1024 bytes ──│  (1º click — loop enquanto STREAMING)
  │◀── PCM raw 1024 bytes ──│
  │      ...
  │◀── (encerra) ───────────│  (2º click)
```
