# Integração Monitor + Coleta no poc-microfone

**Data:** 2026-05-24
**Escopo:** Adicionar abas Monitor e Coleta ao poc-microfone, portando funcionalidades do poc-identificador-de-palavras.
**Fonte:** Entrevista informal + exploração dos dois projetos.

---

## Contexto

Dois projetos coexistem:

| Projeto                           | Caminho                                              | O que tem                                                           |
| --------------------------------- | ---------------------------------------------------- | ------------------------------------------------------------------- |
| **poc-microfone**                 | `/home/marco/projetos/poc-microfone`                 | Gravação WAV via botão + stream ao vivo + Whisper. UI com React/TS  |
| **poc-identificador-de-palavras** | `/home/marco/projetos/poc-identificador-de-palavras` | KWS (MFCC+DTW), pipeline de treinamento Python, endpoint `/monitor` |

O objetivo é implementar no **poc-microfone** (alvo) as funcionalidades abaixo, usando o poc-identificador como referência de código.

---

## O que implementar

### 1. Abas na UI

Substituir o layout atual por duas abas principais:

- **Monitor** — feed de eventos KWS/VAD em tempo real
- **Coleta** — gravação de amostras de treinamento agrupadas por palavra

O conteúdo atual do App.tsx (gravações normais + stream ao vivo) passa a ficar dentro da aba que fizer mais sentido contextualmente, ou pode virar uma terceira aba "Gravações". Decisão de layout fica a critério do agente, mantendo coerência visual.

---

### 2. Aba Monitor

**O que faz:** conecta ao endpoint WebSocket `/monitor` da ESP e exibe um feed rolante de eventos VAD/KWS.

**Referência de firmware:** `poc-identificador-de-palavras/firmware/main/main.c`

- Função `ws_monitor_handler` (linha ~187)
- Função `kws_task` (linha ~223) — onde os eventos são gerados e enviados
- Payload JSON emitido: `{ "rms": float, "threshold": float, "word": string|null, "dists": { [word]: float } }`

**O firmware do poc-microfone precisa receber o pipeline KWS completo.** Copiar/adaptar:

- `poc-identificador-de-palavras/firmware/main/mfcc.c` + `mfcc.h`
- `poc-identificador-de-palavras/firmware/main/dtw.c` + `dtw.h`
- `poc-identificador-de-palavras/firmware/main/templates.h` (gerado pelo pipeline Python)
- Registrar endpoint `/monitor` no servidor HTTP do poc-microfone
- A task KWS deve pausar durante gravação de coleta (igual ao poc-identificador: `g_kws_paused = true` ao conectar `/record`)

**UI — componente MonitorTab:**

- Conecta a `ws://<IP>/monitor` ao entrar na aba
- Feed rolante: cada evento = uma linha com `timestamp`, `rms` e distâncias por palavra
- Quando `word != null` (detecção): linha destacada com badge colorido (ex: verde) e nome da palavra em destaque visual claro — não apenas texto, usar cor de fundo ou borda
- Máximo de ~200 linhas no feed (descartar as mais antigas)
- Desconecta ao sair da aba

---

### 3. Aba Coleta

**O que faz:** permite gravar múltiplas amostras de treinamento de uma palavra em sequência, com corte automático a cada 1.5s pelo browser.

#### 3a. Tipos — ajustar `web/src/types.ts`

```ts
export interface Recording {
  id: string;
  name: string;
  timestamp: number;
  duration: number;
  size: number;
  blob: Blob;
  transcription?: string;
  collection?: {
    // ← novo campo
    word: string; // nome da palavra, ex: "ligar"
    sessionId: string; // UUID da sessão de coleta
  };
}
```

Não criar novo object store no IndexedDB. O store `recordings` existente suporta o campo sem migração de schema (IndexedDB é schemaless — apenas adicionar o campo ao tipo TS).

**Referência:** `poc-microfone/web/src/lib/db.ts`, `poc-microfone/web/src/hooks/useRecordings.ts`

#### 3b. Hook `useCollection`

Criar `web/src/hooks/useCollection.ts`. Responsabilidades:

1. Mantém `word: string` e `sessionId: string` (gerado na hora de "Iniciar Coleta")
2. Conecta ao WebSocket `/record` da ESP (mesmo endpoint do `useConnection`)
3. Acumula chunks PCM recebidos em buffer interno
4. **A cada 24000 amostras acumuladas (= 1.5s a 16kHz):** monta WAV com `assemblePcmToWav` (de `poc-microfone/web/src/lib/wav.ts`) e salva como `Recording` com `collection: { word, sessionId }`. Zera o buffer para a próxima amostra.
5. Ao receber `RECORDING_END:<n>`: processa o fragmento final se tiver amostras suficientes (>= 8000 = 0.5s mínimo aceitável), caso contrário descarta silenciosamente. Encerra a sessão.
6. Expõe: `{ state, sampleCount, startCollection(ip, word), stopCollection() }`

**Referência base:** `poc-microfone/web/src/hooks/useConnection.ts` — copiar a lógica de WebSocket e `assemblePcmToWav`, adaptar para corte periódico.

**Referência wav:** `poc-microfone/web/src/lib/wav.ts` — `assemblePcmToWav(chunks, numSamples)` já existe e funciona.

#### 3c. UI — componente `CollectionTab`

Layout da aba:

```
[ Campo de texto: "Nome da palavra (ex: ligar)" ]
[ Botão: "Iniciar Coleta" ]           ← habilitado só com IP preenchido e palavra não-vazia

── durante coleta ──────────────────────────────
  Gravando: "ligar"   ● 3 amostras capturadas
  [ Pressione o botão da ESP para encerrar ]
  [ indicador visual de que corte é automático a cada 1.5s ]
[ Botão: "Encerrar Coleta" ]           ← cancela via WebSocket close

── lista de amostras da sessão atual (ao vivo) ──
  ligar_001.wav  1.5s
  ligar_002.wav  1.5s
  ligar_003.wav  1.5s  ← vai aparecendo conforme corta
```

- O campo IP reutiliza o mesmo estado global do app (não duplicar)
- O botão "Encerrar Coleta" na UI fecha o WebSocket — o ESP detecta `ws.onclose` e reseta para IDLE (comportamento já existente no firmware)
- Mostrar indicador claro de que "cada amostra é cortada automaticamente a cada 1.5s" para que o usuário saiba que não precisa fazer nada além de falar e esperar

#### 3d. Lista de gravações — ajustar `RecordingList`

Agrupar gravações de coleta em seções colapsáveis por sessão:

- Gravações normais: continuam aparecendo individualmente em ordem cronológica reversa
- Sessões de coleta: agrupadas por `sessionId`, exibidas como item expansível:
  ```
  ▶ ligar — 12 amostras — 24/05/2026 14:32
      ligar_001.wav  1.5s  [▶ play] [⬇ download] [🗑 delete]
      ligar_002.wav  1.5s  ...
  ```
- Ordenação geral: pelo timestamp da amostra mais recente do grupo (para sessões) ou pelo timestamp da gravação (para individuais), intercalados cronologicamente

**Referência:** `poc-microfone/web/src/components/RecordingList.tsx`, `poc-microfone/web/src/components/RecordingItem.tsx`

---

### 4. Firmware — ajustes necessários no poc-microfone

#### 4a. Adicionar pipeline KWS

Copiar para `poc-microfone/main/`:

- `mfcc.c` + `mfcc.h`
- `dtw.c` + `dtw.h`
- `templates.h` (versão atual do poc-identificador ou regerar com as palavras desejadas)

Adicionar ao `CMakeLists.txt` do poc-microfone os novos arquivos fonte.

Registrar endpoint `/monitor` e a `kws_task` — seguir exatamente o padrão do poc-identificador (`main.c` linhas ~185-300).

#### 4b. Timeouts do servidor HTTP

**CRÍTICO:** sessões de coleta podem durar 30–120s. O `esp_http_server` tem timeouts padrão de 5s que vão derrubar a conexão WebSocket durante gravação longa.

Em `start_webserver()` do poc-microfone, adicionar:

```c
config.recv_wait_timeout = 120;  // segundos
config.send_wait_timeout = 120;
```

**Referência:** `poc-microfone/main/poc-microfone.c` função `start_webserver()` (~linha 390).

#### 4c. Reset de estado no `ws.onclose`

O firmware atual já reseta `g_ws_record_fd = -1` e `g_state = APP_IDLE` quando o WebSocket fecha. Verificar que esse caminho é atingido também quando o browser fecha a conexão (não só quando a ESP detecta erro de envio). Testar com desconexão abrupta de rede durante gravação longa.

---

## Pipeline Python — sem mudanças para Coleta

O corte de 1.5s é feito pelo browser. O pipeline Python existente (`extract_features.py`, `generate_templates.py`) continua recebendo WAVs individuais de 1.5s como entrada.

**Única mudança necessária no Python:** ajustar `DURATION_S = 1.5` em `poc-identificador-de-palavras/training/firmware_mfcc.py` para que o MFCC extraído corresponda ao tamanho das amostras coletadas.

Consequência em cascata (automática pelos scripts existentes):

- `N_SAMPLES = 24000`
- `N_FRAMES = (24000 - 400) / 160 + 1 = 148`
- `templates.h` gerado com `KWS_N_FRAMES = 148`
- `mfcc.h` no firmware: `MFCC_WIN_SAMPLES = 24000`, `MFCC_N_FRAMES = 148`

---

## Decisões

| #   | Decisão                          | Valor                                                            |
| --- | -------------------------------- | ---------------------------------------------------------------- |
| 1   | Duração da amostra de coleta     | **1.5s = 24000 amostras**                                        |
| 2   | Monitor: estilo do feed          | Feed rolante, destaque colorido (badge/background) na detecção   |
| 3   | IndexedDB: estrutura para coleta | Campo `collection?` no `Recording` existente, mesmo object store |
| 4   | UI de coleta: entrada da palavra | Campo de texto livre antes de iniciar, não dropdown              |
| 5   | Corte das amostras               | **Browser-side** a cada 24000 amostras acumuladas                |
| 6   | Visualização das coletas         | Seções colapsáveis por sessão na `RecordingList`                 |

---

## Discretion do Agente

- Layout exato das abas (tabs fixos no topo, sidebar, etc.) — manter consistência com o estilo visual atual do poc-microfone
- Fragmento final da coleta: se < 8000 amostras (0.5s), descartar; acima disso, salvar — agente pode ajustar o limiar

---

## Ideias Adiadas

- **Dropdown de palavras pré-cadastradas** na Coleta baseado no `templates.h` atual — surgiu durante discussão sobre entrada de palavra
- **KWS em 2s** — tecnicamente viável (N_FRAMES=198, ring buffer 64KB), não priorizado agora
- **Corte via firmware** (ESP envia `SAMPLE_END` a cada 1.5s) — alternativa descartada em favor do corte no browser

---

## Questões Abertas

- **Palavras nos templates**: o agente deve usar o `templates.h` atual do poc-identificador (treinado com "teste") ou aguardar novas coletas? Recomendado: incluir o `templates.h` existente como placeholder e documentar que deve ser regerado após coletar amostras com o novo fluxo.
