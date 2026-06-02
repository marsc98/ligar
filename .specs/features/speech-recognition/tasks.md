# Speech Recognition — Tasks

**Design:** `.specs/features/speech-recognition/design.md`
**Status:** Draft

---

## Execution Plan

### Phase 1 — Foundation (Paralela)
```
T1[P] ─┐
       ├──→ Phase 2
T2[P] ─┘
```

### Phase 2 — Core (Mista)
```
T2 ──→ T3 ──────────────────────────────┐
T1 ──→ T4[P] ──→ T5 ────────────────────┤
       T6[P] ───────────────────────────┤──→ T10
       T7[P] ──→ ──────────────────────┤
T2 ──→ T8[P] ──→ T9 ───────────────────┘
```

Diagrama simplificado:

```
Phase 1:  T1[P]  T2[P]
Phase 2:  T3  T4[P]  T6[P]  T7[P]  T8[P]
Phase 3:  T5  T9[P]
Phase 4:  T10
```

---

## Task Breakdown

### T1: Instalar dependência + configurar Vite [P]

**What:** Adicionar `@huggingface/transformers` ao package.json e configurar `optimizeDeps.exclude` no vite.config.ts
**Where:**
- `web/package.json`
- `web/vite.config.ts`
**Depends on:** Nenhuma
**Reuses:** Vite config existente
**Requirement:** SR-02

**O que fazer:**
1. `cd web && npm install @huggingface/transformers`
2. Em `vite.config.ts`, adicionar:
```typescript
optimizeDeps: {
  exclude: ['@huggingface/transformers'],
},
```

**Done when:**
- [ ] `@huggingface/transformers` aparece em `package.json` dependencies
- [ ] `vite.config.ts` tem `optimizeDeps.exclude` com o pacote
- [ ] `npm run dev` inicia sem erro de bundling

---

### T2: Atualizar tipos e camada de dados [P]

**What:** Adicionar campo `transcription` ao tipo `Recording` e função `updateTranscription` ao db.ts
**Where:**
- `web/src/types.ts`
- `web/src/lib/db.ts`
**Depends on:** Nenhuma
**Reuses:** Padrão `idbRequest` existente em db.ts
**Requirement:** SR-03, SR-04

**O que fazer:**

`types.ts` — adicionar campo opcional:
```typescript
export interface Recording {
  // ...campos existentes...
  transcription?: string
}
```

`db.ts` — nova função:
```typescript
export function updateTranscription(db: IDBDatabase, id: string, text: string): Promise<void> {
  return new Promise((resolve, reject) => {
    const tx = db.transaction('recordings', 'readwrite')
    const store = tx.objectStore('recordings')
    const req = store.get(id)
    req.onsuccess = () => {
      const rec = req.result
      if (rec) store.put({ ...rec, transcription: text })
      resolve()
    }
    req.onerror = () => reject(req.error)
  })
}
```

**Done when:**
- [ ] `Recording.transcription?: string` presente em types.ts
- [ ] `updateTranscription` exportada de db.ts
- [ ] `cd web && npx tsc --noEmit` sem erros

---

### T3: Atualizar useRecordings

**What:** `addRecording` retorna o `id` criado; expor `updateTranscription`
**Where:** `web/src/hooks/useRecordings.ts`
**Depends on:** T2
**Reuses:** Lógica existente de `addRecording`
**Requirement:** SR-01, SR-03, SR-04

**O que fazer:**

1. Alterar assinatura de `addRecording`: retornar `Promise<string>` (o id gerado)
2. Adicionar `updateTranscription(id, text)` no hook:
```typescript
const updateTranscription = async (id: string, text: string) => {
  const db = dbRef.current
  if (!db) return
  try {
    await dbUpdateTranscription(db, id, text)
    setRecordings(prev =>
      prev.map(r => r.id === id ? { ...r, transcription: text } : r)
    )
  } catch {
    // transcrição perdida, gravação intacta
  }
}
```
3. Exportar `updateTranscription` no retorno do hook

**Done when:**
- [ ] `addRecording` retorna `Promise<string>`
- [ ] `updateTranscription` exposta no hook
- [ ] `cd web && npx tsc --noEmit` sem erros

---

### T4: Criar whisper.worker.ts [P]

**What:** Web Worker com singleton do pipeline Whisper; aceita Float32Array, devolve texto
**Where:** `web/src/workers/whisper.worker.ts`
**Depends on:** T1
**Reuses:** Padrão singleton da doc oficial Transformers.js
**Requirement:** SR-02

**O que fazer:**

```typescript
import { pipeline, env } from '@huggingface/transformers'
import type { AutomaticSpeechRecognitionPipeline, ProgressCallback } from '@huggingface/transformers'

env.allowLocalModels = false

class WhisperPipeline {
  static instance: Promise<AutomaticSpeechRecognitionPipeline> | null = null

  static getInstance(progress_callback: ProgressCallback) {
    this.instance ??= pipeline(
      'automatic-speech-recognition',
      'onnx-community/whisper-tiny',
      { progress_callback, dtype: 'fp32' }
    ) as Promise<AutomaticSpeechRecognitionPipeline>
    return this.instance
  }
}

self.addEventListener('message', async (event: MessageEvent) => {
  const { id, audio, language } = event.data as {
    id: string | null
    audio: Float32Array
    language: string
  }

  try {
    const transcriber = await WhisperPipeline.getInstance((p: unknown) => {
      self.postMessage(p)
    })

    const result = await transcriber(
      { input: audio, sampling_rate: 16000 },
      { language, task: 'transcribe' }
    )

    const text = Array.isArray(result) ? result[0].text : (result as { text: string }).text
    self.postMessage({ status: 'complete', id, text: text.trim() })
  } catch (e) {
    self.postMessage({ status: 'error', id, error: String(e) })
  }
})
```

**Done when:**
- [ ] Arquivo criado em `src/workers/whisper.worker.ts`
- [ ] `cd web && npx tsc --noEmit` sem erros (tsconfig pode precisar de `lib: ["WebWorker"]` — ver nota abaixo)
- [ ] Worker instancia o pipeline sem erro no browser (verificar console)

> **Nota:** Se `tsconfig.app.json` não tiver `"lib": ["WebWorker", "ES2020"]`, adicionar. Workers usam `self` em vez de `window`.

---

### T5: Criar useWhisper.ts

**What:** Hook React que gerencia o worker, fila de jobs, estado do modelo e callbacks
**Where:** `web/src/hooks/useWhisper.ts`
**Depends on:** T4, T2
**Reuses:** Padrão `useRef`/`useEffect` dos hooks existentes
**Requirement:** SR-01, SR-02, SR-05, SR-06, SR-09

**Interface:**
```typescript
type UseWhisperOptions = {
  language: string                                  // ex: 'portuguese'
  onComplete: (id: string, text: string) => void
  onLive: (text: string) => void
}

type UseWhisperReturn = {
  modelStatus: 'idle' | 'loading' | 'ready'
  transcribingIds: Set<string>
  transcribe: (id: string, blob: Blob) => void
  transcribeLive: (audio: Float32Array) => void
}
```

**Comportamento:**
1. Criar worker uma vez: `new Worker(new URL('../workers/whisper.worker.ts', import.meta.url), { type: 'module' })`
2. `transcribe(id, blob)`:
   - Decodifica WAV via `AudioContext.decodeAudioData`
   - Reamostrar para 16kHz se necessário
   - Enfileira `{ id, audio: Float32Array, language }` 
   - Adiciona `id` ao Set `transcribingIds`
3. `transcribeLive(audio)`: enfileira `{ id: null, audio, language }`
4. Fila processada sequencialmente (semáforo interno)
5. `onmessage`:
   - `initiate/progress/done` → atualiza `modelStatus`
   - `ready` → `modelStatus = 'ready'`
   - `complete` com id → chama `onComplete(id, text)`, remove do `transcribingIds`
   - `complete` com id null → chama `onLive(text)`
   - `error` → remove do `transcribingIds`, log silencioso

**Done when:**
- [ ] Hook criado e exportado
- [ ] `modelStatus` reflete `'loading'` durante download e `'ready'` após
- [ ] `transcribingIds` atualiza corretamente ao enfileirar/concluir
- [ ] `cd web && npx tsc --noEmit` sem erros

---

### T6: Criar useStream.ts [P]

**What:** Hook para WebSocket `/stream`: acumula chunks PCM Int16, VAD por RMS, flush Float32Array
**Where:** `web/src/hooks/useStream.ts`
**Depends on:** Nenhuma
**Reuses:** Padrão WebSocket do `useConnection.ts`
**Requirement:** SR-10, SR-11

**Interface:**
```typescript
type UseStreamOptions = {
  onWindow: (audio: Float32Array) => void  // janela pronta para transcrição
}

type UseStreamReturn = {
  streaming: boolean
  connectStream: (ip: string) => void
  disconnectStream: () => void
}
```

**Comportamento:**
- WebSocket `ws://<ip>/stream`, `binaryType = 'arraybuffer'`
- Acumula chunks em `Int16Array` buffer (`bufferRef`)
- A cada chunk: calcular RMS dos últimos 512 samples
- Flush quando: `buffer.length >= 64000` (4s) OU (`buffer.length >= 8000` E `rms < 200`)
- Flush: converter `Int16 → Float32` (dividir por 32768), chamar `onWindow(float32)`, limpar buffer
- `onclose`: limpar buffer

**Done when:**
- [ ] Hook criado e exportado
- [ ] `streaming` é `true` quando WebSocket está aberto
- [ ] Limpa buffer ao desconectar
- [ ] `cd web && npx tsc --noEmit` sem erros

---

### T7: Criar LanguageSelect.tsx [P]

**What:** Componente select com pt/en/es/fr/de; exporta código ISO e nome Whisper
**Where:** `web/src/components/LanguageSelect.tsx`
**Depends on:** Nenhuma
**Reuses:** Estilo inline dos outros componentes do projeto
**Requirement:** SR-07, SR-08

**O que criar:**
```typescript
export type LanguageCode = 'pt' | 'en' | 'es' | 'fr' | 'de'

export const WHISPER_LANG: Record<LanguageCode, string> = {
  pt: 'portuguese',
  en: 'english',
  es: 'spanish',
  fr: 'french',
  de: 'german',
}

type Props = {
  value: LanguageCode
  onChange: (code: LanguageCode) => void
}

export function LanguageSelect({ value, onChange }: Props) { ... }
```

**Done when:**
- [ ] Componente renderiza select com 5 opções
- [ ] `onChange` chamado com código correto ao mudar
- [ ] `cd web && npx tsc --noEmit` sem erros

---

### T8: Criar LiveTranscriptPanel.tsx [P]

**What:** Painel que exibe transcrição acumulada do streaming ao vivo
**Where:** `web/src/components/LiveTranscriptPanel.tsx`
**Depends on:** Nenhuma
**Reuses:** Estilo inline dos outros componentes
**Requirement:** SR-12

**Interface:**
```typescript
type Props = {
  text: string    // texto acumulado da sessão
  active: boolean // streaming conectado
}
```

**Comportamento:**
- Quando `active = false`: não renderiza nada (retorna `null`)
- Quando `active = true`: mostra painel com label "Transcrição ao vivo" e o texto
- Texto vazio: mostra "Aguardando fala..."

**Done when:**
- [ ] Componente não renderiza quando `active = false`
- [ ] Exibe texto ou placeholder quando `active = true`
- [ ] `cd web && npx tsc --noEmit` sem erros

---

### T9: Atualizar RecordingItem.tsx [P]

**What:** Exibir transcrição abaixo de cada gravação; indicador "Transcrevendo..." quando em andamento
**Where:** `web/src/components/RecordingItem.tsx`
**Depends on:** T2
**Reuses:** Estrutura JSX existente do componente
**Requirement:** SR-04, SR-05

**O que adicionar:**

1. Nova prop: `transcribing: boolean`
2. Exibir abaixo das ações:
   - Se `transcribing`: texto cinza itálico "Transcrevendo..."
   - Se `recording.transcription`: texto da transcrição
   - Se nem um nem outro: nada

**Done when:**
- [ ] Prop `transcribing` aceita pelo componente
- [ ] "Transcrevendo..." visível quando `transcribing = true`
- [ ] Texto de transcrição visível quando `recording.transcription` existe
- [ ] `cd web && npx tsc --noEmit` sem erros

---

### T10: Wiring final em App.tsx

**What:** Integrar todos os novos hooks e componentes; conectar fluxo pós-gravação e streaming
**Where:** `web/src/App.tsx`
**Depends on:** T3, T5, T6, T7, T8, T9
**Reuses:** Estrutura existente do App
**Requirement:** SR-01 a SR-12 (integração)

**O que fazer:**

```typescript
const [language, setLanguage] = useState<LanguageCode>('pt')
const [liveText, setLiveText] = useState('')

const { recordings, loading, dbError, addRecording, deleteRecording, updateTranscription } = useRecordings()

const { transcribe, transcribeLive, modelStatus, transcribingIds } = useWhisper({
  language: WHISPER_LANG[language],
  onComplete: updateTranscription,
  onLive: (text) => setLiveText(prev => prev ? prev + ' ' + text : text),
})

const { streaming, connectStream, disconnectStream } = useStream({ onWindow: transcribeLive })

const { state, error, connect, disconnect } = useConnection(
  async (blob, duration) => {
    const id = await addRecording(blob, duration)
    transcribe(id, blob)
  }
)
```

Adicionar no JSX:
- `<LanguageSelect value={language} onChange={setLanguage} />` — antes ou depois do ConnectionPanel
- `{modelStatus === 'loading' && <p>Baixando modelo de fala (primeira vez)...</p>}`
- `<LiveTranscriptPanel text={liveText} active={streaming} />`
- `<RecordingItem ... transcribing={transcribingIds.has(recording.id)} />` em cada item da lista

Limpar `liveText` ao desconectar stream: `onDisconnect` chama `setLiveText('')`.

**Done when:**
- [ ] Transcrição dispara automaticamente após gravação via `/record`
- [ ] Select de idioma muda o idioma nas transcrições seguintes
- [ ] Banner "Baixando modelo" aparece na primeira execução
- [ ] Painel ao vivo aparece quando streaming conectado
- [ ] `cd web && npx tsc --noEmit` sem erros
- [ ] **Teste manual:** gravar → soltar botão → aguardar → transcrição aparece + persiste após reload

---

## Notas de Implementação

**tsconfig para Web Worker:**
Se `whisper.worker.ts` apresentar erros de tipo com `self`, `MessageEvent` etc., criar `web/src/workers/tsconfig.worker.json`:
```json
{
  "extends": "../../tsconfig.app.json",
  "compilerOptions": {
    "lib": ["ES2020", "WebWorker"]
  }
}
```

**Modelo ONNX:**
`onnx-community/whisper-tiny` é a versão quantizada ONNX compatível com Transformers.js v3. Não usar `openai/whisper-tiny` diretamente — requer conversão.

**AudioContext sampleRate:**
O firmware grava a 16kHz. Criar o AudioContext com `new AudioContext({ sampleRate: 16000 })` evita reamostragem.

**VAD threshold:**
O valor `200` (RMS de Int16) é um ponto de partida. Ambientes ruidosos podem precisar de ajuste. Hardcoded por ora.
