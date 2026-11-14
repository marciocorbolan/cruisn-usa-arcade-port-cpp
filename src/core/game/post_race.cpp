/**
 * @file post_race.cpp
 * @brief Tradução de `0x3A46`–`0x3C18`, `0x3F42`–`0x3F67`, `0x6C52`–`0x6C58`, `0x32E7`–`0x32FA` e
 *        `0x3D85`–`0x3DCE`.
 */
#include "core/game/post_race.hpp"

#include "core/game/process_ctx.hpp"

namespace rea::core::game {

namespace {

/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }

constexpr uint32_t kMode = 0xC8C5;  ///< Modo do jogo: bits 0–3 = modo; 0x40 e 0x100 = flags.
constexpr uint32_t kTimeOut = 0xE53F;  ///< 1 = a corrida acabou por tempo
constexpr uint32_t kSkip = 0xE631;     ///< ≠ 0: o jogador apertou para pular
constexpr uint32_t kCross = 0xE636;    ///< 1 = cross-country (etapas em sequência)
constexpr uint32_t kStage = 0xE481;  ///< Etapa atual (1 = primeira).
constexpr uint32_t kPlace = 0xE607;  ///< posição do focado (1 = líder)

/// Liga ou desliga o bit C (carry) de ST, que a rotina original usa como retorno de sucesso ou falha.
void setCarry(Tms32031State& cpu, bool on) {
    if (on) cpu.st |= Tms32031State::kStCarry;
    else cpu.st &= ~Tms32031State::kStCarry;
}

/// R0 = @p v e grava em @p a (o par `LDI`/`STI` do original).
void store(ProcCtx& c, uint32_t a, uint32_t v) {
    c.cpu.r[0] = v;
    c.wr(a, v);
}

/// `LDI [ptr],AR2 / LDI id,R2 / CALL fn`.
void spawn(ProcCtx& c, uint32_t ptr, uint32_t id, uint32_t fn = 0xA844) {
    c.cpu.ar[2] = c.rd(ptr);
    c.cpu.r[2] = id;
    c.call(fn);
}

/// `LDI id,R0 / LDI mask,R1 / CALL $A8CF` (mata processos por ID).
void killIds(ProcCtx& c, uint32_t id, uint32_t mask) {
    c.cpu.r[0] = id;
    c.cpu.r[1] = mask;
    c.call(0xA8CF);
}

/// Troca a música para @p id (`0x91E0`).
void music(ProcCtx& c, uint32_t id) {
    c.cpu.ar[2] = id;
    c.call(0x91E0);
}

/// Troca o modo do jogo (bits 0–3 de `[C8C5]`) por @p m.
void setMode(ProcCtx& c, uint32_t m) { store(c, kMode, (c.rd(kMode) & ~0xFu) | m); }

// --- Auxiliares ---------------------------------------------------------------------------------

/// 06C52  libera todos os objetos do tipo 0x103.
void freeType103(ProcCtx& c) {
    for (;;) {
        c.cpu.ar[2] = 0x103;
        c.call(0x70B8);
        if (!c.cpu.carry()) return;
        c.cpu.ar[2] = c.cpu.ar[0];
        c.call(0x7100);
    }
}

/// 03F50  reassenta os objetos 0x767 (`0x43AB`) e 0x768 (`0x438E`) da lista `[C982]`.
/// Preserva AR0 e AR4.
void resettleProps(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t ar0 = cpu.ar[0], ar4 = cpu.ar[4];
    cpu.ar[0] = c.rd(0xC982);
    for (;;) {
        cpu.r[0] = c.rd(cpu.ar[0]);
        if (cpu.r[0] == 0) break;
        cpu.ar[0] = cpu.r[0];
        cpu.r[0] = c.rd(cpu.ar[0] + 0xF);
        if (cpu.r[0] == 0x767) {
            cpu.ar[4] = cpu.ar[0];
            c.call(0x43AB);
        } else if (cpu.r[0] == 0x768) {
            cpu.ar[4] = cpu.ar[0];
            c.call(0x438E);
        }
    }
    cpu.ar[4] = ar4;
    cpu.ar[0] = ar0;
}

/// 032E7  carry = o tempo `[E4BC]` da etapa entra no placar (`0x9B18` ≠ −1); `*+AR7(2D)` = etapa − 1.
void recordCheck(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    c.call(0x9AA9);
    cpu.r[1] = c.rd(kStage) - 1;
    c.wr(cpu.ar[7] + 0x2D, cpu.r[1]);
    cpu.ar[0] = cpu.r[1] * 4 + c.rd(0x3963);
    cpu.r[0] = c.rd(0xE4BC);
    if (cpu.r[1] == 0xE) c.call(0x330E);
    if (cpu.r[0] == 0) return setCarry(cpu, false);
    c.call(0x9B18);
    setCarry(cpu, cpu.r[0] != 0xFFFFFFFFu);
}

/// 03D85  bandeira da etapa R4 no mapa (`[3D84]` + 2·R4 = posição; objeto `[B10E]`) voando de
/// (±100, −230) até o lugar (processo `[B10F]` = `0x3DB1`), som 0xAE.
void stageFlag(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t ar0 = cpu.ar[0], ar4 = cpu.ar[4], r2 = cpu.r[2], r3 = cpu.r[3];
    cpu.ar[0] = cpu.r[4] * 2 + c.rd(0x3D84);
    cpu.r[2] = c.rd(cpu.ar[0]) - 0x100;
    cpu.r[3] = c.rd(cpu.ar[0] + 1) - 0xB4;
    cpu.rc = 0x16F;
    cpu.ar[2] = c.rd(0xB10E);
    c.call(0x7309);
    c.orField(cpu.ar[0], 0xE, (0x800u << 16) | 0x21u);
    cpu.ar[2] = cpu.ar[0];
    c.call(0x708D);
    cpu.ar[4] = cpu.ar[0];
    c.wr(cpu.ar[4] + 0x11, c.rd(cpu.ar[4] + 1));
    c.wr(cpu.ar[4] + 0x12, c.rd(cpu.ar[4] + 2));
    cpu.r[0] = doubleToTms(100.0);
    c.call(0x8ED1);
    c.wr(cpu.ar[4] + 1, cpu.r[0]);
    c.wf(cpu.ar[4] + 2, -230.0);
    spawn(c, 0xB10F, 0x500, 0xA868);
    c.sound(0xAE);
    cpu.rc = r3;  // quirk: POP RC duas vezes
    cpu.r[2] = r2;
    cpu.ar[4] = ar4;
    cpu.ar[0] = ar0;
}

/// 03DB1  a bandeira desliza 1/4 por quadro até o lugar, girando 0,1 por quadro (até 13 quadros).
void flagFly(ProcCtx& c, bool first) {
    Tms32031State& cpu = c.cpu;
    const uint32_t o = cpu.ar[4];
    if (first) cpu.ar[5] = 0xC;
    else if (s(--cpu.ar[5]) < 0) {
        c.wr(o + 1, c.rd(o + 0x11));
        c.wr(o + 2, c.rd(o + 0x12));
        return c.die();
    }
    const double x = (c.rf(o + 0x11) - c.rf(o + 1)) * 0.25 + c.rf(o + 1);
    c.wf(o + 1, x);
    const double y = (c.rf(o + 0x12) - c.rf(o + 2)) * 0.25 + c.rf(o + 2);
    c.wf(o + 2, y);
    if (x * x + y * y < 25.0) {
        c.wr(o + 1, c.rd(o + 0x11));
        c.wr(o + 2, c.rd(o + 0x12));
        return c.die();
    }
    cpu.r[2] = doubleToTms(c.rf(o + 0x16) + 0.1);
    c.wr(o + 0x16, cpu.r[2]);
    cpu.ar[2] = o + 4;
    c.call(0x93C4);
    c.sleep(1, 0x3DC9);
}

// --- 0x3A46 ---------------------------------------------------------------------------------------

/// `0x3A46`: fim da corrida; guarda o tempo e prepara o pós-corrida.
void raceOver(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    c.wr(cpu.ar[7] + 0xC, c.rd(0xC938));
    store(c, 0xE53C, c.rd(0xE604));
    store(c, 0xC938, 0);
    c.call(0x9166);
    c.call(0x92AD);
    store(c, 0xE860, 0);
    c.wr(0xE483, 0);
    c.call(0x3F42);
    store(c, 0xE918, 0);
    store(c, 0xE593, 1);
    c.call(0xA8F5);
    spawn(c, 0xB0E0, 0x500);
    spawn(c, 0xB0E1, 0x110);
    spawn(c, 0xB0E2, 0x500);
    c.call(0x3F50);
    cpu.dp = 0;
    cpu.sp = c.rd(0x63);
    cpu.jump = 0x4C1B;  // BR $4C1B
}

// --- 0x3A64 ---------------------------------------------------------------------------------------

void afterPodium(ProcCtx& c);
void scoreboards(ProcCtx& c);
void decide(ProcCtx& c);
void finalWin(ProcCtx& c);
void nextStage(ProcCtx& c);

/// 03A70  espera a cortina `[809A79]` terminar.
void waitCurtain(ProcCtx& c) {
    if (c.rd(0x809A79) != 0) return c.sleep(1, 0x3A76);
    // 03A77
    store(c, 0xE53E, c.rd(0xC8E3));
    c.call(0x793A);
    music(c, 0xDB);
    if (c.rd(kTimeOut) != 0) {
        if (c.rd(kPlace) == 1 && c.rd(0xE53E) != 0) store(c, kPlace, 2);
        return scoreboards(c);
    }
    spawn(c, 0xB0E4, 0x5C3);
    c.sound(0x8A);
    c.sound(0x8D);
    music(c, 0x21);
    c.cpu.r[0] = c.rd(kPlace);
    if (c.cpu.r[0] != 1) return scoreboards(c);
    spawn(c, 0xB0E5, 0x500, 0xA868);
    c.lcall(0xB0E6, 0xB0E7);  // 0x864D, volta em 0x3A9B
}

/// `0x3A64`: começa os placares de resultado.
void resultsStart(ProcCtx& c) {
    if (c.rd(kTimeOut) != 0) c.wr(0xE4BC, 0);
    c.wr(0xE4BC, c.rd(0xE85F));  // quirk: sobrescreve o 0 do fim de tempo
    c.call(0x7F4D);
    store(c, kSkip, 0);
    spawn(c, 0xB0E3, 0x500);
    waitCurtain(c);
}

/// 03A9B  placares e recorde; 5 quadros.
void scoreboards(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[5] = cpu.ar[7];
    for (uint32_t p : {0xB0E8u, 0xB0E9u, 0xB0EAu, 0xB0EBu}) spawn(c, p, 0x5C4);
    spawn(c, 0xB0EC, 0x5C7);
    if (c.rd(kTimeOut) == 0) {
        c.call(0x32E7);
        if (cpu.carry()) spawn(c, cpu.r[0] == 0 ? 0xB0ED : 0xB0EE, 0x5C4);
    }
    c.sleep(5, 0x3ABA);
}

void holdLoop(ProcCtx& c);

/// 03ABA  música de fundo e ~2 s de espera (AR5 = 0x77 menos dt por quadro).
void afterPodium(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    if (c.rd(kPlace) != 1) {
        cpu.r[0] = 0;
        cpu.r[1] = 0x64;
        c.call(0x9114);
        music(c, 0xDB);
    }
    c.sound(0x8A);
    cpu.ar[5] = 0x77;
    holdLoop(c);
}

void curtain(ProcCtx& c);

/// Laço de espera dos placares; pular fecha a cortina.
void holdLoop(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    if (c.rd(kSkip) != 0) return curtain(c);
    int32_t n = s(cpu.ar[5]) - s(c.rd(0xC92F));
    if (n < 0) n = 0;
    cpu.ar[5] = static_cast<uint32_t>(n);
    c.sleep(1, 0x3ACC);
}

/// 03ACD  cortina `[B0EF]` e pausa.
void curtain(ProcCtx& c) {
    spawn(c, 0xB0EF, 0x500);
    c.sleep(0xA, 0x3AD2);
}

void flagsLoop(ProcCtx& c);
void waitSkip(ProcCtx& c);

/// 03AD6  paletas, código secreto, cenário do mapa e o recorde da etapa.
void mapScreen(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    c.call(0x95F5);
    const uint32_t ar5 = cpu.ar[5], ar6 = cpu.ar[6];
    cpu.ar[5] = c.rd(0x4365);
    cpu.ar[6] = 4;
    do {
        cpu.ar[2] = c.rd(cpu.ar[5] + 1);
        c.call(0x9ECD);
        if (!cpu.carry()) {
            cpu.r[0] = cpu.ar[2];
            cpu.r[1] = cpu.r[0];
            c.call(0x9EC2);
        }
        cpu.ar[5] += 2;
    } while (s(--cpu.ar[6]) >= 0);
    cpu.ar[6] = ar6;
    cpu.ar[5] = ar5;
    // 03AE5  botões [B0F0] = [B0F1] com o item 3 a ±20 de [C8C8] → cross-country
    cpu.r[1] = c.rd(0xB0F0);
    cpu.r[2] = c.rd(0xB0F1);
    cpu.r[0] = c.rd(0xC937) & cpu.r[1];
    if (cpu.r[0] == cpu.r[2]) {
        int32_t dv = s(c.item(3)) - s(c.rd(0xC8C8));
        if (dv < 0) dv = -dv;
        if (dv <= 0x14) store(c, kCross, 1);
    }
    c.call(0x3F64);
    cpu.ar[2] = c.rd(0xB0F2);
    c.call(0x9DFE);
    cpu.ar[2] = c.rd(0xB0F3);
    c.call(0x9DF6);
    cpu.ar[2] = c.rd(0xB0F4);
    c.call(0xA3C2);
    c.call(0x6754);
    if (c.rd(kCross) == 1) spawn(c, 0xB0F5, 0x555);
    c.sound(0x159);
    cpu.r[5] = c.rd(kStage) - 1;
    cpu.ar[0] = cpu.r[5] * 4 + c.rd(0x3963);
    c.wr(cpu.ar[0] + 1, 1);
    c.wr(cpu.ar[0] + 3, 1);
    c.wr(cpu.ar[0], c.rd(0xE4BC));
    cpu.r[0] = c.rd(0xE4BC);
    if (c.rd(kCross) != 1) return waitSkip(c);
    cpu.ar[3] = c.rd(0x3963);
    cpu.ar[4] = c.rd(0x3964);
    cpu.r[4] = 0;
    cpu.r[7] = doubleToTms(180.0);
    flagsLoop(c);
}

/// 03B14  uma bandeira por etapa já corrida, a cada 2 quadros.
void flagsLoop(ProcCtx& c) {
    if (c.rd(kSkip) != 0) return waitSkip(c);
    c.call(0x3D85);
    c.sleep(2, 0x3B19);
}

void waitSkipLoop(ProcCtx& c);

/// 03B1E  20 quadros (ou até pular).
void waitSkip(ProcCtx& c) {
    c.cpu.ar[5] = 0x13;
    waitSkipLoop(c);
}

void afterWait(ProcCtx& c);

/// Espera o jogador apertar para pular (retoma em `0x3B23`).
void waitSkipLoop(ProcCtx& c) {
    if (c.rd(kSkip) != 0) return afterWait(c);
    c.sleep(1, 0x3B23);
}

/// Restaura o centro do volante (`[EA48]` = `[E89E]`).
void restoreWheel(ProcCtx& c) { c.wr(0xEA48, c.rd(0xE89E)); }

/// 03B24
void afterWait(ProcCtx& c) {
    store(c, 0xC982, 0);
    if (c.item(0x1F) != 0) return c.lcall(0xB0F6, 0xB0F7);  // 0x31A6, volta em 0x3B30
    decide(c);
}

/// 03B32
void decide(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    store(c, 0xC941, 0);
    if (c.rd(kCross) == 1 && c.rd(kStage) == 0xE) return finalWin(c);
    store(c, 0xC941, 0);
    c.wr(0xE8E3, 0);
    c.wf(0x809800, 0.0);
    c.wf(0x809801, -1000.0);
    c.wf(0x809802, -2500.0);
    cpu.r[2] = doubleToTms(0.0);
    c.wr(0x809807, cpu.r[2]);
    cpu.dp = 0;
    cpu.ar[2] = c.rd(0x47);
    c.call(0x938A);
    if (c.rd(kCross) == 1) {
        cpu.ar[0] = c.rd(c.rd(kStage) + c.rd(0x8C12));
        c.call(cpu.ar[0]);
    }
    c.sleep(0xA, 0x3B52);
}

/// 03B56  fim de jogo, seleção de percurso, próxima etapa ou continue.
void decideNext(ProcCtx& c) {
    if (c.rd(kCross) != 1) return c.jump(0x180C);
    if (c.item(0x80) != 0) return c.jump(0x6CE3);
    spawn(c, 0xB0F8, 0x5C4);
    if (c.rd(kTimeOut) == 0 && c.rd(kPlace) == 1 && c.item(0x1A) != 0) {
        c.audit(c.rd(kStage) - 1 + 0xB5);
        c.audit(0x6A);
        setMode(c, 3);
        store(c, 0xE634, c.rd(kStage));
        c.call(0x7A98);
        return c.lcall(0xB0FB, 0xB0FC);  // 03B89: 0x1722, volta em 0x3B8F
    }
    c.lcall(0xB0F9, 0xB0FA);  // 0x1E2C (continue), volta em 0x3B7D
}

/// 03B7D  volta do continue: AR6 = 1 continua; senão desiste (`0x1D59`).
void afterContinue(ProcCtx& c) {
    setMode(c, 3);
    if (c.cpu.ar[6] != 1) return c.jump(0x1D59);
    c.audit(0xD3);
    c.call(0x9937);
    store(c, 0xE634, c.rd(kStage));
    c.call(0x7A98);
    c.lcall(0xB0FB, 0xB0FC);
}

/// 03B95  próxima etapa.
void nextStage(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    c.audit(c.rd(kStage) * 2 + 0x99);
    c.call(0x7648);
    killIds(c, 0x5C4, 0xFFFFFFFFu);
    store(c, kMode, c.rd(kMode) & ~0xC0u);
    for (uint32_t r : {0x6FEBu, 0x8FF0u, 0x9019u, 0xAF55u, 0x7648u}) c.call(r);
    killIds(c, 0x5C4, 0xFFFFFFFFu);
    cpu.r[0] = 0;
    cpu.r[1] = 0xFF;
    c.call(0x9114);
    c.call(0x9108);
    store(c, 0xC8CD, 4);
    store(c, 0xE604, c.item(0x1C) * 5 + 0x3C);
    store(c, kMode, (c.rd(kMode) & ~0x20Fu) | 0x44u);
    spawn(c, 0xB0FF, 0x500, 0xA868);
    cpu.ar[0] = c.rd(c.rd(kStage) + c.rd(0x3965));
    c.call(cpu.ar[0]);
    store(c, 0xE3FE, c.rd(0xE480));
    c.call(0x3F8D);
    const uint32_t ar7 = cpu.ar[7];
    cpu.r[0] = 0x5AA;
    cpu.r[1] = 0xFFFFFFFFu;
    c.call(0xA912);
    cpu.ar[7] = cpu.ar[0];
    c.wr(cpu.ar[7] + 4, 3);
    cpu.r[0] = 0x110;
    cpu.r[1] = 0xFFFFFFFFu;
    c.call(0xA912);
    cpu.ar[2] = cpu.ar[0];
    c.call(0xA91F);
    cpu.ar[7] = ar7;
    store(c, 0xC938, c.rd(cpu.ar[7] + 0xC));
    store(c, 0xC940, 1);
    store(c, 0xC987, 0);
    spawn(c, 0xB100, 0x5C0);
    store(c, kTimeOut, 0);
    c.call(0x1859);
    c.die();
}

void finalAfterEnding(ProcCtx& c);
void finalReset(ProcCtx& c);

/// 03BDB  as 14 etapas vencidas.
void finalWin(ProcCtx& c) {
    store(c, kCross, 2);
    store(c, 0xE483, 1);
    killIds(c, 0x500, 0xFFFFFFFFu);
    killIds(c, 0x5C4, 0xFFFFFFFFu);
    killIds(c, 0x110, 0xFFFFFFFFu);
    c.call(0x7648);
    if (c.item(0x24) == 1) return c.lcall(0xB101, 0xB102);  // 0x8481, volta em 0x3BF3
    finalAfterEnding(c);
}

/// 03BF3
void finalAfterEnding(ProcCtx& c) {
    store(c, kStage, 0xF);
    if (c.item(0x1F) != 0) return c.lcall(0xB103, 0xB104);  // 0x31A6, volta em 0x3BFF
    finalReset(c);
}

/// 03C01
void finalReset(ProcCtx& c) {
    store(c, 0xC941, 0);
    c.call(0x6FEB);
    c.call(0x5FA1);
    store(c, kMode, 2);
    if (c.rd(kPlace) == 1) {
        c.audit(0x71);
        c.audit(0xC2);
        c.audit(0x6A);
    }
    c.lcall(0xB105, 0xB106);  // 0xA1F1, volta em 0x3C16
}

} // namespace

void registerPostRace(SubroutineDispatcher& d) {
    addProc(d, 0x3F42, [](ProcCtx& c) {
        killIds(c, 0x200, 0xF00);
        killIds(c, 0x800, 0xF00);
        killIds(c, 0x5C0, 0xFF0);
        killIds(c, 0x5AA, 0xFFFFFFFFu);
        c.call(0x6C52);
    });
    addProc(d, 0x3F50, resettleProps);
    addProc(d, 0x3F64, [](ProcCtx& c) { killIds(c, 0x857, 0xFFFFFFFFu); });
    addProc(d, 0x6C52, freeType103);
    addProc(d, 0x32E7, recordCheck);
    addProc(d, 0x3D85, stageFlag);
    addProc(d, 0x3DB1, [](ProcCtx& c) { flagFly(c, true); });
    addProc(d, 0x3DC9, [](ProcCtx& c) { flagFly(c, false); });

    addProc(d, 0x3A46, raceOver);
    addProc(d, 0x3A64, resultsStart);
    addProc(d, 0x3A76, waitCurtain);
    addProc(d, 0x3A9B, scoreboards);
    addProc(d, 0x3ABA, afterPodium);
    addProc(d, 0x3ACC, [](ProcCtx& c) {
        if (s(--c.cpu.ar[5]) >= 0) return holdLoop(c);  // DBU AR5
        curtain(c);
    });
    addProc(d, 0x3AD2, [](ProcCtx& c) {
        store(c, 0xE53B, 1);
        c.sleep(0x1E, 0x3AD6);
    });
    addProc(d, 0x3AD6, mapScreen);
    addProc(d, 0x3B19, [](ProcCtx& c) {
        Tms32031State& cpu = c.cpu;
        cpu.r[7] = doubleToTms(tmsToDouble(cpu.r[7]) + 10.0);
        cpu.ar[3] += 4;
        cpu.r[4] += 1;
        if (s(cpu.r[4]) <= s(cpu.r[5])) return flagsLoop(c);
        waitSkip(c);
    });
    addProc(d, 0x3B23, [](ProcCtx& c) {
        if (s(--c.cpu.ar[5]) >= 0) return waitSkipLoop(c);
        afterWait(c);
    });
    addProc(d, 0x3B30, [](ProcCtx& c) {
        restoreWheel(c);
        decide(c);
    });
    addProc(d, 0x3B52, [](ProcCtx& c) {
        store(c, 0xE483, 1);
        c.sleep(0xA, 0x3B56);
    });
    addProc(d, 0x3B56, decideNext);
    addProc(d, 0x3B7D, afterContinue);
    addProc(d, 0x3B8F, [](ProcCtx& c) { c.lcall(0xB0FD, 0xB0FE); });  // 0x1617, volta em 0x3B95
    addProc(d, 0x3B95, nextStage);
    addProc(d, 0x3BF3, finalAfterEnding);
    addProc(d, 0x3BFF, [](ProcCtx& c) {
        restoreWheel(c);
        finalReset(c);
    });
    addProc(d, 0x3C16, [](ProcCtx& c) {
        store(c, 0xC8E8, static_cast<uint32_t>(-2));
        c.jump(0x1E17);
    });
}

} // namespace rea::core::game
