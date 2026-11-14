/**
 * @file race_entry.cpp
 * @brief Tradução de `0x18A6`–`0x1941`, `0x1841`–`0x189F`, `0x1617`–`0x161C`/`0x1679`–`0x168B`,
 *        `0x195D`–`0x1A26`, `0x2939`–`0x29B7` e `0x9CE3`–`0x9CF4`.
 */
#include "core/game/race_entry.hpp"

#include <cmath>
#include <utility>

#include "core/game/process_ctx.hpp"

namespace rea::core::game {

namespace {

/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }
constexpr uint32_t kNone = 0xFFFFFFFFu;  ///< Marcador de "nenhum" (−1).
constexpr uint32_t kCar = 0xE632;  ///< Carro escolhido (0..3).

/// Gabinete da direita fora do atrato (`[C8CC]` bit 2 = 0 e bit 0 = 1).
bool rightCabinet(ProcCtx& c) {
    const uint32_t f = c.rd(0xC8CC);
    return (f & 4u) == 0 && (f & 1u) != 0;
}

/// Procura o objeto do tipo @p type (`0x70BB`); devolve AR0.
uint32_t findObj(ProcCtx& c, uint32_t type) {
    c.cpu.ar[2] = type;
    c.call(0x70BB);
    return c.cpu.ar[0];
}

/// Vetor (0, −400, −2200) girado pelo rumo `[E402]` (matriz `[0x5A]`) em AR2.
void behindOffset(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[2] = c.rd(0xE402);
    cpu.ar[2] = c.rd(0x5A);
    c.call(0x938A);
    cpu.r[2] = cpu.ar[2];
    c.call(0x9414);
    c.wf(cpu.ar[2] + 2, -2200.0);
    c.wf(cpu.ar[2] + 1, -400.0);
    cpu.r[2] = c.rd(0x5A);
    cpu.r[3] = cpu.ar[2];
    c.call(0x941E);
}

/// `0x1841`: câmera atrás e acima do objeto AR4.
void cameraBehind(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    behindOffset(c);
    for (uint32_t i = 0; i < 3; ++i) {
        cpu.r[i] = doubleToTms(c.rf(cpu.ar[4] + 1 + i) + c.rf(cpu.ar[2] + i));
        c.wr(0x809800 + i, cpu.r[i]);
    }
    cpu.dp = 0;
}

/// `0x1859`: zera o estado da corrida e cria os processos da largada.
void raceSetup(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[0] = 0;
    for (uint32_t a : {0xE5F6u, 0xE53Fu, 0xE63Au, 0xE869u, 0xEB4Fu, 0xE63Bu}) c.wr(a, 0);
    c.wr(0xC931, 2);
    c.wf(0xE8EF, 0.0);
    struct Spawn { uint32_t ptr, id; bool after; };
    static constexpr Spawn kProcs[] = {{0xB1F2, 0x830, false}, {0xB1F3, 0x830, false}, {0xB1F4, 0x840, false},
                                       {0xB1F5, 0x830, true},  {0xB1F6, 0x500, false}};
    for (const Spawn& p : kProcs) {
        cpu.ar[2] = c.rd(p.ptr);
        cpu.r[2] = p.id;
        c.call(p.after ? 0xA868 : 0xA844);
    }
    c.wr(0xE5EC, 0x33BB);
    for (uint32_t i = 0; i < 9; ++i) {  // carros da CPU
        cpu.r[4] = i < 8 ? i : (rightCabinet(c) ? 8u : 9u);
        cpu.ar[2] = c.rd(0xB1F7 + i);
        cpu.r[2] = 0x200;
        c.call(0xA844);
    }
}

// --- 0x18A6: da garagem à pista -----------------------------------------------------------

void entryWait(ProcCtx& c);

/// Trecho traduzido da ROM em `0x18A6`.
void entryStart(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    c.call(c.rd(c.rd(0xE634) + c.rd(0x8C12)));
    c.wr(0xE3FE, c.rd(c.rd(0xE634) + c.rd(0x18A5)));
    c.call(0x3F8D);
    c.wr(0xC8C5, 0x2C3);
    c.wr(0xC938, 0);
    c.wr(0xE631, 0);
    c.call(0x7648);
    cpu.ar[2] = 0x14D;
    c.call(0x91E0);
    cpu.ar[2] = c.rd(0xB200);
    cpu.r[2] = 0x5C1;
    c.call(0xA868);
    c.text(0x7660, c.rd(0x18A0), 256.0, 50.0, 0x258, false);
    c.call(0x76BF);
    entryWait(c);
}

void entryMove(ProcCtx& c);

/// `0x18C3`: espera a escolha do carro.
void entryWait(ProcCtx& c) {
    if (c.rd(0xE641) == 0) return c.sleep(1, 0x18C7);
    entryMove(c);
}

/// `0x18C8`: desloca a garagem e prepara a largada.
void entryMove(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[2] = c.rd(0xB201);
    c.call(0x9DFE);
    double dx = 576.0 - c.rf(c.rd(kCar) + c.rd(0xB202));
    if (rightCabinet(c)) dx += 1152.0;
    cpu.r[2] = c.rd(0xE402);
    cpu.ar[2] = c.rd(0x5A);
    c.call(0x939D);
    cpu.r[2] = cpu.ar[2];
    c.call(0x9414);
    c.wf(cpu.ar[2], dx);
    cpu.r[3] = cpu.ar[2];
    c.call(0x941E);
    const double mx = c.rf(cpu.ar[2]), mz = c.rf(cpu.ar[2] + 2);
    cpu.r[6] = doubleToTms(mx);
    cpu.r[7] = doubleToTms(mz);
    auto shift = [&](uint32_t obj) {
        c.wf(obj + 1, c.rf(obj + 1) + mx);
        c.wf(obj + 3, c.rf(obj + 3) + mz);
    };
    for (cpu.r[0] = c.rd(0xE47D); cpu.r[0] != 0; cpu.r[0] = c.rd(cpu.ar[0] + 0x1F)) {  // port: para em 0
        cpu.ar[0] = cpu.r[0];
        shift(cpu.ar[0]);
    }
    cpu.ar[2] = c.rd(0x45);
    c.wf(cpu.ar[2], c.rf(cpu.ar[2]) + mx);
    c.wf(cpu.ar[2] + 2, c.rf(cpu.ar[2] + 2) + mz);
    cpu.ar[3] = c.rd(0xB207);
    for (int i = 0; i < 4; ++i) shift(c.rd(cpu.ar[3]++));
    c.wr(0xC8C5, c.rd(0xC8C5) & ~0x200u);
    c.wr(0xE604, 5);
    c.wr(0xE5ED, 0x100);
    c.call(0x7648);
    // 01903  auditoria do carro e [E640]
    cpu.r[0] = c.rd(kCar);
    if (c.rd(0xE84A) != kNone) {
        uint32_t v = c.rd(kCar);
        if (v == 5) v = 1;
        else if (v == 6) v = 2;
        else if (v == 7) v = 3;
        c.audit(v + 0xE5);
        cpu.r[0] = c.rd(kCar) + 4;
    } else {
        c.audit(c.rd(kCar) + 0xE1);
        cpu.r[0] = c.rd(kCar);
    }
    c.wr(0xE640, cpu.r[0]);
    cpu.r[0] = 0x5C1;
    cpu.r[1] = kNone;
    c.call(0xA8CF);
    if ((c.rd(0xC8CC) & 4u) == 0) return c.lcall(0xB208, 0xB209);  // 0x1617 → 0x1926
    c.jump(0x1926);
}

/// `0x1926`: largada e saída do carro.
void entryLaunch(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    c.call(0x1859);
    for (uint32_t p = 0xB20A; p <= 0xB20E; ++p) {
        cpu.ar[2] = c.rd(p);
        cpu.r[2] = 0x5C1;
        c.call(0xA844);
    }
    c.sleep(0x46, 0x1938);
}

/// Trecho traduzido da ROM em `0x193A`.
void entryEnd(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[0] = 0x5C1;
    cpu.r[1] = kNone;
    c.call(0xA8CF);
    c.call(0x1A8A);
    c.lret();
}

// --- 0x1617: espera do link (só o caminho sem link) ---------------------------------------

/// Trecho traduzido da ROM em `0x1679`.
void linkWaitSolo(ProcCtx& c) {
    c.wr(0xE594, 0);
    c.wr(0xC8E3, 0);
    c.call(0x7A5F);
    c.sleep(1, 0x167F);
}

/// Trecho traduzido da ROM em `0x1680`.
void linkWaitEnd(ProcCtx& c) {
    c.cpu.ar[2] = c.rd(0xEA33);
    if (c.cpu.ar[2] != 0) c.call(0x7100);
    c.wr(0xC8C5, c.rd(0xC8C5) & ~0x200u);
    c.wr(0xE604, 5);
    c.lret();
}

/// Trecho traduzido da ROM em `0x167F`.
void linkWaitSoloEnd(ProcCtx& c) {
    c.call(0x7F42);
    linkWaitEnd(c);
}

/// Texto com `0x7660` + `0x76BF` e flag 0x100 (`0x1649`–`0x165A`).
void waitText(ProcCtx& c, uint32_t ptr, double y) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[2] = c.rd(ptr);
    cpu.r[2] = doubleToTms(256.0);
    cpu.r[3] = doubleToTms(y);
    cpu.rc = 1;
    c.call(0x7660);
    c.call(0x76BF);
    c.orField(cpu.ar[0], 8, 0x100);
}

/// `0x162D`: laço da espera do outro gabinete (um quadro por volta).
void linkWaitLoop(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    c.call(0x7AB7);  // mensagem 4 ([C8D8] daqui)
    if (c.rd(0xE597) != 0) {
        c.wr(0xE597, 0);
        cpu.r[1] = 0x14;
    } else {
        cpu.r[1] = c.rd(0xE604);
        cpu.r[0] = c.rd(0xC8D6) & 0xFu;
        if (cpu.r[0] == 6 || cpu.r[0] == 5) cpu.r[1] = 0x14;
    }
    c.wr(0xE604, cpu.r[1]);
    // 0163C  o outro já escolheu o carro ([C8D8] ≥ 0): segue
    if (c.rd(0xC8E3) != 0 && s(c.rd(0xC8D8)) >= 0) return linkWaitEnd(c);
    // 01640  link perdido e tempo esgotado: corre sozinho
    if (c.rd(0xC8E3) == 0 && s(c.rd(0xE604)) <= 0) return linkWaitSolo(c);
    // 01644
    cpu.r[0] = c.rd(0xE63C) - 1;
    const bool more = s(cpu.r[0]) > 0;
    if (s(cpu.r[0]) < 0) cpu.r[0] = 0;
    c.wr(0xE63C, cpu.r[0]);
    if (!more) {
        waitText(c, 0xB1D8, 100.0);
        waitText(c, 0xB1D9, 146.0);
    }
    // 0165B
    if (c.rd(0xC8E3) != 0) return c.sleep(1, 0x165F);
    // 01660  sem link: START/VIEW apressam o tempo
    cpu.r[0] = (c.rd(0xC937) >> 16) & 0x42u;
    if (cpu.r[0] != 0) {
        cpu.r[0] = c.rd(0xE604) - 1;
        if (s(cpu.r[0]) < 0) cpu.r[0] = 0;
        c.wr(0xE604, cpu.r[0]);
    }
    if (!(s(c.rd(0xE63C)) > 0)) {
        c.call(0x1D3F);
        c.text(0x7651, c.rd(0xB1DA), 256.0, 192.0, 1, true);
    }
    c.sleep(1, 0x1678);
}

/// `0x161F`: espera do outro gabinete (chamada local de `0x1617`).
void linkWait(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    if (c.rd(0xE637) == 0) {
        c.call(0x6FD4);
        cpu.ar[2] = cpu.ar[0];
        c.call(0x7070);
    }
    c.wr(0xE631, 0);
    c.wr(0xE851, 0);
    c.wr(0xC8C5, c.rd(0xC8C5) | 0x200u);
    c.wr(0xE604, 0x14);
    c.call(0x7AB7);
    linkWaitLoop(c);
}

// --- Processos da saída da garagem --------------------------------------------------------

/// `0x195D`: som 0xC9; a porta (0x501 + carro) sobe 14 por quadro durante 50 quadros.
void doorFrame(ProcCtx& c) {
    c.wf(c.cpu.ar[4] + 2, c.rf(c.cpu.ar[4] + 2) - 14.0);
    c.sleep(1, 0x1969);
}

/// Gira o ângulo `[AR4+0x15]` para o rumo `[E402]` (0x1995–0x19A6).
void turnToHeading(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[2] = c.rd(0xE402);
    c.call(0x9308);
    cpu.r[0] = cpu.r[2];
    cpu.r[2] = c.rd(cpu.ar[4] + 0x15);
    c.call(0x9308);
    c.wr(cpu.ar[4] + 0x15, cpu.r[2]);
    c.call(0x94A7);
    const double d = tmsToDouble(cpu.r[0]);
    cpu.r[1] = doubleToTms(std::fabs(d));
    double a = tmsToDouble(cpu.r[2]);
    a += std::fabs(d) < c.k(0x199D) ? d : d * c.k(0x199F);
    cpu.r[2] = doubleToTms(a);
    c.wr(cpu.ar[4] + 0x15, cpu.r[2]);
    cpu.ar[2] = cpu.ar[4] + 4;
    c.call(0x938A);
}

/// `0x1975`: um quadro da câmera indo para trás do carro enquanto ele vira.
void exitTurnFrame(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    behindOffset(c);
    for (uint32_t i = 0; i < 3; ++i) {
        const double target = c.rf(cpu.ar[4] + 1 + i) + c.rf(cpu.ar[2] + i), cam = c.rf(0x809800 + i);
        c.wf(0x809800 + i, (target - cam) * c.k(0x198B + i) + cam);
    }
    cpu.dp = 0;
    turnToHeading(c);
    // 019A7  desce até a altura de repouso [AR4+0x1E]; a base acompanha
    const double dy = (c.rf(cpu.ar[4] + 0x1E) - c.rf(cpu.ar[4] + 2)) * c.k(0x19AA);
    c.wf(cpu.ar[4] + 2, c.rf(cpu.ar[4] + 2) + dy);
    const uint32_t base = findObj(c, c.rd(kCar) + 0x401);
    c.wf(base + 2, c.rf(base + 2) + dy);
    c.sleep(1, 0x19B5);
}

/// Índice do carro limitado a 0..3 (as variantes 4..7 usam o mesmo objeto).
uint32_t carSlot(ProcCtx& c) {
    const uint32_t v = c.rd(kCar);
    return s(v) >= 4 ? v - 4 : v;
}

/// `0x19B7`: vira processo 0x110, prepara o carro do jogador e começa a andar.
void exitDrive(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t ar4 = cpu.ar[4];
    findObj(c, c.rd(kCar) + 0x481);
    cpu.ar[4] = ar4;
    c.setVar(3, 0x110);
    cpu.ar[6] = c.rd(0x45);
    cpu.ar[2] = findObj(c, carSlot(c) + 0x401);
    c.call(0x7100);
    cpu.r[2] = c.rd(cpu.ar[4] + 0x15);
    cpu.r[0] = c.rd(0xE402);
    c.call(0x94A7);
    cpu.r[2] = doubleToTms(tmsToDouble(cpu.r[2]) + tmsToDouble(cpu.r[0]));
    c.wr(cpu.ar[4] + 0x15, cpu.r[2]);
    cpu.ar[2] = cpu.ar[4] + 4;
    c.call(0x938A);
    c.wf(cpu.ar[4] + 2, c.rf(cpu.ar[4] + 2) + (c.rf(cpu.ar[4] + 0x1E) - c.rf(cpu.ar[4] + 2)));
    c.sound(0x23D);
    cpu.ar[5] = 0x18;
    cpu.ar[4] = findObj(c, carSlot(c) + 0x481);
    cpu.ar[2] = cpu.ar[4];
    c.call(0x715F);
    c.call(0x2987);
    cpu.r[6] = doubleToTms(1.0);
    const uint32_t ar5 = cpu.ar[5], r6 = cpu.r[6];
    cpu.ar[5] = c.rd(cpu.ar[4] + 0x1B);
    c.wr(cpu.ar[5] + 0x26, cpu.r[6]);
    cpu.r[6] = c.rd(0xE402);
    for (uint32_t off : {0x2Cu, 0x2Du, 0x2Eu}) c.wr(cpu.ar[5] + off, cpu.r[6]);
    cpu.r[2] = doubleToTms(0.0);
    c.call(0x2C18);
    cpu.r[6] = r6;
    cpu.ar[5] = ar5;
    c.call(0x1841);
    c.sound(0x90);
    cpu.ar[0] = c.rd(0xE46E);
    cpu.r[1] = c.rd(cpu.ar[0] + 0x1E) & ~0xFFu;
    cpu.ar[0] = c.rd(cpu.ar[0] + 0x21);
    cpu.ar[5] = cpu.ar[0];
    c.jump(0x19F9);
}

/// `0x19F9`: anda devagar até 700 (+1152 à direita) do ponto AR5; depois o processo do carro.
void exitDriveFrame(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const double dx = c.rf(cpu.ar[5] + 1) - c.rf(cpu.ar[4] + 1), dz = c.rf(cpu.ar[5] + 3) - c.rf(cpu.ar[4] + 3);
    const double d2 = dx * dx + dz * dz;
    cpu.r[4] = doubleToTms(d2);
    double lim = 700.0;
    if (rightCabinet(c)) lim += 1152.0;
    if (d2 > lim * lim) {
        const uint32_t ar5 = cpu.ar[5];
        cpu.ar[5] = c.rd(cpu.ar[4] + 0x1B);
        c.wf(cpu.ar[5] + 0x24, 0.5);
        cpu.r[2] = doubleToTms(0.0);
        c.call(0x2C18);
        cpu.ar[5] = ar5;
        c.call(0x1841);
        return c.sleep(1, 0x1A16);
    }
    const uint32_t f = c.rd(0xC8CC);
    if ((f & 4u) == 0) {
        cpu.ar[2] = cpu.ar[5];
        cpu.ar[3] = (f & 1u) == 0 ? 0x23C : 0x6BC;
        c.wf(cpu.ar[7] + 0x1B, static_cast<double>(cpu.ar[3]));
        cpu.ar[3] = 0;
        c.call(0x54FD);
    }
    c.wr(0xE63F, 1);
    c.wr(0xC8C5, 0xC4);
    c.jump(0x29B8);
}

/// `0x2987`: prepara o carro do jogador AR4 (modelo do carro `[E640]`, dados `0x2945`/`0x2939`).
void playerCarInit(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    c.wr(0xE876, cpu.ar[4]);
    c.wr(0xE877, cpu.ar[7]);
    cpu.ar[1] = c.rd(0xE640);
    if (s(cpu.ar[1]) >= 4) {  // variantes 4..7 → modelos 0xE, 0x11, 0xF, 3
        static constexpr uint32_t kAlt[] = {0xE, 0x11, 0xF, 0x3};
        cpu.r[0] = cpu.ar[1] <= 7 ? kAlt[cpu.ar[1] - 4] : cpu.ar[1];
        cpu.ar[1] = cpu.r[0];
    }
    cpu.ar[2] = cpu.ar[1];
    cpu.ar[1] = cpu.ar[1] * 0xB + c.rd(0x961B);
    cpu.r[1] = c.rd(cpu.ar[1]);
    c.wr(cpu.ar[4] + 0xD, cpu.r[1]);
    c.call(0x8F3E);
    c.wf(cpu.ar[4] + 0x1E, -1.0);
    cpu.r[0] = 0;
    for (uint32_t a : {0xE605u, 0xE87Au, 0xE87Cu, 0xE897u}) c.wr(a, 0);
    c.wr(cpu.ar[4] + 0x17, cpu.ar[7]);
    cpu.ar[2] = cpu.ar[4];
    c.call(0x7093);
    c.wf(0xE879, 0.0);
    cpu.r[0] = c.rd(0xE640);
    c.call(0x2945);
    c.call(0x2939);
    c.wr(0xE878, cpu.ar[0]);
    c.wr(cpu.ar[0] + 0x40, 0x100);
    c.wr(cpu.ar[4] + 0xF, 0x100);
    c.wr(cpu.ar[0] + 0x56, c.rd(0xE633));
    c.wf(cpu.ar[0] + 0x30, 2.0);
    c.wr(0xE86A, 0xB);
    c.wr(0xEA48, c.rd(0xE89E));
}

} // namespace

void registerRaceEntry(SubroutineDispatcher& d) {
    using C = ProcCtx;
    addProc(d, 0x1841, cameraBehind);
    addProc(d, 0x1859, raceSetup);
    addProc(d, 0x2987, playerCarInit);
    addProc(d, 0x2939, [](C& c) {  // 4 floats do carro R0 de [2918] → +0x23, +0x22, +0x44, +0x45
        c.cpu.ar[2] = c.rd(0x2918) + c.cpu.r[0] * 4;
        for (uint32_t off : {0x23u, 0x22u, 0x44u, 0x45u}) {
            c.cpu.r[0] = c.rd(c.cpu.ar[2]++);
            c.wr(c.cpu.ar[0] + off, c.cpu.r[0]);
        }
    });
    addProc(d, 0x18A6, entryStart);
    addProc(d, 0x18C7, entryWait);
    addProc(d, 0x1926, entryLaunch);
    addProc(d, 0x1938, [](C& c) { c.sleep(1, 0x193A); });
    addProc(d, 0x193A, entryEnd);
    addProc(d, 0x1617, [](C& c) {
        c.wr(0xE63C, 5);
        c.wr(0xEA33, 0);
        if (c.rd(0xC8D1) == 0 || c.rd(0xE63D) != 0) return linkWaitSolo(c);
        linkWait(c);  // 0161F  com link: espera do outro gabinete
    });
    addProc(d, 0x161F, linkWait);
    addProc(d, 0x162D, linkWaitLoop);
    addProc(d, 0x165F, linkWaitLoop);
    addProc(d, 0x1678, linkWaitLoop);
    addProc(d, 0x167F, linkWaitSoloEnd);
    addProc(d, 0x1680, linkWaitEnd);
    addProc(d, 0x195D, [](C& c) {
        c.cpu.ar[2] = 0xC9;
        c.call(0x91E0);
        c.cpu.ar[5] = 0x31;
        c.cpu.ar[4] = findObj(c, c.rd(kCar) + 0x501);
        doorFrame(c);
    });
    addProc(d, 0x1969, [](C& c) {
        if (s(--c.cpu.ar[5]) >= 0) return doorFrame(c);
        c.die();
    });
    addProc(d, 0x196B, [](C& c) {
        c.wr(0xE63F, 0);
        c.cpu.ar[4] = findObj(c, c.rd(kCar) + 0x481);
        c.cpu.r[2] = c.rd(0xE402);
        c.cpu.ar[2] = c.rd(0x5A);
        c.call(0x938A);
        c.cpu.ar[5] = 0x19;
        exitTurnFrame(c);
    });
    addProc(d, 0x19B5, [](C& c) {
        if (s(--c.cpu.ar[5]) >= 0) return exitTurnFrame(c);
        exitDrive(c);
    });
    addProc(d, 0x19F9, exitDriveFrame);
    addProc(d, 0x1A16, exitDriveFrame);
    // 0x9CE3: [E60F] = 150; espera o bit 0x100 do modo; depois −6 por quadro em 25 quadros e zera
    addProc(d, 0x9CE3, [](C& c) {
        c.wr(0xE60F, 0x96);
        c.jump(0x9CE5);
    });
    addProc(d, 0x9CE5, [](C& c) {
        if ((c.rd(0xC8C5) & 0x100u) == 0) return c.sleep(1, 0x9CE5);
        c.cpu.ar[5] = 0x18;
        c.jump(0x9CEC);
    });
    addProc(d, 0x9CEC, [](C& c) {
        c.wr(0xE60F, c.rd(0xE60F) - 6);
        c.sleep(1, 0x9CF1);
    });
    addProc(d, 0x9CF1, [](C& c) {
        if (s(--c.cpu.ar[5]) >= 0) return c.jump(0x9CEC);
        c.wr(0xE60F, 0);
        c.die();
    });
}

} // namespace rea::core::game
