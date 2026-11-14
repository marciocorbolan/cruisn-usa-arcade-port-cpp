/**
 * @file ending.cpp
 * @brief Tradução de `0x8481`–`0x8648`, `0xB075`–`0xB07F` e `0xC100B1`–`0xC1019D`.
 */
#include "core/game/ending.hpp"

#include <iterator>

#include "core/game/process_ctx.hpp"

namespace rea::core::game {

namespace {

/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }

constexpr uint32_t kTicks = 0xC92F;  ///< Quadros (VBLANKs) gastos na última iteração do loop principal.
constexpr uint32_t kCamera = 0x0045;   ///< `[0x45]`: posição da câmera.
constexpr uint32_t kSkyFlags = 0xC8C5;  ///< &0xF: 1 = boot/atração sem watchdog; 2/4 = outros modos.
constexpr uint32_t kPanelsDone = 0xE5F8;  ///< Painéis que terminaram de abrir ou fechar.
constexpr uint32_t kScenery = 0xE5FB;   ///< Objeto de cenário do encerramento.
constexpr uint32_t kListener = 0xE5FC;  ///< Objeto 0x621 (fonte do som 0xBA).
constexpr uint32_t kMatA = 0x005A;  ///< `[0x5A]`: matriz de rascunho.

/// Lê o registrador R@p i como float TMS.
double R(ProcCtx& c, int i) { return tmsToDouble(c.cpu.r[i]); }
/// Grava @p v, convertido para float TMS, no registrador R@p i.
void setR(ProcCtx& c, int i, double v) { c.cpu.r[i] = doubleToTms(v); }
/// Cria o processo `[ptr]` com o ID @p id (`0xA844`).
void spawn(ProcCtx& c, uint32_t ptr, uint32_t id) {
    c.cpu.ar[2] = c.rd(ptr);
    c.cpu.r[2] = id;
    c.call(0xA844);
}
/// Mata os processos com o ID @p id (`0xA8CF`).
void killId(ProcCtx& c, uint32_t id) {
    c.cpu.r[0] = id;
    c.cpu.r[1] = 0xFFFFFFFFu;
    c.call(0xA8CF);
}
/// Chama @p fn com AR2 = @p ar2.
void callAr2(ProcCtx& c, uint32_t fn, uint32_t ar2) {
    c.cpu.ar[2] = ar2;
    c.call(fn);
}
/// `DBU AR5,alvo`: AR5 −= 1 e repete enquanto AR5 ≥ 0.
bool dbu(ProcCtx& c) {
    c.cpu.ar[5] -= 1;
    return s(c.cpu.ar[5]) >= 0;
}
/// `p += (alvo − p)·k` numa coordenada da câmera.
void easeCamera(ProcCtx& c, uint32_t off, double target, uint32_t kPc) {
    const uint32_t a = c.rd(kCamera) + off;
    setR(c, 1, target);
    setR(c, 2, (target - c.rf(a)) * c.k(kPc));
    setR(c, 0, c.rf(a) + R(c, 2));
    c.wr(a, c.cpu.r[0]);
}

// --- Encerramento (`0x8481`) ------------------------------------------------------------------

/// `0x8481`: início do encerramento (câmera, paletas, cenário, textos e processos).
void endingStart(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    c.wr(0xE483, 1);
    c.wr(0xC940, 0);
    c.wr(kSkyFlags, c.rd(kSkyFlags) | 0xC0);
    c.wf(0xE3FD, 0.0);
    c.call(0x6FEB);
    cpu.ar[6] = c.rd(kCamera);
    c.wf(cpu.ar[6] + 1, -600.0);
    c.wf(cpu.ar[6], 0.0);
    c.wf(cpu.ar[6] + 2, -12000.0);
    setR(c, 2, 0.0);
    c.wr(0x809807, cpu.r[2]);
    cpu.dp = 0;
    callAr2(c, 0x938A, c.rd(0x47));
    callAr2(c, 0x9DFE, c.rd(0xB14D));
    callAr2(c, 0x9DF6, c.rd(0xB14E));
    callAr2(c, 0x9DF6, c.rd(0xB14F));
    c.waitDma(1);
    c.waitDma(2);
    c.call(0x4EE3);
    c.wr(0xE5CB, 1);
    callAr2(c, 0xA3C2, c.rd(0xB150));
    c.wr(0xE5CB, 1);
    callAr2(c, 0xA3C2, c.rd(0xB151));
    callAr2(c, 0xA3C2, c.rd(0xB152));
    c.wr(kSkyFlags, c.rd(kSkyFlags) & ~0xC0u);
    callAr2(c, 0x91E0, 3);
    callAr2(c, 0x91E0, 6);
    cpu.ar[2] = c.rd(0xB153);
    cpu.r[2] = 0;
    cpu.r[3] = static_cast<uint32_t>(-30);
    cpu.rc = 0x170;
    c.call(0x7309);
    c.orField(cpu.ar[0], 0xE, 0x29);
    cpu.ar[2] = cpu.ar[0];
    c.wr(kScenery, cpu.ar[2]);
    c.call(0x7070);
    c.text(0x7651, c.rd(0x847F), 256.0, 270.0, 0x2E, true);
    c.text(0x7651, c.rd(0x8480), 256.0, 300.0, 0x2E, true);
    spawn(c, 0xB154, 0x1234);
    c.sleep(15, 0x84E0);
}


/// Trecho traduzido da ROM em `0x84F8`.
void endingApproach(ProcCtx& c) {
    easeCamera(c, 2, -1500.0, 0x84FC);
    easeCamera(c, 1, -4300.0, 0x8502);
    c.sleep(1, 0x8507);
}

/// Trecho traduzido da ROM em `0x8519`.
void endingRetreat(ProcCtx& c) {
    easeCamera(c, 2, -12000.0, 0x851D);
    if (c.cpu.ar[5] == 0x3C) {
        callAr2(c, 0x91E0, 0x15);
        c.cpu.r[0] = 3;
        c.cpu.r[1] = 0x82;
        c.call(0x9114);
    }
    c.sleep(1, 0x8529);
}

/// Registra o fluxo do encerramento.
void registerEndingFlow(SubroutineDispatcher& d) {
    addProc(d, 0x8481, endingStart);
    addProc(d, 0x84E0, [](ProcCtx& c) {
        c.call(0xB06F);
        callAr2(c, 0x440C, c.rd(0xB155));
        c.call(0x858C);
        spawn(c, 0xB156, 0x1235);
        c.wr(kSkyFlags, c.rd(kSkyFlags) | 0xC0);
        c.sleep(5, 0x84EC);
    });
    addProc(d, 0x84EC, [](ProcCtx& c) {
        spawn(c, 0xB157, 0x1234);
        c.sleep(25, 0x84F1);
    });
    addProc(d, 0x84F1, [](ProcCtx& c) {
        callAr2(c, 0x7100, c.rd(kScenery));
        c.sleep(30, 0x84F5);
    });
    addProc(d, 0x84F5, [](ProcCtx& c) {
        c.wr(0xC940, 1);
        c.cpu.ar[5] = 0xB4;
        endingApproach(c);
    });
    addProc(d, 0x8507, [](ProcCtx& c) {
        if (dbu(c)) return endingApproach(c);
        c.sleep(300, 0x850A);
    });
    addProc(d, 0x850A, [](ProcCtx& c) {
        c.call(0xB075);
        killId(c, 0x1235);
        callAr2(c, 0x91C1, 0xBA);
        callAr2(c, 0x91E0, 0x1E);
        c.sleep(15, 0x8514);
    });
    addProc(d, 0x8514, [](ProcCtx& c) {
        callAr2(c, 0x9188, 0x8A);
        callAr2(c, 0x9188, 0x8D);
        c.cpu.ar[5] = 100;
        endingRetreat(c);
    });
    addProc(d, 0x8529, [](ProcCtx& c) {
        if (dbu(c)) return endingRetreat(c);
        c.sleep(1, 0x852C);
    });
    addProc(d, 0x852C, [](ProcCtx& c) {
        c.call(0x6FEB);
        c.call(0x6754);
        c.call(0x8FF0);
        c.call(0x9019);
        c.call(0xAF55);
        killId(c, 0x1234);
        killId(c, 0x850);
        callAr2(c, 0x9DFE, c.rd(0xB158));
        callAr2(c, 0x9DFE, c.rd(0xB159));
        c.lret();
    });
}

// --- Processos do encerramento ----------------------------------------------------------------

/// `0x853F`: volume do som 0xBA = 205 (até 2000), 205·(1 − (d − 2000)·`[B15A]`) limitado a 180, ou 10
/// (além de 12000), com d = distância xz de `[E5FC]` à câmera.
void engineVolume(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[4] = c.rd(kListener);
    cpu.ar[0] = c.rd(kCamera);
    const double dx = c.rf(cpu.ar[4] + 1) - c.rf(cpu.ar[0]);
    const double dz = c.rf(cpu.ar[4] + 3) - c.rf(cpu.ar[0] + 2);
    setR(c, 2, dx * dx + dz * dz);
    c.call(0xA5F1);
    const double d = R(c, 0);
    uint32_t vol;
    if (!(d > 2000.0)) {
        vol = 0xCD;
    } else if (d > 12000.0) {
        vol = 10;
    } else {
        const double v = (1.0 - (d - 2000.0) * c.rf(0xB15A)) * c.k(0x8556);
        setR(c, 0, v);
        vol = tmsFix(v);
        if (s(vol) > 0xB4) vol = 0xB4;
    }
    cpu.r[2] = vol;
    callAr2(c, 0x4330, 0xBA);
    c.sleep(1, 0x855E);
}

/// `0x855F`: sons do encerramento (quadros de espera, `0x9188` = toca, `0x91E0` = trilha).
struct SoundStep {
    uint32_t pc;     ///< Ponto de retomada.
    uint32_t fn;     ///< Rotina chamada; 0 = nada.
    uint32_t arg;  ///< Argumento da 1ª chamada (AR2).
    uint32_t fn2;    ///< segunda chamada (0 = nenhuma).
    uint32_t arg2;  ///< Argumento da 2ª chamada.
    uint32_t wait;   ///< 0 = morre.
};
/// Passos da sequência de sons do encerramento.
constexpr SoundStep kSoundSteps[] = {
    {0x855F, 0, 0, 0, 0, 0x19},
    {0x8561, 0x9188, 0x8D, 0, 0, 0x19},
    {0x8565, 0x91E0, 0x09, 0, 0, 0x2C},
    {0x8569, 0x91E0, 0x0C, 0x9188, 0x8A, 0x4B},
    {0x856F, 0x91E0, 0x0F, 0x9188, 0x8D, 0x32},
    {0x8575, 0x91E0, 0x15, 0, 0, 0x28},
    {0x8579, 0x91E0, 0x12, 0, 0, 0x3C},
    {0x857D, 0x91E0, 0x21, 0, 0, 0x46},
    {0x8581, 0x91E0, 0x18, 0, 0, 0x1E},
    {0x8585, 0x9188, 0x00, 0, 0, 0x28},
    {0x8589, 0x91E0, 0x1B, 0, 0, 0},
};

/// `0x860F`: troca o modelo `+D` pela sequência AR6 (−1 = recomeça) a cada 9..11 quadros.
void frameAnimNext(ProcCtx& c);
/// Espera um quadro e volta a `0x8618`.
void frameAnimWait(ProcCtx& c) { c.sleep(1, 0x8618); }
void frameAnimNext(ProcCtx& c) {  // 08610
    Tms32031State& cpu = c.cpu;
    cpu.r[0] = c.rd(cpu.ar[5]++);
    if (s(cpu.r[0]) < 0) {
        cpu.ar[5] = cpu.ar[6];
        return frameAnimNext(c);
    }
    c.wr(cpu.ar[4] + 0xD, cpu.r[0]);
    cpu.r[4] = 9;
    cpu.ar[2] = 3;
    c.call(0x8EDB);
    cpu.r[4] += cpu.r[0];
    frameAnimWait(c);
}

/// `0x861C`: espera 20..69 quadros e passa a sequência AR6 (5 = pausa de 12, 15 quadros por modelo).
void blinkStart(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[2] = 0x32;
    c.call(0x8EDB);
    cpu.ar[2] = cpu.r[0] + 0x14;
    cpu.ret = 0x8621;
    c.call(0xA874);
}
/// Trecho traduzido da ROM em `0x8622`.
void blinkNext(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[0] = c.rd(cpu.ar[5]++);
    if (s(cpu.r[0]) < 0) return blinkStart(c);
    if (cpu.r[0] == 5) return c.sleep(12, 0x8628);
    c.wr(cpu.ar[4] + 0xD, cpu.r[0]);
    cpu.r[4] = 0xF;
    c.sleep(1, 0x862D);
}

/// `0x863B`: linha de texto AR2 em (256, 20) com cor RC e paleta `0x9EDB([B162])` em `+C`.
void caption(ProcCtx& c, uint32_t text, uint32_t rc) {
    Tms32031State& cpu = c.cpu;
    c.text(0x7651, text, 256.0, 20.0, rc, true);
    cpu.ar[2] = c.rd(0xB162);
    c.call(0x9EDB);
    c.wr(cpu.ar[0] + 0xC, cpu.r[0]);
    c.wr(cpu.ar[1] + 0xC, cpu.r[0]);
}

/// `0x85D4`…`0x8608`: processo `[ptr]` (ID 0x850) com a tabela AR6 = `[table]`; AR6 preservado.
void animSpawn(ProcCtx& c, uint32_t table, uint32_t ptr) {
    const uint32_t ar6 = c.cpu.ar[6];
    c.cpu.ar[6] = c.rd(table);
    spawn(c, ptr, 0x850);
    c.cpu.ar[6] = ar6;
}

/// `0x858C`: anima os objetos da lista `[0x40]` pelo tipo `+F`.
void animateScene(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[4] = c.rd(0x40);
    for (;;) {
        cpu.r[3] = c.rd(cpu.ar[4]);
        if (cpu.r[3] == 0) return;
        cpu.ar[4] = cpu.r[3];
        cpu.r[2] = c.rd(cpu.ar[4] + 0xF);
        switch (cpu.r[2]) {
            case 0x6D0: c.call(0x85D4); break;
            case 0x621: c.call(0x85F9); break;
            case 0x622: c.call(0x85EB); break;
            case 0x623: c.call(0x85F2); break;
            case 0x615: c.call(0x8601); break;
            case 0x616: c.call(0x8608); break;
            default: break;
        }
    }
}

/// Registra os processos do encerramento (volume, sons, legendas e animações).
void registerEndingProcs(SubroutineDispatcher& d) {
    addProc(d, 0x853F, engineVolume);
    addProc(d, 0x855E, engineVolume);
    for (std::size_t i = 0; i < std::size(kSoundSteps); ++i) {
        addProc(d, kSoundSteps[i].pc, [i](ProcCtx& c) {
            const SoundStep& st = kSoundSteps[i];
            if (st.fn != 0) callAr2(c, st.fn, st.arg);
            if (st.fn2 != 0) callAr2(c, st.fn2, st.arg2);
            if (st.wait == 0) return c.die();
            c.sleep(st.wait, kSoundSteps[i + 1].pc);
        });
    }
    addProc(d, 0x858C, animateScene);
    addProc(d, 0x85D4, [](ProcCtx& c) {
        const uint32_t ar6 = c.cpu.ar[6];
        setR(c, 0, c.k(0x85D5));
        for (uint32_t off : {4u, 8u, 0xCu}) c.wr(c.cpu.ar[4] + off, c.cpu.r[0]);
        c.cpu.ar[6] = c.rd(0x435C);
        spawn(c, 0xB15B, 0x850);
        c.cpu.ar[6] = ar6;
    });
    addProc(d, 0x85DF, [](ProcCtx& c) {
        const uint32_t ar6 = c.cpu.ar[6];
        callAr2(c, 0xA8AC, c.rd(c.cpu.ar[4] + 0x17));
        c.cpu.r[0] = c.rd(c.cpu.ar[4] + 0xE) & ~8u;
        c.wr(c.cpu.ar[4] + 0xE, c.cpu.r[0]);
        c.cpu.ar[6] = c.rd(0x4357);
        spawn(c, 0xB15C, 0x850);
        c.cpu.ar[6] = ar6;
    });
    addProc(d, 0x85EB, [](ProcCtx& c) { animSpawn(c, 0x85B2, 0xB15D); });
    addProc(d, 0x85F2, [](ProcCtx& c) { animSpawn(c, 0x85BA, 0xB15E); });
    addProc(d, 0x85F9, [](ProcCtx& c) {
        c.wr(kListener, c.cpu.ar[4]);
        animSpawn(c, 0x85AA, 0xB15F);
    });
    addProc(d, 0x8601, [](ProcCtx& c) { animSpawn(c, 0x85C2, 0xB160); });
    addProc(d, 0x8608, [](ProcCtx& c) { animSpawn(c, 0x85CD, 0xB161); });
    addProc(d, 0x860F, [](ProcCtx& c) {
        c.cpu.ar[5] = c.cpu.ar[6];
        frameAnimNext(c);
    });
    addProc(d, 0x8618, [](ProcCtx& c) {
        c.cpu.r[4] -= c.rd(kTicks);
        if (s(c.cpu.r[4]) <= 0) return frameAnimNext(c);
        frameAnimWait(c);
    });
    addProc(d, 0x861C, blinkStart);
    addProc(d, 0x8621, [](ProcCtx& c) {
        c.cpu.ar[5] = c.cpu.ar[6];
        blinkNext(c);
    });
    addProc(d, 0x8628, blinkNext);
    addProc(d, 0x862D, [](ProcCtx& c) {
        c.cpu.r[4] -= c.rd(kTicks);
        if (s(c.cpu.r[4]) <= 0) return blinkNext(c);
        c.sleep(1, 0x862D);
    });
    addProc(d, 0x8632, [](ProcCtx& c) {
        caption(c, c.rd(0x8630), 0xF);
        c.sleep(0x1A, 0x8637);
    });
    addProc(d, 0x8637, [](ProcCtx& c) {
        caption(c, c.rd(0x8631), 0x14);
        c.die();
    });
    addProc(d, 0x863B, [](ProcCtx& c) { caption(c, c.cpu.ar[2], c.cpu.rc); });
}

// --- Painéis (`0xB07B`–`0xB07F`, `0xC100B1`–`0xC1019D`) -----------------------------------------

/// `0xC10194`: copia RC + 1 palavras de AR0 para AR1 (preserva AR0, AR1 e R0).
void copyWords(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t n = cpu.rc + 1;
    for (uint32_t i = 0; i < n; ++i) c.wr(cpu.ar[1] + i, c.rd(cpu.ar[0] + i));
    cpu.rc = 0xFFFFFFFFu;
}

/// `0xC1014F`: 4 painéis `[B06B+i]` com flags `[B136+i]` em `*+AR7(C+i)`; guarda a posição em `+1E..+20`.
void createPanels(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    for (uint32_t i = 0; i < 4; ++i) {
        const uint32_t model = c.rd(0xB06B + i);
        cpu.ar[1] = model;
        c.call(0x7025);
        cpu.ar[4] = cpu.ar[0];
        c.wr(cpu.ar[4] + 0xE, c.rd(0xB136 + i));
        c.setVar(0xC + i, cpu.ar[0]);
        c.wr(cpu.ar[4] + 0xD, model);
        c.wf(cpu.ar[4] + 3, 368.0);
        cpu.ar[2] = cpu.ar[4];
        c.call(0x7070);
        for (uint32_t k = 0; k < 3; ++k) {
            cpu.r[0] = c.rd(cpu.ar[4] + 1 + k);
            c.wr(cpu.ar[4] + 0x1E + k, cpu.r[0]);
        }
        cpu.ar[1] = model;
    }
}

/// Painel `*+AR7(n)`: `+off` = valor (ou += delta), posição = `[5A]`·(`+1E..+20`), matriz = `[5A]`.
void movePanel(ProcCtx& c, uint32_t n, uint32_t off, double v, bool add) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[0] = c.var(n);
    setR(c, 0, add ? c.rf(cpu.ar[0] + off) + v : v);
    c.wr(cpu.ar[0] + off, cpu.r[0]);
    const uint32_t p = cpu.ar[0];
    cpu.ar[2] = p + 0x1E;
    cpu.r[2] = c.rd(kMatA);
    cpu.r[3] = p + 1;
    c.call(0x941E);
    cpu.ar[1] = p + 4;
    cpu.ar[0] = cpu.r[2];
    cpu.rc = 8;
    c.call(0xB07F);
}

/// `0xC100BB`: R7 ·= `*+AR7(13)`, giro R6 += 0.2, os painéis se afastam ±R7.
void panelsOpenStep(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    setR(c, 7, R(c, 7) * c.rf(cpu.ar[7] + 0x13));
    setR(c, 6, R(c, 6) + c.k(0xC100BC));
    cpu.r[2] = cpu.r[6];
    cpu.ar[2] = c.rd(kMatA);
    c.call(0x93C4);
    const double r7 = R(c, 7);
    movePanel(c, 0xC, 0x1E, r7, true);
    movePanel(c, 0xD, 0x1E, -r7, true);
    movePanel(c, 0xE, 0x1F, -r7, true);
    movePanel(c, 0xF, 0x1F, r7, true);
    c.sleep(1, 0xC100FE);
}

/// Abre os painéis com o fator em @p factorPc e espera 0x13 quadros.
void panelsOpen(ProcCtx& c, uint32_t factorPc) {
    setR(c, 0, c.k(factorPc));
    c.setVar(0x13, c.cpu.r[0]);
    c.call(0xB07E);
    c.sleep(0x13, 0xC100B8);
}

/// `0xC1010E`: R7 ·= 0.91, giro R6 −= 0.2, os painéis voltam a ±R7.
void panelsCloseStep(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    setR(c, 7, R(c, 7) * c.k(0xC1010E));
    setR(c, 6, R(c, 6) - c.k(0xC1010F));
    cpu.r[2] = cpu.r[6];
    cpu.ar[2] = c.rd(kMatA);
    c.call(0x93C4);
    const double r7 = R(c, 7);
    movePanel(c, 0xC, 0x1E, r7, false);
    movePanel(c, 0xD, 0x1E, -r7, false);
    movePanel(c, 0xE, 0x1F, -r7, false);
    movePanel(c, 0xF, 0x1F, r7, false);
    c.sleep(1, 0xC1014B);
}

/// Registra os processos dos 4 painéis.
void registerPanels(SubroutineDispatcher& d) {
    addProc(d, 0xB075, [](ProcCtx& c) {
        c.wr(kPanelsDone, 0);
        spawn(c, 0xB134, 0x5C0);
    });
    addProc(d, 0xB07B, [](ProcCtx& c) { panelsOpen(c, 0xC100B1); });
    addProc(d, 0xC100B1, [](ProcCtx& c) { panelsOpen(c, 0xC100B1); });
    addProc(d, 0xB07C, [](ProcCtx& c) { panelsOpen(c, 0xC100B3); });
    addProc(d, 0xC100B3, [](ProcCtx& c) { panelsOpen(c, 0xC100B3); });
    addProc(d, 0xC100B8, [](ProcCtx& c) {
        c.cpu.ar[5] = 0x40;
        setR(c, 6, 0.0);
        setR(c, 7, c.k(0xC100BA));
        panelsOpenStep(c);
    });
    addProc(d, 0xC100FE, [](ProcCtx& c) {
        if (dbu(c)) return panelsOpenStep(c);
        for (uint32_t n = 0xC; n <= 0xF; ++n) callAr2(c, 0x7100, c.var(n));
        c.wr(kPanelsDone, 1);
        c.die();
    });
    auto closeStart = [](ProcCtx& c) {
        c.call(0xB07E);
        c.cpu.ar[5] = 100;
        setR(c, 6, c.k(0xC1010C));
        c.cpu.r[7] = c.rd(0xB135);
        panelsCloseStep(c);
    };
    addProc(d, 0xB07D, closeStart);
    addProc(d, 0xC1010A, closeStart);
    addProc(d, 0xC1014B, [](ProcCtx& c) {
        if (dbu(c)) return panelsCloseStep(c);
        c.wr(kPanelsDone, 1);
        c.die();
    });
    addProc(d, 0xB07E, createPanels);
    addProc(d, 0xC1014F, createPanels);
    addProc(d, 0xB07F, copyWords);
    addProc(d, 0xC10194, copyWords);
}

} // namespace

void registerEnding(SubroutineDispatcher& d) {
    registerEndingFlow(d);
    registerEndingProcs(d);
    registerPanels(d);
}

} // namespace rea::core::game
