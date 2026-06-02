# ESP32 Audio Recorder — Especificação

## Problem Statement

O firmware da ESP32 já captura áudio via INMP441 e expõe um WebSocket, mas não existe aplicação que receba, persista e permita reproduzir as gravações. Sem uma camada de cliente, o áudio capturado é descartado após o envio — inviabilizando o objetivo central da POC: demonstrar o fluxo completo de áudio de hardware embarcado até o browser.

## Proposed Solution

Uma aplicação web (React + TypeScript + Vite) que conecta diretamente ao WebSocket da ESP32, recebe gravações WAV disparadas pelo botão físico, armazena os arquivos em IndexedDB e exibe uma interface de gravador com lista de reprodução.

O usuário pressiona o botão físico na ESP32 → a gravação acontece no dispositivo → ao soltar, o WAV é enviado via WebSocket → o browser recebe, persiste e disponibiliza para reprodução imediata.

## Goals

- [ ] Demonstrar recepção de áudio WAV de ESP32 via WebSocket no browser
- [ ] Persistir gravações localmente em IndexedDB sem perda entre recargas
- [ ] Reproduzir qualquer gravação armazenada diretamente no browser
- [ ] Manter estrutura de monorepo (firmware + web app no mesmo repositório)

## Out of Scope

| Feature | Razão |
|---|---|
| Backend server intermediário | Decisão de arquitetura: ESP32 como servidor direto |
| Streaming PCM em tempo real (`/stream`) | Fora da decisão de timing; aumenta complexidade sem valor para POC |
| Trigger de gravação pelo browser | Fora do escopo; botão físico é o único gatilho |
| Upload para cloud / API externa | Fora do escopo da POC |
| ESP32 servir a própria página HTML | Adiado; aumenta acoplamento firmware/frontend |
| File System Access API (salvar em disco) | Adiado; IndexedDB atende ao escopo |
| Waveform visualization | P3 adiado; não crítico para demonstrar o fluxo |

---

## User Stories

### P1: Conectar ao WebSocket da ESP32 ⭐ MVP

**User Story**: Como desenvolvedor, quero informar o IP da ESP32 e conectar ao WebSocket `/record` para que a aplicação comece a receber gravações.

**Why P1**: Sem conexão, nenhuma outra funcionalidade existe.

**Acceptance Criteria**:

1. WHEN o usuário informa um IP e clica em "Conectar" THEN o sistema SHALL estabelecer conexão WebSocket com `ws://<IP>/record`
2. WHEN a conexão for estabelecida com sucesso THEN o sistema SHALL exibir status "Conectado" e o IP ativo
3. WHEN a conexão falhar ou for recusada THEN o sistema SHALL exibir mensagem de erro com o motivo
4. WHEN a conexão cair inesperadamente THEN o sistema SHALL exibir status "Desconectado" e oferecer reconexão manual
5. WHEN o usuário clicar em "Desconectar" THEN o sistema SHALL fechar o WebSocket e volcar ao estado inicial

**Independent Test**: Informar IP válido de ESP32 rodando firmware → status muda para "Conectado". Informar IP inválido → mensagem de erro aparece.

---

### P1: Receber e armazenar gravação ⭐ MVP

**User Story**: Como desenvolvedor, quero que ao soltar o botão físico da ESP32 a gravação WAV seja automaticamente recebida e salva, para que eu possa acessá-la depois.

**Why P1**: É o fluxo central da POC — captura → transferência → persistência.

**Acceptance Criteria**:

1. WHEN o botão da ESP32 for pressionado THEN o sistema SHALL exibir indicador visual de "Gravando..."
2. WHEN o botão for solto THEN o sistema SHALL exibir "Recebendo áudio..."
3. WHEN o WebSocket receber o frame do header WAV (44 bytes) THEN o sistema SHALL iniciar acumulação dos dados
4. WHEN todos os chunks de dados forem recebidos e o WebSocket ficar silencioso por 500ms THEN o sistema SHALL montar o Blob WAV completo e salvar no IndexedDB
5. WHEN a gravação for salva com sucesso THEN o sistema SHALL exibir a nova entrada na lista imediatamente
6. WHEN o IndexedDB não estiver disponível THEN o sistema SHALL exibir erro "Armazenamento indisponível" e não perder os dados recebidos (manter Blob em memória)

**Nota de implementação**: O firmware envia header WAV (44 bytes) + chunks de dados em frames binários separados via WebSocket. O cliente deve acumular todos os frames e montar o Blob final. O fim da transmissão é inferido por ausência de novos frames (timeout de 500ms após último chunk).

**Independent Test**: Pressionar e soltar botão físico → nova entrada aparece na lista com tamanho ~500KB (gravação de 16s) ou proporcional à duração.

---

### P1: Listar gravações armazenadas ⭐ MVP

**User Story**: Como desenvolvedor, quero ver todas as gravações salvas com data, hora e tamanho, para que eu possa identificar e acessar cada uma.

**Why P1**: Sem lista, o storage é uma caixa preta.

**Acceptance Criteria**:

1. WHEN a aplicação carrega THEN o sistema SHALL buscar e exibir todas as gravações do IndexedDB
2. WHEN não houver gravações THEN o sistema SHALL exibir mensagem "Nenhuma gravação ainda"
3. WHEN uma nova gravação for salva THEN o sistema SHALL adicionar à lista sem recarregar a página
4. WHEN uma gravação é listada THEN o sistema SHALL exibir: data/hora da gravação, tamanho em KB, e nome gerado automaticamente (`rec-YYYY-MM-DD-HHmmss`)

**Independent Test**: Recarregar a página após salvar gravações → lista persiste com todas as entradas anteriores.

---

### P1: Reproduzir gravação ⭐ MVP

**User Story**: Como desenvolvedor, quero clicar em uma gravação e ouvi-la no browser, para que eu possa confirmar que o áudio capturado pela ESP32 tem qualidade adequada.

**Why P1**: Reprodução é a prova final de que o pipeline funciona end-to-end.

**Acceptance Criteria**:

1. WHEN o usuário clicar em uma gravação da lista THEN o sistema SHALL criar um `Object URL` do Blob e reproduzir via elemento `<audio>` nativo do browser
2. WHEN o áudio estiver reproduzindo THEN o sistema SHALL exibir controles nativos (play/pause, timeline, volume)
3. WHEN o usuário clicar em outra gravação THEN o sistema SHALL parar a reprodução atual e iniciar a nova
4. WHEN o `Object URL` não for mais necessário THEN o sistema SHALL revogar via `URL.revokeObjectURL` para liberar memória

**Independent Test**: Clicar em uma gravação → áudio WAV 16kHz mono reproduz corretamente no browser sem distorção.

---

### P2: Excluir gravação

**User Story**: Como desenvolvedor, quero excluir gravações individuais, para que eu possa gerenciar o espaço de armazenamento local.

**Why P2**: Útil para manter a lista limpa durante testes repetidos, mas não bloqueia demonstrar o fluxo.

**Acceptance Criteria**:

1. WHEN o usuário clicar em "Excluir" em uma gravação THEN o sistema SHALL remover do IndexedDB e da lista
2. WHEN a gravação excluída estiver em reprodução THEN o sistema SHALL parar o áudio antes de excluir
3. WHEN a exclusão falhar THEN o sistema SHALL exibir mensagem de erro e manter a entrada na lista

**Independent Test**: Excluir uma gravação → some da lista imediatamente. Recarregar → não retorna.

---

### P2: Indicador de status da conexão e gravação

**User Story**: Como desenvolvedor, quero feedback visual claro do estado atual do sistema, para que eu saiba quando está pronto para gravar, gravando, ou com problema.

**Why P2**: Sem feedback, o usuário não sabe se o botão funcionou.

**Acceptance Criteria**:

1. WHEN desconectado THEN o sistema SHALL exibir badge cinza "Desconectado"
2. WHEN conectado e aguardando THEN o sistema SHALL exibir badge verde "Pronto"
3. WHEN botão da ESP32 pressionado (gravando) THEN o sistema SHALL exibir badge vermelho pulsante "Gravando..."
4. WHEN recebendo chunks WAV THEN o sistema SHALL exibir badge amarelo "Recebendo..."
5. WHEN o último estado mudar THEN o sistema SHALL fazer transição de badge sem flash/flicker

**Independent Test**: Pressionar e soltar botão durante conexão ativa → sequência de badges: Verde → Vermelho → Amarelo → Verde.

---

### P3: Download da gravação como arquivo WAV

**User Story**: Como desenvolvedor, quero baixar uma gravação como arquivo `.wav`, para que eu possa processá-la em outras ferramentas (Audacity, scripts Python, etc.).

**Why P3**: Útil para demonstrar interoperabilidade, mas reprodução no browser já prova o pipeline.

**Acceptance Criteria**:

1. WHEN o usuário clicar em "Download" em uma gravação THEN o sistema SHALL criar um link `<a>` com `download="<nome>.wav"` e disparar o clique programaticamente
2. WHEN o download for iniciado THEN o arquivo SHALL ter extensão `.wav` e ser um WAV PCM 16-bit válido

**Independent Test**: Baixar gravação → abrir no Audacity → áudio correto sem corrupção.

---

## Edge Cases

- WHEN a ESP32 enviar header WAV com `data_size` inconsistente com chunks recebidos THEN o sistema SHALL usar o tamanho real dos dados recebidos
- WHEN a conexão WebSocket cair durante o recebimento de chunks THEN o sistema SHALL descartar o Blob parcial e exibir "Gravação incompleta — reconecte e tente novamente"
- WHEN o IndexedDB atingir limite de storage THEN o sistema SHALL exibir aviso e listar as gravações mais antigas para exclusão manual
- WHEN o browser não suportar IndexedDB THEN o sistema SHALL exibir erro de compatibilidade no carregamento
- WHEN o buffer de gravação da ESP32 estourar (16s máximo) THEN a gravação para automaticamente — comportamento esperado, nenhum tratamento adicional necessário no cliente

---

## Requirement Traceability

| Requirement ID | Story | Fase | Status |
|---|---|---|---|
| EAR-01 | P1: Conectar ao WebSocket | Design | Pending |
| EAR-02 | P1: Conectar ao WebSocket | Design | Pending |
| EAR-03 | P1: Conectar ao WebSocket | Design | Pending |
| EAR-04 | P1: Receber e armazenar | Design | Pending |
| EAR-05 | P1: Receber e armazenar | Design | Pending |
| EAR-06 | P1: Receber e armazenar | Design | Pending |
| EAR-07 | P1: Listar gravações | Design | Pending |
| EAR-08 | P1: Listar gravações | Design | Pending |
| EAR-09 | P1: Reproduzir gravação | Design | Pending |
| EAR-10 | P1: Reproduzir gravação | Design | Pending |
| EAR-11 | P2: Excluir gravação | Design | Pending |
| EAR-12 | P2: Status de conexão | Design | Pending |
| EAR-13 | P3: Download WAV | Design | Pending |

**Cobertura:** 13 requisitos, 0 mapeados para tasks, 13 pendentes ⚠️

---

## Success Criteria

- [ ] Pressionar e soltar o botão da ESP32 resulta em nova entrada na lista do browser dentro de 3 segundos após soltar
- [ ] Gravações persistem após recarregar a página (IndexedDB)
- [ ] Áudio reproduz corretamente no browser (16kHz, mono, sem distorção)
- [ ] A aplicação funciona em Chrome e Firefox modernos
- [ ] Projeto está estruturado como monorepo (`/firmware` + `/web`) com README explicando como rodar cada parte

---

## Questões Abertas (do Interview)

- **TODO**: Verificar versão do ESP-IDF no ambiente antes de modificar firmware — `i2s_std` API requer ≥ 5.0
- **TODO**: Confirmar se PSRAM está disponível no módulo ESP32 usado — buffer de 512KB pode falhar sem ela (ver `.specs/codebase/CONCERNS.md` M2)
