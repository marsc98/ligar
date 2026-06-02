---
name: coleta-monitor-integration-context
description: Decisões arquiteturais e de implementação para a integração Monitor+Coleta e reorganização modular do poc-microfone
metadata:
  type: project
---

# Integração Monitor+Coleta — Decisões da Entrevista

**Data:** 2026-05-24
**Escopo:** Reorganizar poc-microfone em monolito modular (`firmware/`, `training/`, `web/`) e implementar as funcionalidades do guide.md (abas Monitor + Coleta + pipeline KWS no firmware).
**Fonte:** `guide.md` + entrevista arquitetural 2026-05-24

---

## Decisões

### 1. Estrutura ESP-IDF com `firmware/`

- Mover `main/` → `firmware/main/`
- Atualizar `CMakeLists.txt` raiz com `list(APPEND EXTRA_COMPONENT_DIRS "firmware")` — padrão idêntico ao poc-identificador
- O ESP-IDF é invocado normalmente da raiz; `EXTRA_COMPONENT_DIRS` resolve o componente `main` em `firmware/main/`
- **Rationale:** elimina divergência estrutural entre os dois projetos; padrão já validado no poc-identificador

### 2. Scripts de Training

- Copiar de `poc-identificador/training/` para `poc-microfone/training/`:
  - `firmware_mfcc.py`, `extract_features.py`, `generate_templates.py`, `requirements.txt`
  - Diretórios `samples/` e `features/` com `.gitkeep` (sem dados)
- Ajustar `DURATION_S = 1.5` em `firmware_mfcc.py`
- **Rationale:** poc-microfone fica com pipeline completo e independente desde o dia 1; sem WAVs no histórico git

### 3. poc-identificador-de-palavras

- Manter intocado como referência histórica — nenhuma alteração no repo
- **Rationale:** evita escopo; poc-microfone absorve as features que precisa sem acoplamento

### 4. Orquestração entre módulos

- **Makefile raiz** com targets por módulo e pipelines combinados:
  - Individuais: `firmware-build`, `firmware-flash`, `firmware-monitor`, `firmware-clean`, `web-dev`, `web-build`, `train`, `train-templates`
  - Combinados: `pipeline WORD=<palavra>` (train → templates → build), `flash WORD=<palavra> PORT=<porta>` (train → templates → build → flash)
  - Setup: `setup` (instala deps Python + npm)
- **README raiz** com diagrama do fluxo cross-módulo (`training/ → firmware/main/templates.h → firmware/ build`)
- **Rationale:** POC pessoal — sem camada extra de orquestração além de Makefile; documenta a única dependência real entre módulos

### 5. templates.h inicial

- Copiar `poc-identificador/firmware/main/templates.h` para `firmware/main/templates.h` como placeholder
- Adicionar comentário no arquivo indicando que é exemplo e deve ser regerado com `make train-templates` após coletar amostras reais
- **Rationale:** firmware KWS compila e endpoint `/monitor` funciona desde o primeiro build; feedback loop imediato

---

## Discretion do Agente

- Layout exato das abas (tabs fixos no topo, sidebar, etc.) — manter consistência visual com o estilo atual do poc-microfone
- Fragmento final da coleta: se < 8000 amostras (0.5s), descartar; limiar ajustável
- Targets exatos do Makefile e flags de cada comando — seguir convenções do projeto

---

## Estrutura Final Esperada

```
poc-microfone/
├── CMakeLists.txt          # EXTRA_COMPONENT_DIRS "firmware" + project()
├── Makefile                # Targets por módulo + pipelines combinados
├── README.md               # Diagrama de fluxo + instruções por módulo
├── sdkconfig
├── .gitignore
├── firmware/               # ← novo (era main/)
│   └── main/
│       ├── CMakeLists.txt
│       ├── poc-microfone.c
│       ├── wifi_config.h
│       ├── wifi_config.h.example
│       ├── mfcc.c          # ← novo (copiado do poc-identificador)
│       ├── mfcc.h          # ← novo
│       ├── dtw.c           # ← novo
│       ├── dtw.h           # ← novo
│       └── templates.h     # ← novo (placeholder do poc-identificador)
├── training/               # ← novo (copiado do poc-identificador)
│   ├── firmware_mfcc.py    # DURATION_S = 1.5
│   ├── extract_features.py
│   ├── generate_templates.py
│   ├── requirements.txt
│   ├── samples/.gitkeep
│   └── features/.gitkeep
├── web/                    # sem mudança estrutural
│   └── src/
│       ├── hooks/
│       │   └── useCollection.ts  # ← novo
│       ├── components/
│       │   ├── MonitorTab.tsx    # ← novo
│       │   └── CollectionTab.tsx # ← novo
│       └── types.ts              # campo collection? em Recording
└── .specs/
    └── features/
        └── coleta-monitor-integration/
            ├── guide.md    # spec detalhada da feature
            └── context.md  # este arquivo
```

---

## Ideias Adiadas

- **Deprecar poc-identificador** com README "migrado para poc-microfone" — fora do escopo atual
- **Mesclar histórico git dos dois repos** via `git subtree` — alto risco, escopo muito amplo
- **Dropdown de palavras pré-cadastradas** na Coleta baseado no `templates.h` atual

---

## Questões Abertas

Nenhuma. Todas as decisões foram tomadas. O `guide.md` cobre a spec de implementação completa.
