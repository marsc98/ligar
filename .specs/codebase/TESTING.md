# Testing Infrastructure

**Analisado:** 2026-06-10

## Estado Atual

**Sem infraestrutura de testes.** Nenhum arquivo de teste, nenhum framework configurado, nenhum CI/CD.

## Verificação Manual (procedimento atual)

```
1. make firmware-build && make firmware-flash PORT=/dev/ttyUSB0
2. make firmware-monitor — confirmar:
   - IP atribuído
   - "Sistema pronto. Click para gravar/transmitir."
   - LED GPIO 2 acende ao pressionar botão
3. make web-dev → abrir browser → informar IP
4. Gravação: conectar /record → pressionar → falar → pressionar → ouvir gravação
5. Transcrição: aguardar modelo Whisper carregar → transcrição aparece na gravação
6. Stream: conectar /stream → pressionar botão → falar → waveform/FFT no canvas
7. Monitor KWS: aba Monitor → verificar heartbeats → falar "ligar" → confirmar detecção + LED pisca
8. LED RGB: falar "ligar" → falar "vermelho" (dentro de 2s) → confirmar LED RGB vermelho
9. Threshold: ajustar slider → falar palavra → confirmar mudança de sensibilidade
10. Coleta: aba Coleta → palavra="ligar" → iniciar → pressionar botão repetidamente
11. Treinamento: make train WORD=ligar && make train-templates && make firmware-build
```

## Test Coverage Matrix

| Camada | Tipo de Teste Atual | Risco |
|---|---|---|
| Conversão I2S 32→16-bit | Nenhum | Alto — regressão silenciosa |
| WAV header (`buildWavHeader`) | Nenhum | Médio |
| Machine de estados firmware | Nenhum | Alto |
| KWS MFCC+MLP (firmware) | Nenhum | Alto — parâmetros críticos |
| `firmware_mfcc.py` vs `mfcc.c` | Nenhum | Alto — divergência = zero detecções |
| `firmware_mlp.py` vs `mlp.c` + `weights.h` | Nenhum | Alto — divergência = predições erradas |
| `extract_features.py` alinhamento onset | Nenhum | Alto — bug documentado |
| KWS 2-estágios (IDLE→AWAIT_COLOR→IDLE) | Nenhum | Alto — lógica de timeout e transições |
| `ledc_driver.c` (LEDC init + set_color) | Nenhum | Médio |
| `useConnection` (WS + montagem WAV) | Nenhum | Alto |
| `useStream` (buffer, RMS, flush) | Nenhum | Alto |
| `useWhisper` (fila, worker) | Nenhum | Médio |
| `lib/fft.ts` (FFT Cooley-Tukey) | Nenhum | Médio |
| `lib/db.ts` (IndexedDB CRUD) | Nenhum | Médio |
| `lib/wav.ts` (`assemblePcmToWav`) | Nenhum | Médio |
| `MonitorTab` (parsing JSON + threshold) | Nenhum | Baixo |
| `useCollection` (coleta contínua) | Nenhum | Baixo |

## Gate Check Commands

| Gate | Comando |
|---|---|
| Type check frontend | `cd web && tsc --noEmit` |
| Lint frontend | `cd web && npm run lint` |
| Build frontend | `cd web && npm run build` |
| Build firmware | `idf.py build` (ou `make firmware-build`) |
| Treinamento MLP | `training/.venv/bin/python3 training/train_mlp.py` |
| Paridade Python↔C (MLP) | `training/.venv/bin/python3 training/firmware_mlp.py <wav>` |

Não há testes automatizados — todos os gates são estáticos (build + lint).

## Estratégia Sugerida

| Nível | Ferramenta | O que testar |
|---|---|---|
| Host unit (firmware) | Unity (ESP-IDF nativo) | `mfcc_compute`, `mlp_infer`, `i2s_read_16bit`, máquina de estados |
| Host unit (web) | Vitest | `buildWavHeader`, `assemblePcmToWav`, `lib/fft.ts`, `lib/db.ts` |
| Paridade Python↔C (MFCC) | pytest | Comparar `firmware_mfcc.py` vs saída de `mfcc_compute` em C com mesmo input |
| Paridade Python↔C (MLP) | pytest | Comparar `firmware_mlp.py` vs `mlp_infer` em C com mesmos pesos e input |
| Integração WS (web) | Vitest + mock WS | `useConnection` states, `useStream` RMS flush |
| E2E KWS | pytest + websockets | Injetar WAV pelo /stream → verificar detecção no /monitor |
