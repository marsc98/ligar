# Audio Visualization Tasks

**Design**: `.specs/features/audio-visualization/design.md`
**Status**: Draft

---

## Execution Plan

### Phase 1 — Foundation (todos paralelos, sem deps)
```
T1 (fft.ts)
T2 (useAudioPlayer fix)
T3 (types.ts)
T4 (useStream onChunk)
```

### Phase 2 — Core (paralelos, deps: T1 + T3)
```
T1+T3 ──┬──→ T5 (AudioVisualizer)
         └──→ T6 (useStreamVisualizer)
```

### Phase 3 — Wrappers (paralelos, dep: T5 ou T6)
```
T5 ──→ T8 (RecordingItem) ──┐
T6 ──→ T7 (StreamVisualizer) ─┘  (paralelos entre si)
```

### Phase 4 — Integração parcial (paralelos)
```
T8 ──→ T9 (RecordingList) ──┐
T7 ──→ T10 (LiveTranscript)  ─┘  (paralelos entre si)
```

### Phase 5 — Wire-up final (sequencial)
```
T4 + T6 + T9 + T10 ──→ T11 (App.tsx)
```

---

## Task Breakdown

### T1: Criar `lib/fft.ts` — FFT radix-2 [P]

**What**: Implementar Cooley-Tukey radix-2 com windowing Hann; exportar `computeMagnitudes`
**Where**: `web/src/lib/fft.ts`
**Depends on**: None
**Requirement**: AVIZ-03 (base do FFT)

**Interface a implementar**:
```typescript
export function computeMagnitudes(samples: Float32Array, fftSize?: number): Float32Array
// fftSize: potência de 2, default maior potência ≤ samples.length, max 2048
// Retorna magnitudes normalizadas 0..1, tamanho fftSize/2
```

**Done when**:
- [ ] `computeMagnitudes` aceita Float32Array arbitrário e retorna magnitudes
- [ ] fftSize é clampeado à potência de 2 mais próxima ≤ input.length, max 2048
- [ ] Gate: `cd web && tsc --noEmit` sem erros

**Tests**: none (sem framework; verificar manualmente via console se necessário)
**Gate**: quick (`tsc --noEmit`)

---

### T2: Fix `useAudioPlayer.ts` — onended handler [P]

**What**: Registrar `audio.onended = stop` no `play()` e `audio.onended = null` no `stop()`
**Where**: `web/src/hooks/useAudioPlayer.ts`
**Depends on**: None
**Requirement**: AVIZ-01

**Mudanças**:
```
play():  audio.onended = stop   (antes de audio.play())
stop():  audio.onended = null   (primeiro linha do if(audio))
```

**Done when**:
- [ ] `onended` é registrado em `play()`
- [ ] `onended` é limpo em `stop()`
- [ ] Gate: `cd web && tsc --noEmit` sem erros

**Tests**: none
**Gate**: quick (`tsc --noEmit`)

---

### T3: Adicionar `VisualizerMode` em `types.ts` [P]

**What**: Exportar union type `VisualizerMode` compartilhado entre componentes
**Where**: `web/src/types.ts`
**Depends on**: None
**Requirement**: AVIZ-02, AVIZ-03, AVIZ-05

**Adição**:
```typescript
export type VisualizerMode = 'waveform' | 'fft' | 'both'
```

**Done when**:
- [ ] Tipo exportado de `types.ts`
- [ ] Gate: `tsc --noEmit` sem erros

**Tests**: none
**Gate**: quick

---

### T4: Adicionar `onChunk` callback em `useStream.ts` [P]

**What**: Opcional `onChunk?: (samples: Int16Array) => void` chamado a cada mensagem binária recebida
**Where**: `web/src/hooks/useStream.ts`
**Depends on**: None
**Requirement**: AVIZ-05

**Mudança**:
```typescript
type UseStreamOptions = {
  onWindow: (audio: Float32Array) => void
  onChunk?: (samples: Int16Array) => void  // ← ADD
}
// ws.onmessage: chamar onChunk(chunk) antes do push para bufferRef
```

**Done when**:
- [ ] `onChunk` é chamado antes de empurrar para `bufferRef`
- [ ] Callback é opcional — sem quebra se ausente
- [ ] Gate: `tsc --noEmit` sem erros

**Tests**: none
**Gate**: quick

---

### T5: Criar `AudioVisualizer.tsx` [P]

**What**: Canvas component para gravações salvas — waveform + FFT + toggle + chunk markers + cursor + seek
**Where**: `web/src/components/AudioVisualizer.tsx`
**Depends on**: T1, T3
**Requirement**: AVIZ-02, AVIZ-03, AVIZ-04

**Props**:
```typescript
interface AudioVisualizerProps {
  blob: Blob
  chunkSize?: number  // default 512
  audioRef: React.RefObject<HTMLAudioElement | null>
  isPlaying: boolean
}
```

**Comportamento**:
1. Mount → `new AudioContext()` → `decodeAudioData(await blob.arrayBuffer())` → `getChannelData(0)` → Float32Array em estado
2. `drawWaveform`: downsample para canvas.width, linhas verticais a cada `chunkSize` samples (`#334155`)
3. `drawFFT`: `computeMagnitudes(samples)` → barras verticais igualmente espaçadas (`#3b82f6`)
4. `drawBoth`: waveform 50% topo, FFT 50% base
5. Cursor: `isPlaying === true` → `requestAnimationFrame` loop desenhando linha em `x = (currentTime/duration)*w`; cleanup quando `isPlaying === false`
6. Seek: `onClick` no canvas → `audio.currentTime = (clickX / w) * duration`
7. Toggle: pill buttons **Onda | FFT | Ambos**, modo inicial `'waveform'`
8. Unmount → cancela rAF, `audioContext.close()`
9. Erro em `decodeAudioData` → exibir `"Não foi possível carregar o áudio"` no lugar do canvas

**Done when**:
- [ ] Waveform renderiza com linhas de chunk visíveis
- [ ] Toggle circula pelos 3 modos sem erro
- [ ] Cursor avança quando `isPlaying === true` e some quando `false`
- [ ] Clique no canvas seeka o áudio
- [ ] AudioContext fechado no unmount
- [ ] Gate: `tsc --noEmit` sem erros

**Tests**: none
**Gate**: quick

---

### T6: Criar `useStreamVisualizer.ts` [P]

**What**: Hook com ring buffer + loop rAF para visualização do stream em tempo real
**Where**: `web/src/hooks/useStreamVisualizer.ts`
**Depends on**: T1, T3
**Requirement**: AVIZ-05

**Interface**:
```typescript
interface UseStreamVisualizerReturn {
  canvasRef: React.RefObject<HTMLCanvasElement | null>
  mode: VisualizerMode
  setMode: (m: VisualizerMode) => void
  pushChunk: (samples: Int16Array) => void
  start: () => void
  stop: () => void
}
```

**Implementação**:
- Ring buffer: `Float32Array(4096)`, `writePos` ref circular
- `pushChunk`: normaliza `sample / 32768`, escreve no ring buffer
- `start()`: inicia loop `requestAnimationFrame` que lê ring buffer e renderiza no `canvasRef`
- `stop()`: cancela rAF, zera ring buffer
- Rendering: mesmas funções de `drawWaveform`/`drawFFT`/`drawBoth` (pode duplicar inline ou extrair para `lib/vizUtils.ts`)

**Done when**:
- [ ] `pushChunk` escreve no ring buffer sem alocação
- [ ] `start()`/`stop()` gerenciam rAF corretamente (sem loop órfão)
- [ ] Gate: `tsc --noEmit` sem erros

**Tests**: none
**Gate**: quick

---

### T7: Criar `StreamVisualizer.tsx` [P]

**What**: Wrapper fino — canvas + pill buttons de toggle — para uso no LiveTranscriptPanel
**Where**: `web/src/components/StreamVisualizer.tsx`
**Depends on**: T6, T3

**Props**:
```typescript
interface StreamVisualizerProps {
  canvasRef: React.RefObject<HTMLCanvasElement | null>
  mode: VisualizerMode
  onModeChange: (m: VisualizerMode) => void
}
```

**Layout**: canvas altura 80px + pill buttons no cabeçalho (mesmo estilo do AudioVisualizer)

**Done when**:
- [ ] Canvas renderiza com referência correta
- [ ] Toggle de modo funciona
- [ ] Gate: `tsc --noEmit` sem erros

**Tests**: none
**Gate**: quick

---

### T8: Modificar `RecordingItem.tsx` — botão 📊 + painel expansível [P]

**What**: Adicionar props `isExpanded`, `onToggleViz`, `audioRef`; renderizar AudioVisualizer quando expandido
**Where**: `web/src/components/RecordingItem.tsx`
**Depends on**: T5

**Novas props**:
```typescript
isExpanded: boolean
onToggleViz: () => void
audioRef: React.RefObject<HTMLAudioElement | null>
```

**Mudanças**:
- Botão 📊 chamando `onToggleViz`
- `{isExpanded && <AudioVisualizer blob={recording.blob} audioRef={audioRef} isPlaying={isPlaying} />}`

**Done when**:
- [ ] Botão 📊 presente e chama `onToggleViz`
- [ ] AudioVisualizer monta/desmonta conforme `isExpanded`
- [ ] Gate: `tsc --noEmit` sem erros

**Tests**: none
**Gate**: quick

---

### T9: Modificar `RecordingList.tsx` — estado acordeão + passar audioRef

**What**: Adicionar `expandedId` state; passar `isExpanded`, `onToggleViz`, `audioRef` para cada RecordingItem
**Where**: `web/src/components/RecordingList.tsx`
**Depends on**: T8

**Novas props de RecordingList**:
```typescript
audioRef: React.RefObject<HTMLAudioElement | null>
```

**Estado interno**:
```typescript
const [expandedId, setExpandedId] = useState<string | null>(null)
const toggleViz = (id: string) =>
  setExpandedId(prev => prev === id ? null : id)
```

**Done when**:
- [ ] Apenas um item expandido por vez (acordeão)
- [ ] `audioRef` passado corretamente para RecordingItem
- [ ] Gate: `tsc --noEmit` sem erros

**Tests**: none
**Gate**: quick

---

### T10: Modificar `LiveTranscriptPanel.tsx` — inserir StreamVisualizer [P]

**What**: Aceitar props opcionais do visualizador; renderizar StreamVisualizer acima do texto
**Where**: `web/src/components/LiveTranscriptPanel.tsx`
**Depends on**: T7

**Novas props**:
```typescript
vizProps?: {
  canvasRef: React.RefObject<HTMLCanvasElement | null>
  mode: VisualizerMode
  onModeChange: (m: VisualizerMode) => void
}
```

**Layout**: `{vizProps && <StreamVisualizer {...vizProps} />}` antes do texto

**Done when**:
- [ ] StreamVisualizer aparece quando `active && vizProps` presentes
- [ ] Desaparece quando `!active`
- [ ] Gate: `tsc --noEmit` sem erros

**Tests**: none
**Gate**: quick

---

### T11: Modificar `App.tsx` — wire-up final

**What**: Instanciar `useStreamVisualizer`; conectar `onChunk`; passar `audioRef` para RecordingList; passar `vizProps` para LiveTranscriptPanel; chamar `start()`/`stop()` junto com stream
**Where**: `web/src/App.tsx`
**Depends on**: T4, T6, T9, T10

**Mudanças**:
```typescript
const viz = useStreamVisualizer()

// useStream: adicionar onChunk: viz.pushChunk

// connectStream → viz.start()
// disconnectStream → viz.stop()

// <RecordingList audioRef={audioRef} ... />

// <LiveTranscriptPanel vizProps={{ canvasRef: viz.canvasRef, mode: viz.mode, onModeChange: viz.setMode }} ... />
```

**Done when**:
- [ ] Stream ao vivo anima no canvas durante streaming
- [ ] Painel expansível funciona em cada RecordingItem
- [ ] Gate final: `cd web && tsc --noEmit && npm run lint && npm run build` — zero erros

**Tests**: none
**Gate**: full (`tsc + lint + build`)

---

## Parallelism Summary

| Fase | Tasks | Paralelo? |
|------|-------|-----------|
| 1 | T1, T2, T3, T4 | ✅ todos |
| 2 | T5, T6 | ✅ entre si (deps F1) |
| 3 | T7, T8 | ✅ entre si |
| 4 | T9, T10 | ✅ entre si |
| 5 | T11 | ❌ sequencial |
