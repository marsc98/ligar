# Identificação de Palavras: ESP32 (MFCC + DTW) vs. Modelos de Reconhecimento de Voz

> **Contexto:** Este documento analisa e contrasta dois paradigmas para identificação de palavras em áudio — o pipeline clássico implementado no firmware deste projeto (MFCC + DTW no ESP32) e a abordagem baseada em redes neurais profundas (end-to-end ASR, e.g., Whisper/wav2vec 2.0). O objetivo é aprofundar a compreensão técnica e acadêmica de *como* cada sistema efetivamente reconhece uma palavra.

---

## Índice

1. [Visão Geral dos Paradigmas](#1-visão-geral-dos-paradigmas)
2. [Pipeline ESP32: MFCC + DTW](#2-pipeline-esp32-mfcc--dtw)
   - 2.1 [Aquisição e Pré-processamento do Sinal](#21-aquisição-e-pré-processamento-do-sinal)
   - 2.2 [Extração de Features: MFCC](#22-extração-de-features-mfcc)
   - 2.3 [Normalização: CMVN](#23-normalização-cmvn)
   - 2.4 [Comparação Temporal: DTW](#24-comparação-temporal-dtw)
   - 2.5 [Detecção e Decisão](#25-detecção-e-decisão)
3. [Pipeline de Modelo ASR Neural](#3-pipeline-de-modelo-asr-neural)
   - 3.1 [Representação de Entrada](#31-representação-de-entrada)
   - 3.2 [Arquitetura Transformer Encoder](#32-arquitetura-transformer-encoder)
   - 3.3 [Decoder e Geração de Tokens](#33-decoder-e-geração-de-tokens)
   - 3.4 [Como o Modelo "Identifica" uma Palavra](#34-como-o-modelo-identifica-uma-palavra)
4. [Comparação Técnica Direta](#4-comparação-técnica-direta)
5. [O que Constitui uma "Palavra" em Cada Sistema](#5-o-que-constitui-uma-palavra-em-cada-sistema)
6. [Análise Matemática Comparada](#6-análise-matemática-comparada)
7. [Referências e Leituras Complementares](#7-referências-e-leituras-complementares)

---

## 1. Visão Geral dos Paradigmas

| Dimensão | ESP32 (MFCC + DTW) | Modelo ASR Neural |
|---|---|---|
| **Paradigma** | Template matching clássico | Aprendizado de representação end-to-end |
| **"Memória" de palavra** | Templates MFCC gravados no firmware | Pesos de rede neural treinados em milhares de horas |
| **Vocabulário** | Fixo em compilação (`templates.h`) | Aberto (BPE tokens ou fonemas) |
| **Unidade reconhecida** | Palavra inteira (1 s de áudio) | Sub-palavra (byte-pair encoding) ou fonema |
| **Invariância temporal** | DTW elastica a variações de velocidade | Atenção global sobre toda a sequência |
| **Custo computacional** | O(N² × D) por palavra | Bilhões de FLOPs por inferência |
| **Hardware** | ESP32 (~240 MHz, ~520 KB SRAM) | GPU / NPU com GB de RAM |
| **Treinamento** | Necessário (gera templates) | Não necessário em inferência |

A diferença fundamental: o ESP32 **compara padrões acústicos** contra referências; o modelo neural **decodifica representações latentes** aprendidas.

---

## 2. Pipeline ESP32: MFCC + DTW

### 2.1 Aquisição e Pré-processamento do Sinal

```
INMP441 → I2S 32-bit MSB → Shift >> 16 → Ganho × 16 → Clamp int16_t
```

O microfone INMP441 transmite dados em formato I2S Philips de 32 bits. Os 24 bits significativos ocupam os MSBs — os 8 LSBs são zeros de padding. A conversão para 16 bits é feita com:

```c
// poc-microfone.c:200
int32_t s = (int32_t)(raw32[i] >> 16) * MIC_GAIN;
```

Isso equivale a uma quantização de 16 bits com ganho linear de 16× (~24 dB), seguida de saturação aritmética (clamp) para evitar overflow em `int16_t`. O resultado alimenta um **ring buffer circular** de 16.000 amostras (1 segundo a 16 kHz).

#### Voice Activity Detection (VAD)

Antes de qualquer processamento pesado, o sistema calcula o **RMS** (*Root Mean Square*) do chunk mais recente:

```
RMS = sqrt( (1/N) × Σ x[i]² )
```

Se `RMS < 300.0`, o frame é descartado. Essa heurística simples evita desperdício de CPU em silêncio — é a versão mais básica de um VAD energético, equivalente ao detector de nível descrito por Rabiner & Sambur (1975).

---

### 2.2 Extração de Features: MFCC

Os **Mel-Frequency Cepstral Coefficients** são a representação acústica dominante em reconhecimento de fala desde Davis & Mermelstein (1980). A ideia central: transformar o espectro de potência para uma escala que aproxima a percepção auditiva humana (escala Mel), depois aplicar uma transformada cosseno para desacoplar os coeficientes.

Os parâmetros usados no firmware (`mfcc.h`):

| Parâmetro | Valor | Significado |
|---|---|---|
| `MFCC_SAMPLE_RATE` | 16000 Hz | Taxa de amostragem |
| `MFCC_FRAME_LEN` | 400 amostras | Janela de 25 ms |
| `MFCC_HOP` | 160 amostras | Passo de 10 ms (sobreposição de 60%) |
| `MFCC_N_FFT` | 512 | Tamanho da FFT (512 > 400, zero-padded) |
| `MFCC_N_MELS` | 26 | Número de filtros Mel |
| `MFCC_N_COEFS` | 13 | Coeficientes MFCC por frame |
| `MFCC_N_FRAMES` | 98 | Frames por janela de 1 s |

#### Passo 1 — Pré-ênfase

```c
// mfcc.c:113
re[i] = (curr - 0.97f * prev) * s_hann[i] / 32768.0f;
```

O filtro de pré-ênfase `H(z) = 1 - 0.97 z⁻¹` é um filtro passa-alta de primeira ordem. No domínio da frequência, ele amplifica componentes de alta frequência, compensando o decaimento espectral natural da voz (~6 dB/oitava). Isso melhora a relação sinal-ruído nas frequências altas onde as fricativas (`/s/`, `/f/`) concentram energia.

Ao mesmo tempo, aplica a **janela de Hann**:

```
w[i] = 0.5 × (1 - cos(2π × i / (N-1)))
```

A janela reduz o **vazamento espectral** (*spectral leakage*): sem ela, as bordas do frame introduzem descontinuidades que produzem artefatos em todas as frequências na FFT.

#### Passo 2 — FFT e Espectro de Potência

```c
// mfcc.c:116-120
fft_real(re, im, MFCC_N_FFT);
for (int k = 0; k < n_bins; k++) {
    power[k] = re[k]*re[k] + im[k]*im[k];
}
```

A FFT implementada é o algoritmo **Cooley-Tukey radix-2 DIF** (*Decimation-In-Frequency*) com reordenação bit-reversa. Complexidade: O(N log N). O espectro de potência `|X[k]|²` captura a distribuição de energia por frequência, descartando a fase (irrelevante para a identidade fonética).

#### Passo 3 — Banco de Filtros Mel

```c
// mfcc.c:23-49 (init_tables)
const float fmin = 300.0f;
const float fmax = 8000.0f;
```

A escala Mel é definida pela transformação logarítmica:

```
Mel(f) = 2595 × log₁₀(1 + f/700)
```

Esta fórmula empírica (O'Shaughnessy, 1987) modela a percepção de altura tonal: o ouvido humano é mais sensível a diferenças em frequências baixas do que em altas. A largura de cada filtro triangular cresce linearmente na escala Mel (logaritmicamente na frequência linear).

O banco de 26 filtros triangulares cobre 300–8000 Hz. Cada filtro tem a forma:

```
          ⎧ (f - f_left) / (f_center - f_left),  f_left  ≤ f ≤ f_center
H_m(f) = ⎨ (f_right - f) / (f_right - f_center), f_center < f ≤ f_right
          ⎩ 0,                                    otherwise
```

A energia de cada filtro é calculada e passa por logaritmo natural:

```c
// mfcc.c:128
mel_energy[m] = logf(energy + 1e-9f);
```

O `1e-9` é um *floor* numérico para evitar `log(0)`. O logaritmo serve dois propósitos: (1) comprime a faixa dinâmica, aproximando a percepção de intensidade (lei de Weber-Fechner); (2) converte a convolução no domínio do tempo em adição no domínio cepstral.

#### Passo 4 — DCT (Cepstrum Mel)

```c
// mfcc.c:51-57 (init_tables)
float norm = (c == 0) ? sqrtf(1.0f / MFCC_N_MELS)
                       : sqrtf(2.0f / MFCC_N_MELS);
s_dct[c][m] = norm * cosf((float)M_PI * c * (m + 0.5f) / MFCC_N_MELS);
```

A **DCT-II ortogonal** é aplicada ao log-espectro Mel:

```
c[n] = √(2/M) × Σ_{m=0}^{M-1} log_mel[m] × cos(π × n × (m + 0.5) / M)
```

A DCT desacopola os coeficientes que, no domínio Mel, são correlacionados (filtros adjacentes se sobrepõem). Os 13 primeiros coeficientes (C0–C12) carregam a maior parte da informação fonética: C0 representa energia total, C1–C4 o envelope espectral grosso (vogais), C5–C12 detalhes finos (consoantes).

#### Visão Geral do Pipeline MFCC

```
Áudio raw (int16)
        │
        ▼
   Pré-ênfase (α = 0.97)
        │
        ▼
   Janela Hann (25ms)
        │
        ▼
   FFT-512 → |X[k]|²
        │
        ▼
   26 Filtros Mel (300–8000 Hz)
        │
        ▼
   log(energia mel)
        │
        ▼
   DCT-II (13 coefs)
        │
        ▼
   Frame MFCC: 13 floats
        │  (repetido 98 vezes, passo 10ms)
        ▼
   Matriz [98 × 13]
```

---

### 2.3 Normalização: CMVN

```c
// mfcc.c:142-150
float mean[MFCC_N_COEFS] = {0};
for (int f = 0; f < MFCC_N_FRAMES; f++)
    for (int c = 0; c < MFCC_N_COEFS; c++)
        mean[c] += out[f * MFCC_N_COEFS + c];
for (int c = 0; c < MFCC_N_COEFS; c++)
    mean[c] /= MFCC_N_FRAMES;
for (int f = 0; f < MFCC_N_FRAMES; f++)
    for (int c = 0; c < MFCC_N_COEFS; c++)
        out[f * MFCC_N_COEFS + c] -= mean[c];
```

A **CMVN** (*Cepstral Mean and Variance Normalization*) subtrai a média de cada coeficiente ao longo do tempo. Academicamente, isso implementa a remoção do canal de transmissão: qualquer resposta espectral constante (microfone, sala, ruído de fundo estacionário) se manifesta como uma média não-nula nos MFCCs. Subtraí-la torna os features invariantes ao canal — uma forma de normalização de falante e ambiente.

A versão implementada é **CMN** (apenas média, sem normalização de variância), suficiente para keyword spotting com templates de mesmo ambiente de gravação.

---

### 2.4 Comparação Temporal: DTW

Com os 98 × 13 = 1274 floats da query e de cada template, o sistema precisa medir similaridade. A questão fundamental: **a mesma palavra falada tem duração variável** — uma fala mais lenta estica a sequência de frames. Comparação frame-a-frame (distância Euclidiana direta) falharia.

O **Dynamic Time Warping** resolve isso encontrando o alinhamento temporal ótimo entre duas sequências.

#### Formulação

```
DTW(i, j) = d(qᵢ, rⱼ) + min{
    DTW(i-1, j),    ← extensão vertical (query avança, ref pausa)
    DTW(i, j-1),    ← extensão horizontal (ref avança, query pausa)
    DTW(i-1, j-1)   ← diagonal (ambos avançam juntos)
}
```

Onde `d(qᵢ, rⱼ)` é a distância Euclidiana entre dois frames MFCC:

```c
// dtw.c:10-17
static float euclidean(const float *a, const float *b, int n) {
    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        float d = a[i] - b[i];
        sum += d * d;
    }
    return sqrtf(sum);
}
```

#### Banda de Sakoe-Chiba

```c
// dtw.c:32-33
int j_start = (i - window > 0) ? i - window : 0;
int j_end   = (i + window < n_frames) ? i + window : n_frames - 1;
```

Com `window = MFCC_N_FRAMES / 4 = 24`, apenas células dentro de ±24 frames da diagonal são calculadas. Esta restrição (Sakoe & Chiba, 1978) tem duas funções:

1. **Reduz complexidade**: de O(N²) para O(N × W), onde W = 2×window+1
2. **Evita alinhamentos fisicamente impossíveis**: uma palavra não pode ser ouvida "fora de ordem"

#### Implementação Eficiente em Memória

```c
// dtw.c:7-8
/* Rolling 2-row buffer */
static float s_rows[2][DTW_MAX_FRAMES];
```

Em vez de alocar a matriz completa N×N, o algoritmo mantém apenas **2 linhas** em memória (linha atual e anterior). Isso reduz o consumo de memória de O(N²) para O(N). Crucial para o ESP32 com apenas ~520 KB de SRAM.

#### Score Final

```c
// dtw.c:48
return s_rows[(n_frames - 1) & 1][n_frames - 1] / n_frames;
```

A distância DTW acumulada é normalizada pelo número de frames para ser comparável entre templates de tamanhos diferentes.

---

### 2.5 Detecção e Decisão

```c
// poc-microfone.c:476-491
for (int w = 0; w < KWS_N_WORDS; w++) {
    float wd = 1e9f;
    for (int t = 0; t < KWS_WORDS[w].n_templates; t++) {
        float d = dtw_distance(g_mfcc_out, KWS_WORDS[w].templates[t],
                               MFCC_N_FRAMES, MFCC_N_COEFS, MFCC_N_FRAMES / 4);
        if (d < wd) wd = d;
    }
    word_best[w] = wd;
    if (wd < best_dist) { best_dist = wd; best_word = w; }
}
bool detected = (best_word >= 0 && best_dist < g_dtw_threshold);
```

O sistema é um **classificador 1-NN** (*1-Nearest Neighbor*) no espaço de distâncias DTW:

1. Para cada palavra, computa DTW contra todos os seus templates, guarda o **mínimo** (melhor template)
2. A palavra reconhecida é a de **menor distância DTW** entre todas as palavras
3. Só é aceita se a distância for menor que `g_dtw_threshold` (800.0 por padrão, ajustável via WebSocket)

O threshold é o único mecanismo de rejeição — sem limiar, o sistema sempre detecta *alguma* palavra mesmo em silêncio. O cooldown de 1000ms previne detecções duplicadas da mesma ocorrência.

---

## 3. Pipeline de Modelo ASR Neural

### 3.1 Representação de Entrada

Modelos como **Whisper** (Radford et al., 2022) e **wav2vec 2.0** (Baevski et al., 2020) recebem áudio raw e produzem texto. A entrada para ambos parte do mesmo tipo de representação: **log-mel spectrogram**.

O Whisper usa:
- Janela: 25ms, passo: 10ms (idêntico ao ESP32)
- 80 canais Mel (vs. 26 do ESP32)
- Frequência: 16 kHz (idêntico)
- Contexto: 30 segundos (480.000 amostras vs. 16.000 do ESP32)

```
Áudio [480.000 amostras]
        │
        ▼
Log-Mel Spectrogram [3000 frames × 80 mels]
        │
        ▼
2 × Conv1d (stride=2) → reduz para 1500 frames
        │
        ▼
Positional Encoding (sinusoidal)
        │
        ▼
Transformer Encoder
```

A etapa convolucional inicial serve como **feature extractor local**: captura padrões acústicos de curta duração (onset de consoantes, formantes) antes da atenção global.

---

### 3.2 Arquitetura Transformer Encoder

O encoder Whisper tem entre 4 (Tiny) e 32 (Large) camadas idênticas de:

```
x → LayerNorm → Multi-Head Self-Attention → + residual
  → LayerNorm → FFN (MLP)                 → + residual
```

#### Multi-Head Self-Attention

Cada frame de áudio atende a **todos os outros frames** simultaneamente:

```
Attention(Q, K, V) = softmax(QKᵀ / √d_k) × V
```

Onde `Q`, `K`, `V` são projeções lineares do embedding de cada frame. O fator `1/√d_k` previne saturação do softmax.

**O que isso significa fisicamente:** o modelo aprende quais frames são relevantes entre si. Um frame no meio de uma vogal pode atender ao frame de onset da consoante anterior e ao frame de offset da consoante seguinte para inferir o contexto fonético. Isso é fundamentalmente diferente do DTW, que só compara frames posicionalmente equivalentes.

Com múltiplas "cabeças" (*heads*), o modelo aprende **relações diferentes em paralelo** — uma cabeça pode focar em periodicidade da pitch, outra em transições espectrais, outra em duração silábica.

---

### 3.3 Decoder e Geração de Tokens

O Whisper usa uma arquitetura **encoder-decoder** com decodificação autoregressiva:

```
Encoder output [1500 × d_model]
        │
        ▼ (cross-attention)
Decoder: gera um token por vez
        │
        ▼
Token 1: <|startoftranscript|>
Token 2: <|pt|>  (idioma detectado)
Token n: "li-"
Token n+1: "-gar"
Token n+2: <|endoftext|>
```

O vocabulário usa **Byte-Pair Encoding (BPE)** com ~51.000 tokens. BPE aprende iterativamente os pares de bytes mais frequentes, resultando em tokens que correspondem aproximadamente a morfemas e palavras comuns.

No decoder, cada token gerado é condicionado em:
1. Todos os tokens anteriores (self-attention causal)
2. A representação completa do áudio pelo encoder (cross-attention)

---

### 3.4 Como o Modelo "Identifica" uma Palavra

Esta é a parte mais contra-intuitiva: **o modelo não sabe o que é uma "palavra"** durante a inferência.

O processo real é:

```
1. Encoder transforma 30s de áudio em representações abstratas
2. Decoder emite tokens BPE probabilisticamente
3. Alguns tokens correspondem a palavras, outros a sub-palavras
4. "ligar" pode ser emitido como:
   - Um único token ["ligar"] (se frequente no corpus de treino)
   - Dois tokens ["li", "gar"]
   - Três tokens ["l", "ig", "ar"]
```

A "decisão" de emitir um token específico é o resultado de:

```
P(token_t | token_{1..t-1}, áudio) = softmax(W_lm × h_t)
```

Onde `h_t` é o estado oculto do decoder no passo `t`, e `W_lm` é a matriz de linguagem que mapeia o espaço latente para o vocabulário. A palavra não é "identificada" — ela **emerge** da distribuição de probabilidade sobre tokens dado o contexto acústico e linguístico.

#### Beam Search

Em inferência, o decoder não usa o token mais provável (*greedy decoding*). Usa **beam search** com k=5 hipóteses paralelas:

```
Hipótese 1: "ligar"           → log P = -0.3
Hipótese 2: "lugar"           → log P = -1.2
Hipótese 3: "lidar"           → log P = -2.1
...
Hipótese k: <outros tokens>
```

A sequência com maior log-probabilidade acumulada é selecionada.

---

## 4. Comparação Técnica Direta

### Fluxo de Identificação

```
┌─────────────────────────────────────────────────────────────────────┐
│                          ESP32 (DTW)                                │
├─────────────────────────────────────────────────────────────────────┤
│  Áudio → Ring Buffer (1s) → MFCC [98×13] → DTW vs. templates →     │
│  min_dist < threshold? → SIM: palavra detectada / NÃO: descarta     │
│                                                                     │
│  Decisão: distância no espaço de features acústicas                 │
└─────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│                       Modelo ASR (Whisper)                          │
├─────────────────────────────────────────────────────────────────────┤
│  Áudio → Log-Mel [3000×80] → Conv → Transformer Encoder →          │
│  Representação latente → Decoder autoregressivo → Tokens BPE →      │
│  Pós-processamento → Texto                                          │
│                                                                     │
│  Decisão: distribuição de probabilidade sobre vocabulário           │
└─────────────────────────────────────────────────────────────────────┘
```

### Onde a Palavra "Aparece" em Cada Sistema

| Etapa | ESP32 | Modelo ASR |
|---|---|---|
| **Input** | Ring buffer 16.000 amostras | Waveform 480.000 amostras |
| **Representação** | [98 × 13] MFCCs | [3000 × 80] log-mel |
| **Processamento** | DTW elástico vs. template fixo | Atenção multi-cabeça em toda sequência |
| **Decisão** | `best_dist < threshold` (escalar) | `argmax P(token|contexto)` (distribuição) |
| **Saída** | String fixa do enum de palavras | Sequência de tokens → string |
| **Rejeição** | Limiar DTW explícito | Probabilidade baixa → token `<unk>` ou nada |

---

## 5. O que Constitui uma "Palavra" em Cada Sistema

### No ESP32

Uma palavra é um **template MFCC**:

```
struct {
    const char *name;       // "ligar"
    const float *templates[]; // N matrizes [98×13] gravadas em flash
    int n_templates;
} KWS_WORD;
```

A palavra existe como uma **forma no espaço acústico** — uma trajetória específica de espectros mel ao longo do tempo. Reconhecer a palavra é encontrar uma trajetória de entrada suficientemente próxima desta forma de referência, considerando variações temporais via DTW.

**Não há modelo de linguagem, fonemas, ou semântica.** O sistema não sabe que "ligar" e "desligar" compartilham o sufixo "-ligar". Cada palavra é uma entidade completamente independente.

### No Modelo ASR

Uma palavra é uma **sequência de tokens** no vocabulário BPE, que por sua vez correspondem a **padrões de ativação** nos pesos do decoder. O modelo nunca "vê" palavras — ele opera em representações distribuídas.

Crucialmente, a **representação fonética** está implícita nos pesos do encoder, aprendida por exposição a horas de fala transcrita. O modelo aprende que certos padrões espectrais co-ocorrem regularmente com certas sequências de tokens.

A palavra emerge no cruzamento de dois espaços:
1. **Espaço acústico** (encoder): representações de padrões de fala
2. **Espaço linguístico** (decoder): distribuições sobre sequências válidas de tokens

---

## 6. Análise Matemática Comparada

### Distância DTW (ESP32)

Dado query `Q ∈ ℝ^{N×D}` e referência `R ∈ ℝ^{N×D}`:

```
DTW(Q, R) = (1/N) × min_{p} Σ_{(i,j)∈p} ||Q_i - R_j||₂
```

Onde `p` é um caminho de alinhamento válido com a restrição de banda Sakoe-Chiba `|i - j| ≤ W`.

Esta é uma métrica de distância no **espaço de sequências de vetores acústicos**.

### Inferência Transformer (Modelo ASR)

```
P(w | x) = P(t₁, t₂, ..., tₖ | x)
         = Π P(tᵢ | t_{<i}, f(x))
```

Onde:
- `x` é o áudio bruto
- `f(x)` é a saída do encoder Transformer
- `tᵢ` são tokens BPE
- `w` é a palavra resultante

A palavra é identificada como consequência de **maximizar a log-verossimilhança** da sequência de tokens dado o contexto acústico.

### Complexidade Comparada

| Operação | ESP32 | Modelo (Whisper Large) |
|---|---|---|
| Feature extraction | O(N × FFT + N × M × K) | O(N × FFT + N × M) |
| Matching/Encoding | O(N² × D × T) com DTW | O(L² × d_model × n_heads) |
| Memória | ~8 KB (2 linhas DTW) | ~1.5 GB (pesos float16) |
| Latência | <100 ms no ESP32 | ~1-10 s em CPU, <100ms em GPU |

Onde N=98 frames, D=13 coefs, T=n_templates; L=1500, d_model=1024, n_heads=16.

---

## 7. Referências e Leituras Complementares

### Artigos Fundamentais

| Referência | Contribuição |
|---|---|
| Davis, S. & Mermelstein, P. (1980). *Comparison of parametric representations for monosyllabic word recognition in continuously spoken sentences.* IEEE TASLP. | Introdução dos MFCCs |
| Sakoe, H. & Chiba, S. (1978). *Dynamic programming algorithm optimization for spoken word recognition.* IEEE TASLP. | DTW com banda de restrição |
| Rabiner, L. & Sambur, M. (1975). *An algorithm for determining the endpoints of isolated utterances.* Bell System Technical Journal. | VAD por energia |
| Vaswani, A. et al. (2017). *Attention is all you need.* NeurIPS. | Arquitetura Transformer |
| Radford, A. et al. (2022). *Robust speech recognition via large-scale weak supervision.* (Whisper). ICML 2023. | Modelo Whisper |
| Baevski, A. et al. (2020). *wav2vec 2.0: A framework for self-supervised learning of speech representations.* NeurIPS. | wav2vec 2.0 |
| Sennrich, R. et al. (2016). *Neural Machine Translation of Rare Words with Subword Units.* ACL. | Byte-Pair Encoding |

### Leituras para Aprofundamento

- **Rabiner, L. & Juang, B.-H. (1993).** *Fundamentals of Speech Recognition.* Prentice Hall. — Referência clássica completa sobre ASR clássico (HMM, MFCC, DTW)
- **Huang, X. et al. (2001).** *Spoken Language Processing.* Prentice Hall. — Cobertura de HMMs e sistemas híbridos
- **Graves, A. (2012).** *Supervised Sequence Labelling with Recurrent Neural Networks.* — CTC e RNNs para ASR
- **Watanabe, S. et al. (2018).** *ESPnet: End-to-End Speech Processing Toolkit.* — Framework de referência para ASR end-to-end

---

> **Síntese:** O ESP32 identifica uma palavra ao medir a proximidade geométrica entre uma trajetória MFCC observada e trajetórias de referência via DTW — é um sistema de *pattern matching* determinístico. O modelo ASR identifica palavras ao decodificar probabilisticamente representações latentes aprendidas de dados massivos — é um sistema generativo que distribui probabilidade sobre todo o vocabulário. A diferença não é apenas de escala: são epistemologias diferentes sobre o que é uma palavra.
