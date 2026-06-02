# RGB LED + KWS — Decisões da Entrevista

**Data:** 2026-06-01
**Escopo:** Controle de LED RGB via PWM integrado ao KWS existente, acionado por comando de voz ("ligar" + cor), incluindo reorganização arquitetural do firmware.
**Fonte:** Discussão informal

---

## Decisões

### Estratégia de reconhecimento do comando

- Detecção **sequencial em 2 estágios**: ao detectar "ligar", o firmware entra em modo de escuta de cor por **2 segundos**; qualquer cor detectada nessa janela aciona o LED.
- Se nenhuma cor for detectada no timeout, volta ao idle sem alterar o estado do LED.
- "desligar" é reconhecido **diretamente** (sem prefixo "ligar") e apaga o LED.
- **Rationale:** Evita treinar 9 frases compostas ("ligar_azul", etc.) e dobrar a janela de áudio (MFCC_WIN_SAMPLES atual = 0,5s seria insuficiente para frases compostas).

### Hardware do LED

- LED RGB **cátodo comum** com 3 transistores NPN (um por canal R/G/B), alimentado em **5V**.
- Lógica **não invertida**: GPIO HIGH = LED aceso. LEDC duty 0–255 mapeia direto para apagado–brilho máximo.
- Sem necessidade de resistores adicionais no lado do microcontrolador (transistores fazem o isolamento de corrente).

### Pinos GPIO

| Canal | GPIO |
|-------|------|
| R     | 18   |
| G     | 19   |
| B     | 21   |

- Todos livres no circuito atual e compatíveis com LEDC.
- Pinos ocupados evitados: 2 (LED onboard), 4 (botão), 22/25/26 (I2S).

### Arquitetura do firmware

Estrutura de pastas adotada:

```
firmware/main/
├── app_main.c            # só app_main: init + xTaskCreate
├── app_state.h           # estado global: enums, handles, mutex, queues
├── tasks/
│   ├── audio_task.c/.h
│   ├── kws_task.c/.h
│   ├── button_task.c/.h
│   └── led_task.c/.h     # recebe cor via queue, aplica PWM
├── drivers/
│   ├── i2s_driver.c/.h
│   └── ledc_driver.c/.h  # init LEDC + set_color(r, g, b)
├── kws/
│   ├── mfcc.c/.h
│   ├── dtw.c/.h
│   └── templates.h
└── CMakeLists.txt
```

- Comunicação `kws_task` → `led_task` via `QueueHandle_t` com struct `led_command_t` (cor RGB).
- Estado global compartilhado centralizado em `app_state.h`.

### Comportamento do LED

- LED **permanece aceso** na cor detectada até receber novo comando.
- **"desligar"** apaga o LED diretamente, sem exigir o prefixo "ligar".
- Novo comando "ligar + cor" substitui a cor atual.
- Timeout de 2s na escuta de cor: sem detecção → volta ao idle, LED não muda.

### Vocabulário KWS

Palavras a treinar:

| Palavra    | Observação                        |
|------------|-----------------------------------|
| ligar      | já existe (10 templates)          |
| desligar   | novo                              |
| vermelho   | novo                              |
| verde      | novo                              |
| azul       | novo                              |
| amarelo    | novo                              |
| ciano      | novo                              |
| magenta    | novo                              |
| laranja    | novo                              |
| roxo       | novo                              |
| branco     | novo                              |
| garbage    | já existe                         |

- Templates ficam em **flash** (static const float[]), não consomem RAM.
- Estimativa com 5 templates/palavra: 12 palavras × 5 × 2,5 KB ≈ **150 KB de flash** — viável (flash = 4 MB).
- Detecção de cor só ocorre no modo de escuta (pós-"ligar"), reduzindo falsos positivos em conversa normal.

---

## Discretion do Agente

- Resolução do timer LEDC: 8 bits (0–255), compatível com o catálogo Arduino do protótipo.
- Frequência PWM: 5 kHz (padrão LEDC, imperceptível para LED RGB).
- Número exato de templates por cor nova: mínimo 3, recomendado 5.

---

## Ideias Adiadas

- Efeitos de transição entre cores (fade) — surgiu implicitamente no contexto do catálogo de cores.
- Controle por intensidade via voz ("azul fraco", "azul forte") — presente no protótipo Arduino mas fora do escopo atual.
- Remoção do Wi-Fi/WebSocket para liberar RAM — levantado como possibilidade dado o ~150 KB disponível, mas fora do escopo.

---

## Questões em Aberto

- Nenhuma. Todas as decisões foram resolvidas.
