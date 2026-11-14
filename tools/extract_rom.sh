#!/usr/bin/env bash
# Extrai o(s) .zip encontrados em rom/ para assets/rom_extracted/.
# O nome do zip é genérico: qualquer *.zip em rom/ é aceito.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/assets/rom_extracted"
mkdir -p "$OUT"
shopt -s nullglob
zips=("$ROOT"/rom/*.zip)
if [ ${#zips[@]} -eq 0 ]; then
    echo "Nenhum .zip em rom/" >&2
    exit 1
fi
for z in "${zips[@]}"; do
    echo "Extraindo: $(basename "$z")"
    unzip -oq "$z" -x 'readme.html' -d "$OUT"
done
ls -1 "$OUT" | wc -l | xargs echo "Arquivos:"
