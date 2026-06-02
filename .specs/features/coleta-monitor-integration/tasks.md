# Coleta + Monitor Integration — Tasks

**Data:** 2026-05-24
**Spec:** `spec.md` | **Design:** `design.md`

---

## Dependências entre fases

```
FASE 0 (Reestruturação)
  └── bloqueia tudo: firmware não compila sem a pasta correta

FASE 1A (Firmware KWS)          FASE 1B (Frontend types + hooks)
  └── paralelo com 1B               └── paralelo com 1A

FASE 2 (Frontend UI)
  └── requer 1B completo

FASE 3 (Makefile + README)
  └── requer 0 completo
  └── paralelo com 1A e 1B

FASE 4 (RecordingList grupos)   FASE 5 (Pipeline Makefile)
  └── requer 2 completo             └── requer 3 completo
  └── independente de 5             └── independente de 4
```

---

## FASE 0 — Reestruturação do Repo

**Bloqueia:** todas as outras fases. Executar primeiro, sequencialmente.

### T0.1 — Mover firmware

```
Ação: mover main/ → firmware/main/
Arquivos movidos:
  main/CMakeLists.txt     → firmware/main/CMakeLists.txt
  main/poc-microfone.c    → firmware/main/poc-microfone.c
  main/wifi_config.h      → firmware/main/wifi_config.h
  main/wifi_config.h.example → firmware/main/wifi_config.h.example (se existir)

Verificação: ls firmware/main/ lista os 4 arquivos
```

### T0.2 — Atualizar CMakeLists.txt raiz

```
Arquivo: CMakeLists.txt (raiz)
Mudança: adicionar `list(APPEND EXTRA_COMPONENT_DIRS "firmware")` antes do include

Resultado:
  cmake_minimum_required(VERSION 3.22)
  list(APPEND EXTRA_COMPONENT_DIRS "firmware")
  include($ENV{IDF_PATH}/tools/cmake/project.cmake)
  project(poc-microfone)

Verificação: idf.py build retorna exit 0 (sem os novos arquivos KWS ainda)
```

### T0.3 — Copiar arquivos KWS do poc-identificador

```
Origem: /home/marco/projetos/poc-identificador-de-palavras/firmware/main/
Destino: firmware/main/

Arquivos:
  mfcc.c → firmware/main/mfcc.c
  mfcc.h → firmware/main/mfcc.h
  dtw.c  → firmware/main/dtw.c
  dtw.h  → firmware/main/dtw.h
  templates.h → firmware/main/templates.h

Pós-cópia: adicionar ao início de templates.h comentário:
  /* PLACEHOLDER — gerado com o poc-identificador (palavra "teste").
   * Regerar com: make train WORD=<palavra> && make train-templates
   */

Verificação: ls firmware/main/ lista mfcc.c mfcc.h dtw.c dtw.h templates.h
```

### T0.4 — Atualizar firmware/main/CMakeLists.txt

```
Arquivo: firmware/main/CMakeLists.txt
Mudança: adicionar "mfcc.c" "dtw.c" ao SRCS

Resultado:
  idf_component_register(SRCS "poc-microfone.c" "mfcc.c" "dtw.c"
                      INCLUDE_DIRS "."
                      REQUIRES driver esp_wifi esp_event esp_netif
                               nvs_flash esp_http_server
                      PRIV_REQUIRES esp_driver_gpio esp_driver_i2s)

Verificação: idf.py build compila com mfcc.c e dtw.c sem erros
```

### T0.5 — Criar training/

```
Origem: /home/marco/projetos/poc-identificador-de-palavras/training/
Destino: training/

Arquivos a copiar:
  firmware_mfcc.py       → training/firmware_mfcc.py    [ajustar DURATION_S]
  extract_features.py    → training/extract_features.py
  generate_templates.py  → training/generate_templates.py
  requirements.txt       → training/requirements.txt

Ajuste obrigatório em firmware_mfcc.py:
  DURATION_S = 1.5    (era 1.0)

Criar vazios:
  training/samples/.gitkeep
  training/features/.gitkeep

NÃO copiar: samples/*.wav, features/*.npy (dados do poc-identificador)

Verificação: python3 training/generate_templates.py --help (ou import sem erro)
```

---

## FASE 1A — Firmware KWS (paralelo com 1B e 3)

### T1A.1 — Adicionar includes e globals KWS

```
Arquivo: firmware/main/poc-microfone.c
Seção: topo do arquivo, após #include "wifi_config.h"

Adicionar:
  #include <math.h>
  #include "mfcc.h"
  #include "dtw.h"
  #include "templates.h"

Adicionar ao bloco de globals (após g_ws_stream_fd):
  static int   g_ws_monitor_fd = -1;
  static bool  g_kws_paused    = false;
  static float g_dtw_threshold = 800.0f;

  /* Ring buffer KWS — tamanho definido em mfcc.h (MFCC_WIN_SAMPLES) */
  static int16_t g_kws_ring[MFCC_WIN_SAMPLES];
  static int     g_kws_ring_pos = 0;

  /* Output MFCC — KWS_N_FRAMES × MFCC_N_COEFS floats */
  static float g_mfcc_out[MFCC_N_FRAMES * MFCC_N_COEFS];

Verificação: idf.py build compila (pode ter warnings de unused, ok)
```

### T1A.2 — Modificar ws_record_handler para pausar/retomar KWS

```
Arquivo: firmware/main/poc-microfone.c
Função: ws_record_handler

Na branch HTTP_GET (conexão estabelecida), após g_ws_record_fd = ...:
  g_kws_paused = true;
  ESP_LOGI(TAG, "/record conectado — KWS pausado");

Na branch de erro/fechamento (ret != ESP_OK), após g_ws_record_fd = -1:
  g_kws_paused = false;
  ESP_LOGI(TAG, "/record desconectado — KWS retomado");

Verificação: log "KWS pausado" aparece ao conectar /record
```

### T1A.3 — Adicionar ws_monitor_handler

```
Arquivo: firmware/main/poc-microfone.c
Posição: após ws_stream_handler, antes de audio_task

Implementar ws_monitor_handler:
  - HTTP_GET: g_ws_monitor_fd = httpd_req_to_sockfd(req)
  - recv com payload `{"threshold": float}`: atualiza g_dtw_threshold
  - erro recv: g_ws_monitor_fd = -1

Padrão idêntico aos handlers existentes.

Verificação: endpoint /monitor aceita conexão WebSocket
```

### T1A.4 — Modificar start_webserver (timeouts + /monitor)

```
Arquivo: firmware/main/poc-microfone.c
Função: start_webserver

Adicionar após config.max_open_sockets:
  config.max_open_sockets  = 5;          // era 4
  config.recv_wait_timeout = 120;
  config.send_wait_timeout = 120;

Adicionar registro do endpoint /monitor (após /stream):
  httpd_uri_t ws_monitor_uri = {
      .uri          = "/monitor",
      .method       = HTTP_GET,
      .handler      = ws_monitor_handler,
      .user_ctx     = NULL,
      .is_websocket = true,
  };
  httpd_register_uri_handler(server, &ws_monitor_uri);

Verificação: log "Servidor HTTP iniciado" aparece; /monitor acessível
```

### T1A.5 — Implementar compute_rms (helper)

```
Arquivo: firmware/main/poc-microfone.c
Posição: antes de kws_task

Adicionar função:
  static float compute_rms(const int16_t *buf, size_t n) {
      float sum = 0.0f;
      for (size_t i = 0; i < n; i++) sum += (float)buf[i] * buf[i];
      return sqrtf(sum / n);
  }

Verificação: compila sem erro (math.h já incluído no T1A.1)
```

### T1A.6 — Implementar kws_task

```
Arquivo: firmware/main/poc-microfone.c
Posição: após compute_rms, antes de button_task

Constantes locais (topo do arquivo ou da função):
  #define VAD_RMS_THRESHOLD    300.0f
  #define DETECTION_COOLDOWN_MS 1000

Lógica (ver design.md §kws_task para pseudocódigo completo):
  1. Guard: g_kws_paused OR g_state != APP_IDLE → delay 10ms, continue
  2. i2s_read_16bit para chunk de I2S_READ_CHUNK amostras
  3. Atualiza g_kws_ring circular
  4. compute_rms: se < VAD_RMS_THRESHOLD → continue
  5. Cooldown check com xTaskGetTickCount
  6. mfcc_compute(g_kws_ring, g_kws_ring_pos, MFCC_WIN_SAMPLES, g_mfcc_out)
  7. DTW contra todos KWS_WORDS[w].templates[t]: best_word, best_dist, word_best[]
  8. detected = (best_word >= 0 && best_dist < g_dtw_threshold)
  9. Se g_ws_monitor_fd >= 0: montar JSON e ws_send_text

JSON emitido (spec §Payload):
  {"rms": <float>, "threshold": <float>, "word": <nome|null>, "dists": {<palavra>: <dist>}}

Nota: null em C → usar literal "null" no snprintf quando !detected

Verificação: com /monitor conectado via websocat, JSON aparece a cada chunk VAD
```

### T1A.7 — Registrar kws_task em app_main

```
Arquivo: firmware/main/poc-microfone.c
Função: app_main

Após xTaskCreate(audio_task, ...):
  xTaskCreate(kws_task, "kws_task", 8192, server, 6, NULL);

Verificação: log "KWS iniciado" aparece no boot; sem crash no primeiro frame
```

---

## FASE 1B — Frontend Types + Hook (paralelo com 1A e 3)

### T1B.1 — Atualizar types.ts

```
Arquivo: web/src/types.ts
Mudança: adicionar campo collection? à interface Recording

export interface Recording {
  id: string
  name: string
  timestamp: number
  duration: number
  size: number
  blob: Blob
  transcription?: string
  collection?: {
    word: string
    sessionId: string
  }
}

Sem migração de IndexedDB necessária.

Verificação: TypeScript compila sem erros (tsc --noEmit)
```

### T1B.2 — Criar useCollection.ts

```
Arquivo: web/src/hooks/useCollection.ts

Assinatura pública:
  export function useCollection(
    onSave: (blob: Blob, duration: number, collection: { word: string; sessionId: string }) => void
  ): {
    state: 'idle' | 'connecting' | 'collecting'
    sampleCount: number
    startCollection(ip: string, word: string): void
    stopCollection(): void
  }

Lógica (ver design.md §useCollection para detalhes):
  - buffer: acumula Int16Array de chunks recebidos
  - a cada 24000 amostras: chama assemblePcmToWav, chama onSave, zera buffer
  - RECORDING_END: fragmento final se >= 8000 amostras
  - nomes sequenciais: word_001, word_002 ...
  - sessionId: crypto.randomUUID() na chamada de startCollection

Referência base: useConnection.ts (mesmo padrão de WebSocket + ref management)
Referência wav: lib/wav.ts — assemblePcmToWav(chunks, numSamples)

Verificação: TypeScript compila; teste manual via CollectionTab
```

---

## FASE 2 — Frontend UI (requer 1B completo)

### T2.1 — Adicionar navegação por abas em App.tsx

```
Arquivo: web/src/App.tsx

Adicionar estado: const [activeTab, setActiveTab] = useState<'gravacoes' | 'monitor' | 'coleta'>('gravacoes')

Renderizar TabBar simples (3 botões, estilo coerente com o visual atual dark):
  Gravações | Monitor | Coleta

Condicionalmente renderizar:
  activeTab === 'gravacoes' → conteúdo atual (ConnectionPanel, LiveTranscriptPanel, RecordingList)
  activeTab === 'monitor'   → <MonitorTab ip={ip} />
  activeTab === 'coleta'    → <CollectionTab ip={ip} onSave={...} />

ip continua como estado no App.tsx (compartilhado via props).

Verificação: trocar abas sem crash; aba Gravações mantém comportamento atual
```

### T2.2 — Criar MonitorTab.tsx

```
Arquivo: web/src/components/MonitorTab.tsx
Props: { ip: string }

Interface interna:
  interface MonitorEvent {
    ts: number
    rms: number
    threshold: number
    word: string | null
    dists: Record<string, number>
  }

Estado: events: MonitorEvent[], connected: boolean
WebSocket: gerenciado com useRef + useEffect (conecta no mount, desconecta no unmount)

Parse de mensagem: JSON.parse(ev.data) → prepend ao array → slice(0, 200)

Renderização de cada evento:
  - linha normal: fundo transparente, texto #94a3b8
  - detecção (word != null): fundo #22c55e18, borda-esquerda 3px #22c55e,
    badge verde com nome da palavra

Verificação: com firmware rodando, abrir aba Monitor e ver feed; falar → linha destacada
```

### T2.3 — Criar CollectionTab.tsx

```
Arquivo: web/src/components/CollectionTab.tsx
Props: {
  ip: string
  onSave(blob: Blob, duration: number, collection: { word: string; sessionId: string }): void
}

Usa: useCollection(onSave)

Layout conforme spec §P1-D e guide.md §3c:
  1. Campo texto palavra (label "Nome da palavra")
  2. Botão "Iniciar Coleta" — disabled se !ip.trim() || !word.trim()
  3. Durante collecting:
     - "Gravando: <palavra> ● N amostras capturadas"
     - "✂ corte automático a cada 1.5s"
     - Botão "Encerrar Coleta"
  4. Lista da sessão: amostras salvas aparecem conforme chegam

Verificação: coletar 3 amostras manualmente e verificar que aparecem na lista
```

### T2.4 — Conectar CollectionTab ao useRecordings em App.tsx

```
Arquivo: web/src/App.tsx

Criar handler:
  const handleCollectionSave = async (blob: Blob, duration: number, collection: {...}) => {
    await addRecording(blob, duration, collection)
    // NÃO transcrever amostras de coleta (evita gasto de Whisper desnecessário)
  }

Verificar que addRecording em useRecordings aceita o campo collection e persiste no IndexedDB.
Ajustar lib/db.ts e useRecordings.ts se necessário para passar collection ao salvar.

Verificação: após coleta, abrir aba Gravações e ver amostras no IndexedDB
```

---

## FASE 3 — Makefile + README (paralelo com 1A e 1B)

### T3.1 — Criar Makefile raiz

```
Arquivo: Makefile (raiz do projeto)

Targets mínimos (ver design.md §Makefile):
  help, setup
  firmware-build, firmware-flash PORT=, firmware-monitor PORT=, firmware-clean
  web-dev, web-build
  train WORD=, train-templates
  pipeline WORD=, flash WORD= PORT=

Guard para WORD obrigatório:
  ifndef WORD
    $(error WORD é obrigatório. Uso: make train WORD=<palavra>)
  endif

Verificação: make help imprime todos os targets; make firmware-build funciona
```

### T3.2 — Atualizar README.md raiz

```
Arquivo: README.md

Seções a adicionar/atualizar:
  1. Estrutura do projeto (árvore firmware/ training/ web/)
  2. Diagrama de fluxo cross-módulo (coleta → training → firmware → monitor)
  3. Quickstart por módulo:
     - Firmware: make firmware-build / make firmware-flash PORT=...
     - Web: make web-dev
     - Training: make train WORD=ligar && make train-templates
  4. Pipeline completo: make pipeline WORD=ligar

Verificação: README renderiza corretamente no GitHub (markdown válido)
```

---

## FASE 4 — RecordingList com Grupos Colapsáveis (requer Fase 2)

### T4.1 — Atualizar RecordingList.tsx

```
Arquivo: web/src/components/RecordingList.tsx

Lógica de separação (ver design.md §RecordingList):
  - Separar recordings com e sem collection
  - Agrupar por sessionId
  - Montar lista unificada ordenada por timestamp desc

Novo tipo de item:
  type ListItem =
    | { kind: 'individual'; recording: Recording }
    | { kind: 'session'; sessionId: string; word: string; items: Recording[]; ts: number }

Renderização de sessão colapsável:
  - Colapsado: "▶ <palavra> — N amostras — <data>"
  - Expandido: RecordingItem para cada amostra
  - Toggle via useState<Set<string>> de sessionIds expandidos

RecordingItem sem mudanças.

Verificação: criar 3 coletas de "ligar" + 1 gravação normal → lista mostra grupo + individual
```

---

## FASE 5 — Pipeline Makefile end-to-end (requer Fase 3)

### T5.1 — Implementar targets pipeline e flash no Makefile

```
Arquivo: Makefile

Target pipeline:
  pipeline: guard-WORD train train-templates firmware-build

Target flash:
  flash: pipeline
    idf.py -p $(PORT) flash

Target guard-WORD (internal):
  guard-%:
    @[ "${$*}" ] || (echo "Erro: $* é obrigatório. Uso: make $@ $*=<valor>"; exit 1)

Verificação:
  make pipeline WORD=teste → executa extract_features, generate_templates, idf.py build
  make flash WORD=teste PORT=/dev/ttyUSB0 → full pipeline + flash
```

---

## Resumo de Execução

| Fase | Tasks | Pode paralelizar com |
|---|---|---|
| 0 | T0.1 → T0.2 → T0.3 → T0.4 → T0.5 | Nada (pré-requisito) |
| 1A | T1A.1 → T1A.2 → T1A.3 → T1A.4 → T1A.5 → T1A.6 → T1A.7 | 1B, 3 |
| 1B | T1B.1 → T1B.2 | 1A, 3 |
| 2 | T2.1 → T2.2 → T2.3 → T2.4 | 3 |
| 3 | T3.1 → T3.2 | 1A, 1B |
| 4 | T4.1 | 5 |
| 5 | T5.1 | 4 |

**Caminho crítico:** 0 → 1B → 2 → 4

**Total de tasks:** 20
**Agentes paralelos recomendados após Fase 0:** 3 (firmware, frontend, docs)
