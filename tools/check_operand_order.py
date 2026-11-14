#!/usr/bin/env python3
"""Guarda da ordem dos operandos: toda subtração e comparação do código traduzido já foi revisada?

Roda DENTRO do container (precisa do `unidasm` e do jogo compilado):
    docker compose run --rm -T dev tools/check_operand_order.py            # confere
    docker compose run --rm -T dev tools/check_operand_order.py --aceitar  # grava a lista atual como revisada

O que faz:
1. Desmonta o programa da L4.1 (lido de assets/rom_extracted/ ou do .zip em rom/) com o `unidasm`.
2. Segue o código alcançável a partir das entradas traduzidas (`rea_cruisn --list-translated`).
3. Separa as instruções em que a ordem dos operandos muda o resultado (as mesmas das duas revisões
   registradas em docs/architecture.md, "Ordem dos operandos"):
   - 3 operandos e reversas: SUBF3, SUBI3, SUBRF, SUBRI, CMPF3, CMPI3, ANDN3, SUBB3 (também em paralelo, `||`);
   - 2 operandos: SUBF/SUBI e CMPF/CMPI sem imediato, com src != dst e, nas comparações, seguidas de uma
     condição assimétrica (LT, LE, GT, GE, LO, LS, HI, HS, N, NN...).
4. Compara com tests/operand_order_reviewed.txt, a lista das instruções já conferidas com a tradução.

Ele não prova que o C++ está certo: o código traduzido não é uma cópia linha a linha do assembly, então a
conferência de cada instrução continua sendo feita por quem traduz. O que ele garante é que nenhuma instrução
sensível à ordem entra no código traduzido (rotina nova) sem ser revisada: cada uma que não está na lista sai
no relatório, agrupada pela rotina e com o arquivo que a registra. Depois de conferir, rode com --aceitar.
Código de saída: 0 = tudo revisado; 1 = há instruções sem revisão (ou com outro texto na ROM); 2 = erro.
"""
import bisect
import collections
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
REVIEWED = ROOT / "tests/operand_order_reviewed.txt"
BUILD = pathlib.Path(os.environ.get("REA_BUILD_DIR", "build"))
GAME = ROOT / BUILD / "bin/rea_cruisn"
CODE_WORDS = 0x20000  # o código fica abaixo de 0x20000; acima são dados

THREE = {"SUBF3", "SUBI3", "SUBRF", "SUBRI", "CMPF3", "CMPI3", "ANDN3", "SUBB3"}
TWO = {"SUBF", "SUBI", "CMPF", "CMPI"}
SYMMETRIC_CONDS = {"EQ", "NE", "Z", "NZ", "U", ""}
COND_OPS = re.compile(r"^(B|DB|LDF|LDI|CALL|RETS|RETI)")
UNCOND = {"LDI", "LDF", "LDIU", "LDFU", "B", "BU", "BR", "CALL", "RETSU", "RETIU", "BUD", "BRD"}
FLAG_SETTERS = ("CMP", "TST", "SUB", "ADD", "AND", "OR", "LSH", "MPY", "ABS", "NEG", "FIX", "FLOAT", "XOR",
                "ASH", "NOT")


def die(msg):
    print("Erro: " + msg, file=sys.stderr)
    sys.exit(2)


def program_chips():
    """As 4 EPROMs de programa da L4.1 (u10..u13), da pasta extraída ou de um .zip em rom/."""
    names = [f"cusa-l41.u1{i}" for i in range(4)]
    ext = ROOT / "assets/rom_extracted"
    if all((ext / n).is_file() for n in names):
        return [(ext / n).read_bytes() for n in names]
    for z in sorted((ROOT / "rom").glob("*.zip")):
        with zipfile.ZipFile(z) as f:
            have = {pathlib.PurePath(i).name.lower(): i for i in f.namelist()}
            if all(n in have for n in names):
                return [f.read(have[n]) for n in names]
    die("programa da L4.1 (cusa-l41.u10..u13) não encontrado em assets/rom_extracted/ nem em rom/*.zip")


def disassemble():
    """Endereço -> (mnemônico, operandos, texto sem a palavra codificada)."""
    chips = program_chips()
    data = bytearray(len(chips[0]) * 4)
    for lane, chip in enumerate(chips):
        data[lane::4] = chip
    with tempfile.NamedTemporaryFile(suffix=".bin") as tmp:
        tmp.write(data[:CODE_WORDS * 4])
        tmp.flush()
        try:
            asm = subprocess.run(["unidasm", tmp.name, "-arch", "tms320c3x"], capture_output=True, text=True,
                                 check=True).stdout
        except (OSError, subprocess.CalledProcessError) as e:
            die(f"unidasm falhou ({e}); rode dentro do container dev")
    ins = {}
    for line in asm.splitlines():
        m = re.match(r"([0-9a-f]{5}): [0-9a-f]{8}  (\S+)\s*(.*)", line.rstrip())
        if m:
            ins[int(m.group(1), 16)] = (m.group(2), m.group(3), f"{m.group(2)} {m.group(3)}".strip())
    return ins


def translated_entries():
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    try:
        out = subprocess.run([str(GAME), "--list-translated"], capture_output=True, text=True, env=env,
                             cwd=ROOT, check=True).stdout
    except (OSError, subprocess.CalledProcessError) as e:
        die(f"não consegui rodar {GAME} --list-translated ({e}); compile antes")
    entries = sorted(int(l, 16) for l in out.split() if re.fullmatch(r"0x[0-9A-Fa-f]+", l))
    if not entries:
        die("--list-translated não listou nenhum endereço")
    return entries


def reachable(ins, entries):
    """Endereços de código alcançáveis a partir das entradas (segue CALL/desvios; para em desvio incondicional)."""
    seen, stack = set(), list(entries)
    while stack:
        a = stack.pop()
        while a in ins and a not in seen and a < CODE_WORDS:
            seen.add(a)
            op, args, _ = ins[a]
            m = re.search(r"\$([0-9A-F]{6})", args)
            target = int(m.group(1), 16) if m else None
            if op.startswith(("RETS", "RETI")):
                if op in ("RETSU", "RETIU"):
                    break
            elif op.startswith(("CALL", "LAJ")):
                if target is not None:
                    stack.append(target)
            elif op.startswith(("B", "DB")):
                if target is not None:
                    stack.append(target)
                if op in ("BU", "BR", "BUD", "BRD") or op.startswith("BR"):
                    if op.endswith("D"):
                        seen.update((a + 1, a + 2, a + 3))  # slots do desvio atrasado
                    break
            a += 1
    return seen


def following_condition(ins, a):
    """Condição da próxima instrução condicional depois de uma comparação (None se os flags se perdem antes)."""
    for k in range(1, 7):
        op = ins.get(a + k, ("", "", ""))[0]
        if COND_OPS.match(op) and op not in UNCOND:
            return op
        if op.startswith(FLAG_SETTERS):
            return None
    return None


def sensitive(ins, code):
    """Instruções sensíveis à ordem no código alcançável: endereço -> (texto, nota)."""
    found = {}
    for a in sorted(code):
        if a not in ins:
            continue
        op, args, text = ins[a]
        if THREE & set(re.findall(r"[A-Z0-9]+", text)):
            found[a] = (text, "")
            continue
        if op not in TWO:
            continue
        parts = args.split(",")
        if len(parts) != 2 or re.match(r"^-?[\$0-9]", parts[0]) or parts[0] == parts[1]:
            continue  # imediato ou x − x: a ordem não importa
        note = ""
        if op.startswith("CMP"):
            cond = following_condition(ins, a)
            if cond is None:
                continue
            if re.sub(r"^(LDF|LDI|B|DB|CALL|RETS|RETI)", "", cond).rstrip("D") in SYMMETRIC_CONDS:
                continue
            note = "-> " + cond
        found[a] = (text, note)
    return found


def owners(entry):
    """Arquivos de src/core onde a rotina é registrada (`add(0x...`); sem isso, os que citam o endereço."""
    texts = {p: p.read_text(encoding="utf-8") for p in sorted((ROOT / "src/core").rglob("*.[ch]pp"))}
    for pat in (r"add\(\s*0x0*%X\b", r"0x(?=[0-9A-Fa-f]{4})0*%X\b"):
        rx = re.compile(pat % entry, re.I)
        files = [str(p.relative_to(ROOT / "src")) for p, s in texts.items() if rx.search(s)]
        if files:
            return ", ".join(files[:3])
    return "?"


def load_reviewed():
    rev = {}
    if REVIEWED.is_file():
        for line in REVIEWED.read_text(encoding="utf-8").splitlines():
            if line and not line.startswith("#"):
                addr, text = line.split(" ", 1)
                rev[int(addr, 16)] = text
    return rev


HEADER = """# Instruções sensíveis à ordem dos operandos no código traduzido, já conferidas com a tradução C++.
# Gerada por tools/check_operand_order.py --aceitar (endereço de palavra e instrução, como no disassembly).
# Base: as duas revisões de 2026-11-14 (docs/architecture.md, "Ordem dos operandos"): 221 de 3 operandos e
# reversas e 737 de 2 operandos (660 conferidas uma a uma e 77 cuja diferença só entra ao quadrado ou em ABSF,
# onde a ordem não muda o resultado). Só acrescente uma linha (rodando --aceitar) depois de conferir a instrução
# com a tradução: SUBF3 a,b,d = a - b; SUBF src,dst = dst - src; CMPF src,dst = flags de dst - src.
"""


def main():
    accept = "--aceitar" in sys.argv[1:]
    ins = disassemble()
    entries = translated_entries()
    found = sensitive(ins, reachable(ins, entries))
    if accept:
        REVIEWED.write_text(HEADER + "".join(f"{a:05x} {t}\n" for a, (t, _) in sorted(found.items())),
                            encoding="utf-8")
        print(f"{REVIEWED.relative_to(ROOT)}: {len(found)} instruções gravadas como revisadas")
        return 0
    reviewed = load_reviewed()
    new = {a: v for a, v in found.items() if reviewed.get(a) != v[0]}
    gone = sorted(set(reviewed) - set(found))
    three = sum(1 for t, _ in found.values() if THREE & set(re.findall(r"[A-Z0-9]+", t)))
    print(f"Ordem dos operandos: {len(found)} instruções sensíveis no código traduzido "
          f"({three} de 3 operandos/reversas, {len(found) - three} de 2 operandos), {len(entries)} entradas")
    if gone:
        print(f"  {len(gone)} da lista revisada não estão mais no código alcançável (rode --aceitar para limpar)")
    if not new:
        print("  todas já revisadas")
        return 0
    groups = collections.OrderedDict()
    for a in sorted(new):
        groups.setdefault(entries[bisect.bisect_right(entries, a) - 1], []).append(a)
    print(f"  {len(new)} SEM REVISÃO (confira cada uma com a tradução e depois rode --aceitar):")
    for e, addrs in groups.items():
        print(f"  rotina 0x{e:04X} ({owners(e)})")
        for a in addrs:
            text, note = new[a]
            old = f"   [na lista: {reviewed[a]}]" if a in reviewed else ""
            print(f"    {a:05X}  {text}  {note}{old}".rstrip())
    return 1


if __name__ == "__main__":
    sys.exit(main())
