# Speech Recognition — Especificação

## Problem Statement

O projeto captura e armazena áudio, mas o conteúdo falado é opaco — para saber o que foi dito é preciso reproduzir manualmente cada gravação. Adicionar transcrição automática torna o áudio pesquisável e consumível sem reprodução.

## Proposed Solution

Integrar Transformers.js (Whisper tiny via ONNX Runtime Web) diretamente no frontend React. O modelo roda no browser via WASM, sem backend adicional e sem chave de API. A transcrição é disparada automaticamente ao final de cada gravação (`/record`) e em janelas ao vivo durante o streaming (`/stream`). O idioma é configurável via select na UI.

## Goals

- [ ] Transcrever automaticamente cada gravação ao ser recebida pelo endpoint `/record`
- [ ] Persistir a transcrição no IndexedDB junto com o `Recording`
- [ ] Exibir a transcrição abaixo de cada item na lista de gravações
- [ ] Permitir ao usuário selecionar o idioma (pt, en, es, fr, de) antes de transcrever
- [ ] Exibir transcrição ao vivo durante modo streaming (`/stream`) em painel dedicado

## Out of Scope

| Feature | Razão |
|---------|-------|
| Troca de modelo (tiny → small → base) via UI | Deferred — aumenta complexidade de UX sem ganho claro na PoC |
| Servidor local Python como backend de transcrição | Deferred — escolha foi WASM no browser |
| ESP32 on-device com esp-sr / wake word | Deferred — requer hardware diferente (ESP32-S3) |
| Indexação/busca de gravações por transcrição | Fora do escopo desta feature |
| Re-transcrição manual de gravação existente | Fora do escopo desta feature |

---

## User Stories

### P1: Transcrição automática pós-gravação ⭐ MVP

**User Story:** Como usuário, quero que cada gravação seja transcrita automaticamente ao ser recebida para não precisar reproduzir o áudio para saber o que foi dito.

**Why P1:** Vertical slice completo: trigger → modelo → persistência → exibição. Sem isso a feature não existe.

**Acceptance Criteria:**

1. WHEN o endpoint `/record` finaliza e o WAV é salvo no IndexedDB THEN o sistema SHALL disparar a transcrição via pipeline Whisper (Transformers.js) em background
2. WHEN a transcrição é concluída THEN o sistema SHALL atualizar o `Recording` no IndexedDB com o campo `transcription: string`
3. WHEN a transcrição é concluída THEN o sistema SHALL exibir o texto transcrito abaixo do item correspondente na `RecordingList`
4. WHEN o modelo Whisper ainda não está cacheado THEN o sistema SHALL baixá-lo do Hugging Face Hub antes de transcrever (uma única vez; fica cacheado via Cache API)
5. WHEN uma gravação com `transcription` já salvo é carregada do IndexedDB THEN o sistema SHALL exibir o texto sem re-transcrever
6. WHEN a transcrição está em andamento THEN o sistema SHALL exibir indicador de progresso no item da gravação (ex: "Transcrevendo...")
7. WHEN a transcrição falha (erro de modelo, timeout) THEN o sistema SHALL exibir mensagem de erro no item e não derrubar a gravação

**Independent Test:** Gravar 3s de fala em pt-BR → soltar botão → aguardar → texto transcrito aparece na lista e persiste após reload da página.

---

### P1: Seletor de idioma ⭐ MVP

**User Story:** Como usuário, quero selecionar o idioma da transcrição para que o Whisper use o modelo correto para minha fala.

**Why P1:** Sem isso a acurácia em pt-BR é subótima (Whisper detecta automaticamente, mas erroneamente em clips curtos).

**Acceptance Criteria:**

1. WHEN a UI é carregada THEN o sistema SHALL exibir um `<select>` com as opções: Português (pt), English (en), Español (es), Français (fr), Deutsch (de)
2. WHEN a UI é carregada THEN o idioma padrão SHALL ser `pt` (Português)
3. WHEN o usuário seleciona um idioma THEN o sistema SHALL passar `language: '<código>'` ao pipeline do Transformers.js em todas as transcrições subsequentes
4. WHEN o idioma é alterado THEN gravações já transcritas NÃO SHALL ser re-transcritas automaticamente
5. WHEN o idioma selecionado é aplicado ao streaming ao vivo THEN o mesmo valor SHALL ser usado como `language` nas janelas enviadas ao Whisper

**Independent Test:** Selecionar "en" → gravar fala em inglês → transcrição retorna texto em inglês com acurácia superior ao que retornaria com `language: 'pt'`.

---

### P2: Indicador de download do modelo

**User Story:** Como usuário, quero saber quando o modelo Whisper está sendo baixado para entender por que a transcrição demora na primeira vez.

**Why P2:** UX importante mas não bloqueia o MVP — a transcrição funciona mesmo sem o indicador.

**Acceptance Criteria:**

1. WHEN o modelo ainda não está em cache e é iniciado o download THEN o sistema SHALL exibir banner/toast informando "Baixando modelo de reconhecimento de fala... (primeira vez)"
2. WHEN o download do modelo é concluído THEN o sistema SHALL remover o banner/toast
3. WHEN o modelo já está em cache (Cache API) THEN o sistema SHALL NÃO exibir o banner

**Independent Test:** Limpar Cache API do browser → gravar → banner de download aparece e desaparece ao concluir.

---

### P2: Transcrição ao vivo no modo streaming

**User Story:** Como usuário, quero ver a transcrição aparecer em tempo real enquanto falo no modo streaming para ter feedback imediato do que está sendo reconhecido.

**Why P2:** Fluxo mais complexo (acumulação de chunks + VAD) — depende do P1 estar estável.

**Acceptance Criteria:**

1. WHEN o usuário conecta ao endpoint `/stream` THEN o sistema SHALL abrir um segundo WebSocket para `ws://<ip>/stream` em modo PCM raw
2. WHEN chunks PCM chegam via `/stream` THEN o sistema SHALL acumulá-los em buffer até atingir ~4s de áudio (≈ 128 000 bytes a 16kHz 16-bit mono)
3. WHEN o buffer acumulado atinge 4s OU a amplitude RMS dos últimos 512 samples cai abaixo de threshold (silêncio) THEN o sistema SHALL enviar a janela ao pipeline Whisper para transcrição
4. WHEN a transcrição de uma janela é concluída THEN o sistema SHALL exibir o texto em painel ao vivo na UI, concatenando com o resultado anterior da sessão
5. WHEN a conexão `/stream` é encerrada THEN o sistema SHALL limpar o painel ao vivo e o buffer acumulado
6. WHEN o streaming está ativo THEN a transcrição ao vivo SHALL usar o mesmo idioma selecionado no select

**Independent Test:** Conectar ao `/stream` → falar frases → texto aparece no painel em tempo real com latência ≤ 6s por janela.

---

## Edge Cases

- WHEN o WAV recebido tem duração < 0.5s THEN o sistema SHALL pular a transcrição (áudio insuficiente para Whisper)
- WHEN o browser não suporta WebAssembly THEN o sistema SHALL exibir mensagem "Reconhecimento de fala não suportado neste browser" e desabilitar o select
- WHEN o IndexedDB falha ao salvar a transcrição THEN o sistema SHALL exibir erro e manter a gravação sem transcrição (não reverter o save do áudio)
- WHEN múltiplas gravações chegam em sequência rápida THEN o sistema SHALL enfileirar as transcrições (pipeline Whisper não é re-entrante)
- WHEN o usuário fecha/recarrega a página enquanto a transcrição está em andamento THEN o sistema SHALL perder o resultado silenciosamente (sem dado corrompido no IndexedDB)

---

## Requirement Traceability

| Requirement ID | Story | Fase | Status |
|----------------|-------|------|--------|
| SR-01 | P1: Transcrição pós-gravação — trigger ao salvar WAV | Design | Pending |
| SR-02 | P1: Transcrição pós-gravação — pipeline Whisper em Web Worker | Design | Pending |
| SR-03 | P1: Transcrição pós-gravação — persistência no IndexedDB | Design | Pending |
| SR-04 | P1: Transcrição pós-gravação — exibição na RecordingList | Design | Pending |
| SR-05 | P1: Transcrição pós-gravação — indicador "Transcrevendo..." por item | Design | Pending |
| SR-06 | P1: Transcrição pós-gravação — error handling | Design | Pending |
| SR-07 | P1: Seletor de idioma — select com 5 opções, default pt | Design | Pending |
| SR-08 | P1: Seletor de idioma — language passado ao pipeline | Design | Pending |
| SR-09 | P2: Download do modelo — banner de progresso | Design | Pending |
| SR-10 | P2: Streaming — WebSocket /stream + acumulação de chunks | Design | Pending |
| SR-11 | P2: Streaming — VAD por amplitude RMS | Design | Pending |
| SR-12 | P2: Streaming — painel ao vivo na UI | Design | Pending |

**Cobertura:** 12 requisitos, 0 mapeados para tasks, 12 pendentes ⚠️

---

## Success Criteria

- [ ] Gravar fala em pt-BR de 3s → transcrição correta exibida e persistida em ≤ 10s
- [ ] Selecionar idioma "en" → gravar em inglês → transcrição em inglês
- [ ] Recarregar página → transcrições anteriores aparecem junto com as gravações
- [ ] Conectar ao `/stream` → falar → texto aparece no painel ao vivo com latência ≤ 6s por janela
- [ ] Nenhuma chave de API configurada → sistema funciona completamente offline após carga inicial do modelo
