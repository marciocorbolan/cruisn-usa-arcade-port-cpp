/**
 * @file race_events.cpp
 * @brief Tradução de `0xACCA`–`0xAD92`, `0x9BEA`, `0x3966`–`0x39BE`, `0x5090`, `0x3C7A`,
 *        `0x90DD` e `0x691B`–`0x6958`.
 */
#include "core/game/race_events.hpp"

#include "core/game/process_ctx.hpp"

namespace rea::core::game {

namespace {

/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }

constexpr uint32_t kMode = 0xC8C5;  ///< Modo do jogo: bits 0–3 = modo; 0x40 e 0x100 = flags.

/// Liga @p bits na palavra em @p a.
void setBits(ProcCtx& c, uint32_t a, uint32_t bits) {
    c.cpu.r[0] = c.rd(a) | bits;
    c.wr(a, c.cpu.r[0]);
}
/// Desliga @p bits na palavra em @p a.
void clearBits(ProcCtx& c, uint32_t a, uint32_t bits) {
    c.cpu.r[0] = c.rd(a) & ~bits;
    c.wr(a, c.cpu.r[0]);
}
/// Grava @p v em @p a (via R0).
void store(ProcCtx& c, uint32_t a, uint32_t v) {
    c.cpu.r[0] = v;
    c.wr(a, v);
}

/// `LDI ptr,AR2 / LDI id,R2 / CALL $A844` (cria processo).
void spawn(ProcCtx& c, uint32_t ptr, uint32_t id, uint32_t fn = 0xA844) {
    c.cpu.ar[2] = c.rd(ptr);
    c.cpu.r[2] = id;
    c.call(fn);
}

/// `0xAD00` (sem contar) / `0xAD03`: checkpoint. Preserva R0 e AR2.
void checkpoint(ProcCtx& c, bool count) {
    Tms32031State& cpu = c.cpu;
    const uint32_t r0 = cpu.r[0], ar2 = cpu.ar[2];
    bool pass = true;
    if (count) {
        cpu.r[0] = c.rd(0xE869) + 1;
        c.wr(0xE869, cpu.r[0]);
        pass = s(cpu.r[0]) > s(c.rd(0xE63A));
    }
    if (pass) {
        c.call(0x7A36);
        store(c, 0xE63A, c.rd(0xE63A) + 1);
        store(c, 0xE604, c.rd(0xE604) + c.rd(0xE868));
        if ((c.rd(kMode) & 0xFu) == 4) c.sound(0xFF);
    }
    cpu.ar[2] = ar2;
    cpu.r[0] = r0;
}

/// `0x3998`: chegada da etapa R1 (limite R0).
void finishLine(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    c.wr(0xE480, cpu.r[0]);
    c.wr(0xE481, cpu.r[1]);
    cpu.r[0] = c.rd(kMode);
    cpu.r[1] = cpu.r[0] & 0xFu;
    if (cpu.r[1] != 4) return;
    store(c, kMode, ((cpu.r[0] & ~0xFu) | 5u) & ~0x400u);
    setBits(c, 0xC8DA, 0x800);
    cpu.r[0] = c.rd(0xE607);
    c.call(0x5090);
    cpu.r[2] = c.rd(0xE481);
    cpu.ar[2] = 0xDF;
    c.call(0x99F4);
    c.audit((c.rd(0xE481) - 1) * 2 + 0x9A);
    store(c, 0xE637, 0);
    c.call(0x3C7A);
    store(c, 0xE5F4, 0xB);
    c.wr(0xE868, c.item(0x1D));
    cpu.ar[4] = c.rd(0xE876);
    cpu.ar[5] = c.rd(0xE878);
    c.call(0x65A5);
    spawn(c, 0xB0D7, 0x16, 0xA868);
}

/// `0x693D`: desloca o objeto AR1 R4 unidades no rumo `-[809807]` e testa o carro AR0 (`0x221F`);
/// se o subtipo continuou ≠ 0, volta à posição guardada em `[E8ED/E8EE]`. Preserva AR3.
void pushStep(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t ar3 = cpu.ar[3];
    c.wr(0xE8ED, c.rd(cpu.ar[1] + 1));
    c.wr(0xE8EE, c.rd(cpu.ar[1] + 3));
    cpu.r[2] = c.rd(0x809807);
    cpu.dp = 0;
    c.call(0x9308);
    cpu.r[2] = doubleToTms(-tmsToDouble(cpu.r[2]));
    const double r4 = tmsToDouble(cpu.r[4]);
    c.call(0x92DF);
    c.wf(cpu.ar[1] + 1, tmsToDouble(cpu.r[0]) * r4 + c.rf(cpu.ar[1] + 1));
    c.call(0x92E2);
    c.wf(cpu.ar[1] + 3, tmsToDouble(cpu.r[0]) * r4 + c.rf(cpu.ar[1] + 3));
    c.call(0x221F);
    if ((c.rd(cpu.ar[1] + 0xF) & 0xFu) != 0) {
        c.wr(cpu.ar[1] + 1, c.rd(0xE8ED));
        c.wr(cpu.ar[1] + 3, c.rd(0xE8EE));
    }
    cpu.ar[3] = ar3;
}

/// `0x692C`: dois passos (`[AR3+3]` e `[AR3+4]`, AR3 = `[C62B]` + 5·(subtipo−1)) para subtipos
/// ≠ 0 e ≠ 3. Devolve `true` no quirk `POP AR3 / RETSU` (subtipo zerado no primeiro passo), que
/// desempilha o retorno para 0x6926 em AR3 e retorna direto ao chamador da `0x691B`.
bool pushObject(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[0] = c.rd(cpu.ar[1] + 0xF) & 0xFu;
    if (cpu.r[0] == 0 || cpu.r[0] == 3) return false;
    cpu.ar[3] = (cpu.r[0] - 1) * 5 + c.rd(0xC62B);
    cpu.r[4] = c.rd(cpu.ar[3] + 3);
    pushStep(c);
    cpu.r[0] = c.rd(cpu.ar[1] + 0xF) & 0xFu;
    if (cpu.r[0] == 0) {
        cpu.ar[3] = 0x6926;
        return true;
    }
    cpu.r[4] = c.rd(cpu.ar[3] + 4);
    pushStep(c);
    return false;
}

/// `0x691B`: objetos 0x75x junto ao carro `[E876]`.
void pushObjects(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[0] = c.rd(0xE876);
    if (cpu.ar[0] != 0) {
        cpu.ar[1] = c.rd(0x40);
        cpu.r[0] = c.rd(cpu.ar[1]);
        while (cpu.r[0] != 0) {
            cpu.ar[1] = cpu.r[0];
            if ((c.rd(cpu.ar[1] + 0xF) & 0xFF0u) == 0x750u && pushObject(c)) return;
            cpu.r[0] = c.rd(cpu.ar[1]);
        }
    }
    cpu.r[0] = doubleToTms(0.0);
    c.wr(0xE8ED, cpu.r[0]);
    c.wr(0xE8EE, cpu.r[0]);
}

} // namespace

void registerRaceEvents(SubroutineDispatcher& d) {
    // 0ACCA  despachante: AR0 = índice
    addProc(d, 0xACCA, [](ProcCtx& c) {
        if (c.cpu.ar[0] == 0) return;
        c.cpu.ar[0] = c.rd(c.cpu.ar[0] + c.rd(0xACD0));
        c.call(c.cpu.ar[0]);
    });
    addProc(d, 0xACFF, [](ProcCtx&) {});
    addProc(d, 0xAD44, [](ProcCtx&) {});
    addProc(d, 0xAD00, [](ProcCtx& c) { checkpoint(c, false); });
    addProc(d, 0xAD03, [](ProcCtx& c) { checkpoint(c, true); });
    addProc(d, 0xAD1A, [](ProcCtx& c) { setBits(c, kMode, 0x80); });
    addProc(d, 0xAD1E, [](ProcCtx& c) { clearBits(c, kMode, 0x80); });
    addProc(d, 0xAD22, [](ProcCtx& c) {
        spawn(c, 0xC619, 0x214);
        store(c, 0xE540, 0);
    });
    addProc(d, 0xAD28, [](ProcCtx& c) { store(c, 0xE540, 1); });
    addProc(d, 0xAD2B, [](ProcCtx& c) {
        store(c, 0xE5F4, 0);
        c.wr(0xE8E6, 0);
    });
    addProc(d, 0xAD2F, [](ProcCtx& c) { store(c, 0xE8E6, 1); });
    addProc(d, 0xAD32, [](ProcCtx& c) { clearBits(c, kMode, 0x800); });
    addProc(d, 0xAD36, [](ProcCtx& c) { setBits(c, kMode, 0x800); });
    addProc(d, 0xAD3A, [](ProcCtx& c) { c.call(0xAD32); });
    addProc(d, 0xAD3C, [](ProcCtx& c) { setBits(c, kMode, 0x400); });
    addProc(d, 0xAD40, [](ProcCtx& c) { clearBits(c, kMode, 0x400); });
    addProc(d, 0xAD45, [](ProcCtx& c) {
        setBits(c, kMode, 0x80);
        store(c, 0xE5F4, 0xB);
        store(c, 0xE5F2, 0x3C);
        store(c, 0xE5F3, 0x64);
    });
    addProc(d, 0xAD4F, [](ProcCtx& c) {
        c.cpu.r[0] = doubleToTms(10.0);
        c.wr(0xE3FB, c.cpu.r[0]);
        c.cpu.r[0] = doubleToTms(75.0);
        c.wr(0xE3FD, c.cpu.r[0]);
        store(c, 0xE5F4, 4);
        store(c, 0xE5F2, 0x78);
        store(c, 0xE5F3, 0x78);
    });
    // 0AD5A..0AD6C  sons ambientes
    static constexpr struct { uint32_t pc, id; } kSounds[] = {
        {0xAD5A, 0x15C}, {0xAD5D, 0x165}, {0xAD60, 0x162}, {0xAD63, 0x171},
        {0xAD66, 0x16E}, {0xAD69, 0x16B}, {0xAD6C, 0x168}};
    for (const auto& e : kSounds) {
        const uint32_t id = e.id;
        addProc(d, e.pc, [id](ProcCtx& c) { c.sound(id); });
    }
    addProc(d, 0xAD6F, [](ProcCtx& c) { c.call(0x90DD); });
    addProc(d, 0xAD71, [](ProcCtx& c) {
        c.call(0xAD03);
        store(c, 0xC938, (0x98u << 16) | 0x4900u);
        store(c, 0xC940, 1);
    });
    addProc(d, 0xAD79, [](ProcCtx& c) { clearBits(c, kMode, 0x40); });
    addProc(d, 0xAD7D, [](ProcCtx& c) { setBits(c, kMode, 0x40); });
    addProc(d, 0xAD81, [](ProcCtx& c) {
        Tms32031State& cpu = c.cpu;
        cpu.ar[2] = 0x4C;
        c.call(0x9ECD);
        cpu.ar[2] = c.rd(0xC61A);
        cpu.r[2] = cpu.r[0];
        cpu.r[3] = c.rd(cpu.ar[2]++);
        c.call(0x9F5C);
    });
    addProc(d, 0xAD88, [](ProcCtx& c) {
        Tms32031State& cpu = c.cpu;
        const uint32_t ar2 = cpu.ar[2], r2 = cpu.r[2];
        cpu.r[0] = 0x4C;
        cpu.r[1] = 0x4C;
        c.call(0x9EC2);
        cpu.r[2] = r2;
        cpu.ar[2] = ar2;
    });
    addProc(d, 0xAD90, [](ProcCtx& c) { store(c, 0xE635, 1); });
    addProc(d, 0x9BEA, [](ProcCtx& c) { spawn(c, 0xC62A, 0x500); });

    // 090DD  próxima paleta do céu: [E916] = ([E916] + 1) mod 8 e 0x90F0. Preserva R0.
    addProc(d, 0x90DD, [](ProcCtx& c) {
        const uint32_t r0 = c.cpu.r[0];
        uint32_t v = c.rd(0xE916) + 1;
        if (s(v) >= 8) v = 0;
        store(c, 0xE916, v);
        c.call(0x90F0);
        c.cpu.r[0] = r0;
    });

    // 03966  processo [B0D6] (0x840), névoa −35 e [C8C5] |= 0x80
    addProc(d, 0x3966, [](ProcCtx& c) {
        spawn(c, 0xB0D6, 0x840);
        c.cpu.r[0] = doubleToTms(-35.0);
        c.wr(0xE3FD, c.cpu.r[0]);
        setBits(c, kMode, 0x80);
    });

    // 03970..03996  linhas de chegada: (limite, etapa); 0x3970 mantém o R0 do chamador
    static constexpr struct { uint32_t pc, limit, stage; } kFinish[] = {
        {0x3972, 0x4D6, 0xD}, {0x3975, 0x465, 0xC}, {0x3978, 0x40A, 0xB}, {0x397B, 0x3A3, 0xA},
        {0x397E, 0x33D, 0x9}, {0x3981, 0x2D6, 0x8}, {0x3984, 0x266, 0x7}, {0x3987, 0x204, 0x6},
        {0x398A, 0x1A3, 0x5}, {0x398D, 0x13F, 0x4}, {0x3990, 0x0CF, 0x3}, {0x3993, 0x0A2, 0x2},
        {0x3996, 0x05D, 0x1}};
    for (const auto& e : kFinish) {
        const uint32_t limit = e.limit, stage = e.stage;
        addProc(d, e.pc, [limit, stage](ProcCtx& c) {
            c.cpu.r[0] = limit;
            c.cpu.r[1] = stage;
            finishLine(c);
        });
    }
    addProc(d, 0x3970, [](ProcCtx& c) {
        c.cpu.r[1] = 0xE;
        finishLine(c);
    });
    addProc(d, 0x3998, finishLine);

    // 05090  dificuldade adaptativa: [E639] += (R0 == 1 ? 0,07 : −0,035), em 0,8..1,25
    addProc(d, 0x5090, [](ProcCtx& c) {
        double r1 = (c.cpu.r[0] == 1 ? c.k(0x5091) : c.k(0x5092)) + c.rf(0xE639);
        if (r1 > c.k(0x5095)) r1 = c.k(0x5095);
        if (r1 < c.k(0x5097)) r1 = c.k(0x5097);
        c.cpu.r[1] = doubleToTms(r1);
        c.wr(0xE639, c.cpu.r[1]);
    });
    // 03C7A  para os sons de motor (0x3E8 e os canais 0x1BF..0x1CE)
    addProc(d, 0x3C7A, [](ProcCtx& c) {
        c.cpu.ar[2] = 0x3E8;
        c.call(0x9211);
        for (uint32_t id : {0x1BFu, 0x1C2u, 0x1C5u, 0x1CBu, 0x1CEu}) {
            c.cpu.ar[2] = id;
            c.call(0x91C1);
        }
    });

    addProc(d, 0x691B, pushObjects);
    addProc(d, 0x692C, [](ProcCtx& c) { (void)pushObject(c); });
    addProc(d, 0x693D, pushStep);
}

} // namespace rea::core::game
