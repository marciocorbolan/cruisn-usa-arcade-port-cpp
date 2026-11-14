#!/usr/bin/env bash
# Desmonta o programa do TMS320C31 a partir de um endereço de PALAVRA.
# Uso: tools/disasm.sh <endereco_palavra_hex> [quantidade]
#   ex.: tools/disasm.sh 0x4AD5 64
set -euo pipefail
if [ $# -lt 1 ]; then
    echo "Uso: tools/disasm.sh <endereco_palavra_hex> [quantidade]   (ex.: tools/disasm.sh 0x4AD5 64)" >&2
    exit 2
fi
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/assets/rom_extracted/program.bin"
[ -f "$BIN" ] || python3 -I "$ROOT/tools/interleave_program.py" >/dev/null
addr=$(( $1 ))
count=${2:-64}
unidasm "$BIN" -arch tms320c3x -skip $(( addr * 4 )) -basepc "$(printf '0x%x' "$addr")" -count "$count"
