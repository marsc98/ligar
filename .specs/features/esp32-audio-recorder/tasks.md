# ESP32 Audio Recorder — Tasks

**Design**: `.specs/features/esp32-audio-recorder/design.md`
**Status**: Draft

---

## Execution Plan

### Phase 1 — Bootstrap (Paralelo OK)
```
T1 (firmware) ──┐
                ├──→ Phase 2
T2 (web setup) ─┘
```

### Phase 2 — Foundation (Sequencial, após T2)
```
T2 → T3 (types.ts)
```

### Phase 3 — Libs + Primitivos (Paralelo, após T3)
```
         ┌→ T4 (lib/wav.ts)         ─┐
T3 ──────┼→ T5 (lib/db.ts)          ─┤──→ Phase 4
         ├→ T8 (useAudioPlayer)      ─┤
         └→ T9 (StatusBadge)         ─┘
```

### Phase 4 — Hooks + Painéis (Paralelo, após T4/T5/T8/T9)
```
         ┌→ T6 (useConnection)   [depende T4]     ─┐
T4/T5    ├→ T7 (useRecordings)   [depende T5]     ─┤──→ Phase 5
T8/T9    ├→ T10 (ConnectionPanel)[depende T9]     ─┤
         └→ T11 (RecordingItem)  [depende T8, T9] ─┘
```

### Phase 5 — Composição (Sequencial)
```
T11 → T12 (RecordingList)
```

### Phase 6 — Integração + Documentação (Paralelo OK)
```
T6/T7/T10/T12 ──┬──→ T13 (App.tsx)
                └──→ T14 (README.md)
```

---

## Task Breakdown

### T1: Firmware — adicionar `ws_send_text` + `RECORDING_START` + fix M1

**What**: 3 adições cirúrgicas em `poc-microfone.c`: helper `ws_send_text`, notificação de início de gravação, e limpeza de fd ao falhar `ws_send_binary`
**Where**: `main/poc-microfone.c`
**Depends on**: Nenhuma
**Reuses**: Padrão de `ws_send_binary` (linha 185) como modelo para `ws_send_text`
**Requirement**: EAR-04, EAR-12 (notificação); CONCERNS.md M1 (fix fd)

**Tools**:
- MCP: Nenhum
- Skill: Nenhum

**Implementação**:

1. Adicionar `ws_send_text()` após `ws_send_binary()` (após linha 195):
```c
static esp_err_t ws_send_text(httpd_handle_t hd, int fd, const char *text) {
    httpd_ws_frame_t pkt = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)text,
        .len = strlen(text),
    };
    return httpd_ws_send_frame_async(hd, fd, &pkt);
}
```

2. Em `audio_task`, na branch `APP_RECORDING` (linha ~278), antes do `ESP_LOGI`:
```c
ws_send_text(server, g_ws_record_fd, "RECORDING_START");
```

3. No loop de envio de chunks WAV (linha ~312), substituir:
```c
ws_send_binary(server, fd, g_record_buf + offset, sz * sizeof(int16_t));
```
por:
```c
if (ws_send_binary(server, fd, g_record_buf + offset, sz * sizeof(int16_t)) != ESP_OK) {
    xSemaphoreTake(g_ws_mutex, portMAX_DELAY);
    g_ws_record_fd = -1;
    xSemaphoreGive(g_ws_mutex);
    break;
}
```

**Done when**:
- [ ] `ws_send_text` declarada como `static esp_err_t` após `ws_send_binary`
- [ ] `RECORDING_START` enviado quando `g_state` muda para `APP_RECORDING`
- [ ] Loop de envio de chunks verifica retorno e limpa fd em caso de erro
- [ ] Gate: `idf.py build` compila sem warnings/erros

**Tests**: Verificação manual — `idf.py monitor` mostra "Gravação iniciada" ao pressionar botão
**Gate**: `idf.py build` (requer ambiente ESP-IDF)

---

### T2: Web — scaffold do projeto Vite + React + TypeScript

**What**: Criar estrutura inicial do projeto React com Vite na pasta `web/`
**Where**: `web/` (novo diretório)
**Depends on**: Nenhuma
**Reuses**: Template `react-ts` do Vite
**Requirement**: EAR-01 (pré-requisito estrutural)

**Tools**:
- MCP: Context7 (verificar versão atual do Vite e template react-ts)
- Skill: Nenhum

**Passos**:
```bash
cd /home/marco/projetos/poc-microfone
npm create vite@latest web -- --template react-ts
cd web && npm install
```

Remover arquivos de template desnecessários: `src/App.css`, `src/assets/`, conteúdo de `src/App.tsx` e `src/index.css`.

Criar estrutura de diretórios:
```
web/src/lib/
web/src/hooks/
web/src/components/
```

**Done when**:
- [ ] `web/` existe com `package.json`, `vite.config.ts`, `tsconfig.json`, `index.html`
- [ ] `web/src/main.tsx` e `web/src/App.tsx` presentes e limpos (sem template boilerplate)
- [ ] Diretórios `lib/`, `hooks/`, `components/` criados em `src/`
- [ ] Gate: `cd web && npm run build` conclui sem erros

**Tests**: Build TypeScript limpo
**Gate**: `cd web && npm run build`

---

### T3: `web/src/types.ts` — modelos de dados compartilhados

**What**: Arquivo de tipos TypeScript com `Recording` e `ConnectionState`
**Where**: `web/src/types.ts`
**Depends on**: T2
**Reuses**: Modelos do design.md
**Requirement**: EAR-04, EAR-07, EAR-09, EAR-12

**Tools**:
- MCP: Nenhum
- Skill: Nenhum

**Conteúdo**:
```typescript
export interface Recording {
  id: string
  name: string
  timestamp: number
  duration: number
  size: number
  blob: Blob
}

export type ConnectionState =
  | 'disconnected'
  | 'connecting'
  | 'idle'
  | 'recording'
  | 'receiving'
  | 'saving'
```

**Done when**:
- [ ] `Recording` exporta todos os 6 campos com tipos corretos
- [ ] `ConnectionState` exporta os 6 estados exatos
- [ ] Gate: `cd web && npm run build` sem erros de tipo

**Tests**: Verificação de tipo via build
**Gate**: `cd web && npm run build`

---

### T4: `web/src/lib/wav.ts` — WAV assembler e parser de header [P]

**What**: Funções puras para montar Blob WAV e calcular duração a partir do header
**Where**: `web/src/lib/wav.ts`
**Depends on**: T3
**Reuses**: Nenhum código existente — lógica pura
**Requirement**: EAR-04, EAR-05, EAR-06

**Tools**:
- MCP: Nenhum (lógica de bytes, sem lib externa)
- Skill: Nenhum

**Interface**:
```typescript
export function parseWavDuration(headerBuffer: ArrayBuffer): number
// Lê bytes 40-43 (data_size, uint32 LE) ÷ 32000 (16kHz × 1ch × 2bytes)

export function assembleWavBlob(chunks: ArrayBuffer[]): Blob
// new Blob(chunks, { type: 'audio/wav' })
```

**Detalhe de `parseWavDuration`**: usar `DataView` para leitura little-endian:
```typescript
const view = new DataView(headerBuffer)
const dataSize = view.getUint32(40, true) // little-endian
return dataSize / 32000
```

**Done when**:
- [ ] `parseWavDuration` retorna segundos corretos para header de 16kHz mono 16-bit
- [ ] `assembleWavBlob` retorna `Blob` com `type === 'audio/wav'`
- [ ] Nenhuma dependência externa no arquivo
- [ ] Gate: `cd web && npm run build` sem erros

**Tests**: Verificação manual: logar duração calculada ao receber gravação
**Gate**: `cd web && npm run build`

---

### T5: `web/src/lib/db.ts` — IndexedDB wrapper Promise-based [P]

**What**: Abstração sobre IndexedDB para CRUD de gravações
**Where**: `web/src/lib/db.ts`
**Depends on**: T3
**Reuses**: Nenhum código existente
**Requirement**: EAR-05, EAR-06, EAR-07, EAR-11

**Tools**:
- MCP: Context7 (verificar API IDBObjectStore e padrão de Promise wrapping)
- Skill: Nenhum

**Interface**:
```typescript
export function openDB(): Promise<IDBDatabase>
// DB: 'poc-microfone', v1
// Store: 'recordings', keyPath: 'id'
// Index: 'by-timestamp' em campo 'timestamp'

export function saveRecording(db: IDBDatabase, rec: Recording): Promise<void>
export function getAllRecordings(db: IDBDatabase): Promise<Recording[]>
// Ordenado por timestamp desc

export function deleteRecording(db: IDBDatabase, id: string): Promise<void>
```

**Padrão de Promise wrapping**:
```typescript
function idbRequest<T>(req: IDBRequest<T>): Promise<T> {
  return new Promise((resolve, reject) => {
    req.onsuccess = () => resolve(req.result)
    req.onerror = () => reject(req.error)
  })
}
```

**Done when**:
- [ ] `openDB` cria DB e store se não existirem (via `onupgradeneeded`)
- [ ] `saveRecording` persiste todos os campos de `Recording` incluindo `Blob`
- [ ] `getAllRecordings` retorna array ordenado por `timestamp` decrescente
- [ ] `deleteRecording` remove por `id`
- [ ] Nenhum `callback` exposto — tudo Promise
- [ ] Gate: `cd web && npm run build` sem erros

**Tests**: Verificação manual: salvar gravação, recarregar página, confirmar persistência
**Gate**: `cd web && npm run build`

---

### T6: `web/src/hooks/useConnection.ts` — WebSocket + state machine [P]

**What**: Hook React que gerencia ciclo de vida do WebSocket, implementa a state machine de 6 estados e emite Blob ao fim da transmissão
**Where**: `web/src/hooks/useConnection.ts`
**Depends on**: T3 (tipos), T4 (wav assembler)
**Reuses**: `ConnectionState` de `types.ts`, `assembleWavBlob` e `parseWavDuration` de `lib/wav.ts`
**Requirement**: EAR-01, EAR-02, EAR-03, EAR-04, EAR-05, EAR-12

**Tools**:
- MCP: Context7 (verificar padrões de useRef para WebSocket em React)
- Skill: Nenhum

**Interface**:
```typescript
export function useConnection(onRecordingSaved: (blob: Blob, duration: number) => void): {
  state: ConnectionState
  error: string | null
  connect(ip: string): void
  disconnect(): void
}
```

**Lógica da state machine** (ver design.md):
- `ws.onopen` → `'idle'`
- text frame `"RECORDING_START"` → `'recording'`
- primeiro binary frame → `'receiving'`, acumula chunks, reinicia timer 500ms
- timer dispara → `'saving'` → `assembleWavBlob` → `onRecordingSaved` → `'idle'`
- `ws.onclose` / `ws.onerror` → `'disconnected'`, se em `'receiving'` mostra erro

**Cleanup**: `useRef` para guardar WebSocket, `useEffect` retorna `ws.close()`

**Done when**:
- [ ] `connect(ip)` abre `WebSocket('ws://' + ip + '/record')`
- [ ] Transições de estado corretas para todos os 6 casos
- [ ] Timeout de 500ms detecta fim de transmissão
- [ ] `onRecordingSaved` chamado com Blob e duração
- [ ] Desconexão durante `'receiving'` define `error` com mensagem de gravação incompleta
- [ ] `disconnect()` fecha WebSocket e vai para `'disconnected'`
- [ ] Gate: `cd web && npm run build` sem erros

**Tests**: Verificação manual: sequência de estados visível via StatusBadge
**Gate**: `cd web && npm run build`

---

### T7: `web/src/hooks/useRecordings.ts` — CRUD reativo no IndexedDB [P]

**What**: Hook React que abre IndexedDB, mantém lista de gravações em estado reativo e expõe operações de CRUD
**Where**: `web/src/hooks/useRecordings.ts`
**Depends on**: T3 (tipos), T5 (db wrapper)
**Reuses**: Todas as funções de `lib/db.ts`; `Recording` de `types.ts`
**Requirement**: EAR-05, EAR-06, EAR-07, EAR-08, EAR-11

**Tools**:
- MCP: Nenhum
- Skill: Nenhum

**Interface**:
```typescript
export function useRecordings(): {
  recordings: Recording[]
  loading: boolean
  dbError: string | null
  addRecording(blob: Blob, duration: number): Promise<void>
  deleteRecording(id: string): Promise<void>
}
```

**Lógica de `addRecording`**:
- Gera `id = crypto.randomUUID()`
- Gera `name = 'rec-' + new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19)`
- Salva via `saveRecording(db, rec)` → adiciona no topo do estado local

**Done when**:
- [ ] `useEffect` abre DB no mount; `loading` é `true` até DB estar pronto
- [ ] `dbError` captura `!window.indexedDB`, falha em `openDB`, e `QuotaExceededError`
- [ ] `recordings` começa com todos os registros do IndexedDB, ordenados por `timestamp` desc
- [ ] `addRecording` persiste e atualiza estado sem reload
- [ ] `deleteRecording` remove de IndexedDB e de estado
- [ ] Gate: `cd web && npm run build` sem erros

**Tests**: Verificação manual: salvar 2 gravações, recarregar, verificar persistência
**Gate**: `cd web && npm run build`

---

### T8: `web/src/hooks/useAudioPlayer.ts` — player com Object URL [P]

**What**: Hook React que gerencia qual gravação está tocando via elemento `<audio>` ref e ciclo de vida de Object URLs
**Where**: `web/src/hooks/useAudioPlayer.ts`
**Depends on**: T3 (tipos)
**Reuses**: `Recording` de `types.ts`
**Requirement**: EAR-09, EAR-10

**Tools**:
- MCP: Nenhum
- Skill: Nenhum

**Interface**:
```typescript
export function useAudioPlayer(): {
  currentId: string | null
  audioRef: React.RefObject<HTMLAudioElement>
  play(recording: Recording): void
  stop(): void
}
```

**Lógica**:
- `play(r)`: revogar URL anterior (se houver) → `URL.createObjectURL(r.blob)` → setar `audioRef.current.src` → `.play()` → `setCurrentId(r.id)`
- `stop()`: `.pause()` → `.src = ''` → `URL.revokeObjectURL(url)` → `setCurrentId(null)`
- Cleanup no unmount: revogar URL ativa

**Done when**:
- [ ] `play()` cria Object URL e inicia reprodução
- [ ] `play()` ao mudar gravação para a reprodução atual e reinicia
- [ ] `stop()` pausa, limpa src e revoga URL
- [ ] Object URL revogado em todos os caminhos de saída
- [ ] `audioRef` exposto para ser passado ao elemento `<audio>` no DOM
- [ ] Gate: `cd web && npm run build` sem erros

**Tests**: Verificação manual: clicar em duas gravações diferentes, áudio troca corretamente
**Gate**: `cd web && npm run build`

---

### T9: `web/src/components/StatusBadge.tsx` — badge visual de estado [P]

**What**: Componente React que exibe badge colorido baseado em `ConnectionState`
**Where**: `web/src/components/StatusBadge.tsx`
**Depends on**: T3 (tipos)
**Reuses**: `ConnectionState` de `types.ts`
**Requirement**: EAR-12

**Tools**:
- MCP: Nenhum
- Skill: Nenhum

**Interface**:
```typescript
interface StatusBadgeProps {
  state: ConnectionState
}
export function StatusBadge({ state }: StatusBadgeProps): JSX.Element
```

**Mapeamento** (via objeto de configuração, sem switch):
```typescript
const CONFIG: Record<ConnectionState, { label: string; color: string; pulse: boolean }> = {
  disconnected: { label: 'Desconectado', color: '#6b7280', pulse: false },
  connecting:   { label: 'Conectando...', color: '#3b82f6', pulse: true },
  idle:         { label: 'Pronto',        color: '#22c55e', pulse: false },
  recording:    { label: 'Gravando...',   color: '#ef4444', pulse: true },
  receiving:    { label: 'Recebendo...', color: '#eab308', pulse: false },
  saving:       { label: 'Salvando...',  color: '#eab308', pulse: false },
}
```

**Done when**:
- [ ] Todos os 6 estados renderizam label e cor corretos
- [ ] Estados `connecting` e `recording` têm animação de pulse via CSS
- [ ] Sem dependência de biblioteca de UI externa
- [ ] Gate: `cd web && npm run build` sem erros

**Tests**: Verificação visual: montar com cada estado e confirmar cor/texto
**Gate**: `cd web && npm run build`

---

### T10: `web/src/components/ConnectionPanel.tsx` — painel de conexão [P]

**What**: Componente com input de IP, botão Conectar/Desconectar e exibição de erro
**Where**: `web/src/components/ConnectionPanel.tsx`
**Depends on**: T3 (tipos), T9 (StatusBadge)
**Reuses**: `ConnectionState` de `types.ts`, `StatusBadge`
**Requirement**: EAR-01, EAR-02, EAR-03

**Tools**:
- MCP: Nenhum
- Skill: Nenhum

**Interface**:
```typescript
interface ConnectionPanelProps {
  state: ConnectionState
  error: string | null
  onConnect(ip: string): void
  onDisconnect(): void
}
```

**Comportamento**:
- Input IP desabilitado quando `state !== 'disconnected'`
- Botão "Conectar" visível quando `state === 'disconnected'`
- Botão "Desconectar" visível nos demais estados
- `error` exibido em texto vermelho abaixo do input
- `StatusBadge` renderizado inline

**Done when**:
- [ ] Input desabilitado corretamente quando conectado
- [ ] Botão muda entre Conectar/Desconectar de acordo com estado
- [ ] Erro exibido quando `error !== null`
- [ ] Enter no input com `state === 'disconnected'` chama `onConnect`
- [ ] Gate: `cd web && npm run build` sem erros

**Tests**: Verificação manual: conectar com IP inválido → erro aparece
**Gate**: `cd web && npm run build`

---

### T11: `web/src/components/RecordingItem.tsx` — item de gravação com ações [P]

**What**: Componente de linha individual para uma gravação: metadados + botões de ação
**Where**: `web/src/components/RecordingItem.tsx`
**Depends on**: T3 (tipos), T8 (useAudioPlayer — para `isPlaying`)
**Reuses**: `Recording` de `types.ts`
**Requirement**: EAR-08, EAR-09, EAR-11, EAR-13

**Tools**:
- MCP: Nenhum
- Skill: Nenhum

**Interface**:
```typescript
interface RecordingItemProps {
  recording: Recording
  isPlaying: boolean
  onPlay(recording: Recording): void
  onDelete(id: string): void
  onDownload(recording: Recording): void
}
```

**Exibe**:
- Nome (`recording.name`)
- Data/hora: `new Date(recording.timestamp).toLocaleString('pt-BR')`
- Duração: `mm:ss` formatado a partir de `recording.duration`
- Tamanho: `${(recording.size / 1024).toFixed(0)} KB`
- Botão Play/Pause (muda ícone baseado em `isPlaying`)
- Botão Download (cria `<a download>` programaticamente)
- Botão Excluir

**Done when**:
- [ ] Todos os metadados exibidos corretamente
- [ ] `isPlaying` muda visual do botão de play
- [ ] Download cria `<a href=objectUrl download=name.wav>` e dispara clique
- [ ] Object URL do download revogado após `setTimeout(100ms)` para liberar memória
- [ ] Gate: `cd web && npm run build` sem erros

**Tests**: Verificação visual dos metadados; download testado manualmente
**Gate**: `cd web && npm run build`

---

### T12: `web/src/components/RecordingList.tsx` — lista de gravações com estado vazio

**What**: Componente container que renderiza lista de `RecordingItem` ou mensagem de estado vazio
**Where**: `web/src/components/RecordingList.tsx`
**Depends on**: T3 (tipos), T11 (RecordingItem)
**Reuses**: `Recording` de `types.ts`, `RecordingItem`
**Requirement**: EAR-07, EAR-08

**Tools**:
- MCP: Nenhum
- Skill: Nenhum

**Interface**:
```typescript
interface RecordingListProps {
  recordings: Recording[]
  loading: boolean
  currentId: string | null
  onPlay(recording: Recording): void
  onDelete(id: string): void
  onDownload(recording: Recording): void
}
```

**Comportamento**:
- `loading === true` → spinner ou texto "Carregando..."
- `recordings.length === 0` → "Nenhuma gravação ainda"
- Caso contrário → lista de `RecordingItem`

**Done when**:
- [ ] Estado de loading exibido enquanto IndexedDB não carregou
- [ ] Mensagem de vazio quando sem gravações
- [ ] Lista renderiza item por item com `key={recording.id}`
- [ ] `currentId` passado a cada item como `isPlaying`
- [ ] Gate: `cd web && npm run build` sem erros

**Tests**: Verificação visual com lista vazia e com itens
**Gate**: `cd web && npm run build`

---

### T13: `web/src/App.tsx` — integração de todos os hooks e componentes

**What**: Componente raiz que instancia os 3 hooks e conecta todos os componentes
**Where**: `web/src/App.tsx`
**Depends on**: T6 (useConnection), T7 (useRecordings), T8 (useAudioPlayer), T10 (ConnectionPanel), T12 (RecordingList)
**Reuses**: Todos os hooks e componentes anteriores
**Requirement**: EAR-01 a EAR-13 (integração end-to-end)

**Tools**:
- MCP: Nenhum
- Skill: Nenhum

**Estrutura**:
```tsx
export function App() {
  const { recordings, loading, dbError, addRecording, deleteRecording } = useRecordings()
  const { currentId, audioRef, play, stop } = useAudioPlayer()
  const { state, error, connect, disconnect } = useConnection(
    (blob, duration) => addRecording(blob, duration)
  )

  return (
    <main>
      <h1>ESP32 Audio Recorder</h1>
      {dbError && <p role="alert">{dbError}</p>}
      <ConnectionPanel state={state} error={error} onConnect={connect} onDisconnect={disconnect} />
      <RecordingList
        recordings={recordings}
        loading={loading}
        currentId={currentId}
        onPlay={play}
        onDelete={(id) => { if (currentId === id) stop(); deleteRecording(id) }}
        onDownload={/* inline anchor download */}
      />
      <audio ref={audioRef} />
    </main>
  )
}
```

**Done when**:
- [ ] `onRecordingSaved` de `useConnection` chama `addRecording` com blob e duração
- [ ] Excluir gravação em reprodução chama `stop()` antes de `deleteRecording`
- [ ] Elemento `<audio>` único no DOM com `ref={audioRef}`
- [ ] `dbError` exibido quando IndexedDB falha
- [ ] Gate: `cd web && npm run build` sem erros
- [ ] Verificação manual end-to-end: pressionar botão ESP32 → gravação aparece na lista → reproduz

**Tests**: Verificação manual end-to-end
**Gate**: `cd web && npm run build`

---

### T14: `README.md` — documentação do monorepo

**What**: README raiz explicando o projeto, estrutura, pré-requisitos e como rodar cada parte
**Where**: `README.md`
**Depends on**: T2 (confirmar comandos do web app), T1 (confirmar comandos do firmware)
**Reuses**: Informações do spec, design e STACK.md
**Requirement**: Success Criteria do spec (estrutura de monorepo documentada)

**Tools**:
- MCP: Nenhum
- Skill: Nenhum

**Seções**:
1. O que é este projeto (1 parágrafo)
2. Estrutura do repositório
3. Hardware necessário (pinout do INMP441)
4. Firmware — pré-requisitos, configurar SSID/senha, build e flash
5. Web app — pré-requisitos, `npm install`, `npm run dev`, acessar no browser
6. Como usar (passo a passo: ligar ESP32 → abrir app → informar IP → pressionar botão)

**Done when**:
- [ ] Comandos de firmware e web app corretos e testados
- [ ] Pinout documentado (já existe no cabeçalho do .c — copiar)
- [ ] Passo a passo de uso completo

**Tests**: Seguir o README do zero em ambiente limpo
**Gate**: Leitura humana

---

## Resumo de Paralelismo

| Phase | Tasks Paralelas | Bloqueio |
|---|---|---|
| 1 | T1, T2 | — |
| 2 | T3 | T2 |
| 3 | T4, T5, T8, T9 | T3 |
| 4 | T6, T7, T10, T11 | T4 (→T6), T5 (→T7), T9 (→T10), T8+T9 (→T11) |
| 5 | T12 | T11 |
| 6 | T13, T14 | T6+T7+T10+T12 (→T13), T1+T2 (→T14) |

**Total**: 14 tasks, máximo 4 em paralelo (Phase 3 e 4)
