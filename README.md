# ligar

ESP32 + INMP441 — keyword spotting por voz para controle de LED RGB.

Diga **"ligar"** seguido de uma cor e o LED acende. Diga **"desligar"** e apaga.
Interface web embarcada para coleta de amostras, monitoramento KWS em tempo real e transcrição Whisper.

---

## Diagrama do circuito

```
                        ┌──────────────────────────────────────────────────────────┐
                        │                      ESP32-D0WD                          │
                        │                                                          │
  INMP441               │  GPIO 26 ◀── BCLK ──────────┐                           │
 ┌─────────┐            │  GPIO 25 ◀── WS ────────────┤  I2S (16 kHz, mono)       │
 │ VDD 3.3V│───── 3.3V  │  GPIO 22 ◀── SD ────────────┘  32-bit slot, Philips     │
 │ GND     │───── GND   │                                                          │
 │ SCK     │─────────── │  GPIO 26                                                 │
 │ WS      │─────────── │  GPIO 25                                                 │
 │ SD      │─────────── │  GPIO 22                                                 │
 │ L/R     │───── GND   │                                                          │
 └─────────┘            │  GPIO 4 ──── [BOTÃO] ──── GND   (pull-up interno)        │
                        │  GPIO 2 ──── [LED onboard]                               │
                        │                                                          │
                        │  GPIO 18 ──┐                         5V                  │
                        │  GPIO 19 ──┤── 1kΩ ── BASE [BC547]   │                  │
                        │  GPIO 21 ──┘          COLETOR ──── 220Ω ──── ânodos      │
                        │                       EMISSOR ──── GND        (×10 LEDs) │
                        │                                   cátodos ─── GND        │
                        │                                                          │
                        │  Wi-Fi STA (WPA2)                                        │
                        └───────────────────────────┬──────────────────────────────┘
                                                    │ LAN (ws://<IP>/...)
                                          ┌─────────▼──────────┐
                                          │   Browser (SPA)    │
                                          │                    │
                                          │ aba Gravações      │
                                          │ aba Monitor KWS    │
                                          │ aba Coleta         │
                                          └────────────────────┘
```

### Circuito RGB (transistor BC547, ativo-baixo)

```
5V ──── 220Ω ──── COLETOR ──┬── ânodo LED 1
                             ├── ânodo LED 2   (até 10 LEDs em paralelo por canal)
         BC547               └── ...
BASE ◀── 1kΩ ◀── GPIO (3.3V)    cátodos ──── GND
EMISSOR ──── GND

  GPIO LOW  (duty=0)   → transistor OFF → 5V nos ânodos → LED ACESO
  GPIO HIGH (duty=255) → transistor ON  → Vce_sat ≈ 0V  → LED APAGADO
  (firmware inverte: duty_real = 255 − valor_solicitado)
```

| Canal | GPIO | LEDC | Resistor | I_total (5 LEDs) |
|-------|------|------|----------|-----------------|
| Vermelho (R) | 18 | CH0 | 220 Ω | ~13,6 mA |
| Verde (G)    | 19 | CH1 | 220 Ω | ~12,7 mA |
| Azul (B)     | 21 | CH2 | 220 Ω | ~8,2 mA  |

---

## Fluxo do firmware

```
                              app_main (boot)
                                    │
              ┌─────────────────────┼──────────────────────┐
              │                     │                      │
              ▼                     ▼                      ▼
       i2s_driver_init()    ledc_driver_init()        wifi_init()
                                                           │
                                                      (3s delay)
                                                           │
                                                    start_webserver()
                                                    register_handlers()

Tasks FreeRTOS criadas:

 Prio 12 ┌─────────────────┐
         │ i2s_reader_task │  único leitor de hardware I2S/DMA
         └────────┬────────┘
                  │ 512 amostras por chunk (i2s_chunk_t)
         ┌────────┴────────┐
         ▼                 ▼
 g_audio_queue        g_kws_queue
         │                 │
 Prio 10 │        Prio 6   │
 ┌───────▼──────┐  ┌───────▼──────┐
 │  audio_task  │  │   kws_task   │
 └──────────────┘  └──────────────┘

 Prio 5
 ┌──────────────┐    ┌──────────────┐
 │ button_task  │    │   led_task   │
 └──────┬───────┘    └──────▲───────┘
        │ g_click_queue     │ g_led_queue
        └───────────────────┘
              (via audio_task e kws_task)
```

### Machine de estados — audio_task

```
            ┌──────────┐
            │   IDLE   │◀──────────────────────────────────────────┐
            └────┬─────┘                                           │
                 │                                                 │
    click + stream_fd≥0          click + record_fd≥0              │
                 │                        │                        │
                 ▼                        ▼                        │
          ┌──────────────┐      ┌──────────────────┐               │
          │  STREAMING   │      │   RECORDING      │               │
          │  LED GPIO2=1 │      │   LED GPIO2=1    │               │
          │  chunks PCM  │      │   chunks PCM     │               │
          │  → /stream   │      │   → /record      │               │
          └──────┬───────┘      └────────┬─────────┘               │
                 │ click                 │ click                   │
                 ▼                       ▼                         │
          LED GPIO2=0           "RECORDING_END:<n>" → WS           │
                 └──────────────────────┴─────────────────────────►┘
```

### Pipeline KWS — kws_task (contínuo, independente do audio_task)

```
i2s_reader_task
      │ i2s_chunk_t (512 amostras)
      ▼
 g_kws_queue
      │
      ▼
 Ring buffer circular (8000 amostras = 0,5 s)
      │
      ▼
 VAD: compute_rms(chunk)
      │
      ├── RMS ≥ 300 → voiced, acumula chunks, continua
      │
      └── RMS < 300 (silêncio após voiced):
             │
             ├── voiced_chunks < 3? → rejeita (too_short) → heartbeat → /monitor
             ├── cooldown < 1000ms? → rejeita (cooldown)  → heartbeat → /monitor
             │
             ▼
           mfcc_compute(ring, pos, 8000, out)
           → 48 frames × 13 coefs (pré-ênfase, FFT, mel filterbank 26 mels,
             DCT, CMVN)
             │
             ▼
           temporal_var < 0,3? → rejeita (var_gate) → /monitor
             │
             ▼
           mlp_infer(mfcc_out, probs)
           → softmax sobre 10 classes
             (amarelo/azul/branco/desligar/garbage/
              laranja/ligar/roxo/verde/vermelho)
             │
             ▼
           best = argmax(probs)
           valid = (probs[best] ≥ threshold) && (best ≠ garbage)
             │
             ├── !valid → word = null → JSON → /monitor
             │
     ┌───── modo KWS ──────┐
     │                     │
   IDLE               AWAIT_COLOR
     │                     │
     ▼                     ▼
 word == "ligar"?     color_lookup(word)?
   → modo AWAIT_COLOR   → g_led_queue {r,g,b}
     (timeout 2s)        → modo KWS_IDLE
 word == "desligar"?  word == "desligar"?
   → g_led_queue {0,0,0} → g_led_queue {0,0,0}
     │                     → modo KWS_IDLE
     └─────────────────────┘
             │
             ▼
         LED onboard pisca 200ms
         JSON → /monitor WebSocket
```

### Protocolo /record (montagem WAV client-side)

```
Browser                          ESP32
   │──── WS connect ────────────►│
   │                             │
   │  (1º click no botão)        │
   │◄── "RECORDING_START" ───────│
   │◄── [PCM chunk 1024 bytes] ──│  loop: I2S → WS direto (sem buffer)
   │◄── [PCM chunk 1024 bytes] ──│
   │◄── ... ─────────────────────│
   │  (2º click no botão)        │
   │◄── "RECORDING_END:<n>" ─────│  n = total de amostras enviadas
   │                             │
   │  assemblePcmToWav(chunks, n)│  buildWavHeader(n) + todos os chunks
   │  → Blob WAV → IndexedDB     │
```

---

## Pipeline de treinamento

```
1. Coletar amostras (aba Coleta na UI)
   → WAV salvo no IndexedDB com palavra associada
   → Baixar ZIP → extrair em training/samples/<palavra>_001.wav ...

2. make extract WORD=<palavra>
   → extract_features.py: WAV → MFCC alinhado por onset → features/<palavra>.npy

3. make train-mlp
   → train_mlp.py: features/*.npy → MLP treinado → firmware/main/kws/weights.h

4. make firmware-build
   → idf.py build com weights.h atualizado

5. make firmware-flash PORT=/dev/ttyUSB0
   → idf.py flash

6. Aba Monitor na UI
   → feedback visual em tempo real do KWS (probabilidades MLP, palavra detectada)
```

---

## Estrutura do projeto

```
ligar/
├── firmware/
│   └── main/
│       ├── app_main.c          # Boot, Wi-Fi, HTTP server, criação de tasks
│       ├── app_state.h         # Globals compartilhados (queues, mutex, fds, state)
│       ├── wifi_config.h       # Credenciais Wi-Fi (não versionado)
│       ├── drivers/
│       │   ├── i2s_driver.c/h  # I2S Philips 32-bit → 16-bit + gain
│       │   └── ledc_driver.c/h # LED RGB via LEDC/PWM (8-bit, 5kHz)
│       ├── kws/
│       │   ├── mfcc.c/h        # MFCC: pré-ênfase, FFT, mel (26), DCT, CMVN
│       │   ├── mlp.c/h         # Inferência MLP: forward pass, softmax
│       │   ├── weights.h       # AUTO-GERADO por train_mlp.py — não editar
│       │   ├── dtw.c/h         # DTW rolling 2-row, banda Sakoe-Chiba (w=6)
│       │   ├── templates.h     # AUTO-GERADO por generate_templates.py — legacy
│       │   └── color_catalog.h # Lookup: nome de cor → {r,g,b} (9 cores)
│       └── tasks/
│           ├── i2s_reader_task.c/h  # Leitor exclusivo I2S → g_audio_queue + g_kws_queue
│           ├── audio_task.c/h       # Machine de estados, WS /record e /stream
│           ├── button_task.c/h      # Polling GPIO4, debounce 50ms → g_click_queue
│           ├── kws_task.c/h         # KWS 2 estágios, WS /monitor, HTTP /threshold /led
│           └── led_task.c/h         # g_led_queue → ledc_set_color(r,g,b)
├── training/
│   ├── firmware_mfcc.py        # Réplica Python exata do mfcc.c
│   ├── firmware_mlp.py         # Validação paridade Python ↔ C: inferência com weights.h
│   ├── extract_features.py     # WAV → MFCC → .npy
│   ├── train_mlp.py            # Treina MLP → firmware/main/kws/weights.h
│   ├── generate_templates.py   # .npy → templates.h (legacy — não usado na inferência)
│   ├── capture_monitor.py      # Captura eventos /monitor via WS
│   ├── samples/                # WAVs de treino: <palavra>_NNN.wav
│   └── features/               # MFCCs: <palavra>.npy
├── web/
│   └── src/
│       ├── App.tsx             # 3 abas: Gravações / Monitor / Coleta
│       ├── hooks/              # useConnection, useRecordings, useStream,
│       │                       # useStreamVisualizer, useWhisper, useCollection
│       ├── components/         # MonitorTab, CollectionTab, RecordingList, …
│       ├── lib/                # db.ts, wav.ts, fft.ts, vizUtils.ts
│       └── utils/              # monitorLogger.ts (sessão JSONL)
├── CMakeLists.txt
├── Makefile
└── sdkconfig
```

---

## Hardware — pinout completo

| Componente | GPIO | Função |
|---|---|---|
| INMP441 BCLK | 26 | I2S bit clock |
| INMP441 WS | 25 | I2S word select (LRCLK) |
| INMP441 SD | 22 | I2S data in |
| INMP441 L/R | GND | Seleciona canal esquerdo |
| INMP441 VDD | 3.3V | Alimentação |
| Botão | 4 | Pull-up interno, polling 10ms |
| LED onboard | 2 | Aceso em gravação/stream/detecção KWS |
| LED RGB — R | 18 | LEDC CH0 (via BC547, ativo-baixo) |
| LED RGB — G | 19 | LEDC CH1 (via BC547, ativo-baixo) |
| LED RGB — B | 21 | LEDC CH2 (via BC547, ativo-baixo) |

### Configurações de áudio

| Parâmetro | Valor | Motivo |
|---|---|---|
| Sample rate | 16 kHz | Padrão para reconhecimento de voz |
| Bit depth | 16-bit | Após conversão de 32-bit (INMP441 MSBs) |
| Canais | Mono | L/R → GND = canal esquerdo |
| Ganho digital | 16× (~24 dB) | Ajustável via `#define MIC_GAIN` |
| Chunk I2S | 512 amostras | 32 ms de áudio por iteração |

---

## Endpoints

| Endpoint | Tipo | Descrição |
|---|---|---|
| `ws://<IP>/record` | WebSocket | PCM raw chunks + sinalizadores RECORDING_START/END |
| `ws://<IP>/stream` | WebSocket | PCM raw contínuo enquanto STREAMING |
| `ws://<IP>/monitor` | WebSocket | Eventos KWS em JSON + heartbeat 1Hz |
| `GET /threshold?v=<f>` | HTTP | Atualiza limiar MLP [0,1] em runtime |
| `GET /led?color=<c>&intensity=<0-100>` | HTTP | Controle direto do LED RGB |

### Payload /monitor

```json
{
  "rms": 1234.5,
  "threshold": 0.50,
  "var": 0.85,
  "word": "ligar",
  "probs": {
    "amarelo": 0.01, "azul": 0.00, "branco": 0.00,
    "desligar": 0.02, "garbage": 0.01, "laranja": 0.00,
    "ligar": 0.92, "roxo": 0.00, "verde": 0.01, "vermelho": 0.03
  },
  "kws_mode": "idle"
}
```

`word` é `null` quando nenhuma palavra é detectada. `rejected` aparece com o motivo (`too_short`, `cooldown`, `var_gate`). `threshold` é probabilidade MLP no intervalo [0, 1], ajustável via `/threshold`.

---

## Quickstart

### Dependências

```bash
make setup
```

### Firmware

```bash
# Pré-requisito: ESP-IDF configurado
cp firmware/main/wifi_config.h.example firmware/main/wifi_config.h
# Editar com SSID e senha

make firmware-build
make firmware-flash PORT=/dev/ttyUSB0
```

### Web

```bash
make web-dev
# Abrir http://localhost:5173
# Inserir o IP do ESP32 (exibido no serial após boot)
```

### Training — adicionar uma palavra nova

```bash
# 1. Coletar amostras via aba Coleta, baixar o .zip, extrair em training/samples/
make extract WORD=verde      # WAV → MFCC → features/verde.npy
make train-mlp               # treina MLP → weights.h
make firmware-build
make firmware-flash PORT=/dev/ttyUSB0
```

### Pipeline completo

```bash
make pipeline WORD=verde               # extract + train-mlp + build
make flash WORD=verde PORT=/dev/ttyUSB0 # pipeline + flash
```

---

## Palavras reconhecidas

**Triggers:** `ligar`, `desligar`

**Cores (após "ligar"):** `vermelho`, `verde`, `azul`, `amarelo`, `ciano`, `magenta`, `laranja`, `roxo`, `branco`
