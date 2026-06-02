.DEFAULT_GOAL := help

PORT ?= /dev/ttyUSB0
BAUD ?= 115200

.PHONY: help setup \
        firmware-build firmware-flash firmware-monitor firmware-clean \
        web-dev web-build \
        train train-templates \
        pipeline flash

help:
	@echo "Targets disponíveis:"
	@echo ""
	@echo "  setup              Instala dependências Python e Node"
	@echo ""
	@echo "  firmware-build     Compila o firmware (idf.py build)"
	@echo "  firmware-flash     Flasheia o firmware (PORT=$(PORT))"
	@echo "  firmware-monitor   Monitor serial (PORT=$(PORT))"
	@echo "  firmware-clean     Remove artefatos de build"
	@echo ""
	@echo "  web-dev            Servidor de desenvolvimento web"
	@echo "  web-build          Build de produção web"
	@echo ""
	@echo "  train WORD=<palavra>       Extrai features de training/samples/<palavra>_*.wav"
	@echo "  train-templates            Gera firmware/main/templates.h"
	@echo ""
	@echo "  pipeline WORD=<palavra>    train + train-templates + firmware-build"
	@echo "  flash WORD=<palavra> PORT= pipeline + firmware-flash"
	@echo ""
	@echo "  Exemplo: make pipeline WORD=ligar"

setup:
	pip3 install -r training/requirements.txt
	cd web && npm install

firmware-build:
	idf.py build

firmware-flash:
	idf.py -p $(PORT) flash

firmware-monitor:
	idf.py -p $(PORT) monitor

firmware-clean:
	idf.py fullclean

web-dev:
	cd web && npm run dev

web-build:
	cd web && npm run build

guard-%:
	@[ "${$*}" ] || (echo "Erro: $* é obrigatório. Uso: make $(MAKECMDGOALS) $*=<valor>"; exit 1)

train: guard-WORD
	python3 training/extract_features.py --word $(WORD)

train-templates:
	cd training && /home/marco/.espressif/python_env/idf6.1_py3.12_env/bin/python3 generate_templates.py --words ligar garbage desligar vermelho verde azul amarelo ciano magenta laranja roxo branco

pipeline: guard-WORD train train-templates firmware-build

flash: guard-WORD pipeline
	idf.py -p $(PORT) flash
