# Coleta + Monitor Integration — Spec

## Problem Statement

O poc-microfone hoje é um gravador WAV com transcrição Whisper. O poc-identificador tem o pipeline KWS (MFCC+DTW) e coleta de amostras, mas são projetos separados. O objetivo é consolidar tudo em um único repo modular, portando Monitor e Coleta para o poc-microfone.

## Proposed Solution

Reorganizar poc-microfone em três módulos (`firmware/`, `training/`, `web/`) e implementar: (1) pipeline KWS no firmware com endpoint `/monitor`, (2) aba Monitor na UI exibindo feed de eventos KWS em tempo real, (3) aba Coleta para gravar amostras de treinamento segmentadas automaticamente a cada 1.5s.

## Goals

- [ ] Projeto organizado em `firmware/`, `training/`, `web/` com Makefile raiz
- [ ] Firmware compila com pipeline KWS (MFCC+DTW) e expõe `/monitor` via WebSocket
- [ ] Aba Monitor exibe feed rolante de eventos VAD/KWS com destaque visual em detecções
- [ ] Aba Coleta grava amostras de 1.5s automaticamente, agrupadas por palavra/sessão
- [ ] RecordingList distingue gravações normais de sessões de coleta

## Out of Scope

| Feature | Reason |
|---|---|
| Dropdown de palavras pré-cadastradas na Coleta | Deferred — guide.md §Ideias Adiadas |
| KWS em janelas de 2s | Deferred — guide.md §Ideias Adiadas |
| Corte de amostras via firmware | Deferred — guide.md §Ideias Adiadas |
| Deprecar poc-identificador | Deferred — context.md §Ideias Adiadas |
| Mesclar histórico git dos repos | Deferred — context.md §Ideias Adiadas |
| Testes automatizados | Fora do escopo desta feature |

---

## User Stories

### P1-A: Reestruturação modular do projeto ⭐ MVP

**User Story:** Como desenvolvedor, quero o projeto organizado em `firmware/`, `training/` e `web/` com Makefile raiz, para entender e operar cada módulo de forma independente.

**Why P1:** Prerequisito estrutural para todos os outros stories. Sem isso, o firmware não compila com os novos arquivos KWS.

**Acceptance Criteria:**

1. WHEN o dev executa `idf.py build` na raiz THEN o firmware SHALL compilar sem erros a partir de `firmware/main/`
2. WHEN o dev executa `make web-dev` THEN o servidor de desenvolvimento web SHALL iniciar
3. WHEN o dev executa `make train WORD=teste` THEN o script `training/extract_features.py` SHALL rodar para a palavra `teste`
4. WHEN o dev executa `make train-templates` THEN `training/generate_templates.py` SHALL gerar `firmware/main/templates.h`
5. WHEN o dev executa `make help` THEN o Makefile SHALL listar todos os targets disponíveis com descrição
6. WHEN o dev clona o repo THEN `firmware/main/templates.h` SHALL existir (placeholder do poc-identificador) com comentário indicando que deve ser regerado

**Independent Test:** `idf.py build` na raiz retorna exit 0 após a reestruturação.

---

### P1-B: Pipeline KWS no firmware ⭐ MVP

**User Story:** Como pesquisador, quero que o firmware ESP32 execute o pipeline KWS (MFCC+DTW) e transmita eventos de detecção via WebSocket `/monitor`, para monitorar reconhecimento de palavras em tempo real.

**Why P1:** Base do endpoint que alimenta a aba Monitor.

**Acceptance Criteria:**

1. WHEN o firmware inicia THEN SHALL criar `kws_task` com prioridade 6, stack 8192 bytes
2. WHEN um cliente conecta ao `ws://<IP>/monitor` THEN o firmware SHALL aceitar a conexão e começar a emitir eventos JSON
3. WHEN `kws_task` processa um frame de áudio THEN SHALL emitir JSON com campos `rms` (float), `threshold` (float), `word` (string|null), `dists` ({palavra: float})
4. WHEN `word != null` no payload JSON THEN SHALL significar detecção positiva com distância DTW abaixo do threshold
5. WHEN o cliente conecta ao `ws://<IP>/record` (coleta) THEN `g_kws_paused` SHALL ser `true` e `kws_task` SHALL parar de processar
6. WHEN a conexão `/record` fecha THEN `g_kws_paused` SHALL voltar a `false` e `kws_task` SHALL retomar
7. WHEN `start_webserver()` é chamada THEN `config.recv_wait_timeout` e `config.send_wait_timeout` SHALL ser 120 segundos
8. WHEN o firmware compila THEN `firmware/main/CMakeLists.txt` SHALL incluir `mfcc.c` e `dtw.c` como SRCS

**Independent Test:** Conectar a `ws://<IP>/monitor` com `websocat` e verificar JSON chegando a cada frame.

---

### P1-C: Aba Monitor na UI ⭐ MVP

**User Story:** Como pesquisador, quero uma aba Monitor que exiba um feed rolante de eventos KWS em tempo real, com destaque visual quando uma palavra é detectada, para acompanhar o funcionamento do sistema de reconhecimento.

**Why P1:** Entrega o valor central do Monitor — feedback visual do KWS em tempo real.

**Acceptance Criteria:**

1. WHEN o usuário navega para a aba Monitor THEN a UI SHALL conectar automaticamente a `ws://<IP>/monitor`
2. WHEN o usuário sai da aba Monitor THEN a UI SHALL desconectar o WebSocket
3. WHEN um evento chega com `word == null` THEN SHALL aparecer uma linha no feed com `timestamp`, `rms` e distâncias por palavra
4. WHEN um evento chega com `word != null` THEN a linha SHALL ter destaque visual claro (cor de fundo ou borda colorida) e o nome da palavra em evidência — não apenas texto plano
5. WHEN o feed acumula mais de 200 linhas THEN SHALL descartar as mais antigas (FIFO)
6. WHEN o IP não está preenchido THEN o botão de conectar ao Monitor SHALL estar desabilitado
7. WHEN a conexão WebSocket cai THEN a UI SHALL exibir estado de desconectado sem crash

**Independent Test:** Com firmware rodando, abrir a aba Monitor e ver feed de eventos; falar a palavra treinada e observar linha destacada.

---

### P1-D: Aba Coleta na UI ⭐ MVP

**User Story:** Como pesquisador, quero uma aba Coleta que grave amostras de treinamento segmentadas automaticamente a cada 1.5s por palavra, para construir o dataset de treinamento do KWS sem processamento manual.

**Why P1:** Fecha o loop: Coleta → training → KWS → Monitor.

**Acceptance Criteria:**

1. WHEN o usuário digita uma palavra e clica "Iniciar Coleta" THEN a UI SHALL conectar ao `ws://<IP>/record` e iniciar sessão com `sessionId` UUID único
2. WHEN o botão "Iniciar Coleta" é clicado THEN SHALL estar habilitado apenas se IP estiver preenchido e campo de palavra não estiver vazio
3. WHEN o buffer acumula 24000 amostras (1.5s a 16kHz) THEN o hook `useCollection` SHALL montar WAV com `assemblePcmToWav` e salvar como `Recording` com `collection: { word, sessionId }`
4. WHEN o buffer é montado em WAV THEN SHALL zerar o buffer interno para iniciar a próxima amostra imediatamente
5. WHEN a ESP envia `RECORDING_END:<n>` THEN se o buffer tiver ≥ 8000 amostras (0.5s) SHALL salvar fragmento final; abaixo disso SHALL descartar silenciosamente
6. WHEN uma amostra é salva THEN SHALL aparecer na lista da sessão atual com nome `<palavra>_<NNN>.wav` e duração em segundos
7. WHEN o usuário clica "Encerrar Coleta" THEN a UI SHALL fechar o WebSocket
8. WHEN o WebSocket fecha THEN o firmware SHALL resetar estado para APP_IDLE (comportamento já existente)
9. WHEN a coleta está ativa THEN a UI SHALL exibir indicador claro de que "cada amostra é cortada automaticamente a cada 1.5s"
10. WHEN `Recording` tem campo `collection` THEN `types.ts` SHALL definir o campo como `collection?: { word: string; sessionId: string }`

**Independent Test:** Iniciar coleta para palavra "teste", pressionar botão na ESP, aguardar 5s, encerrar — verificar que 3 gravações aparecem no IndexedDB com campo `collection`.

---

### P2-A: RecordingList com sessões colapsáveis

**User Story:** Como pesquisador, quero que as gravações de coleta apareçam agrupadas por sessão na lista, para distinguir amostras de treinamento de gravações normais.

**Why P2:** Melhora organização mas a Coleta funciona sem isso (gravações aparecem individualmente).

**Acceptance Criteria:**

1. WHEN há gravações com `collection` do mesmo `sessionId` THEN RecordingList SHALL exibi-las agrupadas sob um item expansível `▶ <palavra> — N amostras — <data>`
2. WHEN o grupo está expandido THEN SHALL mostrar cada amostra individual com ações de play, download e delete
3. WHEN há gravações sem `collection` THEN SHALL continuar aparecendo individualmente em ordem cronológica reversa
4. WHEN grupos e individuais são exibidos juntos THEN ordenação SHALL ser pelo timestamp mais recente (da última amostra do grupo ou da gravação individual)

**Independent Test:** Criar 3 coletas de "teste" e 1 gravação normal; verificar que a lista mostra grupo colapsável + gravação individual intercalados por data.

---

### P3-A: Pipeline end-to-end via Makefile

**User Story:** Como desenvolvedor, quero rodar `make pipeline WORD=ligar` para executar todo o fluxo de treinamento até flash do firmware, para iterar rapidamente em novas palavras.

**Why P3:** Conveniência — cada step já é executável individualmente pelo P1-A.

**Acceptance Criteria:**

1. WHEN `make pipeline WORD=<palavra>` é executado THEN SHALL rodar em sequência: `extract_features.py`, `generate_templates.py`, `idf.py build`
2. WHEN `make flash WORD=<palavra> PORT=<porta>` é executado THEN SHALL rodar: pipeline completo + `idf.py -p <porta> flash`
3. WHEN `WORD` não é fornecido THEN Makefile SHALL exibir mensagem de erro e abortar
4. WHEN `PORT` não é fornecido em `make flash` THEN Makefile SHALL usar `/dev/ttyUSB0` como default

**Independent Test:** Executar `make pipeline WORD=teste` em ambiente com Python e IDF configurados; verificar que `firmware/main/templates.h` é atualizado e build completa.

---

## Edge Cases

- WHEN o WebSocket `/monitor` cai durante feed ativo THEN MonitorTab SHALL exibir estado desconectado sem acumular listeners órfãos
- WHEN o usuário troca de aba durante coleta ativa THEN `useCollection` SHALL manter WebSocket aberto e sessão ativa (aba pode ser retomada)
- WHEN o browser fecha durante coleta THEN WebSocket fecha → firmware reseta para APP_IDLE
- WHEN `RECORDING_END` chega com buffer < 8000 amostras THEN fragmento final é descartado sem erro visível ao usuário
- WHEN `firmware/main/templates.h` é placeholder THEN KWS roda mas detecções são para a palavra "teste" do poc-identificador

---

## Requirement Traceability

| ID | Story | Fase | Status |
|---|---|---|---|
| CMI-01 | P1-A: Reestruturação `firmware/` | Design | Pending |
| CMI-02 | P1-A: Reestruturação `training/` | Design | Pending |
| CMI-03 | P1-A: CMakeLists.txt raiz | Design | Pending |
| CMI-04 | P1-A: Makefile raiz | Design | Pending |
| CMI-05 | P1-A: README raiz | Design | Pending |
| CMI-06 | P1-B: mfcc.c/h + dtw.c/h no firmware | Design | Pending |
| CMI-07 | P1-B: kws_task + /monitor endpoint | Design | Pending |
| CMI-08 | P1-B: g_kws_paused + HTTP timeouts | Design | Pending |
| CMI-09 | P1-C: MonitorTab component | Design | Pending |
| CMI-10 | P1-C: WebSocket /monitor no frontend | Design | Pending |
| CMI-11 | P1-D: types.ts collection field | Design | Pending |
| CMI-12 | P1-D: useCollection hook | Design | Pending |
| CMI-13 | P1-D: CollectionTab component | Design | Pending |
| CMI-14 | P2-A: RecordingList grupos colapsáveis | Design | Pending |
| CMI-15 | P3-A: Makefile pipeline + flash | Design | Pending |

---

## Success Criteria

- [ ] `idf.py build` na raiz compila sem warnings adicionais
- [ ] `ws://<IP>/monitor` emite JSON válido após boot do firmware
- [ ] Aba Monitor exibe feed e destaca detecções visualmente
- [ ] Coleta de 5 amostras de "teste" gera 5 arquivos WAV de ~1.5s no IndexedDB
- [ ] `make train-templates` gera `firmware/main/templates.h` válido
