# RGB LED + KWS — Especificação

## Problem Statement

O firmware atual detecta a palavra "ligar" mas não executa nenhuma ação física além de piscar o LED onboard. O usuário quer controlar um LED RGB externo por voz: falar "ligar" seguido de uma cor deve acender o LED naquela cor; falar "desligar" deve apagá-lo. O firmware também está em um único arquivo monolítico (~850 linhas), dificultando manutenção e adição de novas funcionalidades.

## Proposed Solution

Adicionar controle PWM de LED RGB via LEDC do ESP-IDF, expandir o vocabulário KWS com 9 cores + "desligar", implementar máquina de estados de 2 estágios no KWS (detecta "ligar" → entra em modo de escuta de cor por 2s), e reorganizar o firmware em módulos separados por responsabilidade (`tasks/`, `drivers/`, `kws/`).

## Goals

- [ ] Falar "ligar azul" → LED RGB acende azul em até 3s após o fim da fala
- [ ] Falar "desligar" → LED apaga
- [ ] Firmware reorganizado em arquivos separados, compilando sem erros com `idf.py build`
- [ ] Novas palavras treinadas com mínimo 3 templates cada, taxa de acerto ≥ 80% em testes manuais

## Out of Scope

| Feature | Razão |
|---------|-------|
| Fade/transição entre cores | Adiado — decisão da entrevista |
| Controle de intensidade por voz ("azul fraco") | Adiado — decisão da entrevista |
| Remoção do Wi-Fi/WebSocket | Fora do escopo desta feature |
| Interface web para controlar LED | Não solicitado |

---

## User Stories

### P1: Driver LEDC para LED RGB ⭐ MVP

**User Story**: Como desenvolvedor, quero um driver LEDC encapsulado em `drivers/ledc_driver.c` para poder controlar o LED RGB com PWM sem duplicar configuração.

**Por que P1**: Base de hardware para tudo mais. Sem isso, nenhuma outra story pode ser testada.

**Acceptance Criteria**:

1. WHEN `ledc_driver_init()` é chamado no boot THEN o sistema SHALL configurar LEDC com resolução 8 bits, frequência 5 kHz, nos canais GPIO 18 (R), 19 (G), 21 (B)
2. WHEN `ledc_set_color(r, g, b)` é chamado com valores 0–255 THEN o sistema SHALL aplicar o duty cycle correspondente em cada canal sem inversão de sinal
3. WHEN `ledc_set_color(0, 0, 0)` é chamado THEN todos os canais SHALL ter duty 0 (LED apagado)
4. WHEN `ledc_set_color(255, 255, 255)` é chamado THEN todos os canais SHALL ter duty 255 (branco máximo)

**Independent Test**: Chamar `ledc_set_color` com vermelho puro (255,0,0) após o boot e verificar visualmente que apenas o canal R acende.

---

### P1: Task de LED com fila de comandos ⭐ MVP

**User Story**: Como firmware, quero uma task dedicada ao LED que receba comandos via queue para poder atualizar o LED sem acoplar KWS ao driver diretamente.

**Por que P1**: Desacopla o KWS do hardware; a task de LED pode ser testada independentemente.

**Acceptance Criteria**:

1. WHEN `led_task` é criada THEN o sistema SHALL criar uma `QueueHandle_t` para `led_command_t` com capacidade mínima de 4 comandos
2. WHEN um `led_command_t` com `{r, g, b}` é enviado à queue THEN `led_task` SHALL chamar `ledc_set_color(r, g, b)` em até 50ms
3. WHEN a queue está vazia THEN `led_task` SHALL bloquear sem consumir CPU (`xQueueReceive` com `portMAX_DELAY`)
4. WHEN um novo comando chega com LED já aceso THEN o sistema SHALL substituir a cor imediatamente sem efeito de transição

**Independent Test**: Enviar comandos de cor direto para a queue (sem KWS) e observar o LED trocar de cor corretamente.

---

### P1: Máquina de estados KWS 2 estágios ⭐ MVP

**User Story**: Como usuário, quero falar "ligar" seguido do nome de uma cor para acender o LED naquela cor.

**Por que P1**: É o fluxo principal da feature.

**Acceptance Criteria**:

1. WHEN KWS detecta "ligar" no estado `KWS_IDLE` THEN o sistema SHALL transicionar para `KWS_AWAIT_COLOR` e iniciar timer de 2s
2. WHEN KWS está em `KWS_AWAIT_COLOR` e detecta uma cor válida THEN o sistema SHALL enviar `led_command_t` com os valores RGB da cor à queue do LED e retornar a `KWS_IDLE`
3. WHEN KWS está em `KWS_AWAIT_COLOR` e o timer de 2s expira sem detecção de cor THEN o sistema SHALL retornar a `KWS_IDLE` sem alterar o estado do LED
4. WHEN KWS detecta "ligar" em `KWS_AWAIT_COLOR` THEN o sistema SHALL reiniciar o timer de 2s (novo "ligar" = nova tentativa)
5. WHEN KWS está em qualquer estado e detecta "desligar" THEN o sistema SHALL enviar `led_command_t {0,0,0}` e retornar a `KWS_IDLE`

**Independent Test**: Falar "ligar azul" e verificar que o LED acende azul. Falar "ligar" e aguardar 3s sem dizer cor — LED não deve mudar.

---

### P1: Vocabulário de cores treinado ⭐ MVP

**User Story**: Como usuário, quero que o sistema reconheça os nomes das 9 cores para poder acionar o LED por voz.

**Por que P1**: Sem templates, a máquina de estados não detecta nenhuma cor.

**Acceptance Criteria**:

1. WHEN o firmware é compilado THEN `templates.h` SHALL conter templates para: vermelho, verde, azul, amarelo, ciano, magenta, laranja, roxo, branco, desligar
2. WHEN cada nova palavra é gravada THEN SHALL ter mínimo 3 templates (recomendado 5)
3. WHEN o sistema compara uma palavra de cor em `KWS_AWAIT_COLOR` THEN SHALL comparar contra templates de cores apenas (não contra "ligar" nem "garbage")
4. WHEN o sistema está em `KWS_IDLE` THEN SHALL comparar apenas contra "ligar", "desligar" e "garbage"

**Independent Test**: Falar cada cor em sequência após "ligar" e verificar que o LED acende na cor correta em ≥ 8 de 10 tentativas.

---

### P1: Reorganização arquitetural do firmware ⭐ MVP

**User Story**: Como desenvolvedor, quero o firmware organizado em módulos separados por responsabilidade para poder navegar e manter o código com facilidade.

**Por que P1**: Pré-requisito para implementar as demais stories sem criar débito técnico.

**Acceptance Criteria**:

1. WHEN `idf.py build` é executado THEN o projeto SHALL compilar sem erros ou warnings novos
2. WHEN a estrutura é criada THEN SHALL existir os diretórios `firmware/main/tasks/`, `firmware/main/drivers/`, `firmware/main/kws/`
3. WHEN `app_main.c` é lido THEN SHALL conter apenas inicialização (NVS, GPIO, I2S, Wi-Fi, HTTP) e `xTaskCreate` — sem lógica de negócio inline
4. WHEN `app_state.h` é lido THEN SHALL declarar todas as variáveis globais compartilhadas, handles de queue e mutex
5. WHEN cada task (`audio_task`, `kws_task`, `button_task`, `led_task`) é lida THEN SHALL estar em arquivo próprio em `tasks/` com header correspondente
6. WHEN `CMakeLists.txt` é lido THEN SHALL listar todos os `.c` dos subdiretórios em `SRCS`

**Independent Test**: `idf.py build && idf.py flash` seguido de teste funcional completo — gravar áudio, stream, KWS e LED devem funcionar como antes da refatoração.

---

### P2: Catálogo de cores com valores RGB

**User Story**: Como firmware, quero um catálogo centralizado de cores com valores RGB para não duplicar os valores entre templates e código de acionamento.

**Por que P2**: Importante para consistência mas não bloqueia MVP — pode-se hardcodar inicialmente.

**Acceptance Criteria**:

1. WHEN uma cor é detectada pelo KWS THEN o sistema SHALL buscar seus valores RGB em uma tabela estática `color_catalog[]` em vez de switch/case inline
2. WHEN uma cor é adicionada ao vocabulário THEN SHALL ser adicionada também ao catálogo com os mesmos valores do protótipo Arduino

**Independent Test**: Modificar o valor de uma cor no catálogo e verificar que o LED reflete a mudança sem alterar o código de detecção.

---

### P2: Monitor WebSocket reporta estado KWS e LED

**User Story**: Como desenvolvedor, quero que o WebSocket `/monitor` inclua o estado atual do LED e o modo KWS para poder depurar remotamente.

**Por que P2**: Facilita debugging mas não é necessário para funcionamento.

**Acceptance Criteria**:

1. WHEN o monitor envia JSON de heartbeat THEN SHALL incluir campos `"kws_mode": "idle" | "await_color"` e `"led": {"r": N, "g": N, "b": N}`
2. WHEN KWS transiciona para `KWS_AWAIT_COLOR` THEN o próximo frame do monitor SHALL refletir o novo estado

**Independent Test**: Abrir a aba Monitor no app web, falar "ligar" e observar `kws_mode` mudar para `"await_color"` no JSON.

---

## Edge Cases

- WHEN LED está aceso em verde e usuário fala "ligar vermelho" THEN o sistema SHALL substituir para vermelho imediatamente
- WHEN usuário fala rápido demais e "ligar" e "azul" caem na mesma janela de 0,5s THEN o sistema SHALL detectar "ligar" no primeiro evento e "azul" no segundo (janelas separadas pelo VAD)
- WHEN Wi-Fi cai durante operação THEN o LED SHALL permanecer no estado atual (LED é independente de conectividade)
- WHEN o botão é pressionado durante `KWS_AWAIT_COLOR` THEN o sistema SHALL iniciar gravação/streaming normalmente e cancelar o modo de escuta de cor

---

## Requirement Traceability

| ID       | Story                            | Status  |
|----------|----------------------------------|---------|
| LED-01   | P1: Driver LEDC — init           | Pending |
| LED-02   | P1: Driver LEDC — set_color      | Pending |
| LED-03   | P1: Task LED — queue             | Pending |
| LED-04   | P1: Task LED — substituição      | Pending |
| KWS-01   | P1: KWS 2 estágios — IDLE→AWAIT  | Pending |
| KWS-02   | P1: KWS 2 estágios — cor→IDLE    | Pending |
| KWS-03   | P1: KWS 2 estágios — timeout     | Pending |
| KWS-04   | P1: KWS 2 estágios — desligar    | Pending |
| KWS-05   | P1: Vocabulário — templates      | Pending |
| KWS-06   | P1: Vocabulário — escopo por modo| Pending |
| ARCH-01  | P1: Arquitetura — build limpo    | Pending |
| ARCH-02  | P1: Arquitetura — estrutura dirs | Pending |
| ARCH-03  | P1: Arquitetura — app_main slim  | Pending |
| ARCH-04  | P1: Arquitetura — app_state.h    | Pending |
| ARCH-05  | P1: Arquitetura — tasks separadas| Pending |
| CAT-01   | P2: Catálogo de cores            | Pending |
| MON-01   | P2: Monitor — kws_mode + led     | Pending |

**Cobertura:** 17 requisitos, 0 mapeados para tasks ⚠️

---

## Success Criteria

- [ ] `idf.py build` passa sem erros após refatoração
- [ ] "ligar" + cor acende LED na cor correta em ≥ 8/10 tentativas por cor
- [ ] "desligar" apaga LED em ≥ 9/10 tentativas
- [ ] Timeout de 2s funciona: falar "ligar" sem cor não altera LED
- [ ] Gravação/stream via botão não são afetados pela adição do LED
