# ESP32 Audio Recorder — Decisões de Interview

**Data:** 2026-05-20
**Escopo:** Sistema POC onde o ESP32 captura áudio via botão físico, e uma aplicação web recebe, armazena e reproduz as gravações — demonstrando fluxo completo de áudio de hardware embarcado até o browser.
**Fonte:** Discussão informal

---

## Decisões

### Arquitetura de Transporte

- ESP32 permanece como servidor HTTP/WebSocket (porta 80)
- Browser conecta diretamente na ESP32 via `ws://<IP>/record`
- Sem backend intermediário
- **Rationale:** Foco da POC é o fluxo de áudio (captura → transferência → storage), não infraestrutura. Sem servidor para manter.

### Timing de Envio de Dados

- Usar endpoint `/record` existente: grava no buffer enquanto botão pressionado, envia WAV completo ao soltar
- Endpoint `/stream` (PCM raw em tempo real) fora do escopo desta feature
- Browser recebe `ArrayBuffer` via WebSocket → converte para `Blob` → armazena
- **Rationale:** WAV pronto elimina reassemblar PCM no browser. Arquivo imediatamente reproduzível com `<audio>`. Menor complexidade de cliente.

### Armazenamento no Cliente

- **IndexedDB** para persistir gravações
- Armazenar `Blob` diretamente (sem base64 overhead)
- Cada entrada: `{ id, timestamp, duration, blob, name }`
- **Rationale:** WAV de 16s ≈ 500 KB. localStorage exigiria base64 (+33%) e estouraria com ~10 gravações. IndexedDB armazena Blob nativo, capacidade em centenas de MB.

### Stack da Aplicação Web

- **React + TypeScript + Vite** (`npm create vite@latest -- --template react-ts`)
- Aplicação frontend estática — sem servidor Node/backend
- Projeto web coexiste no mesmo repositório do firmware (monorepo)
- **Rationale:** React familiar, ecossistema com exemplos de Web Audio API, TypeScript garante tipagem dos dados de gravação e da API IndexedDB.

---

## Discretion do Agente

- Layout e design visual da interface — manter simples e funcional para POC
- Estrutura de diretórios do projeto Vite dentro do repo
- Nome do banco IndexedDB e da object store
- Estratégia de exibição de duração (calcular do tamanho do arquivo ou do header WAV)

---

## Ideias Adiadas

- Opção B de arquitetura (ESP32 como cliente, servidor recebe áudio) — surgiu na discussão de transporte
- ESP32 servir a própria página HTML via SPIFFS (dispositivo autônomo) — surgiu na discussão de stack
- Streaming em tempo real com feedback de nível de áudio (Web Audio API + `/stream`) — surgiu na discussão de timing
- File System Access API para salvar gravações direto no disco — surgiu na discussão de storage
- Trigger de gravação pelo browser (além do botão físico) — não discutido, capturado para referência

---

## Questões Abertas

- **Versão do ESP-IDF** no ambiente de build — impacta se a API `i2s_std` está disponível (requer ≥ 5.0). Verificar antes de modificar firmware.
- **PSRAM disponível** no módulo ESP32 usado — o buffer de gravação de 512 KB faz fallback para SRAM interna se não houver PSRAM (ver CONCERNS.md C2/M2)
