/**
 * @file credits.cpp
 * @brief Tradução de `0xA179`–`0xA1E9` e `0xA1F1`–`0xA2AA` (créditos finais).
 */
#include "core/game/credits.hpp"

#include "core/game/process_ctx.hpp"

namespace rea::core::game {

namespace {

/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }

constexpr uint32_t kCreditsId = 0xFFFF88D0u;  ///< `LDI −$7730,R2`.
constexpr uint32_t kStyle = 0xE5FA;           ///< Rotina de estilo das linhas.
constexpr uint32_t kSpacing = 0xE5F9;         ///< Espaçamento (float).
constexpr uint32_t kFrozen = 0xE91C;  ///< != 0: não move os textos.
constexpr uint32_t kTextList = 0x763F;  ///< `[763F]` = 0xE91A.
constexpr uint32_t kTextFree = 0x763E;  ///< `[763E]` = 0xE919.
constexpr uint32_t kTextFreeCount = 0xE91B;  ///< Nós de texto livres (a `0x7648` começa com 0xA0).

/// Lê o registrador R@p i como float TMS.
double R(ProcCtx& c, int i) { return tmsToDouble(c.cpu.r[i]); }
/// Grava @p v, convertido para float TMS, no registrador R@p i.
void setR(ProcCtx& c, int i, double v) { c.cpu.r[i] = doubleToTms(v); }
/// Cria o processo de créditos com o código em `[@p ptr]` (`0xA844`).
void spawn(ProcCtx& c, uint32_t ptr) {
    c.cpu.ar[2] = c.rd(ptr);
    c.cpu.r[2] = kCreditsId;
    c.call(0xA844);
}
/// Liga @p bits no campo 8 dos objetos AR0 e AR1.
void orFlag(ProcCtx& c, uint32_t bits) {
    c.orField(c.cpu.ar[0], 8, bits);
    c.orField(c.cpu.ar[1], 8, bits);
}

/// `0xA295`: linha (x = R2, y = R3, AR2 = texto, vida 9999) subindo 1 por quadro, estilo `[E5FA]`.
void creditLine(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.rc = 0x270F;
    c.call(0x7651);
    setR(c, 0, c.k(0xA297));
    c.wr(cpu.ar[0] + 6, cpu.r[0]);
    c.wr(cpu.ar[1] + 6, cpu.r[0]);
    cpu.r[0] = c.rd(kStyle);
    c.call(cpu.r[0]);
}

/// Lê `x, texto` do roteiro e cria a linha em y = R6.
void lineFromScript(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    setR(c, 2, static_cast<double>(s(c.rd(cpu.ar[4]++))));
    cpu.r[3] = cpu.r[6];
    cpu.ar[2] = c.rd(cpu.ar[4]++);
    creditLine(c);
}

/// Cria o processo @p ptr com AR4/AR5 = os dois textos (AR4/AR5 do roteiro preservados).
void spawnForLine(ProcCtx& c, uint32_t ptr) {
    Tms32031State& cpu = c.cpu;
    const uint32_t ar4 = cpu.ar[4], ar5 = cpu.ar[5];
    cpu.ar[4] = cpu.ar[0];
    cpu.ar[5] = cpu.ar[1];
    spawn(c, ptr);
    cpu.ar[5] = ar5;
    cpu.ar[4] = ar4;
}

void scriptWait(ProcCtx& c);

/// `0xA21A`: executa os comandos até precisar esperar.
void scriptStep(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[7] = c.rd(cpu.ar[4]++);
    switch (s(cpu.r[7])) {
        case 10:
            c.wr(kStyle, c.rd(cpu.ar[4]++));
            c.wf(kSpacing, c.k(0xA21F));
            return scriptWait(c);
        case 8:
            lineFromScript(c);
            break;
        case 7:
            lineFromScript(c);
            orFlag(c, 0x200);
            break;
        case 4:
            c.wr(kFrozen, 1);
            return c.sleep(60, 0xA237);
        case 99:
            cpu.ar[4]++;  // o x do roteiro é ignorado
            setR(c, 2, -344.0);
            cpu.r[3] = cpu.r[6];
            cpu.ar[2] = c.rd(cpu.ar[4]++);
            creditLine(c);
            orFlag(c, 0x100);
            spawnForLine(c, 0xB14B);
            break;
        case 3:
            lineFromScript(c);
            orFlag(c, 0x100);
            spawnForLine(c, 0xB14C);
            break;
        case -1:
            cpu.r[0] = kCreditsId;
            cpu.r[1] = 0xFFFFFFFFu;
            c.call(0xA8CF);
            return c.lret();
        case 0:
            break;
        case 2:
            lineFromScript(c);
            lineFromScript(c);
            orFlag(c, 0x200);
            break;
        default:
            lineFromScript(c);
            orFlag(c, 0x100);
            break;
    }
    // 0A212
    setR(c, 6, R(c, 6) + c.rf(kSpacing));
    scriptWait(c);
}

/// `0xA213`: enquanto R6 ≥ 402, espera um quadro e desce R6 de 1.
void scriptWait(ProcCtx& c) {
    if (R(c, 6) < 402.0) return scriptStep(c);
    c.sleep(1, 0xA218);
}

/// Trecho traduzido da ROM em `0xA1F1`.
void creditsStart(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    c.wr(0xC8C5, 2);
    c.wr(0xE541, 1);
    c.wr(0xC938, 0);
    c.wr(0xC940, 0);
    setR(c, 2, 0.0);
    for (uint32_t p : {0x45u, 0x46u}) {
        cpu.ar[6] = c.rd(p);
        for (uint32_t k = 0; k < 3; ++k) c.wr(cpu.ar[6] + k, cpu.r[2]);
    }
    cpu.ar[2] = c.rd(0x47);
    c.call(0x938A);
    c.sleep(1, 0xA205);
}

/// Trecho traduzido da ROM em `0xA205`.
void creditsScript(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    spawn(c, 0xB148);
    spawn(c, 0xB149);
    c.wr(kStyle, c.rd(0xB14A));
    c.wf(kSpacing, c.k(0xA20D));
    setR(c, 6, 420.0);
    cpu.ar[5] = 2;
    cpu.ar[4] = c.rd(0xA2AB);
    setR(c, 6, R(c, 6) + c.rf(kSpacing));
    scriptWait(c);
}

// --- Processos auxiliares ----------------------------------------------------------------------

/// `0xA179`: textos que passaram de y = −25 voltam à lista livre.
void recycleTexts(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[4] = c.rd(kTextList);
    cpu.r[0] = c.rd(cpu.ar[4]);
    while (cpu.r[0] != 0) {
        cpu.ar[4] = cpu.r[0];
        if (c.rf(cpu.ar[4] + 4) > c.k(0xA17E)) {
            cpu.r[0] = c.rd(cpu.ar[4]);
            continue;
        }
        cpu.r[7] = c.rd(cpu.ar[4]);
        cpu.ar[1] = c.rd(kTextList);
        while (c.rd(cpu.ar[1]) != cpu.ar[4]) cpu.ar[1] = c.rd(cpu.ar[1]);
        c.wr(cpu.ar[1], c.rd(cpu.ar[4]));
        c.wr(kTextFreeCount, c.rd(kTextFreeCount) + 1);
        cpu.ar[1] = c.rd(kTextFree);
        c.wr(cpu.ar[4], c.rd(cpu.ar[1]));
        c.wr(cpu.ar[1], cpu.ar[4]);
        cpu.r[0] = cpu.r[7];
    }
    c.sleep(1, 0xA194);
}

/// `0xA1D1`: gira o anel (`+15` += 0.01; x, y = 130·(sen, cos)).
void ringSpin(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    for (uint32_t o = c.rd(c.rd(0x40)); o != 0; o = c.rd(o)) {
        cpu.ar[0] = o;
        setR(c, 2, c.rf(o + 0x15) + c.k(0xA1D6));
        c.wr(o + 0x15, cpu.r[2]);
        cpu.r[6] = cpu.r[2];
        c.call(0x92E2);
        c.wf(o + 1, R(c, 0) * c.k(0xA1DA));
        cpu.r[2] = cpu.r[6];
        c.call(0x92DF);
        c.wf(o + 2, R(c, 0) * c.k(0xA1DE));
        cpu.ar[0] = o;
    }
    c.sleep(1, 0xA1E3);
}

/// `0xA1A1`: cenário dos créditos (paleta `[B142]`, gráficos `[B143]`) e anel de objetos `[B144]`.
void ringStart(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.dp = 0;
    cpu.ar[6] = c.rd(0x45);
    for (uint32_t k = 0; k < 3; ++k) c.wf(cpu.ar[6] + k, 0.0);
    cpu.ar[2] = c.rd(0xB142);
    c.call(0x9DF6);
    c.waitDma(1);
    c.waitDma(2);
    c.call(0x4EE3);
    c.wr(0xE5CB, 1);
    cpu.ar[2] = c.rd(0xB143);
    c.call(0xA3C2);
    cpu.ar[5] = c.rd(0xB144);
    setR(c, 6, c.k(0xA1B9));
    for (;;) {
        cpu.r[0] = c.rd(cpu.ar[5]++);
        if (s(cpu.r[0]) < 0) break;
        cpu.ar[2] = cpu.r[0];
        c.call(0x700A);
        const uint32_t o = cpu.ar[0];
        cpu.r[2] = cpu.r[6];
        c.call(0xB01A);
        c.wf(o + 1, R(c, 0) * c.k(0xA1C2));
        cpu.r[2] = cpu.r[6];
        c.call(0xB042);
        c.wf(o + 2, R(c, 0) * c.k(0xA1C8));
        c.wf(o + 3, 368.0);
        c.wr(o + 0x15, cpu.r[6]);
        cpu.ar[0] = o;
        cpu.ar[2] = o;
        c.call(0x7093);
        setR(c, 6, R(c, 6) - c.k(0xA1CF));
    }
    ringSpin(c);
}

/// `0xA24E`: x dos dois textos AR4/AR5 vai a 259/256 (5% por quadro).
void slideIn(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    c.wf(cpu.ar[4] + 3, (259.0 - c.rf(cpu.ar[4] + 3)) * c.k(0xA251) + c.rf(cpu.ar[4] + 3));
    c.wf(cpu.ar[5] + 3, (256.0 - c.rf(cpu.ar[5] + 3)) * c.k(0xA257) + c.rf(cpu.ar[5] + 3));
    c.sleep(1, 0xA24E);
}

/// `0xA26F`: em y ≤ 250 troca o texto de AR4/AR5 por `[A26C]` e morre.
void swapText(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    if (c.rf(cpu.ar[4] + 4) > 250.0) return c.sleep(1, 0xA26F);
    cpu.r[0] = c.rd(0xA26C);
    c.wr(cpu.ar[4] + 1, cpu.r[0]);
    c.wr(cpu.ar[5] + 1, cpu.r[0]);
    c.die();
}

} // namespace

void registerCredits(SubroutineDispatcher& d) {
    addProc(d, 0xA1F1, creditsStart);
    addProc(d, 0xA205, creditsScript);
    addProc(d, 0xA218, [](ProcCtx& c) {
        setR(c, 6, R(c, 6) - c.k(0xA218));
        scriptWait(c);
    });
    addProc(d, 0xA237, [](ProcCtx& c) {
        c.wr(kFrozen, 0);
        scriptWait(c);
    });
    addProc(d, 0xA295, creditLine);
    addProc(d, 0xA29D, [](ProcCtx& c) { orFlag(c, 0x100); });
    addProc(d, 0xA2A4, [](ProcCtx& c) { orFlag(c, 0x200); });
    addProc(d, 0xA179, recycleTexts);
    addProc(d, 0xA194, recycleTexts);
    addProc(d, 0xA1A1, ringStart);
    addProc(d, 0xA1E3, ringSpin);
    addProc(d, 0xA1E4, [](ProcCtx& c) {
        Tms32031State& cpu = c.cpu;
        c.call(0x76FA);
        cpu.ar[2] = c.rd(0xB145);
        c.call(0x9EDB);
        c.wr(cpu.ar[0] + 0xC, cpu.r[0]);
        c.wr(cpu.ar[1] + 0xC, cpu.r[0]);
    });
    addProc(d, 0xA24C, [](ProcCtx& c) { c.sleep(1, 0xA24E); });
    addProc(d, 0xA24E, slideIn);
    addProc(d, 0xA26D, [](ProcCtx& c) { c.sleep(1, 0xA26F); });
    addProc(d, 0xA26F, swapText);
}

} // namespace rea::core::game
