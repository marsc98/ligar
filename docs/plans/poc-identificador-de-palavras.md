# Guia de Implementação — poc-identificador-de-palavras

**Base:** `poc-microfone` (ESP32 + INMP441 + React SPA via WebSocket)
**Objetivo:** ESP32 identifica localmente palavras para as quais foi treinada, sem depender de servidor externo ou internet.
**Abordagem:** MFCC + DTW (Dynamic Time Warping) — funciona com 10-20 amostras por palavra, roda na ESP32 sem PSRAM.

---

## Índice

1. [Visão Geral](#1-visão-geral)
2. [Arquitetura](#2-arquitetura)
3. [Estrutura de Arquivos](#3-estrutura-de-arquivos)
4. [Fase 1 — Coleta de Amostras (Web App)](#4-fase-1--coleta-de-amostras-web-app)
5. [Fase 2 — Treinamento (Python)](#5-fase-2--treinamento-python)
6. [Fase 3 — Firmware ESP32](#6-fase-3--firmware-esp32)
7. [Fluxo Completo End-to-End](#7-fluxo-completo-end-to-end)
8. [Parâmetros Ajustáveis](#8-parâmetros-ajustáveis)
9. [Dependências](#9-dependências)
10. [Limitações e Próximos Passos](#10-limitações-e-próximos-passos)

---

## 1. Visão Geral

### O que o projeto faz

```
┌──────────────────────────────────────────────────────────────┐
│  TREINAMENTO (offline, uma vez)                              │
│                                                              │
│  Usuário fala palavra 15x  →  Web App grava WAVs            │
│  Python extrai MFCCs       →  Gera templates.h              │
│  idf.py build flash        →  ESP32 atualizada              │
└──────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────┐
│  INFERÊNCIA (contínua, na ESP32)                             │
│                                                              │
│  INMP441 → I2S → ring buffer → janela 0.5s                  │
│  → MFCC → DTW vs. templates → score                         │
│  → threshold → GPIO output / UART / WebSocket               │
└──────────────────────────────────────────────────────────────┘
```

### Escolha técnica: por que DTW e não rede neural

| Critério | DTW | TFLite Micro |
|---|---|---|
| Amostras necessárias | 10-20 | 200-500+ por palavra |
| Infraestrutura | Python puro | TensorFlow + quantização |
| RAM inferência (ESP32) | ~35 KB | ~50-150 KB |
| Precisão (poucas amostras) | Boa | Ruim sem augmentation |
| Complexidade | Baixa | Alta |

Com 10-20 amostras DTW é superior. Se o projeto evoluir para vocabulário maior ou precisão mais alta, migrar para Edge Impulse aproveitando os mesmos WAVs.

---

## 2. Arquitetura

### Diagrama de componentes

```
poc-identificador-de-palavras/
│
├── web/          ← SPA de coleta (simplificada de poc-microfone)
│                   Grava WAVs nomeados word_001.wav, word_002.wav...
│
├── training/     ← Scripts Python
│                   Lê WAVs → extrai MFCCs → gera templates.h
│
└── firmware/     ← ESP32-IDF
                    Lê I2S → MFCC em C → DTW vs templates → ação
```

### Fluxo de dados na inferência

```
I2S 16kHz 16-bit mono
  │
  ▼ (a cada chunk de 160 amostras = 10ms)
Ring buffer circular (8000 amostras = 0.5s)
  │
  ▼ (quando RMS > threshold de voz — VAD simples)
Janela de 8000 amostras
  │
  ▼
Pre-emphasis → Framing (48 frames × 400 samples, hop 160) → Hann window
  │
  ▼
FFT 512-pt → Mel filterbank (26 filtros, 300-8000 Hz) → log → DCT
  │
  ▼
MFCC matrix (48 frames × 13 coef) = query
  │
  ▼
DTW(query, template_0) → dist_0
DTW(query, template_1) → dist_1
...
DTW(query, template_N) → dist_N
  │
  ▼
min(dist_i) < THRESHOLD → palavra detectada → ação
```

### Memória estimada (ESP32 sem PSRAM, sem Wi-Fi)

| Componente | RAM |
|---|---|
| Ring buffer áudio | 16 KB |
| Buffer MFCC query (48×13 floats) | 2.5 KB |
| DTW matrix (48×48 floats) | 9 KB |
| Mel filterbank (precomputado em flash) | 0 |
| Templates (15 × 48×13 floats, em flash) | 0 |
| Stack tasks + heap sistema | ~50 KB |
| **Total** | **~77 KB** |

Heap livre após boot (sem Wi-Fi): ~300 KB. Margem confortável.

Se manter Wi-Fi (para receber novos templates OTA): ~150 KB livres. Ainda cabe.

---

## 3. Estrutura de Arquivos

```
poc-identificador-de-palavras/
│
├── CMakeLists.txt
├── sdkconfig
├── .gitignore
│
├── firmware/
│   └── main/
│       ├── CMakeLists.txt
│       ├── main.c              ← loop principal, VAD, DTW decision
│       ├── mfcc.h
│       ├── mfcc.c              ← pre-emphasis, framing, FFT, mel, DCT
│       ├── dtw.h
│       ├── dtw.c               ← DTW com banda Sakoe-Chiba
│       ├── templates.h         ← GERADO pelo training script
│       └── wifi_config.h       ← opcional, se usar Wi-Fi para OTA
│
├── training/
│   ├── requirements.txt
│   ├── extract_features.py     ← lê WAVs, extrai MFCCs, valida
│   ├── generate_templates.py   ← gera templates.h a partir dos MFCCs
│   └── samples/                ← WAVs coletados pela web app
│       ├── ligar_001.wav
│       ├── ligar_002.wav
│       └── ...
│
└── web/
    ├── package.json
    ├── vite.config.ts
    ├── index.html
    └── src/
        ├── main.tsx
        ├── App.tsx             ← modo coleta: prompt + countdown + auto-nome
        ├── types.ts
        ├── hooks/
        │   ├── useConnection.ts    ← igual ao poc-microfone
        │   └── useRecordings.ts    ← igual ao poc-microfone, sem transcription
        ├── components/
        │   ├── ConnectionPanel.tsx
        │   ├── CollectionPanel.tsx ← NOVO: UI de coleta guiada
        │   └── SampleList.tsx      ← lista amostras + download zip
        └── lib/
            ├── db.ts
            └── wav.ts
```

---

## 4. Fase 1 — Coleta de Amostras (Web App)

### Diferenças em relação ao poc-microfone

Remover: `useWhisper`, `useStream`, `useStreamVisualizer`, `AudioVisualizer`, `LiveTranscriptPanel`, `LanguageSelect`, `fft.ts`, `vizUtils.ts`, `whisper.worker.ts`.

Adicionar: `CollectionPanel.tsx`, `SampleList.tsx`.

### CollectionPanel — comportamento esperado

```
1. Usuário digita o nome da palavra (ex: "ligar")
2. Clica em "Iniciar coleta"
3. App exibe: "Prepare-se... fale 'ligar' em 3... 2... 1..."
4. Ao chegar em 0: conecta /record automaticamente
5. Usuário fala a palavra
6. Após detectar RECORDING_END: salva como "ligar_001.wav"
7. Aguarda 2s e repete o prompt para a próxima amostra
8. Após N amostras: exibe lista com opção de download individual ou ZIP
```

### `CollectionPanel.tsx`

```tsx
import { useState, useRef } from 'react'
import type { ConnectionState } from '../types'

type Props = {
  state: ConnectionState
  onConnect: (ip: string) => void
  onDisconnect: () => void
  ip: string
  onIpChange: (ip: string) => void
  onSampleReady: (name: string) => void
  totalTarget: number
  currentCount: number
}

export function CollectionPanel({
  state, onConnect, onDisconnect, ip, onIpChange,
  onSampleReady, totalTarget, currentCount
}: Props) {
  const [word, setWord] = useState('')
  const [collecting, setCollecting] = useState(false)
  const [countdown, setCountdown] = useState<number | null>(null)
  const timerRef = useRef<ReturnType<typeof setTimeout> | null>(null)

  const startNextSample = () => {
    if (currentCount >= totalTarget) {
      setCollecting(false)
      return
    }
    setCountdown(3)
    let n = 3
    const tick = () => {
      n--
      if (n > 0) {
        setCountdown(n)
        timerRef.current = setTimeout(tick, 1000)
      } else {
        setCountdown(0)
        onConnect(ip)
      }
    }
    timerRef.current = setTimeout(tick, 1000)
  }

  // Quando state muda de 'receiving' para 'idle', uma amostra foi salva
  const prevStateRef = useRef(state)
  if (prevStateRef.current !== state) {
    if (prevStateRef.current === 'receiving' && state === 'idle' && collecting) {
      onSampleReady(`${word}_${String(currentCount + 1).padStart(3, '0')}`)
      setTimeout(startNextSample, 2000)
    }
    prevStateRef.current = state
  }

  return (
    <div>
      <input
        value={word}
        onChange={e => setWord(e.target.value)}
        placeholder="Nome da palavra (ex: ligar)"
        disabled={collecting}
      />
      <input
        value={ip}
        onChange={e => onIpChange(e.target.value)}
        placeholder="IP da ESP32"
        disabled={collecting}
      />
      {!collecting ? (
        <button
          onClick={() => { setCollecting(true); startNextSample() }}
          disabled={!word.trim() || !ip.trim()}
        >
          Iniciar coleta ({totalTarget} amostras)
        </button>
      ) : (
        <div>
          {countdown !== null && countdown > 0 && (
            <p>Prepare-se... {countdown}</p>
          )}
          {countdown === 0 && state === 'recording' && (
            <p>Fale "{word}" agora!</p>
          )}
          {state === 'receiving' && <p>Recebendo...</p>}
          <p>Amostra {currentCount + 1} de {totalTarget}</p>
          <button onClick={() => { setCollecting(false); onDisconnect() }}>
            Cancelar
          </button>
        </div>
      )}
    </div>
  )
}
```

### `App.tsx` — orquestração de coleta

```tsx
import { useState } from 'react'
import { useConnection } from './hooks/useConnection'
import { useRecordings } from './hooks/useRecordings'
import { CollectionPanel } from './components/CollectionPanel'
import { SampleList } from './components/SampleList'

export function App() {
  const [ip, setIp] = useState('192.168.0.')
  const [pendingName, setPendingName] = useState<string | null>(null)

  const { recordings, addRecording, deleteRecording } = useRecordings()

  const { state, connect, disconnect } = useConnection(
    async (blob, duration) => {
      if (pendingName) {
        await addRecording(blob, duration, pendingName)
        setPendingName(null)
      }
    }
  )

  return (
    <main>
      <h1>Coletor de Amostras</h1>
      <CollectionPanel
        state={state}
        ip={ip}
        onIpChange={setIp}
        onConnect={connect}
        onDisconnect={disconnect}
        onSampleReady={setPendingName}
        totalTarget={15}
        currentCount={recordings.length}
      />
      <SampleList recordings={recordings} onDelete={deleteRecording} />
    </main>
  )
}
```

### Modificação em `useRecordings.ts`

Adicionar parâmetro `name` opcional em `addRecording`:

```ts
const addRecording = async (blob: Blob, duration: number, name?: string) => {
  const id = crypto.randomUUID()
  const timestamp = Date.now()
  const autoName = name ?? `rec_${new Date(timestamp).toISOString().slice(0,19).replace('T','_')}`
  // ... salva com autoName
}
```

### Modificação em `useConnection.ts`

O `useConnection` do poc-microfone funciona sem mudanças — já suporta `RECORDING_END:<n>` e retorna blob + duration.

---

## 5. Fase 2 — Treinamento (Python)

### `requirements.txt`

```
numpy>=1.24
scipy>=1.10
librosa>=0.10
soundfile>=0.12
```

### `extract_features.py`

```python
"""
Extrai MFCCs de todos os WAVs em samples/ e salva como .npy.
Uso: python extract_features.py --word ligar
"""
import argparse
import numpy as np
import librosa
from pathlib import Path

# Parâmetros — devem ser IDÊNTICOS ao firmware C
SAMPLE_RATE   = 16000
DURATION_S    = 0.5
N_SAMPLES     = int(SAMPLE_RATE * DURATION_S)   # 8000
N_FFT         = 512
HOP_LENGTH    = 160                              # 10ms
WIN_LENGTH    = 400                              # 25ms
N_MELS        = 26
N_MFCC        = 13
N_FRAMES      = (N_SAMPLES - WIN_LENGTH) // HOP_LENGTH + 1  # 48

def extract_mfcc(wav_path: Path) -> np.ndarray:
    y, sr = librosa.load(str(wav_path), sr=SAMPLE_RATE, mono=True)

    # Centraliza e normaliza RMS
    y = y - y.mean()
    rms = np.sqrt(np.mean(y ** 2))
    if rms > 0:
        y = y / rms * 0.1

    # Pad ou trim para DURATION_S
    if len(y) < N_SAMPLES:
        y = np.pad(y, (0, N_SAMPLES - len(y)))
    else:
        # Encontra onset da voz e centraliza
        onset = np.argmax(np.abs(y) > 0.01 * np.max(np.abs(y)))
        start = max(0, onset - N_SAMPLES // 4)
        y = y[start:start + N_SAMPLES]
        if len(y) < N_SAMPLES:
            y = np.pad(y, (0, N_SAMPLES - len(y)))

    mfcc = librosa.feature.mfcc(
        y=y, sr=SAMPLE_RATE,
        n_mfcc=N_MFCC,
        n_fft=N_FFT,
        hop_length=HOP_LENGTH,
        win_length=WIN_LENGTH,
        n_mels=N_MELS,
        fmin=300, fmax=8000,
    )
    # Shape: (N_MFCC, N_FRAMES) → transpõe para (N_FRAMES, N_MFCC)
    mfcc = mfcc[:, :N_FRAMES].T
    assert mfcc.shape == (N_FRAMES, N_MFCC), f"Shape inesperado: {mfcc.shape}"
    return mfcc.astype(np.float32)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--word', required=True, help='Nome da palavra (ex: ligar)')
    args = parser.parse_args()

    samples_dir = Path('samples')
    out_dir = Path('features')
    out_dir.mkdir(exist_ok=True)

    wavs = sorted(samples_dir.glob(f'{args.word}_*.wav'))
    if not wavs:
        print(f'Nenhum WAV encontrado para "{args.word}" em {samples_dir}')
        return

    features = []
    for wav in wavs:
        try:
            mfcc = extract_mfcc(wav)
            features.append(mfcc)
            print(f'OK  {wav.name}  shape={mfcc.shape}')
        except Exception as e:
            print(f'ERR {wav.name}: {e}')

    out = out_dir / f'{args.word}.npy'
    np.save(str(out), np.stack(features))
    print(f'\n{len(features)} amostras salvas em {out}')
    print(f'Shape final: {np.stack(features).shape}  (amostras, frames, coefs)')

if __name__ == '__main__':
    main()
```

### `generate_templates.py`

```python
"""
Gera firmware/main/templates.h a partir dos arquivos .npy em features/.
Uso: python generate_templates.py --words ligar desligar parar
"""
import argparse
import numpy as np
from pathlib import Path
from datetime import datetime

TEMPLATE_COUNT = 10  # quantos templates manter por palavra (os de menor variância)

def select_templates(features: np.ndarray, n: int) -> np.ndarray:
    """Seleciona os N templates mais representativos (menores distâncias inter-amostra)."""
    if len(features) <= n:
        return features
    # Calcula distância euclidiana média de cada amostra em relação às demais
    dists = []
    for i, f in enumerate(features):
        others = np.delete(features, i, axis=0)
        d = np.mean([np.linalg.norm(f - o) for o in others])
        dists.append(d)
    # Seleciona as N com menor distância média (mais próximas do centróide)
    idx = np.argsort(dists)[:n]
    return features[idx]

def array_to_c(arr: np.ndarray, name: str) -> str:
    flat = arr.flatten()
    vals = ', '.join(f'{v:.6f}f' for v in flat)
    return f'static const float {name}[] = {{{vals}}};\n'

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--words', nargs='+', required=True)
    args = parser.parse_args()

    features_dir = Path('features')
    out_path = Path('../firmware/main/templates.h')

    lines = [
        '/* AUTO-GERADO por generate_templates.py — NÃO EDITAR MANUALMENTE */',
        f'/* Gerado em: {datetime.now().isoformat()} */',
        '#pragma once',
        '#include <stdint.h>',
        '',
        f'#define KWS_N_FRAMES  48',
        f'#define KWS_N_MFCC   13',
        f'#define KWS_N_COEFS  (KWS_N_FRAMES * KWS_N_MFCC)',
        '',
    ]

    word_defs = []
    for word in args.words:
        npy = features_dir / f'{word}.npy'
        if not npy.exists():
            print(f'AVISO: {npy} não encontrado, pulando.')
            continue

        features = np.load(str(npy))  # (N_amostras, 48, 13)
        selected = select_templates(features, TEMPLATE_COUNT)
        n = len(selected)

        lines.append(f'/* ── Palavra: "{word}" — {n} templates ── */')
        for i, tmpl in enumerate(selected):
            lines.append(array_to_c(tmpl, f'kws_{word}_tmpl_{i:02d}'))

        lines.append(f'static const float * const kws_{word}_templates[] = {{')
        for i in range(n):
            lines.append(f'    kws_{word}_tmpl_{i:02d},')
        lines.append(f'}};')
        lines.append(f'static const int kws_{word}_n_templates = {n};')
        lines.append('')

        word_defs.append(word)

    # Tabela de palavras para lookup em runtime
    lines.append(f'#define KWS_N_WORDS {len(word_defs)}')
    lines.append('')
    lines.append('typedef struct {')
    lines.append('    const char          *name;')
    lines.append('    const float * const *templates;')
    lines.append('    int                  n_templates;')
    lines.append('} kws_word_t;')
    lines.append('')
    lines.append('static const kws_word_t KWS_WORDS[] = {')
    for word in word_defs:
        lines.append(f'    {{ "{word}", kws_{word}_templates, kws_{word}_n_templates }},')
    lines.append('};')

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text('\n'.join(lines) + '\n')
    print(f'Gerado: {out_path}')
    print(f'Palavras: {word_defs}')

if __name__ == '__main__':
    main()
```

---

## 6. Fase 3 — Firmware ESP32

### `firmware/main/CMakeLists.txt`

```cmake
idf_component_register(
    SRCS "main.c" "mfcc.c" "dtw.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_wifi esp_http_server esp_netif nvs_flash freertos
)
```

### `mfcc.h`

```c
#pragma once
#include <stddef.h>

/* Parâmetros — devem ser IDÊNTICOS ao training/extract_features.py */
#define MFCC_SAMPLE_RATE  16000
#define MFCC_FRAME_LEN    400       /* 25ms */
#define MFCC_HOP          160       /* 10ms */
#define MFCC_N_FFT        512
#define MFCC_N_MELS       26
#define MFCC_N_COEFS      13
#define MFCC_N_FRAMES     48        /* (8000 - 400) / 160 + 1 */
#define MFCC_WIN_SAMPLES  8000      /* 0.5s */

/*
 * Extrai MFCC de uma janela de MFCC_WIN_SAMPLES amostras int16_t.
 * out: buffer de saída float[MFCC_N_FRAMES][MFCC_N_COEFS], linha-maior.
 */
void mfcc_compute(const int16_t *samples, float *out);
```

### `mfcc.c`

```c
#include "mfcc.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

/* ─── Constantes precomputadas ─────────────────────────────────────────── */

/* Janela de Hann para MFCC_FRAME_LEN amostras */
static float s_hann[MFCC_FRAME_LEN];

/*
 * Mel filterbank: MFCC_N_MELS filtros sobre MFCC_N_FFT/2+1 bins.
 * Gerado em tempo de init para economizar flash.
 */
static float s_mel_fb[MFCC_N_MELS][MFCC_N_FFT / 2 + 1];

/* Matriz DCT-II para MFCC: (MFCC_N_COEFS × MFCC_N_MELS) */
static float s_dct[MFCC_N_COEFS][MFCC_N_MELS];

static bool s_initialized = false;

/* ─── Helpers ──────────────────────────────────────────────────────────── */

static float hz_to_mel(float hz) {
    return 2595.0f * log10f(1.0f + hz / 700.0f);
}

static float mel_to_hz(float mel) {
    return 700.0f * (powf(10.0f, mel / 2595.0f) - 1.0f);
}

static void init_tables(void) {
    /* Hann window */
    for (int i = 0; i < MFCC_FRAME_LEN; i++) {
        s_hann[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (MFCC_FRAME_LEN - 1)));
    }

    /* Mel filterbank */
    const float fmin    = 300.0f;
    const float fmax    = 8000.0f;
    const int   n_bins  = MFCC_N_FFT / 2 + 1;
    const float mel_min = hz_to_mel(fmin);
    const float mel_max = hz_to_mel(fmax);

    float mel_pts[MFCC_N_MELS + 2];
    for (int i = 0; i < MFCC_N_MELS + 2; i++) {
        float mel = mel_min + (mel_max - mel_min) * i / (MFCC_N_MELS + 1);
        /* Converte para bin FFT mais próximo */
        mel_pts[i] = (mel_to_hz(mel) / (MFCC_SAMPLE_RATE / 2.0f)) * (n_bins - 1);
    }

    memset(s_mel_fb, 0, sizeof(s_mel_fb));
    for (int m = 0; m < MFCC_N_MELS; m++) {
        float left   = mel_pts[m];
        float center = mel_pts[m + 1];
        float right  = mel_pts[m + 2];
        for (int k = 0; k < n_bins; k++) {
            float fk = (float)k;
            if (fk >= left && fk <= center && center > left) {
                s_mel_fb[m][k] = (fk - left) / (center - left);
            } else if (fk > center && fk <= right && right > center) {
                s_mel_fb[m][k] = (right - fk) / (right - center);
            }
        }
    }

    /* DCT-II normalizada */
    for (int c = 0; c < MFCC_N_COEFS; c++) {
        float norm = (c == 0) ? sqrtf(1.0f / MFCC_N_MELS)
                               : sqrtf(2.0f / MFCC_N_MELS);
        for (int m = 0; m < MFCC_N_MELS; m++) {
            s_dct[c][m] = norm * cosf(M_PI * c * (m + 0.5f) / MFCC_N_MELS);
        }
    }

    s_initialized = true;
}

/* FFT in-place, Cooley-Tukey iterativo, N deve ser potência de 2 */
static void fft_real(float *re, float *im, int n) {
    /* Bit-reversal */
    int bits = 0;
    int tmp_n = n;
    while (tmp_n >>= 1) bits++;
    for (int i = 0; i < n; i++) {
        int rev = 0, x = i;
        for (int b = 0; b < bits; b++) { rev = (rev << 1) | (x & 1); x >>= 1; }
        if (rev > i) {
            float t = re[i]; re[i] = re[rev]; re[rev] = t;
            t = im[i]; im[i] = im[rev]; im[rev] = t;
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        float ang = -2.0f * M_PI / len;
        float wRe = cosf(ang), wIm = sinf(ang);
        for (int i = 0; i < n; i += len) {
            float tRe = 1.0f, tIm = 0.0f;
            for (int j = 0; j < len / 2; j++) {
                float uRe = re[i+j],   uIm = im[i+j];
                float vRe = re[i+j+len/2] * tRe - im[i+j+len/2] * tIm;
                float vIm = re[i+j+len/2] * tIm + im[i+j+len/2] * tRe;
                re[i+j]         = uRe + vRe;
                im[i+j]         = uIm + vIm;
                re[i+j+len/2]   = uRe - vRe;
                im[i+j+len/2]   = uIm - vIm;
                float nRe = tRe * wRe - tIm * wIm;
                tIm = tRe * wIm + tIm * wRe;
                tRe = nRe;
            }
        }
    }
}

/* ─── API pública ──────────────────────────────────────────────────────── */

void mfcc_compute(const int16_t *samples, float *out) {
    if (!s_initialized) init_tables();

    static float re[MFCC_N_FFT];
    static float im[MFCC_N_FFT];
    static float mel_energy[MFCC_N_MELS];
    const int n_bins = MFCC_N_FFT / 2 + 1;

    /* Pre-emphasis */
    static float pre[MFCC_WIN_SAMPLES];
    pre[0] = (float)samples[0];
    for (int i = 1; i < MFCC_WIN_SAMPLES; i++) {
        pre[i] = (float)samples[i] - 0.97f * (float)samples[i - 1];
    }

    for (int frame = 0; frame < MFCC_N_FRAMES; frame++) {
        int offset = frame * MFCC_HOP;

        /* Windowing */
        memset(re, 0, sizeof(re));
        memset(im, 0, sizeof(im));
        for (int i = 0; i < MFCC_FRAME_LEN; i++) {
            re[i] = pre[offset + i] * s_hann[i] / 32768.0f;
        }

        fft_real(re, im, MFCC_N_FFT);

        /* Power spectrum */
        float power[MFCC_N_FFT / 2 + 1];
        for (int k = 0; k < n_bins; k++) {
            power[k] = re[k] * re[k] + im[k] * im[k];
        }

        /* Mel filterbank */
        for (int m = 0; m < MFCC_N_MELS; m++) {
            float energy = 0.0f;
            for (int k = 0; k < n_bins; k++) {
                energy += s_mel_fb[m][k] * power[k];
            }
            mel_energy[m] = logf(energy + 1e-9f);
        }

        /* DCT */
        float *row = out + frame * MFCC_N_COEFS;
        for (int c = 0; c < MFCC_N_COEFS; c++) {
            float sum = 0.0f;
            for (int m = 0; m < MFCC_N_MELS; m++) {
                sum += s_dct[c][m] * mel_energy[m];
            }
            row[c] = sum;
        }
    }
}
```

### `dtw.h`

```c
#pragma once

/*
 * Calcula a distância DTW entre duas sequências MFCC.
 * query, ref: arrays float[n_frames * n_coefs], row-major.
 * Usa banda de Sakoe-Chiba (window) para reduzir custo O(n²) → O(n*w).
 */
float dtw_distance(const float *query, const float *ref,
                   int n_frames, int n_coefs, int window);
```

### `dtw.c`

```c
#include "dtw.h"
#include <float.h>
#include <math.h>

#define DTW_MAX_FRAMES 64

static float s_dp[DTW_MAX_FRAMES][DTW_MAX_FRAMES];

static float euclidean(const float *a, const float *b, int n) {
    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        float d = a[i] - b[i];
        sum += d * d;
    }
    return sqrtf(sum);
}

float dtw_distance(const float *query, const float *ref,
                   int n_frames, int n_coefs, int window) {
    for (int i = 0; i < n_frames; i++)
        for (int j = 0; j < n_frames; j++)
            s_dp[i][j] = FLT_MAX;

    s_dp[0][0] = euclidean(query, ref, n_coefs);

    for (int i = 0; i < n_frames; i++) {
        int j_start = (i - window > 0) ? i - window : 0;
        int j_end   = (i + window < n_frames) ? i + window : n_frames - 1;
        for (int j = j_start; j <= j_end; j++) {
            if (i == 0 && j == 0) continue;
            float cost = euclidean(
                query + i * n_coefs,
                ref   + j * n_coefs,
                n_coefs
            );
            float prev = FLT_MAX;
            if (i > 0 && s_dp[i-1][j]   < prev) prev = s_dp[i-1][j];
            if (j > 0 && s_dp[i][j-1]   < prev) prev = s_dp[i][j-1];
            if (i > 0 && j > 0 && s_dp[i-1][j-1] < prev) prev = s_dp[i-1][j-1];
            s_dp[i][j] = cost + prev;
        }
    }
    return s_dp[n_frames-1][n_frames-1] / n_frames;
}
```

### `main.c`

```c
/*
 * poc-identificador-de-palavras — Main
 *
 * Pinout (igual ao poc-microfone):
 *   INMP441 SCK  → GPIO 26 (BCLK)
 *   INMP441 WS   → GPIO 25 (LRCLK)
 *   INMP441 SD   → GPIO 22 (DATA IN)
 *   INMP441 L/R  → GND
 *   LED          → GPIO 2  (acende ao detectar palavra)
 *   (Sem botão — detecção contínua por VAD)
 */

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

#include "mfcc.h"
#include "dtw.h"
#include "templates.h"   /* gerado pelo training script */

#define PIN_BCLK    26
#define PIN_WS      25
#define PIN_DATA_IN 22
#define PIN_LED     2

#define MIC_GAIN    16
#define I2S_CHUNK   160   /* 10ms = 1 hop MFCC */

/* DTW: janela de Sakoe-Chiba (frames). 10 = ±100ms de variação permitida */
#define DTW_WINDOW  10

/*
 * Threshold de distância DTW abaixo do qual a palavra é considerada detectada.
 * CALIBRAR: rodar com ESP_LOGI e ajustar conforme as distâncias reportadas.
 * Valor inicial conservador — diminuir para maior sensibilidade.
 */
#define DTW_THRESHOLD 18.0f

/*
 * RMS mínimo para considerar que há voz no frame.
 * Evita computar MFCC em silêncio.
 */
#define VAD_RMS_THRESHOLD 500

/* Cooldown após detecção (ms) para evitar múltiplos triggers */
#define DETECTION_COOLDOWN_MS 1500

static const char *TAG = "KWS";
static i2s_chan_handle_t g_rx_chan = NULL;

/* Ring buffer de 0.5s (MFCC_WIN_SAMPLES amostras int16_t) */
static int16_t g_ring[MFCC_WIN_SAMPLES];
static int      g_ring_pos = 0;

/* MFCC output: N_FRAMES × N_COEFS floats */
static float g_mfcc_out[MFCC_N_FRAMES * MFCC_N_COEFS];

/* Buffer linear para DTW (cópia ordenada do ring) */
static int16_t g_window[MFCC_WIN_SAMPLES];

/* ─── I2S ──────────────────────────────────────────────────────────────── */

static void i2s_init(void) {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &g_rx_chan));

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(MFCC_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PIN_BCLK,
            .ws   = PIN_WS,
            .dout = I2S_GPIO_UNUSED,
            .din  = PIN_DATA_IN,
        },
    };
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(g_rx_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(g_rx_chan));
}

static size_t i2s_read_chunk(int16_t *buf, size_t n_samples) {
    int32_t raw[I2S_CHUNK];
    size_t  bytes_read = 0;
    esp_err_t err = i2s_channel_read(g_rx_chan, raw,
                                     n_samples * sizeof(int32_t),
                                     &bytes_read, portMAX_DELAY);
    if (err != ESP_OK) return 0;
    size_t n = bytes_read / sizeof(int32_t);
    for (size_t i = 0; i < n; i++) {
        int32_t s = (int32_t)(raw[i] >> 16) * MIC_GAIN;
        if (s >  32767) s =  32767;
        if (s < -32768) s = -32768;
        buf[i] = (int16_t)s;
    }
    return n;
}

/* ─── VAD simples (RMS) ────────────────────────────────────────────────── */

static float compute_rms(const int16_t *buf, int n) {
    float sum = 0.0f;
    for (int i = 0; i < n; i++) sum += (float)buf[i] * buf[i];
    return sqrtf(sum / n);
}

/* ─── KWS task ─────────────────────────────────────────────────────────── */

static void kws_task(void *arg) {
    int16_t chunk[I2S_CHUNK];
    TickType_t last_detection = 0;

    ESP_LOGI(TAG, "KWS iniciado — monitorando %d palavras", KWS_N_WORDS);
    for (int i = 0; i < KWS_N_WORDS; i++) {
        ESP_LOGI(TAG, "  [%d] \"%s\" (%d templates)",
                 i, KWS_WORDS[i].name, KWS_WORDS[i].n_templates);
    }

    while (1) {
        size_t n = i2s_read_chunk(chunk, I2S_CHUNK);
        if (n == 0) continue;

        /* Insere chunk no ring buffer */
        for (size_t i = 0; i < n; i++) {
            g_ring[g_ring_pos] = chunk[i];
            g_ring_pos = (g_ring_pos + 1) % MFCC_WIN_SAMPLES;
        }

        /* VAD: só processa se o chunk atual tiver energia suficiente */
        float rms = compute_rms(chunk, (int)n);
        if (rms < VAD_RMS_THRESHOLD) continue;

        /* Cooldown pós-detecção */
        TickType_t now = xTaskGetTickCount();
        if ((now - last_detection) < pdMS_TO_TICKS(DETECTION_COOLDOWN_MS)) continue;

        /* Copia ring buffer em ordem cronológica */
        for (int i = 0; i < MFCC_WIN_SAMPLES; i++) {
            g_window[i] = g_ring[(g_ring_pos + i) % MFCC_WIN_SAMPLES];
        }

        /* Extrai MFCC da janela */
        mfcc_compute(g_window, g_mfcc_out);

        /* DTW contra todos os templates de todas as palavras */
        float best_dist  = FLT_MAX;
        int   best_word  = -1;

        for (int w = 0; w < KWS_N_WORDS; w++) {
            for (int t = 0; t < KWS_WORDS[w].n_templates; t++) {
                float d = dtw_distance(
                    g_mfcc_out,
                    KWS_WORDS[w].templates[t],
                    MFCC_N_FRAMES, MFCC_N_COEFS, DTW_WINDOW
                );
                if (d < best_dist) {
                    best_dist = d;
                    best_word = w;
                }
            }
        }

        ESP_LOGI(TAG, "VAD ativa — melhor match: \"%s\" dist=%.2f (threshold=%.2f)",
                 best_word >= 0 ? KWS_WORDS[best_word].name : "?",
                 best_dist, DTW_THRESHOLD);

        if (best_dist < DTW_THRESHOLD && best_word >= 0) {
            ESP_LOGI(TAG, ">>> DETECTADO: \"%s\" (dist=%.2f) <<<",
                     KWS_WORDS[best_word].name, best_dist);

            gpio_set_level(PIN_LED, 1);
            vTaskDelay(pdMS_TO_TICKS(300));
            gpio_set_level(PIN_LED, 0);

            last_detection = xTaskGetTickCount();

            /*
             * PONTO DE EXTENSÃO: adicionar aqui a ação desejada.
             * Ex: publicar em fila, acionar relay, enviar via UART, etc.
             */
        }
    }
}

/* ─── app_main ─────────────────────────────────────────────────────────── */

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    gpio_config_t led = {
        .intr_type    = GPIO_INTR_DISABLE,
        .mode         = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << PIN_LED),
    };
    ESP_ERROR_CHECK(gpio_config(&led));
    gpio_set_level(PIN_LED, 0);

    i2s_init();

    ESP_LOGI(TAG, "Heap livre: %lu bytes",
             (unsigned long)esp_get_free_heap_size());

    xTaskCreate(kws_task, "kws_task", 16384, NULL, 10, NULL);
}
```

---

## 7. Fluxo Completo End-to-End

### Primeira vez (treinamento)

```bash
# 1. Inicie a web app de coleta
cd web && npm install && npm run dev

# 2. Conecte ao ESP32 (IP da ESP32 deve estar acessível)
#    No browser: informe IP → iniciar coleta → fale a palavra 15x
#    Download dos WAVs gerados

# 3. Coloque os WAVs em training/samples/
mv ~/Downloads/ligar_*.wav training/samples/

# 4. Instale dependências Python
cd training && pip install -r requirements.txt

# 5. Extraia features
python extract_features.py --word ligar

# 6. Gere templates.h
python generate_templates.py --words ligar

# 7. Build e flash do firmware
cd ../firmware && idf.py build flash monitor

# 8. Calibre o threshold
#    Observe o serial: "VAD ativa — melhor match: "ligar" dist=X.XX"
#    Fale a palavra várias vezes e anote os valores de dist
#    Fale outras coisas e anote os valores de dist
#    Defina DTW_THRESHOLD entre os dois grupos
```

### Adicionar nova palavra

```bash
# Só repetir a coleta para a nova palavra e regerar templates.h
python extract_features.py --word desligar
python generate_templates.py --words ligar desligar
idf.py build flash
```

---

## 8. Parâmetros Ajustáveis

| Parâmetro | Arquivo | Default | Efeito |
|---|---|---|---|
| `DTW_THRESHOLD` | `main.c` | 18.0 | Diminuir = mais sensível, mais falsos positivos |
| `VAD_RMS_THRESHOLD` | `main.c` | 500 | Diminuir = detecta voz mais fraca |
| `DETECTION_COOLDOWN_MS` | `main.c` | 1500 | Tempo mínimo entre detecções |
| `DTW_WINDOW` | `main.c` | 10 | Flexibilidade de alinhamento temporal |
| `TEMPLATE_COUNT` | `generate_templates.py` | 10 | Templates por palavra embarcados |
| `DURATION_S` | `extract_features.py` | 0.5 | Duração esperada da palavra (ajustar para palavras longas) |
| `MIC_GAIN` | `main.c` | 16 | Ganho do microfone |

---

## 9. Dependências

### Firmware

- ESP-IDF v5.x ou superior (mesmo do poc-microfone)
- Sem componentes externos — apenas os drivers padrão do IDF

### Python (treinamento)

```
numpy>=1.24
scipy>=1.10
librosa>=0.10
soundfile>=0.12
```

### Web (coleta)

- React 19 + TypeScript + Vite (igual ao poc-microfone)
- Sem novas dependências de runtime
- Remover `@huggingface/transformers` (não usado aqui)

---

## 10. Limitações e Próximos Passos

### Limitações do DTW

| Limitação | Impacto | Mitigação |
|---|---|---|
| Sensível a ruído de fundo | Falsos positivos | Aumentar `VAD_RMS_THRESHOLD` ou usar filtro passa-alta |
| Vocabulário fixo pós-flash | Requer reflash para novas palavras | Armazenar templates em NVS + endpoint WebSocket para OTA de templates |
| Palavras longas (>1s) | `DURATION_S` precisa ser ajustado | Aumentar `DURATION_S`, `MFCC_WIN_SAMPLES`, `MFCC_N_FRAMES` |
| 1 palavra detectada por vez | Não detecta sobreposição | Não relevante para comando por voz |

### Próximos passos possíveis

1. **OTA de templates via WebSocket:** manter Wi-Fi ativo, endpoint `/templates` recebe novo `templates.h` serializado (array binário) e salva em NVS — sem reflash para adicionar palavras
2. **Delta MFCC (ΔMFCC):** adicionar coeficientes delta melhora precisão sem aumentar custo de memória significativamente
3. **Migração para TFLite Micro:** quando tiver >200 amostras por palavra, treinar CNN no Edge Impulse e exportar para `esp-tflite-micro` — precisão superior para vocabulários maiores
4. **ESP32-S3:** substituir ESP32-D0WD por S3 com PSRAM para vocabulário maior e modelos mais robustos
