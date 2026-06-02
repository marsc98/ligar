# Speech Recognition — Decisões de Entrevista

**Data:** 2026-05-21
**Escopo:** Adicionar reconhecimento de fala ao poc-microfone, identificando palavras do áudio capturado pelo ESP32 INMP441, com processamento inteiramente no browser.
**Fonte:** Discussão informal

---

## Decisões

### Onde roda o reconhecimento

- **Browser via WASM** usando Transformers.js (Hugging Face)
- Zero backend adicional, zero chave de API
- Modelo baixado do Hugging Face Hub na primeira execução e cacheado via Cache API
- Modelo inicial: `openai/whisper-tiny`
- **Rationale:** integração direta com o stack TypeScript/React existente; modelos cacheados localmente após primeiro download; vocabulário aberto com suporte sólido a pt-BR

### Engine

- **Transformers.js** — `@huggingface/transformers`
- API: `pipeline('automatic-speech-recognition', 'openai/whisper-tiny')`
- Inferência via Web Worker para não bloquear a UI thread

### Fluxo de entrada

- **Ambos os fluxos**, acionados pelos respectivos endpoints já existentes:
  - **Pós-gravação** (`/record`): ao receber o WAV completo e salvar no IndexedDB, o Blob é passado ao pipeline Whisper para transcrição do arquivo inteiro
  - **Streaming ao vivo** (`/stream`): chunks PCM raw acumulados em janelas, processados pelo Whisper com VAD simples (detecção de silêncio) para determinar quando enviar uma janela
- **Rationale:** Whisper processa melhor arquivos completos; streaming requer acumulação de chunks com heurística de silêncio

### Idioma

- Select na UI com lista curada: **pt, en, es, fr, de**
- Valor passado como `language` ao pipeline do Transformers.js
- Default: `pt` (português)
- **Rationale:** idioma conhecido melhora acurácia no modelo tiny; select permite flexibilidade sem listar os ~99 idiomas do Whisper

### Onde aparece o resultado

- **Pós-gravação:** transcrição persistida no IndexedDB junto com a gravação + exibida na UI ao lado de cada item na lista
- **Streaming:** painel ao vivo na UI exibindo texto em tempo real; sem persistência (fluxo efêmero por natureza)

---

## Agent's Discretion

- Tamanho do chunk de acumulação para o streaming (ms de áudio por janela) — usar boa prática para Whisper (~3-5s de contexto)
- Estratégia de VAD para o streaming — amplitude threshold simples é suficiente para PoC
- Layout exato do select de idioma e do painel de streaming — manter consistência visual com o design atual

---

## Deferred Ideas

- Troca de modelo (tiny → small → base) via UI — surgiu durante discussão do engine
- Servidor local Python como fallback de qualidade superior — surgiu durante discussão de onde roda
- ESP32-S3 on-device com esp-sr para wake word — surgiu durante discussão de onde roda

---

## Open Questions

- O streaming Whisper no browser pode ter latência imprevisível dependendo do hardware do cliente — validar com dispositivo real durante implementação
