.DEFAULT_GOAL := help

PORT  ?= /dev/ttyUSB0
BAUD  ?= 115200
PYTHON := training/.venv/bin/python3
WORDS  ?= ligar garbage desligar vermelho verde azul amarelo laranja roxo branco

.PHONY: help setup \
        firmware-build firmware-flash firmware-monitor firmware-clean \
        web-dev web-build \
        extract extract-all train-mlp train-templates \
        pipeline pipeline-all flash

help:
	@echo "Targets disponíveis:"
	@echo ""
	@echo "  setup                      Cria venv Python, instala dependências Python e Node"
	@echo ""
	@echo "  firmware-build             Compila o firmware (idf.py build)"
	@echo "  firmware-flash             Flasheia o firmware (PORT=$(PORT))"
	@echo "  firmware-monitor           Monitor serial (PORT=$(PORT))"
	@echo "  firmware-clean             Remove artefatos de build"
	@echo ""
	@echo "  web-dev                    Servidor de desenvolvimento web"
	@echo "  web-build                  Build de produção web"
	@echo ""
	@echo "  extract WORD=<palavra>     Extrai features de training/samples/<palavra>_*.wav"
	@echo "  extract-all                Extrai features de todas as palavras (WORDS=$(WORDS))"
	@echo "  train-mlp                  Treina MLP → gera firmware/main/kws/weights.h"
	@echo "  train-templates            Gera firmware/main/kws/templates.h"
	@echo ""
	@echo "  pipeline WORD=<palavra>    extract + train-mlp + firmware-build"
	@echo "  pipeline-all               extract-all + train-mlp + firmware-build"
	@echo "  flash WORD=<palavra>       pipeline + firmware-flash"
	@echo ""
	@echo "  Exemplo: make pipeline WORD=ligar"
	@echo "  Exemplo: make pipeline-all"

setup:
	python3 -m venv training/.venv
	training/.venv/bin/pip install -r training/requirements.txt
	cd web && yarn install

firmware-build:
	cd firmware && idf.py build

firmware-flash:
	cd firmware && idf.py -p $(PORT) flash

firmware-monitor:
	cd firmware && idf.py -p $(PORT) monitor

firmware-clean:
	cd firmware && idf.py fullclean

web-dev:
	cd web && yarn dev

web-build:
	cd web && yarn build

guard-%:
	@[ "${$*}" ] || (echo "Erro: $* é obrigatório. Uso: make $(MAKECMDGOALS) $*=<valor>"; exit 1)

extract: guard-WORD
	$(PYTHON) training/extract_features.py --word $(WORD)

extract-all:
	@for word in $(WORDS); do \
		echo ">>> Extraindo features: $$word"; \
		$(PYTHON) training/extract_features.py --word $$word; \
	done

train-mlp:
	$(PYTHON) training/train_mlp.py

train-templates:
	cd training && ../$(PYTHON) generate_templates.py --words $(WORDS)

pipeline: guard-WORD extract train-mlp firmware-build

pipeline-all: extract-all train-mlp firmware-build

flash: guard-WORD pipeline
	cd firmware && idf.py -p $(PORT) flash
