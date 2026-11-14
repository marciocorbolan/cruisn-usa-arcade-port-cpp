#!/usr/bin/env python3
"""Intercala as EPROMs de programa (u10..u13) em um binário de palavras de
32 bits little-endian (formato que o unidasm espera para o TMS320C31).

Uso: tools/interleave_program.py [dir_extraido] [saida]
Padrões: assets/rom_extracted  ->  assets/rom_extracted/program.bin
Byte i*4+0 = u10[i] (D0-D7) ... i*4+3 = u13[i] (D24-D31).
"""
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
src = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "assets/rom_extracted"
out = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else src / "program.bin"
chips = [(src / f"cusa-l41.u1{i}").read_bytes() for i in range(4)]
if len({len(c) for c in chips}) != 1:
    sys.exit("EPROMs com tamanhos diferentes")
data = bytearray(len(chips[0]) * 4)
for lane, chip in enumerate(chips):
    data[lane::4] = chip
out.write_bytes(data)
print(f"{out}: {len(data) // 4:#x} palavras, RESET={int.from_bytes(data[0:4], 'little'):#x}")
