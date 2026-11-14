#!/usr/bin/env python3
"""Extrai o programa do ADSP-2105 da placa de som DCS a partir das ROMs de som (cusa.u2..u9).

Uso: tools/extract_dcs.py [diretório_das_roms] [diretório_de_saída]
     (padrão: assets/rom_extracted e assets/rom_extracted)

Layout (conferido na própria ROM):
- Banco n = 0x1000 bytes a partir de n * 0x1000 em cusa.u2 (janela de dados `0x2000`–`0x2FFF`,
  banco em `DM(0x3000)`; cada leitura devolve um byte).
- Boot do ADSP-2105: palavra i = bytes 4i, 4i+1, 4i+2 (o 4º é ignorado), tamanho = 8 * (byte 3 + 1).
  - Banco 0: página de autoteste (checksums das ROMs, RAM); no fim força o boot do banco 1.
  - Banco 1: sistema de som residente (1024 palavras, PM interna `0x0000`–`0x03FF`).
- O residente carrega overlays na PM externa a partir de `0x0800` com a rotina `0x03F1`
  (3 bytes por palavra, alto/médio/baixo, e pula o 4º):
  - banco 0, deslocamento 0x800, 0x200 palavras: inicialização (CALL `0x0800`);
  - banco 2, deslocamento 0x000, 0x300 palavras: sistema principal.

Saída (palavras de 24 bits em 32 bits little-endian, o formato do `unidasm -arch adsp21xx`):
- dcs_boot0.bin, dcs_resident.bin, dcs_overlay_init.bin, dcs_overlay_main.bin.
"""
import os
import sys


def boot_page(rom: bytes, off: int) -> list:
    n = 8 * (rom[off + 3] + 1)
    return [(rom[off + 4 * i] << 16) | (rom[off + 4 * i + 1] << 8) | rom[off + 4 * i + 2] for i in range(n)]


def overlay(rom: bytes, bank: int, offset: int, words: int) -> list:
    base = bank * 0x1000 + offset
    return [(rom[base + 4 * i] << 16) | (rom[base + 4 * i + 1] << 8) | rom[base + 4 * i + 2] for i in range(words)]


def save(path: str, words: list) -> None:
    with open(path, "wb") as f:
        for w in words:
            f.write(bytes([w & 0xFF, (w >> 8) & 0xFF, (w >> 16) & 0xFF, 0]))


def main() -> int:
    src = sys.argv[1] if len(sys.argv) > 1 else "assets/rom_extracted"
    dst = sys.argv[2] if len(sys.argv) > 2 else src
    with open(os.path.join(src, "cusa.u2"), "rb") as f:
        rom = f.read()
    parts = {
        "dcs_boot0.bin": boot_page(rom, 0x0000),
        "dcs_resident.bin": boot_page(rom, 0x1000),
        "dcs_overlay_init.bin": overlay(rom, 0, 0x800, 0x200),
        "dcs_overlay_main.bin": overlay(rom, 2, 0x000, 0x300),
    }
    for name, words in parts.items():
        save(os.path.join(dst, name), words)
        print(f"{name}: {len(words)} palavras")
    return 0


if __name__ == "__main__":
    sys.exit(main())
