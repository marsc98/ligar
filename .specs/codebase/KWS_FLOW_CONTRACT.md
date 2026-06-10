# KWS Flow + WebSocket Contract

Documento de referência obrigatório para qualquer mudança no firmware que afete KWS, áudio ou WebSocket. Leia antes de modificar `kws_task.c`, `audio_task.c`, `i2s_reader_task.c` ou qualquer parte do pipeline de detecção.

---

## Fluxo de sinal (ponta a ponta)

```
INMP441 (I2S)
    ↓  i2s_reader_task  (prio 12)
    │  lê chunks de 512 amostras (32ms a 16kHz)
    │  envia para DOIS queues com timeout=0 (dropa se cheio)
    ├──→ g_audio_queue (size 4) → audio_task  (prio 10)  [streaming/recording]
    └──→ g_kws_queue   (size 4) → kws_task    (prio  6)  [detecção de palavras]

kws_task:
    ring buffer circular 8000 amostras (0.5s)
    ↓ VAD: RMS >= 300 → acumula chunks voiced
    ↓ fim de fala (chunk silencioso após voiced):
        voiced_chunks >= 3 → prossegue
        cooldown 1000ms → rejeita
        temporal_var >= 0.3 → prossegue
        temporal_var <  0.3 → rejeita (var_gate)
    ↓ mfcc_compute → float[48×13]
    ↓ mlp_infer → probs[10]
    ↓ best = argmax(probs); valid = probs[best] >= 0.75 && best != garbage_idx
    ↓ state machine IDLE / AWAIT_COLOR
    ↓ ws_send_text → /monitor WebSocket
```

---

## JSON enviado pelo firmware (ws://<ip>/monitor)

Todos os eventos têm este subconjunto obrigatório:

```json
{ "rms": 123.4, "word": null | "ligar", "probs": {}, "kws_mode": "idle" }
```

### Campos por tipo de evento

| Evento       | Campos presentes                                         |
|--------------|----------------------------------------------------------|
| heartbeat    | `rms`, `word: null`, `probs: {}`, `kws_mode`            |
| too_short    | + `rejected: "too_short"`                               |
| cooldown     | + `rejected: "cooldown"`                                |
| var_gate     | + `var`, `rejected: "var_gate"`                         |
| detecção     | + `var`, `word: "<classe>"` ou `null`, `probs: {<cls>:<p>,...}` |

### Schema completo (TypeScript)

```ts
interface MonitorEvent {
  ts: number           // adicionado no cliente
  rms: number
  word: string | null
  probs: Record<string, number>  // 10 classes, softmax
  var?: number
  rejected?: string    // "too_short" | "cooldown" | "var_gate"
  kws_mode?: string    // "idle" | "await_color"
}
```

**NUNCA** mais envie `threshold`, `dists`, ou `garbage_dist` — esses campos eram do DTW e foram removidos.

---

## State machine KWS

```
KWS_IDLE
  ──"ligar"──→  KWS_AWAIT_COLOR  (timeout 2s)
  ──timeout──→  KWS_IDLE
  ──cor/desligar em AWAIT_COLOR──→ KWS_IDLE

Palavras reconhecidas em IDLE:     ligar, desligar
Palavras reconhecidas em AWAIT_COLOR: cores + desligar + ligar (reinicia timer)
```

---

## WebSocket fd lifecycle

- `g_ws_monitor_fd` = -1 quando desconectado.
- Conexão: HTTP GET em `/monitor` → `g_ws_monitor_fd = fd`.
- Desconexão detectada de **duas formas**:
  1. Handler recebe CLOSE frame → `g_ws_monitor_fd = -1`
  2. `ws_send_text` retorna erro → `g_ws_monitor_fd = -1`
- `g_ws_mutex` protege leitura/escrita de `g_ws_monitor_fd`.
- `httpd_ws_send_frame_async` é chamado de `kws_task` (não da task HTTP). Isso é correto — a API `_async` é thread-safe.

---

## Checklist obrigatório antes de qualquer PR que toque o firmware KWS

### Se mudou o formato JSON de `/monitor`:
- [ ] Atualizar `MonitorEvent` em `web/src/utils/monitorLogger.ts`
- [ ] Atualizar parsing em `web/src/components/MonitorTab.tsx`
- [ ] Verificar todos os 6 pontos de `ws_send_text` em `kws_task.c` (too_short, cooldown, var_gate, detecção IDLE, detecção AWAIT_COLOR, heartbeat)
- [ ] Atualizar este documento

### Se mudou o pipeline de áudio (I2S, queues, tasks):
- [ ] Confirmar que `i2s_reader_task` ainda envia para `g_kws_queue` E `g_audio_queue`
- [ ] Confirmar tamanho das queues e prioridades das tasks
- [ ] Verificar que `vTaskDelay` dentro de `kws_task` não bloqueia além do queue timeout (~128ms → overflow com queues de 4)

### Se mudou o modelo KWS (DTW→MLP ou novo modelo):
- [ ] Verificar que o número e a ordem das classes batem entre `weights.h` e `training/train_mlp.py`
- [ ] Verificar que `MLP_N_FRAMES × MLP_N_COEFS == MFCC_N_FRAMES × MFCC_N_COEFS` (atualmente 48×13=624)
- [ ] Verificar que `mfcc_compute` usa os mesmos parâmetros do Python (sample_rate, frame_len, hop, n_mels, n_coefs)
- [ ] Atualizar `probs` no frontend para mostrar as novas classes

### Se mudou o state machine:
- [ ] Atualizar diagrama neste documento
- [ ] Verificar que `kws_mode` no JSON reflete o novo estado

---

## Padrão de falha recorrente: schema drift

**Sintoma**: monitor WebSocket conecta, eventos chegam, mas probabilidades/palavras não aparecem corretamente no frontend.

**Causa**: firmware muda campos JSON (`dists→probs`, remove `threshold`, adiciona `kws_mode`) mas frontend não é atualizado.

**Como detectar**: abrir DevTools → Network → WS → ver payload raw. Se o JSON do firmware tem campos que o frontend não lê, ou vice-versa, o schema está dessincronizado.

**Prevenção**: sempre que alterar qualquer campo JSON em `kws_task.c`, executar o checklist acima.
