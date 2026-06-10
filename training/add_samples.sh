#!/usr/bin/env bash
# Adiciona amostras de uma pasta externa para training/samples/,
# renomeando para continuar a sequência numérica existente.
#
# Uso: ./add_samples.sh <pasta_origem> <palavra>
# Exemplo: ./add_samples.sh ~/Downloads/garbage_samples garbage
#
# A pasta origem deve conter arquivos nomeados <palavra>_NNN.wav (qualquer NNN).
# Os arquivos são copiados para training/samples/ com numeração contínua.

set -euo pipefail

SAMPLES_DIR="$(cd "$(dirname "$0")/samples" && pwd)"

if [[ $# -ne 2 ]]; then
  echo "Uso: $0 <pasta_origem> <palavra>"
  exit 1
fi

SRC="$1"
WORD="$2"

if [[ ! -d "$SRC" ]]; then
  echo "Erro: pasta '$SRC' não encontrada"
  exit 1
fi

# Encontra o maior número existente para a palavra
last=0
for f in "$SAMPLES_DIR"/${WORD}_*.wav; do
  [[ -e "$f" ]] || continue
  num="${f##*_}"
  num="${num%.wav}"
  num=$((10#$num))
  (( num > last )) && last=$num
done

# Coleta e ordena os arquivos de entrada
mapfile -t files < <(find "$SRC" -maxdepth 1 -name "${WORD}_*.wav" | sort -V)

if [[ ${#files[@]} -eq 0 ]]; then
  echo "Nenhum arquivo '${WORD}_*.wav' encontrado em '$SRC'"
  exit 1
fi

echo "Último número existente: $last"
echo "Arquivos a importar: ${#files[@]}"
echo ""

next=$((last + 1))
for f in "${files[@]}"; do
  dest="$SAMPLES_DIR/$(printf '%s_%03d.wav' "$WORD" "$next")"
  cp "$f" "$dest"
  echo "  $f → $(basename "$dest")"
  next=$((next + 1))
done

echo ""
echo "Importados ${#files[@]} arquivo(s). Próximo número disponível: $next"
