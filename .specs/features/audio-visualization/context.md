# Audio Visualization — Interview Decisions

**Date:** 2026-05-23
**Scope:** Três ajustes na UI: fix do botão play, componente de visualização de áudio por gravação, e visualização em tempo real no stream.
**Source:** Discussão informal

---

## Decisions

### Fix: Botão Play não reseta ao fim do áudio

- Adicionar listener `onended` no elemento `<audio>` dentro de `useAudioPlayer` para chamar `stop()` automaticamente
- Arquivo: `web/src/hooks/useAudioPlayer.ts`
- Bug: `currentId` nunca é zerado quando o áudio termina naturalmente pois não há handler `onended`

### Modos de Visualização

- Três modos: **Onda** (amplitude × tempo, padrão) → **FFT** (espectro de frequência) → **Ambos** (onda em cima, FFT embaixo)
- Toggle circula entre os três modos via pill buttons no cabeçalho do componente
- O mesmo sistema de modos se aplica tanto ao visualizador de gravações salvas quanto ao de stream ao vivo

### Visualizador de Gravações Salvas

- Painel expansível dentro de cada `RecordingItem` — botão 📊 na linha
- Clica → expande para baixo com canvas de altura ~120px; clica de novo → recolhe
- **Forma de onda:** renderiza PCM decodificado do WAV (via `AudioContext.decodeAudioData`)
- **Chunks:** linhas verticais finas separando cada chunk; hover mostra "Chunk #N — X amostras"
- **Durante playback:** cursor de progresso (linha vertical) avança sobre a forma de onda via `requestAnimationFrame` + `audio.currentTime`; permite seekar clicando no gráfico
- **FFT:** pré-computado uma vez quando o painel abre, estático (não anima durante play exceto no modo stream)

### Visualizador de Stream ao Vivo

- Fica dentro do `LiveTranscriptPanel`, acima do texto de transcrição
- Aparece e desaparece junto com o painel (quando `active === true`)
- **Forma de onda e FFT animados em tempo real** via Web Audio API (`AnalyserNode`)
- PCM dos chunks WebSocket alimenta o `AnalyserNode` via `AudioWorklet` ou `ScriptProcessorNode`
- Mesmo sistema de toggle de modos (Onda / FFT / Ambos)

---

## Agent's Discretion

- Altura exata do canvas (sugerido ~120px para gravações, pode ajustar para stream)
- Cores e estilo dos gráficos (manter paleta existente: `#3b82f6` para onda, outra cor para FFT)
- Número de bins FFT
- Posicionamento exato dos pill buttons dentro do cabeçalho

---

## Deferred Ideas

- Nenhuma ideia fora de escopo surgiu durante a entrevista

---

## Open Questions

- Nenhuma questão em aberto
