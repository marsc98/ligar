# await-color-led — Decisões da Entrevista

**Data:** 2026-06-06
**Scope:** Adicionar LED indicador na GPIO 23 que acende quando o firmware entra em KWS_AWAIT_COLOR (aguardando cor após "ligar")
**Source:** discussão informal

---

## Decisões

### Timing do acionamento

- LED acende imediatamente quando `s_kws_mode = KWS_AWAIT_COLOR` é setado (`kws_task.c:327`)
- Sem delay artificial — a latência natural de processamento (~10–50ms) já existe entre o fim da palavra e o acionamento

### Ciclo de vida do LED

- **Acende:** ao entrar em `KWS_AWAIT_COLOR`
- **Apaga** nos três casos de saída do estado:
  1. Cor detectada com sucesso
  2. "desligar" dito em AWAIT_COLOR
  3. Timeout de 2000ms

### Hardware

- GPIO: **23**
- LED: **vermelho**, Vf ≈ 2.0V
- Resistor: **220Ω** (I ≈ 6mA, seguro para o GPIO do ESP32 a 3.3V)
- Ligação: GPIO 23 → resistor 220Ω → anodo do LED → catodo → GND

### Implementação

- Usar `gpio_set_level(PIN_AWAIT_LED, 1/0)` diretamente em `kws_task.c`, igual ao padrão do `PIN_LED 2` existente
- Definir `#define PIN_AWAIT_LED 23` no topo do arquivo

---

## Deferred Ideas

- Piscar o LED no timeout para feedback de "não entendi" — descartado para manter simplicidade
