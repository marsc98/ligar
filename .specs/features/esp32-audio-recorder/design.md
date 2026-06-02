# ESP32 Audio Recorder — Design

**Spec**: `.specs/features/esp32-audio-recorder/spec.md`
**Status**: Draft

---

## Architecture Overview

Monorepo com dois componentes independentes: firmware ESP32 (C/ESP-IDF, existente com pequenas alterações) e web app (React + TypeScript + Vite, novo). O browser conecta diretamente ao WebSocket da ESP32 — sem backend.

```mermaid
graph TD
    BTN[Botão GPIO 4]:::hw --> FW
    MIC[INMP441 I2S]:::hw --> FW

    subgraph ESP32["ESP32 — firmware (porta 80)"]
        FW[audio_task\nbtn_task]
        WS[WebSocket /record]
        FW -->|text frame: RECORDING_START| WS
        FW -->|binary frames: WAV header + chunks| WS
    end

    WS -->|ws://IP/record| HOOK

    subgraph Browser["Browser — React app"]
        HOOK[useConnection\nstate machine]
        HOOK -->|chunks acumulados| ASM[lib/wav.ts\nassembler]
        ASM -->|Blob audio/wav| DB_HOOK[useRecordings]
        DB_HOOK -->|CRUD| IDB[(IndexedDB\npoc-microfone)]
        DB_HOOK -->|Recording[]| LIST[RecordingList]
        LIST -->|Blob| PLAYER[useAudioPlayer\n Object URL → audio]
    end

    classDef hw fill:#666,color:#fff
```

---

## Estrutura de Diretórios

```
poc-microfone/
├── CMakeLists.txt              # ESP-IDF root (inalterado)
├── main/                       # Firmware (alterações mínimas)
│   ├── CMakeLists.txt
│   └── poc-microfone.c
├── web/                        # React app (novo)
│   ├── src/
│   │   ├── lib/
│   │   │   ├── db.ts           # IndexedDB wrapper Promise-based
│   │   │   └── wav.ts          # WAV assembler + parser de header
│   │   ├── hooks/
│   │   │   ├── useConnection.ts   # WebSocket + state machine
│   │   │   ├── useRecordings.ts   # CRUD recordings + lista
│   │   │   └── useAudioPlayer.ts  # Object URL + elemento <audio>
│   │   ├── components/
│   │   │   ├── ConnectionPanel.tsx
│   │   │   ├── StatusBadge.tsx
│   │   │   ├── RecordingList.tsx
│   │   │   └── RecordingItem.tsx
│   │   ├── types.ts
│   │   ├── App.tsx
│   │   └── main.tsx
│   ├── index.html
│   ├── package.json
│   └── vite.config.ts
└── README.md
```

---

## Code Reuse Analysis

### Firmware existente a reaproveitar

| Componente | Arquivo | Como usar |
|---|---|---|
| Endpoint `/record` WebSocket | `main/poc-microfone.c:198` | Reutilizar sem alterações no handler de handshake |
| `ws_send_binary()` | `poc-microfone.c:185` | Modelo para novo `ws_send_text()` |
| Estado `APP_RECORDING` | `poc-microfone.c:278` | Adicionar notificação de texto antes do log existente |

### Alterações necessárias no firmware

Duas adições cirúrgicas em `poc-microfone.c`:

**1. Helper `ws_send_text()`** — análogo ao `ws_send_binary` existente:
```c
static esp_err_t ws_send_text(httpd_handle_t hd, int fd, const char *text) {
    httpd_ws_frame_t pkt = {
        .final = true,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)text,
        .len = strlen(text),
    };
    return httpd_ws_send_frame_async(hd, fd, &pkt);
}
```

**2. Notificação `RECORDING_START`** em `audio_task` (linha ~278):
```c
if (btn && g_ws_record_fd >= 0 && g_state != APP_RECORDING) {
    g_state = APP_RECORDING;
    g_record_len = 0;
    ws_send_text(server, g_ws_record_fd, "RECORDING_START"); // NOVO
    ESP_LOGI(TAG, "Gravação iniciada");
}
```

**Motivação**: sem essa notificação, o browser não tem como saber que o botão foi pressionado — o firmware não envia nada durante a gravação. A notificação habilita o badge "Gravando..." (EAR-04 / EAR-12).

**3. Fix M1 (CONCERNS.md)** — limpar fd quando `ws_send_binary` falha durante transmissão:
```c
if (ws_send_binary(server, fd, g_record_buf + offset, ...) != ESP_OK) {
    xSemaphoreTake(g_ws_mutex, portMAX_DELAY);
    g_ws_record_fd = -1;
    xSemaphoreGive(g_ws_mutex);
    break;
}
```

---

## State Machine — `useConnection`

```
DISCONNECTED
    │ connect(ip)
    ▼
CONNECTING
    │ ws.onopen
    ▼
IDLE ◀─────────────────────────────────────────────────────┐
    │ text frame "RECORDING_START"                          │
    ▼                                                       │
RECORDING                                                   │
    │ primeiro binary frame (header WAV, 44 bytes)          │
    ▼                                                       │
RECEIVING                                                   │
    │ 500ms sem novos frames (Blob montado + salvo)         │
    ▼                                                       │
SAVING ─────────────────────────────────────────────────────┘
    │
    (qualquer estado) ws.onclose / ws.onerror
    ▼
DISCONNECTED
```

**Tipo:**
```typescript
type ConnectionState =
  | 'disconnected'
  | 'connecting'
  | 'idle'
  | 'recording'
  | 'receiving'
  | 'saving'
```

---

## Componentes

### `lib/db.ts`

- **Propósito**: Abstração Promise-based sobre IndexedDB. Isola toda API assíncrona/callback do resto do app.
- **Location**: `web/src/lib/db.ts`
- **Interfaces**:
  ```typescript
  openDB(): Promise<IDBDatabase>
  saveRecording(db: IDBDatabase, rec: Recording): Promise<void>
  getAllRecordings(db: IDBDatabase): Promise<Recording[]>
  deleteRecording(db: IDBDatabase, id: string): Promise<void>
  ```
- **Schema**: DB `poc-microfone` v1, object store `recordings` (keyPath: `id`), índice `by-timestamp` em `timestamp`

---

### `lib/wav.ts`

- **Propósito**: Monta Blob WAV a partir de chunks recebidos e extrai duração do header.
- **Location**: `web/src/lib/wav.ts`
- **Interfaces**:
  ```typescript
  parseWavDuration(headerBuffer: ArrayBuffer): number  // segundos
  assembleWavBlob(chunks: ArrayBuffer[]): Blob         // type: 'audio/wav'
  ```
- **Lógica de duração**: bytes 40–43 do header = `data_size` (uint32 LE). `duration = data_size / (16000 × 1 × 2)` → segundos para 16kHz mono 16-bit.
- **Reuses**: nenhum código existente — lógica pura, sem dependências.

---

### `hooks/useConnection.ts`

- **Propósito**: Gerencia ciclo de vida do WebSocket, state machine de gravação, acumula chunks e emite Blob ao fim.
- **Location**: `web/src/hooks/useConnection.ts`
- **Interfaces**:
  ```typescript
  function useConnection(onRecordingSaved: (blob: Blob) => void): {
    state: ConnectionState
    error: string | null
    connect(ip: string): void
    disconnect(): void
  }
  ```
- **Lógica de fim de transmissão**: `setTimeout(500ms)` reiniciado a cada frame binário. Ao disparar → `assembleWavBlob(chunks)` → `onRecordingSaved(blob)`.
- **Cleanup**: `useEffect` retorna `() => ws.close()` para evitar leak na desmontagem.

---

### `hooks/useRecordings.ts`

- **Propósito**: CRUD de gravações no IndexedDB, mantém lista reativa em estado React.
- **Location**: `web/src/hooks/useRecordings.ts`
- **Interfaces**:
  ```typescript
  function useRecordings(): {
    recordings: Recording[]
    loading: boolean
    dbError: string | null
    addRecording(blob: Blob): Promise<void>
    deleteRecording(id: string): Promise<void>
  }
  ```
- **Geração de nome**: `rec-${new Date().toISOString().replace(/[:.]/g, '-').slice(0,19)}`
- **Ordenação**: mais recentes primeiro (sort por `timestamp` desc em memória)

---

### `hooks/useAudioPlayer.ts`

- **Propósito**: Gerencia qual gravação está tocando, cria e revoga Object URLs.
- **Location**: `web/src/hooks/useAudioPlayer.ts`
- **Interfaces**:
  ```typescript
  function useAudioPlayer(): {
    currentId: string | null
    play(recording: Recording): void
    stop(): void
  }
  ```
- **Object URL lifecycle**: `URL.createObjectURL(blob)` ao `play()`, `URL.revokeObjectURL(url)` ao `stop()` ou ao trocar gravação.
- **Elemento `<audio>`**: ref para elemento único no DOM, compartilhado entre gravações.

---

### `components/ConnectionPanel.tsx`

- **Propósito**: Input de IP + botão Conectar/Desconectar.
- **Props**: `state: ConnectionState`, `onConnect(ip): void`, `onDisconnect(): void`, `error: string | null`

---

### `components/StatusBadge.tsx`

- **Propósito**: Badge colorido que reflete `ConnectionState`.
- **Props**: `state: ConnectionState`
- **Mapeamento visual**:

| State | Cor | Texto | Animação |
|---|---|---|---|
| `disconnected` | cinza | Desconectado | — |
| `connecting` | azul | Conectando... | pulse |
| `idle` | verde | Pronto | — |
| `recording` | vermelho | Gravando... | pulse |
| `receiving` | amarelo | Recebendo... | — |
| `saving` | amarelo | Salvando... | — |

---

### `components/RecordingList.tsx`

- **Propósito**: Lista de gravações com estado vazio.
- **Props**: `recordings: Recording[]`, `loading: boolean`, `currentId: string | null`, `onPlay(r): void`, `onDelete(id): void`, `onDownload(r): void`

---

### `components/RecordingItem.tsx`

- **Propósito**: Item individual com nome, data, tamanho e ações.
- **Props**: `recording: Recording`, `isPlaying: boolean`, ações herdadas de `RecordingList`
- **Exibe**: nome, `new Date(timestamp).toLocaleString('pt-BR')`, `(size / 1024).toFixed(0) KB`, duração em `mm:ss`

---

## Data Models

### `types.ts`

```typescript
export interface Recording {
  id: string        // crypto.randomUUID()
  name: string      // "rec-2026-05-20-143022"
  timestamp: number // Date.now() ao salvar
  duration: number  // segundos, do header WAV
  size: number      // blob.size em bytes
  blob: Blob        // type: 'audio/wav'
}

export type ConnectionState =
  | 'disconnected'
  | 'connecting'
  | 'idle'
  | 'recording'
  | 'receiving'
  | 'saving'
```

---

## Error Handling Strategy

| Cenário | Handling | O que o usuário vê |
|---|---|---|
| IP inválido / recusado | `ws.onerror` antes de `onopen` | "Não foi possível conectar ao IP informado" |
| WS cai durante RECEIVING | `ws.onclose` em estado `receiving` | "Gravação incompleta — reconecte e tente novamente" |
| IndexedDB indisponível | try/catch em `openDB()` | "Armazenamento indisponível" |
| Quota do IndexedDB excedida | catch `QuotaExceededError` | "Armazenamento cheio — exclua gravações antigas" |
| Browser sem IndexedDB | `!window.indexedDB` no mount | "Seu browser não suporta armazenamento local" |

---

## Tech Decisions

| Decisão | Escolha | Rationale |
|---|---|---|
| Detecção de fim de transmissão | Timeout 500ms após último frame | Firmware não envia frame de fim explícito; 500ms > delay `vTaskDelay(5ms)` entre chunks do firmware |
| Notificação "RECORDING_START" | Text frame WebSocket antes dos dados | Única forma de o browser saber que o botão foi pressionado sem polling |
| Object URL por demanda | Criar em `play()`, revogar em `stop()` | Evita manter URLs abertas para todos os Blobs da lista |
| IndexedDB aberto uma vez | `openDB()` no mount de `useRecordings` | Abrir/fechar a cada operação é mais lento e verboso |
| Sem lib de IndexedDB (idb, Dexie) | API nativa com wrapper simples | Dependência zero para 4 operações simples; não justifica overhead de pacote |

---

## Mitigações de CONCERNS.md

| Concern | Relevância | Ação no design |
|---|---|---|
| M1 — fd WebSocket não limpo | Alta | Fix incluído no firmware: verificar retorno de `ws_send_binary` e zerara fd |
| C1 — Credenciais hardcoded | Fora do escopo web | Sem ação nesta feature |
| M2 — Buffer sem PSRAM | Fora do escopo web | Documentado como TODO nas Open Questions do spec |
