# Audio Visualization Specification

## Problem Statement

A UI de gravação não fornece feedback visual sobre o áudio capturado. O botão de play não reseta após o áudio terminar, e não há forma de inspecionar o conteúdo de uma gravação ou monitorar o stream ao vivo visualmente. Isso dificulta debugar a qualidade do microfone e entender o que foi capturado.

## Proposed Solution

Três melhorias complementares: (1) fix do ciclo de vida do player, (2) visualizador expansível por gravação com forma de onda, marcadores de chunk e espectro FFT, (3) visualizador em tempo real no painel de stream ao vivo.

## Goals

- [ ] Botão play sempre reflete o estado real de reprodução
- [ ] Usuário pode inspecionar visualmente qualquer gravação salva
- [ ] Usuário pode monitorar o áudio do stream ao vivo em tempo real
- [ ] Visualização de chunks deixa claro como o buffer foi construído

## Out of Scope

| Feature | Reason |
|---------|--------|
| Edição/corte de áudio | Fora do escopo de inspeção |
| Exportar imagem do gráfico | Complexidade sem demanda identificada |
| Espectrograma (tempo × frequência × amplitude) | Complexidade elevada, deferred |
| Comparação entre gravações | Deferred |

---

## User Stories

### P1: Fix — Botão Play Reseta ao Fim do Áudio ⭐ MVP

**User Story**: Como usuário, quero que o botão de play volte ao estado normal após o áudio terminar naturalmente, para saber quando a reprodução acabou sem precisar verificar manualmente.

**Why P1**: Bug existente que quebra a experiência básica de reprodução.

**Acceptance Criteria**:

1. WHEN o áudio termina naturalmente (sem intervenção do usuário) THEN o sistema SHALL chamar `stop()` e zerar `currentId`
2. WHEN `currentId` é zerado THEN o `RecordingItem` SHALL exibir `▶` (não `⏸`)
3. WHEN o usuário clica `▶` em outra gravação enquanto uma toca THEN o sistema SHALL parar a anterior e iniciar a nova

**Independent Test**: Reproduzir uma gravação curta e aguardar o fim — o botão deve voltar a `▶` sem interação.

---

### P1: Visualizador de Gravação — Forma de Onda com Chunks ⭐ MVP

**User Story**: Como usuário, quero expandir uma gravação para ver a forma de onda do áudio capturado, com os chunks WebSocket marcados, para entender o que foi gravado e como o buffer foi construído.

**Why P1**: Funcionalidade central do feature; sem isso não há visualizador.

**Acceptance Criteria**:

1. WHEN o usuário clica no botão 📊 em um `RecordingItem` THEN o sistema SHALL expandir um painel abaixo da linha com canvas de altura ~120px
2. WHEN o painel expande THEN o sistema SHALL decodificar o WAV do blob via `AudioContext.decodeAudioData` e renderizar a forma de onda PCM no canvas
3. WHEN a forma de onda é renderizada THEN o sistema SHALL desenhar linhas verticais finas separando cada chunk (baseado no tamanho de chunk `I2S_READ_CHUNK = 512` amostras)
4. WHEN o usuário passa o mouse sobre o gráfico THEN o sistema SHALL exibir tooltip com "Chunk #N — X amostras" correspondente à posição
5. WHEN o usuário clica no botão 📊 novamente THEN o sistema SHALL recolher o painel
6. WHEN apenas um painel está expandido por vez THEN o sistema SHALL recolher qualquer outro aberto ao abrir um novo (comportamento acordeão)

**Independent Test**: Abrir qualquer gravação, clicar 📊 — painel aparece com forma de onda e marcadores verticais visíveis.

---

### P1: Toggle de Modos de Visualização ⭐ MVP

**User Story**: Como usuário, quero alternar entre forma de onda, espectro FFT e ambos, para inspecionar o áudio sob diferentes perspectivas.

**Why P1**: Parte do MVP do visualizador; decidido na entrevista como requisito base.

**Acceptance Criteria**:

1. WHEN o painel de visualização está aberto THEN o sistema SHALL exibir pill buttons no cabeçalho: **Onda | FFT | Ambos**
2. WHEN o usuário clica **Onda** THEN o sistema SHALL exibir apenas a forma de onda em altura total (~120px)
3. WHEN o usuário clica **FFT** THEN o sistema SHALL exibir apenas o espectro de frequência (barras) em altura total (~120px); FFT SHALL ser pré-computado do WAV via `AnalyserNode` ou FFT manual
4. WHEN o usuário clica **Ambos** THEN o sistema SHALL exibir forma de onda na metade superior (~60px) e FFT na metade inferior (~60px)
5. WHEN o modo muda THEN o canvas SHALL re-renderizar sem fechar/abrir o painel
6. WHEN o painel abre pela primeira vez THEN o modo padrão SHALL ser **Onda**

**Independent Test**: Abrir painel, clicar nos três modos — cada um renderiza corretamente sem erro no console.

---

### P2: Cursor de Progresso e Seek na Forma de Onda

**User Story**: Como usuário, quero ver um cursor avançando na forma de onda durante a reprodução e poder clicar para seekar, para ter controle visual da posição de reprodução.

**Why P2**: Enriquece a experiência de playback mas não é necessário para o visualizador funcionar.

**Acceptance Criteria**:

1. WHEN o usuário clica play em uma gravação com o painel expandido THEN o sistema SHALL desenhar uma linha vertical sobre a forma de onda que avança via `requestAnimationFrame` proporcional a `audio.currentTime / audio.duration`
2. WHEN o áudio para ou termina THEN o cursor SHALL desaparecer
3. WHEN o painel está no modo **FFT** THEN o cursor NOT SHALL aparecer (FFT é estático para gravações)
4. WHEN o painel está no modo **Ambos** THEN o cursor SHALL aparecer apenas na metade da forma de onda
5. WHEN o usuário clica em qualquer ponto da forma de onda THEN o sistema SHALL seekar `audio.currentTime` proporcionalmente à posição X clicada

**Independent Test**: Reproduzir gravação com painel aberto — cursor avança. Clicar no meio do gráfico — reprodução pula para o meio.

---

### P2: Visualizador do Stream ao Vivo em Tempo Real

**User Story**: Como usuário, quero ver a forma de onda e/ou FFT animados em tempo real enquanto o stream está ativo, para monitorar o que o microfone está captando.

**Why P2**: Dependente da infraestrutura do visualizador (P1); requer Web Audio API com `AnalyserNode`.

**Acceptance Criteria**:

1. WHEN `streaming === true` THEN o `LiveTranscriptPanel` SHALL exibir um canvas de visualização acima do texto de transcrição
2. WHEN o canvas é exibido THEN o sistema SHALL conectar os chunks PCM WebSocket a um `AudioContext` + `AnalyserNode` para alimentar a visualização em tempo real
3. WHEN o stream está ativo THEN o canvas SHALL animar continuamente via `requestAnimationFrame`
4. WHEN `streaming === false` THEN o canvas SHALL ser removido/ocultado junto com o painel
5. WHEN o usuário alterna modos (Onda / FFT / Ambos) no stream THEN o canvas SHALL re-renderizar imediatamente com o novo modo
6. WHEN o modo é **Onda** THEN o sistema SHALL exibir o buffer de tempo do `AnalyserNode` (forma de onda deslizante)
7. WHEN o modo é **FFT** THEN o sistema SHALL exibir as barras de frequência do `AnalyserNode`

**Independent Test**: Iniciar stream, falar — canvas anima em resposta ao áudio. Alternar modos — visualização muda.

---

## Edge Cases

- WHEN o blob da gravação é inválido ou corrompido THEN o sistema SHALL exibir mensagem "Não foi possível carregar o áudio" no painel em vez de travar
- WHEN a gravação tem duração zero THEN o sistema SHALL exibir canvas vazio sem erro
- WHEN o `AudioContext` não é suportado pelo browser THEN o sistema SHALL ocultar o botão 📊 e exibir mensagem de incompatibilidade
- WHEN o painel está no modo **Ambos** e a altura disponível é muito pequena THEN o sistema SHALL garantir mínimo de 40px por seção
- WHEN o stream é interrompido abruptamente (WS fecha) THEN o sistema SHALL parar a animação sem deixar loop órfão de `requestAnimationFrame`

---

## Requirement Traceability

| Requirement ID | Story | Status |
|----------------|-------|--------|
| AVIZ-01 | P1: Fix play button | Pending |
| AVIZ-02 | P1: Waveform + chunks | Pending |
| AVIZ-03 | P1: Toggle modos | Pending |
| AVIZ-04 | P2: Cursor + seek | Pending |
| AVIZ-05 | P2: Stream visualizer | Pending |

---

## Success Criteria

- [ ] AVIZ-01: Botão play reseta em 100% dos casos após reprodução natural
- [ ] AVIZ-02: Forma de onda renderiza com marcadores de chunk para qualquer gravação
- [ ] AVIZ-03: Toggle entre 3 modos funciona sem erros de console
- [ ] AVIZ-04: Cursor de progresso avança sincronizado com o áudio
- [ ] AVIZ-05: Stream ao vivo anima em tempo real sem memory leak (sem `requestAnimationFrame` órfão)
