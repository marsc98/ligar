# Brainstorming — Melhoria do KWS no ESP32

**Data:** 2026-05-26  
**Contexto:** O pipeline MFCC + DTW atual reconhece falsos positivos em batidas e sons aleatórios. Treinamento com ~10-15 amostras se mostrou ineficaz. Objetivo: identificar a palavra "ligar" de forma confiável com ESP32 + INMP441.

---

## Diagnóstico do Problema Atual

### Bug Crítico: `generate_templates.py:20`

```python
# ERRADO — seleciona os N templates com MAIOR distância dos outros (outliers)
idx = np.argsort(dists)[-n:]

# CORRETO — seleciona os N mais próximos do centróide (representativos)
idx = np.argsort(dists)[:n]
```

Templates de outliers como referência → matching inconsistente → qualquer coisa passa.

### Janela de Detecção Longa Demais

- Janela atual: **1 segundo** (98 frames)
- Duração típica de "ligar": **~0.4–0.5s**
- Resultado: ~50% da matriz MFCC é silêncio/ruído antes/depois da palavra
- O DTW alinha esse silêncio de forma não-determinística → falsos positivos

### VAD Insuficiente

- RMS puro com threshold 300 — qualquer impacto (batida, palma) passa
- Batidas têm energia alta concentrada em banda ampla, similar ao onset consonantal
- Falta discriminação espectral no VAD

### Sem Modelo Negativo

- Sistema só pergunta "quão parecido com 'ligar'?" — nunca "quão improvável é que isso seja voz?"
- Um ruído aleatório sempre vai ter alguma distância DTW; sem background model, não há referência de rejeição

### CMVN Parcial

- Implementação atual: só CMN (subtrai média dos coeficientes)
- CVN (normalização de variância) ausente
- Ambientes com ruído variável distorcem os features sem CVN

---

## Caminhos de Melhoria

### Caminho A — Correções no Pipeline Atual (curto prazo)

Menor esforço, máximo impacto antes de mudar arquitetura.

| Melhoria | Impacto | Esforço | Prioridade |
|---|---|---|---|
| Corrigir bug `select_templates` | Crítico | Trivial | 1 |
| Reduzir janela para 0.5s (8000→ amostras, 48→98 frames) | Alto | Baixo | 2 |
| CMVN completo (subtrai variância) | Médio | Baixo | 3 |
| VAD com ZCR + energia em banda voz (300–3400 Hz) | Médio | Médio | 4 |
| Delta-MFCC e delta-delta-MFCC | Alto | Médio | 5 |
| Background model | Alto | Médio | 6 |

#### Delta-MFCC

Adiciona derivada temporal dos coeficientes MFCC como features extras:

```
Δc[t] = (c[t+1] - c[t-1]) / 2
ΔΔc[t] = Δc[t+1] - Δc[t-1]
```

Feature final por frame: 39 coefs (13 + 13Δ + 13ΔΔ) em vez de 13.

**Por que ajuda:** "ligar" tem padrão de onset consonantal (`/l/`) e transição vogal-oclusiva (`/i/-/g/`) bem distintos. Batidas têm delta perto de zero (energia surge instantaneamente, sem dinâmica de fala). Aumenta custo DTW de O(N²×13) para O(N²×39) — ainda cabe na ESP32.

#### Background Model

1. Gravar 20–30 samples de ruído ambiente, palmas, barulhos genéricos
2. Extrair MFCCs desses samples
3. Durante detecção, calcular DTW contra templates da palavra **e** contra background
4. Aceitar apenas se `dist_palavra < threshold_palavra` **e** `dist_palavra < dist_background * fator`

Isso adiciona um segundo critério de decisão baseado em plausibilidade de voz.

#### VAD Melhorado

```c
// Combinar RMS com Zero Crossing Rate
float zcr = 0;
for (int i = 1; i < N; i++)
    zcr += (buf[i] >= 0) != (buf[i-1] >= 0);
zcr /= N;

// Voz tem ZCR moderado (0.01–0.1) e energia concentrada em 300–3400 Hz
// Batidas têm ZCR alto (muitas transições de fase) e energia broadband
bool is_voice = (rms > VAD_RMS_THRESHOLD) && (zcr < ZCR_MAX);
```

---

### Caminho B — Edge Impulse + TFLite Micro (médio prazo)

Plataforma online gratuita para treinar modelos de KWS para microcontroladores.

**Fluxo:**
1. Coletar 50–100 amostras de "ligar" (estrutura já existe no projeto)
2. Coletar 50–100 amostras de "background" (ruído, outras palavras, silêncio)
3. Upload para Edge Impulse Studio
4. Treinar CNN 1D sobre MFCCs com data augmentation automático (noise, pitch, speed)
5. Exportar biblioteca C para ESP32 (`edge-impulse-sdk`)
6. Integrar no firmware substituindo o bloco MFCC+DTW

**Vantagens:**
- Aprende generalização real (diferentes vozes, velocidades, ambientes)
- Classe "unknown" e "background noise" nativas — rejeição robusta
- FAR esperado < 5%, FRR < 10% com 50+ amostras

**Limitações:**
- RAM: ~80–120KB para inferência. Com Wi-Fi ativo (~150KB livres), pode ficar apertado
- Sem Wi-Fi ativa fica confortável (~300KB livres)
- Requer reflash para trocar o modelo
- Não roda "ao vivo" — precisa de treinamento offline

**Compatibilidade:** ESP32-D0WD (hardware atual) sem PSRAM. Modelos menores cabem.

---

### Caminho C — ESP-SR da Espressif

Biblioteca oficial da Espressif com:
- **AFE (Audio Front End):** filtro de ruído adaptativo, cancelamento de eco
- **WakeNet:** rede neural quantizada de wake word
- **MultiNet:** reconhecimento de comandos pós-wake

**Limitações para o projeto atual:**
- Requer ESP32-S3 para WakeNet completo (ou ESP32 com PSRAM para versões menores)
- Palavras customizadas em português — processo de treinamento complexo
- Documentação focada em inglês/chinês

**Quando considerar:** upgrade de hardware para ESP32-S3, ou se precisar de produto com robustez nível Alexa.

---

### Caminho D — Porcupine (Picovoice)

SDK comercial com tier gratuito para wake word customizado.

**Prós:** performance de produto, baixíssimo FAR, suporte ESP32  
**Contras:** SDK fechado, tier gratuito limitado (3 palavras), aprovação manual da palavra

---

## Recomendação

### Ordem de execução

**Fase 1 (hoje):** Corrigir bugs e ajustar parâmetros
1. Bug `select_templates[:n]`
2. Reduzir janela para 0.5s
3. CMVN completo
4. Regravar amostras e regerar templates

**Fase 2 (se Fase 1 insuficiente):** Melhorar features e VAD
1. Delta-MFCC
2. VAD com ZCR
3. Background model

**Fase 3 (se Fase 2 insuficiente):** Edge Impulse
1. Coletar 100+ amostras de "ligar" + 100 de background
2. Treinar e exportar modelo

**Fase 4 (decisão de produto):** ESP-SR ou Porcupine com hardware upgrade

---

## Parâmetros Atuais vs. Recomendados

| Parâmetro | Atual | Recomendado |
|---|---|---|
| Janela MFCC | 1s (98 frames) | 0.5s (48 frames) |
| N_FRAMES | 98 | 48 |
| N_COEFS | 13 (MFCC) | 39 (MFCC + Δ + ΔΔ) |
| DTW_WINDOW | 40 | 10 (25% de 48) |
| DTW_THRESHOLD | 12.0 | Calibrar pós-correção |
| VAD | RMS > 300 | RMS + ZCR + banda voz |
| select_templates | `[-n:]` outliers | `[:n]` centróide |
| Background model | Não existe | Adicionar |

---

## Referências

- Furui, S. (1986). *Speaker-independent isolated word recognition based on emphasized spectral dynamics.* — Delta-MFCC
- Rabiner & Sambur (1975). *Endpoint detection via energy + ZCR* — VAD melhorado
- Edge Impulse documentation: https://docs.edgeimpulse.com/docs/tutorials/responding-to-your-voice
- esp-sr: https://github.com/espressif/esp-sr
