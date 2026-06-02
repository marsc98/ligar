# Audio Visualization Design

**Spec**: `.specs/features/audio-visualization/spec.md`
**Status**: Draft

---

## Architecture Overview

Dois contextos independentes de visualização compartilham a mesma lógica de rendering (Canvas 2D + toggle de modo), mas com fontes de dados e ciclos de vida diferentes.

```
┌─────────────────────────────────────────────────────┐
│                    GRAVAÇÃO SALVA                    │
│                                                     │
│  RecordingItem                                      │
│  ├─ botão 📊 → toggle expandedId (em RecordingList)│
│  └─ [expandido] AudioVisualizer                     │
│       ├─ blob → AudioContext.decodeAudioData        │
│       │         └─ Float32Array (samples)           │
│       ├─ drawWaveform(samples, chunkSize)           │
│       ├─ drawFFT(fft(samples))     [lib/fft.ts]     │
│       └─ cursor: audio.currentTime via rAF          │
└─────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────┐
│                   STREAM AO VIVO                    │
│                                                     │
│  useStream ──onChunk──► useStreamVisualizer         │
│                          ├─ RingBuffer (4096 amostras)
│                          ├─ rAF loop (ativo enquanto streaming)
│                          └─ canvasRef               │
│                                                     │
│  LiveTranscriptPanel                                │
│  ├─ StreamVisualizer (canvas + toggle)              │
│  └─ texto de transcrição                           │
└─────────────────────────────────────────────────────┘
```

---

## Code Reuse Analysis

### Existing Components to Leverage

| Componente | Localização | Como usar |
|------------|-------------|-----------|
| `useAudioPlayer` | `hooks/useAudioPlayer.ts` | Modificar: add `onended` + expor `audioRef` para cursor |
| `RecordingItem` | `components/RecordingItem.tsx` | Modificar: add botão 📊 + slot expansível |
| `RecordingList` | `components/RecordingList.tsx` | Modificar: gerenciar `expandedId` (acordeão) |
| `LiveTranscriptPanel` | `components/LiveTranscriptPanel.tsx` | Modificar: inserir `StreamVisualizer` acima do texto |
| `useStream` | `hooks/useStream.ts` | Modificar: add callback `onChunk?: (samples: Int16Array) => void` |
| paleta de cores existente | inline styles no App/componentes | Reutilizar: `#3b82f6`, `#1e293b`, `#334155`, `#f1f5f9` |

### Novos Arquivos

| Arquivo | Propósito |
|---------|-----------|
| `components/AudioVisualizer.tsx` | Visualizador para gravações salvas |
| `components/StreamVisualizer.tsx` | Visualizador para stream ao vivo |
| `hooks/useStreamVisualizer.ts` | Ring buffer + rAF loop + FFT para stream |
| `lib/fft.ts` | Radix-2 Cooley-Tukey FFT mínimo |

---

## Components

### `lib/fft.ts`

- **Purpose**: FFT radix-2 sem dependências externas
- **Location**: `web/src/lib/fft.ts`
- **Interface**:
  ```typescript
  export function computeMagnitudes(samples: Float32Array, fftSize?: number): Float32Array
  // Retorna array de magnitude (0..1) com tamanho fftSize/2
  // fftSize padrão: maior potência de 2 ≤ samples.length, max 2048
  ```
- **Notas**: Windowing Hann aplicado antes do FFT para reduzir spectral leakage

---

### `components/AudioVisualizer.tsx`

- **Purpose**: Canvas de waveform/FFT para uma gravação WAV salva
- **Location**: `web/src/components/AudioVisualizer.tsx`
- **Interface**:
  ```typescript
  interface AudioVisualizerProps {
    blob: Blob
    chunkSize?: number              // default: 512 (I2S_READ_CHUNK)
    audioRef: React.RefObject<HTMLAudioElement | null>
    isPlaying: boolean
  }
  ```
- **Ciclo de vida**:
  1. Mount → `AudioContext.decodeAudioData(blob)` → armazena `Float32Array` em estado local
  2. `useEffect([mode, samples])` → re-renderiza canvas
  3. `isPlaying` → inicia loop `requestAnimationFrame` para cursor; cleanup no `false`
  4. Unmount → cancela rAF, fecha `AudioContext`
- **Rendering**:
  - `drawWaveform`: divide samples em `canvasWidth` fatias, desenha min/max de cada fatia, linhas verticais a cada `chunkSize` samples (cor `#334155`)
  - `drawFFT`: chama `computeMagnitudes`, desenha barras horizontais igualmente espaçadas
  - `drawBoth`: canvas dividido em dois — waveform 50% topo, FFT 50% base
  - Cursor: linha vertical `#f1f5f9` na posição `x = (currentTime / duration) * canvasWidth`
- **Seek**: listener `onClick` no canvas → `audio.currentTime = (clickX / canvasWidth) * duration`
- **Dependencies**: `lib/fft.ts`, Web Audio API

---

### `hooks/useStreamVisualizer.ts`

- **Purpose**: Mantém ring buffer de amostras do stream e expõe `pushChunk` + `canvasRef` para renderização em tempo real
- **Location**: `web/src/hooks/useStreamVisualizer.ts`
- **Interface**:
  ```typescript
  type VisualizerMode = 'waveform' | 'fft' | 'both'

  interface UseStreamVisualizerReturn {
    canvasRef: React.RefObject<HTMLCanvasElement | null>
    mode: VisualizerMode
    setMode: (m: VisualizerMode) => void
    pushChunk: (samples: Int16Array) => void
    start: () => void
    stop: () => void
  }
  ```
- **Implementação**:
  - Ring buffer: `Float32Array` de 4096 posições (normalizado: `sample / 32768`)
  - `pushChunk` escreve no ring buffer com wrap-around
  - `start()` → inicia loop `requestAnimationFrame` que lê do ring buffer e renderiza no canvas
  - `stop()` → cancela rAF, zera buffer
  - Rendering idêntico ao `AudioVisualizer` (funções compartilhadas via `lib/vizUtils.ts` ou inlined)
- **Dependencies**: `lib/fft.ts`

---

### `components/StreamVisualizer.tsx`

- **Purpose**: Wrapper fino — canvas + pill buttons de toggle — para uso no `LiveTranscriptPanel`
- **Location**: `web/src/components/StreamVisualizer.tsx`
- **Interface**:
  ```typescript
  interface StreamVisualizerProps {
    pushChunk: (samples: Int16Array) => void  // injetado de useStreamVisualizer
    canvasRef: React.RefObject<HTMLCanvasElement | null>
    mode: VisualizerMode
    onModeChange: (m: VisualizerMode) => void
  }
  ```
- **Dependencies**: `hooks/useStreamVisualizer.ts`

---

## Modificações em Arquivos Existentes

### `useAudioPlayer.ts`
```
play():  audio.onended = stop
stop():  audio.onended = null
```

### `useStream.ts`
```typescript
type UseStreamOptions = {
  onWindow: (audio: Float32Array) => void
  onChunk?: (samples: Int16Array) => void  // ← ADD
}
// Chamar onChunk(chunk) dentro de ws.onmessage, antes do processamento do flush
```

### `RecordingList.tsx`
```
+ estado: expandedId: string | null
+ passa isExpanded={expandedId === r.id} e onToggleViz={() => setExpandedId(...)} para RecordingItem
```

### `RecordingItem.tsx`
```
+ props: isExpanded, onToggleViz
+ botão 📊 chama onToggleViz
+ {isExpanded && <AudioVisualizer blob={recording.blob} audioRef={audioRef} isPlaying={isPlaying} />}
```
> `audioRef` precisa ser passado de `App` → `RecordingList` → `RecordingItem` → `AudioVisualizer`

### `LiveTranscriptPanel.tsx`
```
+ props: visualizer?: { pushChunk, canvasRef, mode, onModeChange }
+ {visualizer && <StreamVisualizer {...visualizer} />}
```

### `App.tsx`
```
+ const viz = useStreamVisualizer()
+ passa viz.pushChunk como onChunk para useStream
+ passa viz para LiveTranscriptPanel
+ chama viz.start()/viz.stop() junto com connectStream/disconnectStream
+ passa audioRef para RecordingList
```

---

## Data Models

### VisualizerMode (compartilhado)
```typescript
// web/src/types.ts — adicionar
export type VisualizerMode = 'waveform' | 'fft' | 'both'
```

---

## Error Handling Strategy

| Cenário | Handling | User vê |
|---------|----------|---------|
| `decodeAudioData` falha | catch → estado `error` | "Não foi possível carregar o áudio" no painel |
| `AudioContext` não suportado | try/catch no constructor | botão 📊 oculto |
| Canvas não disponível | ref null-check | nada renderiza, sem throw |
| Stream interrompido com rAF ativo | `stop()` chamado em `ws.onclose` | animação para limpa |

---

## Tech Decisions

| Decisão | Escolha | Rationale |
|---------|---------|-----------|
| Rendering | Canvas 2D raw | Nenhuma dependência nova; controle total; suficiente para waveform/FFT simples |
| FFT | Implementação própria radix-2 | ~50 linhas, zero deps; Web Audio AnalyserNode offline não funciona em OfflineAudioContext |
| Ring buffer | Float32Array manual | Simples, rápido, zero alocação no loop rAF |
| ScriptProcessorNode | Não usado | Deprecated; ring buffer manual é suficiente para visualização |
| AudioContext (gravações) | Criado por componente, fechado no unmount | Evita leak; browsers limitam AudioContexts simultâneos |
| Estado do acordeão | `expandedId` em `RecordingList` | Componente natural de fronteira; `RecordingItem` permanece presentacional |
