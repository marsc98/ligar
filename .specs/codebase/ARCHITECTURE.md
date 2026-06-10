# Architecture

**Analisado:** 2026-06-10
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
│  LED GPIO23 ◀── kws_task(AWAIT_CLR) │   GET /led?color=&int=  │ aba Coleta:    CollectionTab         │
│  LED RGB GPIO18/19/21 ◀── led_task   │                         │ aba Luz:       LightTab              │
│  esp_http_server (porta 80)          │                         │ useWhisper (Web Worker)              │
│  kws_task (MFCC+MLP, 2 estágios)     │                         │ useRecordings (IndexedDB)            │
└──────────────────────────────────────┘                         └──────────────────────────────────────┘

       training/ (Python)
       ┌─────────────────────────────────────────────────────┐
       │ samples/*.wav → extract_features.py → features/*.npy │
       │ features/*.npy → train_mlp.py → kws/weights.h        │
       └────────────────────────────┬────────────────────────┘
                                    │ (commitar weights.h)
                                    ▼
                           firmware/main/kws/weights.h → idf.py build
```

## Firmware — Componentes e Fluxo

### Tasks FreeRTOS

| Task | Stack | Prioridade | Responsabilidade |
|---|---|---|---|
| `i2s_reader_task` | 4096 bytes | 12 | Leitura exclusiva do I2S DMA; distribui chunks via `g_audio_queue` e `g_kws_queue` |
| `audio_task` | 8192 bytes | 10 | Machine de estados, consome `g_audio_queue`, envio WS /record e /stream |
| `kws_task` | 8192 bytes | 6 | KWS 2-estágios contínuo: consome `g_kws_queue` → VAD → MFCC → MLP → /monitor |
| `button_task` | 2048 bytes | 5 | Polling GPIO4 + debounce 50ms, publica em `g_click_queue` |
| `led_task` | 2048 bytes | 5 | Consume `g_led_queue`, chama `ledc_set_color(r,g,b)` |
| (main_task) | padrão IDF | — | Init, cria tasks e termina |

**Nota:** `i2s_reader_task` é o único leitor do hardware I2S. `audio_task` e `kws_task` consomem de filas separadas — nenhuma disputa de DMA. `kws_task` roda continuamente (não pausa durante gravação).

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

**Nota:** IDLE dá prioridade a `/stream` se ambos os fds estiverem conectados. KWS roda em paralelo e de forma independente ao estado da machine.

### Fluxo KWS (kws_task) — 2 estágios (contínuo)

```
g_kws_queue ← i2s_reader_task (chunk 512 amostras)
  → ring buffer circular (8000 amostras / 0.5s) [posição s_kws_ring_pos]
  → VAD: RMS ≥ 300 → acumula; RMS < 300 → fim de segmento voiced
  → filtros: min 3 chunks voiced; cooldown 1000ms após detecção
  → mfcc_compute(ring, pos, size, out) → s_mfcc_out [48×13 = 624 floats]
  → compute_temporal_var() ≥ 0.3 (rejeita silêncio/ruído como "var_gate")
  → mlp_infer(mfcc, probs) → probs[10] (softmax)
  → best = argmax(probs); valid = probs[best] ≥ threshold && best ≠ garbage_idx

  SE modo KWS_IDLE:
    → "ligar" detectado → modo KWS_AWAIT_COLOR (timeout 2s)
    → "desligar" detectado → led_queue{0,0,0} (apaga LED RGB)
    → resultado → JSON {"probs":{...}, "kws_mode":"idle", ...} → /monitor

  SE modo KWS_AWAIT_COLOR:
    → cor detectada → color_lookup() → led_queue{r,g,b} → modo KWS_IDLE
    → "desligar" → apaga LED → modo KWS_IDLE
    → "ligar" → reinicia timer (2s)
    → timeout 2s → volta KWS_IDLE
    → resultado → JSON {"probs":{...}, "kws_mode":"await_color", ...} → /monitor
```

**RGB LED:** `led_task` consome `g_led_queue` e chama `ledc_set_color()`. LED onboard GPIO2 pisca 200ms em qualquer detecção; LED RGB GPIO18/19/21 mantém a cor indefinidamente até novo comando.

**Alinhamento no ring buffer:** O ring captura amostras continuamente; quando `s_was_voiced` cai para falso, o ring contém os 0.5s terminando na posição da última amostra silenciosa. A palavra alinhada ao final é a premissa do pipeline — se a palavra ocupar menos que 0.5s, haverá silêncio inicial (padding natural).

### Sincronização

- `g_ws_mutex` — Mutex que protege `g_ws_record_fd`, `g_ws_stream_fd` e `g_ws_monitor_fd`
- `g_click_queue` — `QueueHandle_t(depth=1)` entre `button_task` e `audio_task`; substitui o antigo `volatile bool g_button_pressed` + ISR (race condition eliminada)
- `g_audio_queue` — Fila de chunks PCM de `i2s_reader_task` para `audio_task`
- `g_kws_queue` — Fila de chunks PCM de `i2s_reader_task` para `kws_task`

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
| `useLightControl` | Envia `GET /led?color=&intensity=` para controle direto do LED RGB |

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

**Format JSON completo:** `rms`, `threshold` (valor atual do MLP threshold), `var?`, `word` (string ou null), `probs` (objeto classe→probabilidade, 10 classes), `rejected?` ("too_short" | "cooldown" | "var_gate"), `kws_mode` ("idle" | "await_color"), `ts` (adicionado client-side).

> Schema TypeScript completo em `KWS_FLOW_CONTRACT.md`.

### `/threshold` — Controle de sensibilidade

```
GET /threshold?v=2.5  →  HTTP 200 "OK"
```

Atualiza `g_dtw_threshold` em tempo real. Enviado pelo MonitorTab via `fetch()` com debounce 150ms.

### `/led` — Controle direto do LED RGB

```
GET /led?color=vermelho&intensity=80  →  HTTP 200 "OK"
```

Aciona o LED RGB diretamente (sem KWS). `intensity` é 0–100. Handler em `kws_task.c`.

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

---

## Hardware — Circuito LED RGB

### Topologia dos Transistores (BC547, NPN)

Cada canal (R, G, B) usa um BC547 como chave ativa-baixa:

```
5V ──── 220Ω ──── COLETOR ──── ânodo(s) LED
                               cátodo(s) LED ──── GND
                  BC547
BASE ◀── 1kΩ ◀── GPIO (3.3V)
EMISSOR ──── GND
```

**Comportamento:**
- GPIO HIGH (duty=255) → transistor ON → coletor ≈ Vce_sat (0.1V) → LED **apagado**
- GPIO LOW (duty=0) → transistor OFF → coletor puxado a 5V via 220Ω → LED **aceso**

O circuito é **ativo-baixo**: `duty=0` acende, `duty=255` apaga. O firmware deve inverter os valores antes de passar ao LEDC: `duty_real = 255 - valor_solicitado`.

### Corrente com múltiplos LEDs em paralelo (220Ω compartilhado)

O resistor de 220Ω limita a corrente **total** do canal, não por LED:

| Cor   | Vf típico | I_total = (5V − Vf) / 220Ω | I por LED (5 LEDs) |
|-------|-----------|-----------------------------|--------------------|
| Vermelho | ~2.0V  | ~13.6mA                     | ~2.7mA             |
| Verde    | ~2.2V  | ~12.7mA                     | ~2.5mA             |
| Azul     | ~3.2V  | ~8.2mA                      | ~1.6mA             |

Azul recebe significativamente menos corrente que vermelho/verde, causando desequilíbrio de cor em misturas (branco fica amarelado). Para balancear: usar um resistor por LED (ex.: 220Ω × 5 = um 220Ω por LED) ou reduzir o resistor compartilhado para ~47Ω.

### Pinos GPIO → Canal LEDC

| GPIO | Canal LEDC | Cor |
|------|-----------|-----|
| 18   | CH0       | Vermelho |
| 19   | CH1       | Verde    |
| 21   | CH2       | Azul     |
