# RGB LED + KWS — Tasks

**Design**: `.specs/features/rgb-led-kws/design.md`
**Status**: Draft

---

## Execution Plan

### Phase 1: Refatoração Estrutural (Sequential)
```
T1 → T2 → [T3, T4, T5, T6, T7 P] → T8 → T9
```

### Phase 2: LEDC + Color Data + Treinamento (Parallel após T9)
```
         ┌─→ T10 → T11 ──────────────────────┐
         │                                    │
T9 ──────┼─→ T12 → T13 → T14 → T15b ─────────┼──→ T16 → T17
         │                                    │
         └─→ T15a (gravar amostras) ──────────┘
              (pode iniciar junto com T10/T12)
```

> **T15 dividida em duas:** T15a = gravar + extrair features (`make train WORD=X`) — não depende de T13/T14, pode rodar imediatamente após T9. T15b = gerar templates.h (`make train-templates`) — depende de T14 (script atualizado).

### Phase 3: KWS State Machine (Sequential após T11 + T15b)
```
T11 + T15b → T16 → T17
```

---

## Task Breakdown

### T1: Criar estrutura de diretórios + mover arquivos kws/

**What**: Criar `tasks/`, `drivers/`, `kws/` e mover `mfcc.c/.h`, `dtw.c/.h` para `kws/`
**Where**: `firmware/main/`
**Depends on**: Nenhuma
**Reuses**: Arquivos existentes — apenas move, não altera conteúdo

**Done when**:
- [ ] Diretórios `firmware/main/tasks/`, `firmware/main/drivers/`, `firmware/main/kws/` existem
- [ ] `mfcc.c`, `mfcc.h`, `dtw.c`, `dtw.h`, `templates.h` estão em `kws/`
- [ ] Arquivos originais removidos de `main/` (somente esses 5)
- [ ] `#include "mfcc.h"` etc. ainda compilam (CMakeLists.txt ainda aponta para `main/` — será atualizado em T8)

**Requirement**: ARCH-02
**Gate**: `idf.py build` (ainda deve compilar com poc-microfone.c existente, só move arquivos)

---

### T2: Criar app_state.h

**What**: Header com todas as declarações `extern` de estado global compartilhado entre tasks
**Where**: `firmware/main/app_state.h`
**Depends on**: T1
**Reuses**: Globais de `poc-microfone.c` linhas 72–102

**Conteúdo**:
```c
#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_http_server.h"

typedef enum { APP_IDLE, APP_RECORDING, APP_STREAMING } app_state_t;

extern volatile app_state_t  g_app_state;
extern volatile float        g_dtw_threshold;
extern bool                  g_kws_paused;
extern int                   g_ws_record_fd;
extern int                   g_ws_stream_fd;
extern int                   g_ws_monitor_fd;
extern SemaphoreHandle_t     g_ws_mutex;
extern QueueHandle_t         g_click_queue;
extern QueueHandle_t         g_led_queue;   // novo
```

**Done when**:
- [ ] Arquivo criado com todas as declarações acima
- [ ] Nenhuma definição (sem `=` inicializadores) — só `extern`
- [ ] `app_state_t` enum inclui os 3 estados

**Requirement**: ARCH-04
**Gate**: Header-only, sem build gate obrigatório

---

### T3: Criar drivers/i2s_driver.c/.h [P]

**What**: Extrair `i2s_init()` e `i2s_read_16bit()` de `poc-microfone.c` para `drivers/i2s_driver.c/.h`. Aproveitar para remover código de diagnóstico (CONCERNS.md tech debt).
**Where**: `firmware/main/drivers/i2s_driver.c`, `firmware/main/drivers/i2s_driver.h`
**Depends on**: T2
**Reuses**: `poc-microfone.c:139–210` — extrair sem alterar lógica

**Interface**:
```c
// i2s_driver.h
void   i2s_driver_init(void);
size_t i2s_read_16bit(int16_t *out_buf, size_t num_samples);
```

**Remover nesta task** (CONCERNS tech debt):
```c
// poc-microfone.c:191-201 — bloco entre /* DIAGNÓSTICO — remover */
static uint32_t dbg_calls = 0;
if (++dbg_calls <= 5) { ... ESP_LOGI ... }
```

**Done when**:
- [ ] `drivers/i2s_driver.h` define interface acima
- [ ] `drivers/i2s_driver.c` implementa as duas funções com `g_rx_chan` como static interno
- [ ] Bloco de diagnóstico **removido** de `i2s_read_16bit`
- [ ] Constantes de pino (`PIN_BCLK`, `PIN_WS`, etc.) movidas para este arquivo ou para header compartilhado

**Requirement**: ARCH-05
**Gate**: Build após T8+T9

---

### T4: Criar tasks/button_task.c/.h [P]

**What**: Extrair `button_task()` de `poc-microfone.c` para `tasks/button_task.c/.h`
**Where**: `firmware/main/tasks/button_task.c`, `firmware/main/tasks/button_task.h`
**Depends on**: T2
**Reuses**: `poc-microfone.c:663–696` — extrair sem alterar lógica

**Interface**:
```c
// button_task.h
void button_task(void *arg);
```

**Done when**:
- [ ] Task extraída, usa `g_click_queue` de `app_state.h`
- [ ] `PIN_BUTTON` definido em `button_task.c` ou header de pinos compartilhado

**Requirement**: ARCH-05
**Gate**: Build após T8+T9

---

### T5: Criar tasks/audio_task.c/.h [P]

**What**: Extrair `audio_task()` + handlers WS `/record` e `/stream` de `poc-microfone.c`
**Where**: `firmware/main/tasks/audio_task.c`, `firmware/main/tasks/audio_task.h`
**Depends on**: T2
**Reuses**: `poc-microfone.c:237–285, 340–448` — extrair sem alterar lógica

**Interface**:
```c
// audio_task.h
void audio_task(void *arg);                         // arg = httpd_handle_t
void audio_task_register_handlers(httpd_handle_t);  // registra /record e /stream
```

**Done when**:
- [ ] Task extraída, usa `g_app_state`, `g_ws_mutex`, `g_ws_record_fd`, `g_ws_stream_fd` de `app_state.h`
- [ ] Handlers `/record` e `/stream` exportados via `audio_task_register_handlers`
- [ ] `fill_wav_header()` movida para este arquivo (só usada aqui)

**Requirement**: ARCH-05
**Gate**: Build após T8+T9

---

### T6: Criar tasks/kws_task.c/.h [P]

**What**: Extrair `kws_task()` + handler WS `/monitor` de `poc-microfone.c`. Comportamento atual preservado — state machine 2 estágios **não** implementada aqui.
**Where**: `firmware/main/tasks/kws_task.c`, `firmware/main/tasks/kws_task.h`
**Depends on**: T2
**Reuses**: `poc-microfone.c:451–654` — extrair sem alterar lógica

**Interface**:
```c
// kws_task.h
void kws_task(void *arg);                         // arg = httpd_handle_t
void kws_task_register_handlers(httpd_handle_t);  // registra /monitor
```

**Done when**:
- [ ] Task extraída com comportamento idêntico ao original
- [ ] Buffers `s_kws_ring` e `s_mfcc_out` são `static` locais em `kws_task.c` (não em `app_state.h`)
- [ ] Handler `/monitor` e `/threshold` exportados via `kws_task_register_handlers`
- [ ] `compute_rms`, `compute_temporal_var`, `find_word_idx` são funções `static` internas

**Requirement**: ARCH-05
**Gate**: Build após T8+T9

---

### T7: Criar tasks/led_task.c/.h (stub) [P]

**What**: Criar `led_task` como stub funcional — consome a queue mas apenas loga o comando, sem acionar LED ainda. Permite build limpo antes de T10.
**Where**: `firmware/main/tasks/led_task.c`, `firmware/main/tasks/led_task.h`
**Depends on**: T2

**Interface**:
```c
// led_task.h
typedef struct { uint8_t r, g, b; } led_command_t;
void led_task(void *arg);
```

**Implementação stub**:
```c
void led_task(void *arg) {
    led_command_t cmd;
    while (1) {
        if (xQueueReceive(g_led_queue, &cmd, portMAX_DELAY) == pdTRUE)
            ESP_LOGI("LED", "cmd r=%d g=%d b=%d", cmd.r, cmd.g, cmd.b);
    }
}
```

**Done when**:
- [ ] Header com `led_command_t` e declaração de `led_task`
- [ ] Implementação stub que não usa LEDC (LEDC ainda não existe)
- [ ] `g_led_queue` consumida de `app_state.h`

**Requirement**: LED-03
**Gate**: Build após T8+T9

---

### T8: Criar app_main.c slim + definições de globais

**What**: Criar `app_main.c` com `app_main()` enxuto e definições (não apenas `extern`) de todas as variáveis globais declaradas em `app_state.h`
**Where**: `firmware/main/app_main.c`
**Depends on**: T3, T4, T5, T6, T7

**Estrutura**:
```c
// Definições dos globais de app_state.h
volatile app_state_t g_app_state    = APP_IDLE;
volatile float       g_dtw_threshold = DTW_THRESHOLD_DEFAULT;
// ...
QueueHandle_t g_led_queue; // novo

void app_main(void) {
    // NVS init
    g_ws_mutex    = xSemaphoreCreateMutex();
    g_click_queue = xQueueCreate(1, sizeof(uint8_t));
    g_led_queue   = xQueueCreate(4, sizeof(led_command_t)); // novo
    i2s_driver_init();
    // ledc_driver_init() — chamado em T10
    gpio_init();           // inline ou movido para button_task
    wifi_init();           // inline ou arquivo separado
    vTaskDelay(pdMS_TO_TICKS(3000));
    httpd_handle_t server = start_webserver();
    audio_task_register_handlers(server);
    kws_task_register_handlers(server);
    xTaskCreate(button_task, "btn",   2048, NULL,   5, NULL);
    xTaskCreate(audio_task,  "audio", 8192, server, 10, NULL);
    xTaskCreate(kws_task,    "kws",   8192, server, 6,  NULL);
    xTaskCreate(led_task,    "led",   2048, NULL,   5,  NULL); // novo
}
```

**Done when**:
- [ ] Todas as variáveis globais **definidas** (não extern) neste arquivo
- [ ] `app_main()` contém apenas init + xTaskCreate
- [ ] `start_webserver()`, `wifi_init()`, `gpio_init()` definidas aqui ou delegadas a funções auxiliares locais
- [ ] `g_led_queue` criada com capacidade 4

**Requirement**: ARCH-03
**Gate**: Build após T9

---

### T9: Atualizar CMakeLists.txt + Build Gate + Remover poc-microfone.c

**What**: Atualizar CMakeLists para apontar para os novos arquivos, compilar, e remover `poc-microfone.c` após build limpo
**Where**: `firmware/main/CMakeLists.txt`
**Depends on**: T8

```cmake
idf_component_register(
  SRCS
    "app_main.c"
    "tasks/audio_task.c"
    "tasks/kws_task.c"
    "tasks/button_task.c"
    "tasks/led_task.c"
    "drivers/i2s_driver.c"
    "kws/mfcc.c"
    "kws/dtw.c"
  INCLUDE_DIRS "." "tasks" "drivers" "kws"
  REQUIRES driver esp_wifi esp_event esp_netif nvs_flash esp_http_server
  PRIV_REQUIRES esp_driver_gpio esp_driver_i2s
)
```
*(ledc adicionado em T10 após implementar o driver)*

**Done when**:
- [ ] `idf.py build` passa sem erros ou warnings novos
- [ ] `poc-microfone.c` **removido** (só após build limpo)
- [ ] Funcionalidade existente verificada: flash + TESTING.md steps 1–8 passam

**Requirement**: ARCH-01
**Gate**: `idf.py build` + smoke test manual (TESTING.md steps 1–8)

---

### T10: Implementar drivers/ledc_driver.c [P após T9]

**What**: Implementar driver LEDC completo com init e set_color
**Where**: `firmware/main/drivers/ledc_driver.c`, `firmware/main/drivers/ledc_driver.h`
**Depends on**: T9

**Interface**:
```c
void ledc_driver_init(void);
void ledc_set_color(uint8_t r, uint8_t g, uint8_t b);
```

**Configuração**:
```c
// Timer 0: low_speed, 8-bit, 5kHz
// CH0 → GPIO 18 (R), CH1 → GPIO 19 (G), CH2 → GPIO 21 (B)
// ledc_set_color: ledc_set_duty + ledc_update_duty por canal
```

**Done when**:
- [ ] `ledc_driver_init()` configura timer 0 e 3 canais sem `ESP_ERROR_CHECK` falhar no boot
- [ ] `ledc_set_color(255,0,0)` acende apenas canal R visualmente
- [ ] `ledc_set_color(0,0,0)` apaga LED completamente
- [ ] `app_main.c` chama `ledc_driver_init()` antes de `xTaskCreate`
- [ ] `CMakeLists.txt` inclui `"drivers/ledc_driver.c"` e `esp_driver_ledc` em `PRIV_REQUIRES`

**Requirement**: LED-01, LED-02
**Gate**: `idf.py build` + verificação visual R/G/B individualmente

---

### T11: Implementar tasks/led_task.c (completo) [P após T9]

**What**: Substituir stub de led_task pela implementação completa que consome `g_led_queue` e chama `ledc_set_color`
**Where**: `firmware/main/tasks/led_task.c`
**Depends on**: T9 (T10 pode ser desenvolvido em paralelo mas T11 precisa de T10 para testar)

**Implementação**:
```c
void led_task(void *arg) {
    led_command_t cmd;
    while (1) {
        xQueueReceive(g_led_queue, &cmd, portMAX_DELAY);
        ledc_set_color(cmd.r, cmd.g, cmd.b);
    }
}
```

**Done when**:
- [ ] `led_task.c` usa `ledc_set_color` de `drivers/ledc_driver.h`
- [ ] Stub de log removido
- [ ] Bloqueia em `portMAX_DELAY` quando queue vazia

**Requirement**: LED-03, LED-04
**Gate**: Smoke test — enviar `led_command_t` diretamente na queue via código temporário e observar LED trocar de cor

---

### T12: Criar kws/color_catalog.h [P após T9]

**What**: Header com catálogo estático de cores RGB e função de lookup inline
**Where**: `firmware/main/kws/color_catalog.h`
**Depends on**: T9

**Conteúdo**: 9 entradas (vermelho, verde, azul, amarelo, ciano, magenta, laranja, roxo, branco) com valores RGB idênticos ao protótipo Arduino. Função `color_lookup(name, &r, &g, &b)` retorna `bool`.

**Done when**:
- [ ] 9 entradas presentes com valores RGB corretos (conferir com catálogo Arduino do protótipo)
- [ ] `color_lookup` compila e retorna false para nome inválido

**Requirement**: CAT-01
**Gate**: Header-only, sem build gate específico

---

### T13: Reestruturar kws/templates.h (dois arrays) [P após T9]

**What**: Adaptar `templates.h` para estrutura com `KWS_TRIGGERS[]` e `KWS_COLORS[]` em vez de `KWS_WORDS[]`. Mantém templates existentes (ligar, garbage). Arrays de cores ficam **vazios** até T15 (treinamento).
**Where**: `firmware/main/kws/templates.h`
**Depends on**: T9

**Nova estrutura**:
```c
// Triggers (verificados em KWS_IDLE)
extern const kws_word_t KWS_TRIGGERS[];
extern const int        KWS_N_TRIGGERS;
// → ligar (10 templates), desligar (0 até T15), garbage (N templates)

// Cores (verificadas em KWS_AWAIT_COLOR)
extern const kws_word_t KWS_COLORS[];
extern const int        KWS_N_COLORS;
// → 9 entradas com 0 templates até T15
```

**Done when**:
- [ ] `KWS_WORDS[]` removido e substituído pelos dois arrays
- [ ] Templates de ligar e garbage preservados em `KWS_TRIGGERS`
- [ ] `KWS_COLORS` declara as 9 entradas com `n_templates = 0` como placeholder
- [ ] `kws_task.c` atualizado para usar novo nome (ainda referencia só `KWS_TRIGGERS` por ora)
- [ ] `idf.py build` passa

**Requirement**: KWS-05, KWS-06
**Gate**: `idf.py build`

---

### T14: Atualizar training/generate_templates.py

**What**: Adaptar `generate_templates.py` para gerar os dois arrays (`KWS_TRIGGERS` e `KWS_COLORS`) em vez do antigo `KWS_WORDS`. Classificação por palavra: ligar/desligar/garbage → TRIGGERS; demais → COLORS.
**Where**: `training/generate_templates.py`
**Depends on**: T13

**Done when**:
- [ ] Script gera header com `KWS_TRIGGERS[]` + `KWS_N_TRIGGERS` e `KWS_COLORS[]` + `KWS_N_COLORS`
- [ ] Executar `make train-templates` com amostras existentes de "ligar" produz output compilável
- [ ] `idf.py build` passa após regenerar templates com script atualizado

**Requirement**: KWS-05
**Gate**: `make train-templates && idf.py build`

---

### T15a: Gravar amostras de voz: desligar + 9 cores [P após T9]

**What**: Coletar amostras de áudio e extrair features para as 10 novas palavras. Não depende de T13/T14 — pode rodar em paralelo com o desenvolvimento do LEDC.
**Where**: `training/samples/`, `training/features/`
**Depends on**: T9 (firmware rodando para coleta via botão/app)

**Palavras a gravar**: desligar, vermelho, verde, azul, amarelo, ciano, magenta, laranja, roxo, branco

```bash
# Por palavra (mínimo 3 execuções, recomendado 5):
make train WORD=<palavra>   # coleta via app ou botão + extrai features
```

**Done when**:
- [ ] Cada uma das 10 palavras tem ≥ 3 arquivos em `training/features/<palavra>/`
- [ ] `make train WORD=vermelho` (e demais) roda sem erro

**Requirement**: KWS-05
**Gate**: `ls training/features/` mostra 10 diretórios com ≥ 3 arquivos cada

---

### T15b: Gerar templates.h com todas as palavras

**What**: Executar `make train-templates` com o script atualizado (T14) para gerar `templates.h` completo com KWS_TRIGGERS + KWS_COLORS preenchidos.
**Where**: `firmware/main/kws/templates.h`
**Depends on**: T14 (script atualizado) + T15a (amostras coletadas)

```bash
make train-templates   # gera templates.h
idf.py build           # valida compilação
```

**Done when**:
- [ ] `KWS_N_TRIGGERS` = 3 (ligar + desligar + garbage)
- [ ] `KWS_N_COLORS` = 9 (cada cor com ≥ 3 templates)
- [ ] `idf.py build` passa
- [ ] Monitor KWS mostra distâncias DTW para cada cor abaixo do threshold em ≥ 2/3 tentativas faladas

**Requirement**: KWS-05
**Gate**: `make train-templates && idf.py build` + verificação manual de distâncias no Monitor

---

### T16: Implementar state machine KWS 2 estágios

**What**: Adicionar `kws_mode_t` e lógica de 2 estágios em `kws_task.c`. Esta é a task de integração final.
**Where**: `firmware/main/tasks/kws_task.c`
**Depends on**: T11 (LED funcionando), T15 (templates treinados)

**Lógica a implementar**:
```c
typedef enum { KWS_IDLE, KWS_AWAIT_COLOR } kws_mode_t;
static kws_mode_t s_kws_mode = KWS_IDLE;
static TickType_t s_color_timeout_tick = 0;

// Em KWS_IDLE: comparar KWS_TRIGGERS
// → "ligar": s_kws_mode = KWS_AWAIT_COLOR; s_color_timeout_tick = now
// → "desligar": enviar {0,0,0} para g_led_queue
// Em KWS_AWAIT_COLOR: comparar KWS_COLORS
// → cor: color_lookup(name, &r,&g,&b); enviar {r,g,b} para g_led_queue; → KWS_IDLE
// → "ligar" (via KWS_TRIGGERS): reiniciar timer
// → "desligar" (via KWS_TRIGGERS): enviar {0,0,0}; → KWS_IDLE
// → timeout 2s: → KWS_IDLE
// JSON do monitor: incluir "kws_mode":"idle"/"await_color"
```

**Done when**:
- [ ] `kws_mode_t` declarado e `s_kws_mode` inicializado como `KWS_IDLE`
- [ ] WHEN "ligar" detectado em KWS_IDLE THEN modo muda para KWS_AWAIT_COLOR
- [ ] WHEN cor detectada em KWS_AWAIT_COLOR THEN LED acende na cor correta e volta a KWS_IDLE
- [ ] WHEN timeout 2s THEN volta a KWS_IDLE sem alterar LED
- [ ] WHEN "desligar" detectado em qualquer modo THEN LED apaga
- [ ] JSON do monitor inclui campo `"kws_mode"`
- [ ] Funcionalidade anterior (gravação, stream, botão) não é afetada

**Requirement**: KWS-01, KWS-02, KWS-03, KWS-04, MON-01
**Gate**: `idf.py build` + smoke test completo (TESTING.md steps 1–8 + validação LED)

---

### T17: Smoke test final de integração

**What**: Verificar todos os success criteria da spec
**Where**: Hardware + Monitor web
**Depends on**: T16

**Checklist**:
- [ ] `idf.py build` passa sem erros
- [ ] TESTING.md steps 1–8 passam (funcionalidade anterior não regrediu)
- [ ] "ligar azul" → LED acende azul (≥ 8/10 tentativas)
- [ ] "desligar" → LED apaga (≥ 9/10 tentativas)
- [ ] "ligar" sem dizer cor → LED não muda após 2s
- [ ] Monitor mostra `kws_mode: await_color` após "ligar"
- [ ] Trocar cor: "ligar verde" com LED azul aceso → LED muda para verde

**Requirement**: Todos
**Gate**: Manual — checklist acima documentado como passando

---

## Mapa de Requisitos → Tasks

| Requisito | Task |
|-----------|------|
| ARCH-01 | T9 |
| ARCH-02 | T1 |
| ARCH-03 | T8 |
| ARCH-04 | T2 |
| ARCH-05 | T3, T4, T5, T6, T7 |
| LED-01, LED-02 | T10 |
| LED-03, LED-04 | T7, T11 |
| KWS-01..04 | T16 |
| KWS-05, KWS-06 | T13, T14, T15a, T15b |
| CAT-01 | T12 |
| MON-01 | T16 |

**Cobertura**: 17 requisitos → 17 tasks, todos mapeados ✅
