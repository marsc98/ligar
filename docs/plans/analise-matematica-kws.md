# Análise Matemática do Pipeline MFCC+DTW

**Data:** 2026-05-27  
**Base:** logs de `monitor_2026-05-27T05-17-33.jsonl` + leitura do código completo

---

## Veredicto Direto

O pipeline MFCC+DTW **pode** funcionar nesse hardware — a teoria está certa. A implementação atual tem dois defeitos fundamentais que tornam toda detecção impossível, independente de threshold ou garbage ratio. Os defeitos são corrigíveis sem mudança de arquitetura.

---

## O que os logs mostram

### Distribuição das distâncias DTW (225 eventos ativos)

| Palavra | Mín | Máx | Centro |
|---|---|---|---|
| `ligar` | 4.1 | 5.2 | ~4.7 |
| `garbage` | 4.1 | 4.9 | ~4.7 |

**Ambas as distribuições são idênticas.** Zero separação. Zero detecções (`word: null` em todos os 225 eventos).

### O que esses valores significam matematicamente

Após CMVN por utterance (`std=1` por coeficiente), a distância euclidiana esperada entre dois vetores de dimensão 13 **independentes aleatórios** é:

```
E[||q_i - r_j||] = sqrt(2 × n_coefs) = sqrt(2 × 13) ≈ 5.10
```

Os valores observados (4.1–5.2) estão **exatamente nesse intervalo de ruído aleatório**. O pipeline está computando distâncias indistinguíveis de ruído. Os templates não têm sinal.

---

## Causa Raiz 1 — Desalinhamento Treinamento/Inferência (CRÍTICO)

### O que o treinamento faz (`extract_features.py:23-31`)

```python
onset = int(np.argmax(abs_y > peak * 0.05))
start = max(0, onset - N_SAMPLES // 4)   # onset no frame ≈12
y_int16 = y_int16[start:start + N_SAMPLES]
```

O onset da palavra está fixado em ~25% do início da janela = **sample 2000 = frame 12**.

### O que a inferência faz (`poc-microfone.c:526`)

```c
mfcc_compute(g_kws_ring, g_kws_ring_pos, MFCC_WIN_SAMPLES, g_mfcc_out);
```

O ring buffer de 8000 amostras gira continuamente. A palavra pode estar em **qualquer posição** — do frame 0 ao frame 47.

### Consequência matemática

O DTW Sakoe-Chiba com `window=12` permite no máximo ±12 frames de deslocamento relativo. A diferença real pode ser de até 36 frames:

```
Desalinhamento máximo = 47 - 12 = 35 frames = 350ms
Capacidade do DTW     = window = 12 frames = 120ms
Lacuna               = 35 - 12 = 23 frames não compensáveis
```

Com esse desalinhamento, o DTW é forçado a comparar frames de fala do template com frames de silêncio da query (e vice-versa). Resultado: distância sempre alta, independente de ser "ligar" ou ruído.

**Evidência nos logs:** o MFCC é computado a cada chunk de 512 amostras (32ms) enquanto VAD está ativo. O log mostra múltiplas medidas por evento sonoro (e.g., linhas 5-13: 8 medidas em ~0.6s), todas com distâncias 4.1-4.8. Se a detecção funcionasse, ao menos UMA dessas posições do ring buffer teria a palavra bem alinhada — mas nenhuma detecta. Isso confirma que o desalinhamento derrota o DTW em todas as posições.

---

## Causa Raiz 2 — CMVN por Utterance Destrói o Gate de Variância Temporal (ALTO)

### O que `compute_temporal_var` deveria fazer

Rejeitar silêncio ou sons estacionários (ruído branco, zumbido) que não têm dinâmica temporal.

### O que acontece após CMVN

O CMVN (`mfcc.c:141-161`) força `std_coef = 1` para CADA coeficiente **dentro da janela de 0.5s**. Portanto:

```
compute_temporal_var(após CMVN) ≈ (1/N_COEFS) × sum_c(sqrt(variance_c)) = (1/13) × 13 × 1.0 = 1.0
```

Isso é exatamente o que os logs mostram: `var:1` em **todos os** 225 eventos ativos. O gate `TEMPORAL_VAR_THRESHOLD = 0.3` **nunca rejeita nada** porque o CMVN garante que a variância seja sempre ≈1.

O gate é um no-op. A função existe mas não tem efeito.

---

## Causa Raiz 3 — CMVN Incompatível entre Treinamento e Inferência (MÉDIO)

Mesmo que o alinhamento fosse perfeito, o CMVN por utterance introduz inconsistência porque a estatística depende do **conteúdo** da janela:

```
mean_train[c] = (30 frames × μ_voz + 18 frames × μ_silêncio) / 48
mean_infer[c] = (X frames × μ_voz + (48-X) frames × conteúdo_variável) / 48
```

Para `X` diferente (palavra parcialmente fora do buffer), `mean_train ≠ mean_infer`, a normalização é diferente, e os frames após CMVN são incomparáveis.

Com CMVN global (parâmetros fixos do treinamento), essa inconsistência desaparece.

---

## Análise do Garbage Ratio

A lógica atual (`poc-microfone.c:568-571`):

```c
float ratio = best_dist / garbage_dist;
ratio_ok = (ratio < GARBAGE_RATIO_THRESHOLD);  // GARBAGE_RATIO_THRESHOLD = 0.75
```

Isso exige `ligar_dist / garbage_dist < 0.75`, ou seja, ligar deve ser **pelo menos 25% mais próximo** do template do que garbage.

Com as distâncias observadas (~4.7 ± 0.3 para ambos), o ratio é:

```
ratio ≈ 4.7 / 4.7 = 1.0 → rejeitado (1.0 > 0.75)
```

O garbage ratio é a principal causa de rejeição nos logs (`rejected:"garbage_ratio"` frequente a partir da linha 93). Mas isso não é o problema — é o sintoma correto respondendo ao problema errado. O ratio alto acontece porque as distâncias são ambas altas e equivalentes, não porque o modelo está diferenciando.

---

## A Abordagem Pode Funcionar?

**Sim.** DTW com MFCC é tecnicamente viável para KWS simples em embedded. O estado da arte antes de redes neurais era exatamente MFCC+DTW, com FAR < 1% e FRR < 5% para palavras isoladas em ambiente controlado. O hardware ESP32-D0WD tem capacidade de processamento suficiente para isso.

O que precisa mudar é a **correspondência entre treinamento e inferência**.

---

## Fixes Ordenados por Impacto

### Fix 1 — Alinhamento por offset (CRÍTICO, impacto máximo)

**Problema:** treinamento alinha pelo onset, inferência usa posição arbitrária.

**Solução:** computar MFCC **somente quando o VAD transiciona de ativo para silêncio** (palavra acabou de terminar). Nesse momento, a palavra está no final do ring buffer. Reajustar o treinamento para alinhar pelo offset.

**Treinamento (`extract_features.py`):**
```python
# Detecta offset: último sample com energia acima de 5% do pico
energy = np.abs(y_int16).astype(np.float32)
peak = energy.max()
voiced = energy > peak * 0.05
last_voiced = len(voiced) - 1 - np.argmax(voiced[::-1])
end = min(len(y_int16), last_voiced + SAMPLE_RATE // 20)  # +50ms de contexto
start = max(0, end - N_SAMPLES)
y_int16 = y_int16[start:start + N_SAMPLES]
```

**Firmware (`kws_task` em `poc-microfone.c`):**
```c
static bool was_voiced = false;
bool is_voiced = (rms >= VAD_RMS_THRESHOLD);

if (was_voiced && !is_voiced) {
    // palavra acabou de terminar — alinhamento consistente
    mfcc_compute(g_kws_ring, g_kws_ring_pos, MFCC_WIN_SAMPLES, g_mfcc_out);
    // ... DTW comparison ...
}
was_voiced = is_voiced;
```

**Custo:** zero memória extra, mudança cirúrgica em 2 arquivos.

---

### Fix 2 — CMVN Global (ALTO, resolve inconsistência de normalização)

**Problema:** CMVN por utterance produz estatísticas diferentes para a mesma palavra em contextos de janela diferentes.

**Solução:** computar `mean` e `std` globais durante o treinamento e gravar como constantes. Aplicar normalização fixa no firmware.

**Treinamento:** ao final de `generate_templates.py`, calcular e salvar:
```python
all_features = np.concatenate([features_word1, features_word2, features_garbage])
global_mean = all_features.reshape(-1, N_COEFS).mean(axis=0)
global_std  = all_features.reshape(-1, N_COEFS).std(axis=0) + 1e-8
```

Emitir em `templates.h`:
```c
static const float KWS_CMVN_MEAN[13] = { ... };
static const float KWS_CMVN_STD[13]  = { ... };
```

**Firmware:** substituir o bloco CMVN em `mfcc.c:141-161` para usar as constantes.

**Custo:** 26 floats = 104 bytes adicionais.

---

### Fix 3 — Gate de Variância Temporal Funcional (MÉDIO)

**Problema:** após CMVN, `compute_temporal_var` retorna sempre ≈1.0.

**Opção A:** computar variância temporal ANTES do CMVN (dividir `mfcc_compute` em duas fases).

**Opção B:** usar ZCR como gate (independe de CMVN):
```c
// ZCR: voz tem 0.01-0.10; batidas/ruído têm > 0.15
float zcr = 0;
for (size_t i = 1; i < n; i++)
    zcr += (float)((chunk[i] >= 0) != (chunk[i-1] >= 0));
zcr /= n;
if (zcr > ZCR_MAX_THRESHOLD) continue;  // rejeita impulsos
```

**Opção B é recomendada** — zero custo de memória, não depende de MFCC.

---

### Fix 4 — Delta-MFCC (MÉDIO, aumenta discriminação)

Adiciona derivada temporal dos coeficientes. "Ligar" tem transições /l/→/i/→/g/→/a/ bem distintas; batidas têm Δ≈0 (energia surge instantaneamente).

```
Feature por frame: [mfcc₀..₁₂, Δmfcc₀..₁₂] = 26 coeficientes
Δc[t] = (c[t+1] - c[t-1]) / 2
```

**Custo de memória (com 5 templates por palavra):**
```
5 templates × 2 palavras × 48 frames × 26 coefs × 4 bytes = 49.920 bytes
Query buffer: 48 × 26 × 4 = 4.992 bytes
Total adicional: ~30KB sobre o estado atual
```

Com Wi-Fi ativo (~150KB livres), cabe confortavelmente.

**Custo de CPU:** DTW escala linearmente com `n_coefs`. 26 em vez de 13 = 2× mais lento. Ainda viável no ESP32 (DTW atual roda em <5ms).

---

## Sequência Recomendada

```
Fase 1 (baixo esforço, alto impacto):
  1. Fix 1: alinhamento por offset — regravar templates
  2. Fix 3B: gate de ZCR

Fase 2 (se Fase 1 insuficiente):
  3. Fix 2: CMVN global
  4. Fix 4: delta-MFCC

Fase 3 (se Fase 2 insuficiente):
  → Edge Impulse (CNN sobre MFCC, veja brainstorm-melhoria-kws.md)
```

---

## Parâmetros Sugeridos Pós-Fix

| Parâmetro | Atual | Pós-Fix 1 | Pós-Fix 1+4 |
|---|---|---|---|
| Janela MFCC | 0.5s / 48 frames | 0.5s / 48 frames | 0.5s / 48 frames |
| N_COEFS | 13 | 13 | 26 (MFCC+Δ) |
| DTW_WINDOW | 12 | 6–8 | 5–6 |
| DTW_THRESHOLD | 4.6 (adaptativo) | 2.5–3.5 (calibrar) | 1.8–2.5 (calibrar) |
| GARBAGE_RATIO | 0.75 | 0.80 | 0.80 |
| Momento do MFCC | Cada chunk VAD ativo | Transição voz→silêncio | Transição voz→silêncio |

Com Fix 1 aplicado, o DTW_WINDOW pode ser apertado (6–8) porque o alinhamento será consistente — sem precisar compensar deslocamentos grandes. Isso reduz falsos positivos.

---

## Resumo Executivo

| Problema | Severidade | Fix |
|---|---|---|
| Treinamento usa onset, inferência usa posição arbitrária | 🔴 Crítico | Alinhamento por offset |
| `compute_temporal_var` sempre retorna ≈1.0 (gate inútil) | 🟠 Alto | Gate de ZCR pré-CMVN |
| CMVN por utterance: estatísticas incompatíveis | 🟠 Alto | CMVN global |
| Sem delta-MFCC: dinâmica temporal ausente | 🟡 Médio | Adicionar Δ |

A abordagem MFCC+DTW **tem fundamento matemático sólido** para esse caso de uso. O bloqueio atual é implementação, não teoria. Fix 1 sozinho deve produzir as primeiras detecções reais.
