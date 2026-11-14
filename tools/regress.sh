#!/usr/bin/env bash
# Regressão de ponta a ponta do port (roda DENTRO do container):
#   docker compose run --rm -T dev tools/regress.sh [--quick]
#
# 1. compila e roda a suíte de testes;
# 2. primeiro boot com CMOS nova: calibração dos controles (0x0CC0) por roteiro;
# 3. com a CMOS calibrada: 3 fichas, START, câmbio/carro/percurso e a corrida;
# 4. dois gabinetes ligados (--link master/slave), cada um com a sua CMOS (pulado com --quick);
# 5. ordem dos operandos: toda subtração/comparação do código traduzido está na lista revisada
#    (tools/check_operand_order.py e tests/operand_order_reviewed.txt).
# Em cada execução confere: nenhuma rotina pendente, nenhum PC de processo sem tradução.
# Saídas (logs e capturas PPM) ficam em build/regress/. Código de saída 0 = tudo passou.
# REA_BUILD_DIR=pasta usa outro build (ex.: build-asan, com sanitizers); as saídas vão para pasta/regress/.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
B=${REA_BUILD_DIR:-build}
OUT=$B/regress
BIN=./$B/bin/rea_cruisn
QUICK=0
[ "${1:-}" = "--quick" ] && QUICK=1
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy

fails=0
pass() { printf '  [ OK ] %s\n' "$1"; }
fail() { printf '  [FAIL] %s\n' "$1"; fails=$((fails + 1)); }
expect() {  # expect "descrição" comando...
    local what=$1
    shift
    if "$@" >/dev/null 2>&1; then pass "$what"; else fail "$what"; fi
}
# Toda execução: 0 rotinas pendentes no fim e lista de PCs não traduzidos vazia.
clean_run() {
    local log=$1
    expect "$log: 0 rotinas pendentes" grep -q 'rotinas pendentes=0$' "$log"
    expect "$log: nenhum PC de processo sem tradução" grep -qE 'PCs não traduzidos:[[:space:]]*$' "$log"
}
# Byte útil (bits 24–31) do item n da CMOS no log do --peek.
item() { grep -i "^\[0x$(printf '%06X' $((0x9C0000 + 4 * $2 + 3)))\]" "$1" | sed -E 's/.*= 0x(..).*/\1/'; }
# O mesmo em decimal; 0 se o item não aparece no log (a checagem falha com [FAIL] em vez de abortar o script).
itemdec() { local v; v=$(item "$1" "$2"); echo $((16#${v:-0})); }

rm -rf "$OUT"
mkdir -p "$OUT"

echo "== 1. Compilação e testes"
if cmake -S . -B "$B" >"$OUT/cmake.log" 2>&1 && cmake --build "$B" -j"$(nproc)" >"$OUT/build.log" 2>&1; then
    pass "compilação"
else
    fail "compilação (veja $OUT/build.log)"
    exit 1
fi
./$B/bin/rea_core_tests >"$OUT/tests.log" 2>&1
expect "suíte de testes ($(grep -c '^\[ OK \]' "$OUT/tests.log") verificações)" grep -q '^SUCESSO' "$OUT/tests.log"

echo "== 2. Primeiro boot e calibração (CMOS nova)"
CAL='test@200-210,wheel@250-450:0,test@400-410,wheel@500-700:255,test@650-660,gas@750-950:255,test@900-910'
PEEK=$(for n in 1 2 3 4 5 6 7; do printf '0x%X,' $((0x9C0000 + 4 * n + 3)); done)
$BIN --frames 1300 --cmos "$OUT/cmos.nv" --press "$CAL" --peek "${PEEK%,}" \
    --shots 100:"$OUT/calibrate.ppm" >"$OUT/calibrate.log" 2>&1
L=$OUT/calibrate.log
expect "boot terminou depois da calibração" grep -q '^RESET:' "$L"
expect "CMOS gravada em $OUT/cmos.nv (32 KiB)" test "$(stat -c %s "$OUT/cmos.nv" 2>/dev/null)" = 32768
left=$(itemdec "$L" 3); right=$(itemdec "$L" 4); gas=$(itemdec "$L" 2); rest=$(itemdec "$L" 1)
expect "volante: esquerda ($left) < centro < direita ($right)" test "$left" -lt 64 -a "$right" -gt 192
expect "acelerador: repouso ($rest) < fundo ($gas)" test "$rest" -lt 16 -a "$gas" -gt 192
clean_run "$L"

echo "== 3. Partida com a CMOS calibrada"
RACE='coin1@1100-1106,coin1@1150-1156,coin1@1200-1206,start@1300-1306,view1@1700-1706,view1@2100-2106,view1@2500-2506,view1@2900-2906,gas@3200-9999:255'
$BIN --frames 4200 --cmos "$OUT/cmos.nv" --press "$RACE" --peek 0xC8C5 \
    --shots 600:"$OUT/attract.ppm",3590:"$OUT/race_start.ppm",4190:"$OUT/race.ppm" >"$OUT/race.log" 2>&1
L=$OUT/race.log
expect "CMOS restaurada (sem nova calibração)" grep -q '(restaurada)' "$L"
polys=$(sed -nE 's/.*polígonos=([0-9]+).*/\1/p' "$L")
expect "corrida desenhada (${polys:-0} polígonos 3D)" test "${polys:-0}" -gt 100000
expect "pipeline 3D sem travas" grep -q 'travas=0$' "$L"
clean_run "$L"

if [ "$QUICK" = 0 ]; then
    echo "== 4. Dois gabinetes ligados"
    cp "$OUT/cmos.nv" "$OUT/cmos_master.nv"
    cp "$OUT/cmos.nv" "$OUT/cmos_slave.nv"
    rm -f "$OUT/link.shm"
    $BIN --frames 4200 --link master --link-file "$OUT/link.shm" --cmos "$OUT/cmos_master.nv" --press "$RACE" \
        --shots 4190:"$OUT/link_master.ppm" >"$OUT/link_master.log" 2>&1 &
    $BIN --frames 4200 --link slave --link-file "$OUT/link.shm" --cmos "$OUT/cmos_slave.nv" \
        --shots 4190:"$OUT/link_slave.ppm" >"$OUT/link_slave.log" 2>&1
    wait
    expect "Master trocou pacotes sem erro" grep -qE 'Master ok/erro=[1-9][0-9]*/0 ' "$OUT/link_master.log"
    # Os erros do Slave depois que o Master sai são timeouts de desconexão (normais); confere só os pacotes.
    m_ok=$(sed -nE 's/.*Master ok\/erro=([0-9]+)\/.*/\1/p' "$OUT/link_master.log")
    s_ok=$(sed -nE 's/.*Slave ok\/erro=([0-9]+)\/[0-9]+$/\1/p' "$OUT/link_slave.log" | tail -1)
    expect "Slave trocou os mesmos pacotes (${s_ok:-0} x ${m_ok:-0}, diferença <= 5)" \
        test "${s_ok:-0}" -gt 0 -a $(( ${s_ok:-0} - ${m_ok:-0} )) -le 5 -a $(( ${m_ok:-0} - ${s_ok:-0} )) -le 5
    clean_run "$OUT/link_master.log"
    clean_run "$OUT/link_slave.log"
fi

echo "== 5. Ordem dos operandos"
if tools/check_operand_order.py >"$OUT/operand_order.log" 2>&1; then
    pass "subtrações e comparações do código traduzido revisadas ($(sed -nE 's/.*: ([0-9]+) instruções sensíveis.*/\1/p' "$OUT/operand_order.log"))"
else
    fail "ordem dos operandos: há instruções sem revisão (veja $OUT/operand_order.log)"
fi

echo
if [ "$fails" = 0 ]; then
    echo "REGRESSÃO OK (saídas em $OUT/)"
else
    echo "REGRESSÃO FALHOU: $fails item(ns) (logs em $OUT/)"
fi
exit $((fails != 0))
