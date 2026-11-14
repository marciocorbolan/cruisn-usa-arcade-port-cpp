/**
 * @file results_boards.cpp
 * @brief Tradução de `0x39BF`–`0x3A45` e `0x3DCF`–`0x3F41`.
 */
#include "core/game/results_boards.hpp"

#include "core/game/process_ctx.hpp"

namespace rea::core::game {

namespace {

/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }

/// `LDI txt,AR2 / FLOAT x,R2 / FLOAT y,R3 / LDI rc,RC / CALL $7651` (+ estilo) e flags |= @p flag.
void shadowText(ProcCtx& c, uint32_t txt, double x, double y, uint32_t rc, uint32_t style, uint32_t flag) {
    c.cpu.ar[2] = txt;
    c.cpu.r[2] = doubleToTms(x);
    c.cpu.r[3] = doubleToTms(y);
    c.cpu.rc = rc;
    c.call(0x7651);
    if (style != 0) c.call(style);
    if (flag != 0) {
        c.orField(c.cpu.ar[0], 8, flag);
        c.orField(c.cpu.ar[1], 8, flag);
    }
}

/// Põe a paleta `[ptr]` (`0x9EDB`) nos dois nós de texto AR0 e AR1.
void paletteBoth(ProcCtx& c, uint32_t ptr) {
    const uint32_t a0 = c.cpu.ar[0], a1 = c.cpu.ar[1];
    c.cpu.ar[2] = c.rd(ptr);
    c.call(0x9EDB);
    c.wr(a0 + 0xC, c.cpu.r[0]);
    c.wr(a1 + 0xC, c.cpu.r[0]);
}

/// Guarda os nós de texto do rótulo (AR0/AR1) nas variáveis 0x16/0x17 do processo.
void keepLabel(ProcCtx& c) {
    c.wr(c.cpu.ar[7] + 0x16, c.cpu.ar[0]);
    c.wr(c.cpu.ar[7] + 0x17, c.cpu.ar[1]);
}
/// Guarda os nós de texto do valor (AR0/AR1) nas variáveis 0x18/0x19 do processo.
void keepValue(ProcCtx& c) {
    c.wr(c.cpu.ar[7] + 0x18, c.cpu.ar[0]);
    c.wr(c.cpu.ar[7] + 0x19, c.cpu.ar[1]);
}

/// x da frente (`*+AR7(back)`, x) e da sombra (`*+AR7(front)`, x + 3).
void placeX(ProcCtx& c, uint32_t shadowSlot, uint32_t frontSlot, double x) {
    c.wf(c.rd(c.cpu.ar[7] + frontSlot) + 3, x);
    c.wf(c.rd(c.cpu.ar[7] + shadowSlot) + 3, x + 3.0);
}

// --- 0x3DFE: deslize comum -----------------------------------------------------------------------

/// Um quadro da entrada do placar: x se aproxima de 250 (passo mínimo 8) até parar.
void slideInStep(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    double r6 = tmsToDouble(cpu.r[6]), r7 = tmsToDouble(cpu.r[7]);
    double d = (250.0 - r6) * c.k(0x3E03);
    if (d < 8.0) d = 8.0;
    r6 += d;
    if (r6 > 250.0) r6 = 250.0;
    placeX(c, 0x16, 0x17, r6);
    d = (260.0 - r7) * c.k(0x3E12);
    if (d < -12.0) d = -12.0;
    r7 += d;
    if (r7 < 260.0) r7 = 260.0;
    placeX(c, 0x18, 0x19, r7);
    cpu.r[6] = doubleToTms(r6);
    cpu.r[7] = doubleToTms(r7);
    c.sleep(1, 0x3E21);
}

/// Começa a entrada do placar de x = 512, por 0x1F quadros.
void slideStart(ProcCtx& c) {
    c.cpu.ar[5] = 0x1F;
    c.cpu.r[6] = doubleToTms(0.0);
    c.cpu.r[7] = doubleToTms(512.0);
    slideInStep(c);
}

void slideOutStep(ProcCtx& c);

/// `0x3E30`: espera `[E483]` != 0; então toca o som 0x7E e começa a saída do placar.
void slideHold(ProcCtx& c) {
    if (c.rd(0xE483) == 0) return c.sleep(1, 0x3E30);
    // 03E32  sai
    c.sound(0x7E);
    c.cpu.ar[5] = 0x1F;
    c.cpu.r[6] = doubleToTms(250.0);
    c.cpu.r[7] = doubleToTms(260.0);
    slideOutStep(c);
}

/// Um quadro da saída do placar: x se aproxima de −20.
void slideOutStep(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    double r6 = tmsToDouble(cpu.r[6]), r7 = tmsToDouble(cpu.r[7]);
    r6 += (-20.0 - r6) * c.k(0x3E39);
    placeX(c, 0x16, 0x17, r6);
    r7 += (532.0 - r7) * c.k(0x3E43);
    placeX(c, 0x18, 0x19, r7);
    cpu.r[6] = doubleToTms(r6);
    cpu.r[7] = doubleToTms(r7);
    c.sleep(1, 0x3E4D);
}

// --- Placares ------------------------------------------------------------------------------------

/// 03DCF  velocidade média.
void averageSpeed(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const bool kph = c.item(0x22) != 0;
    shadowText(c, c.rd(kph ? 0xB111 : 0xB110), 0.0, 20.0, 0x3E7, 0, 0);
    keepLabel(c);
    c.orField(cpu.ar[0], 8, 0x200);
    c.orField(cpu.ar[1], 8, 0x200);
    const double dist = c.item(0x22) != 0 ? c.rf(0xE47E) * c.k(0x3DE7) : c.rf(0xE47E);
    cpu.r[0] = doubleToTms(dist);
    cpu.r[1] = c.rd(0xE608);
    c.call(0xA509);
    cpu.r[2] = tmsFix(tmsToDouble(cpu.r[0]));
    cpu.ar[2] = cpu.ar[7] + 0xC;
    c.call(0xA762);
    shadowText(c, cpu.ar[2], 512.0, 20.0, 0x3E7, 0x7698, 0);
    keepValue(c);
    paletteBoth(c, 0xB112);
    c.wf(0xE47E, 0.0);
    c.wf(0xE608, 0.0);
    slideStart(c);
}

/// 03E4F  recorde da pista: iniciais + tempo.
void trackRecord(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    shadowText(c, c.rd(0xB113), 0.0, 120.0, 0x3E7, 0x76FA, 0x200);
    keepLabel(c);
    cpu.r[6] = c.rd(0xE481) - 1;
    cpu.r[7] = 0;
    c.call(0x9AD2);
    c.call(0x9B06);
    cpu.ar[1] = cpu.ar[7] + 0x1A;
    c.wr(cpu.ar[1], (cpu.r[1] & 0xFFu) | ((cpu.r[2] & 0xFFu) << 8) | ((cpu.r[3] & 0xFFu) << 16) | (0x20u << 24));
    c.wr(cpu.ar[1] + 1, 0x20);
    cpu.ar[2] = cpu.ar[7] + 0xC;
    c.call(0x6099);
    cpu.ar[0] = cpu.ar[1];
    cpu.ar[1] = cpu.ar[2];
    cpu.ar[2] = cpu.ar[0];
    c.call(0x77E7);
    shadowText(c, cpu.ar[2], 512.0, 120.0, 0x3E7, 0x76FA, 0);
    keepValue(c);
    slideStart(c);
}

/// 03ED8  tempo da etapa (ou `[B119]` no fim de tempo).
void stageTime(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    shadowText(c, c.rd(0xB118), 0.0, 50.0, 0x3E7, 0x76FA, 0x200);
    keepLabel(c);
    if (c.rd(0xE53F) != 0) {
        c.wr(0xE4BC, 0);
        cpu.ar[2] = c.rd(0xB119);
    } else {
        cpu.r[0] = c.rd(0xE85F);
        c.wr(0xE4BC, cpu.r[0]);
        cpu.ar[2] = cpu.ar[7] + 0xC;
        c.call(0x6099);
    }
    shadowText(c, cpu.ar[2], 512.0, 50.0, 0x3E7, 0x76FA, 0);
    keepValue(c);
    paletteBoth(c, 0xB11A);
    c.wr(0xE85F, 0);
    slideStart(c);
}

/// 03F01  posição: as imagens de `[B1A5]` em y = R5.
void placeDraw(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t a4 = cpu.ar[4], y = cpu.r[5];
    cpu.ar[2] = c.rd(a4);
    cpu.r[2] = 0xDC + c.rd(a4 + 2);
    cpu.r[3] = y;
    cpu.r[4] = 0x900;
    c.call(0xAF48);
    cpu.ar[2] = c.rd(a4 + 1);
    cpu.r[2] = 0xFF;
    cpu.r[3] = y;
    cpu.r[4] = 0x900;
    c.call(0xAF48);
    cpu.ar[4] = a4;
    cpu.r[5] = y;
}

/// Move a posição (R5) em direção a @p target e retoma em @p resume.
void placeMove(ProcCtx& c, double target, uint32_t resume) {
    Tms32031State& cpu = c.cpu;
    const double y = static_cast<double>(s(cpu.r[5]));
    cpu.r[5] = tmsFix((target - y) * c.k(0x3F0B) + y);
    placeDraw(c);
    c.sleep(1, resume);
}

/// Redesenha a posição e espera um quadro (`0x3F2A`).
void placeHold(ProcCtx& c) {
    placeDraw(c);
    c.sleep(1, 0x3F2A);
}

// --- 0x3E82/0x3E87: aviso de recorde ---------------------------------------------------------------

/// 03ECC  pisca: 30 quadros com o texto `*+AR7(C)`, 5 com `[B117]`.
void blink(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[4] += c.rd(0xC92F);
    if (s(cpu.r[4]) < 0x1E) {
        cpu.r[0] = c.rd(cpu.ar[7] + 0xC);
    } else {
        if (s(cpu.r[4]) >= 0x23) cpu.r[4] = 0;
        cpu.r[0] = c.rd(0xB117);
    }
    c.wr(cpu.ar[4] + 1, cpu.r[0]);
    c.wr(cpu.ar[5] + 1, cpu.r[0]);
}

/// Aproxima `[obj+4]` (y) de @p target em 10 %, limitado ao alvo (por baixo ou por cima).
void approachY(ProcCtx& c, uint32_t obj, double target, bool rising) {
    const double y = c.rf(obj + 4);
    double v = (target - y) * c.k(0x3EA0) + y;
    if (rising ? v < target : v > target) v = target;
    c.wf(obj + 4, v);
}

/// Um quadro da subida do texto de recorde (até y = 150) e o pisca (`0x3ECC`).
void recordRise(ProcCtx& c) {
    approachY(c, c.cpu.ar[4], 152.0, true);
    approachY(c, c.cpu.ar[5], 150.0, true);
    c.call(0x3ECC);
    c.sleep(1, 0x3EB1);
}

/// Um quadro da descida do texto de recorde (até y = 450).
void recordFall(ProcCtx& c) {
    approachY(c, c.cpu.ar[4], 452.0, false);
    approachY(c, c.cpu.ar[5], 450.0, false);
    c.sleep(1, 0x3EC7);
}

/// Mostra o texto de recorde `[ptr]` com sombra, embaixo da tela.
void recordShow(ProcCtx& c, uint32_t ptr) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[2] = c.rd(ptr);
    c.wr(cpu.ar[7] + 0xC, cpu.ar[2]);
    shadowText(c, cpu.ar[2], 256.0, 450.0, 0x3E7, 0, 0x100);
    cpu.ar[4] = cpu.ar[0];
    cpu.ar[5] = cpu.ar[1];
    paletteBoth(c, 0xB116);
    recordRise(c);
}

// --- 0x39CF: vencedor do link ----------------------------------------------------------------------

/// 039BF  letreiro `[B0D8]` em (256, 278), 7 quadros.
void linkBanner(ProcCtx& c) {
    shadowText(c, c.rd(0xB0D8), 256.0, 278.0, 7, 0, 0x100);
    paletteBoth(c, 0xB0D9);
}

/// 03A2C  devolve o objeto AR2 da lista `[0x44]` à lista livre `[C983]`. Preserva R0, R1, AR1, AR2.
void releaseObject(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t r0 = cpu.r[0], r1 = cpu.r[1], ar1 = cpu.ar[1], ar2 = cpu.ar[2];
    cpu.r[1] = c.rd(0x44);
    for (;;) {
        cpu.ar[1] = cpu.r[1];
        cpu.r[1] = c.rd(cpu.ar[1]);
        if (cpu.r[1] == 0) break;
        if (cpu.r[1] != cpu.ar[2]) continue;
        c.wr(cpu.ar[1], c.rd(cpu.ar[2]));
        c.wr(cpu.ar[2], c.rd(0xC983));
        c.wr(0xC983, cpu.ar[2]);
        c.wr(0xC989, c.rd(0xC989) + 1);
        c.wr(cpu.ar[2] + 0x1F, 0);
        c.wr(cpu.ar[2] + 0xE, 0);
        break;
    }
    cpu.ar[2] = ar2;
    cpu.ar[1] = ar1;
    cpu.r[1] = r1;
    cpu.r[0] = r0;
}

/// Cria um objeto do letreiro do link com o modelo `[model]`, a altura @p y e a paleta `[pal]`; guarda-o na variável @p slot.
void linkObj(ProcCtx& c, uint32_t model, double y, uint32_t rc, uint32_t pal, uint32_t slot) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[2] = c.rd(model);
    cpu.r[2] = 0;
    cpu.r[3] = static_cast<uint32_t>(static_cast<int32_t>(y));
    cpu.rc = rc;
    c.call(0x7309);
    c.orField(cpu.ar[0], 0xE, 0x4A3);
    cpu.ar[2] = cpu.ar[0];
    c.call(0x708D);
    const uint32_t o = cpu.ar[0];
    cpu.ar[2] = c.rd(pal);
    c.call(0x9EDB);
    c.wr(o + 0x10, cpu.r[0]);
    c.wr(cpu.ar[7] + slot, o);
    cpu.ar[0] = o;
}

/// Desloca em z os 3 objetos do letreiro do link.
void linkShift(ProcCtx& c, double dz) {
    for (uint32_t slot : {0xCu, 0xDu, 0xEu}) {
        const uint32_t o = c.rd(c.cpu.ar[7] + slot);
        c.wf(o + 3, c.rf(o + 3) + dz);
    }
}

/// Laço do letreiro do link: com `[E53B]` != 0, libera os objetos (`0x3A2C`) e encerra.
void linkLoop(ProcCtx& c) {
    if (c.rd(0xE53B) != 0) {
        for (uint32_t slot : {0xCu, 0xDu, 0xEu}) {
            c.cpu.ar[2] = c.rd(c.cpu.ar[7] + slot);
            c.call(0x3A2C);
        }
        return c.die();
    }
    c.call(0x39BF);
    c.sleep(0xA, 0x3A05);
}


// --- 0x3CDF/0x3D31: nome da etapa do cross-country -------------------------------------------------

/// `LDI txt,AR2 / FLOAT 256,R2 / FLOAT y,R3 / LDI 0x5A,RC / CALL $7651`, flags |= 0x100, guarda o par
/// em `*+AR7(slot)`/`*+AR7(slot+1)` e aplica o estilo `0x76FA`.
void stageLabel(ProcCtx& c, uint32_t txt, double y, uint32_t slot) {
    Tms32031State& cpu = c.cpu;
    shadowText(c, txt, 256.0, y, 0x5A, 0, 0x100);
    c.wr(cpu.ar[7] + slot, cpu.ar[0]);
    c.wr(cpu.ar[7] + slot + 1, cpu.ar[1]);
    c.call(0x76FA);
}

/// x = R6 nas frentes (`*+AR7(D/F)`) e R6 + 3 nas sombras (`*+AR7(C/E)`).
void stageLabelX(ProcCtx& c) {
    const double x = tmsToDouble(c.cpu.r[6]);
    placeX(c, 0xC, 0xD, x);
    placeX(c, 0xE, 0xF, x);
}

/// R6 += (alvo − R6)·k; posiciona e dorme 1 quadro.
void stageLabelStep(ProcCtx& c, double target, uint32_t kPc, uint32_t resume) {
    const double r6 = tmsToDouble(c.cpu.r[6]);
    c.cpu.r[6] = doubleToTms(r6 + (target - r6) * c.k(kPc));
    stageLabelX(c);
    c.sleep(1, resume);
}

/// Centraliza o rótulo da etapa (x = 256) e espera @p n quadros.
void stageLabelHold(ProcCtx& c, uint32_t n, uint32_t resume) {
    c.cpu.r[6] = doubleToTms(256.0);
    stageLabelX(c);
    c.sleep(n, resume);
}

// --- 0x3C4A: câmera que sobe olhando o próximo nó ------------------------------------------------

/// Um quadro da subida da câmera do pódio: x, z e y se aproximam do alvo.
void cameraRiseStep(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const double r6 = tmsToDouble(cpu.r[6]), r7 = tmsToDouble(cpu.r[7]);
    const double x = c.rf(0x809800), z = c.rf(0x809802);
    c.wf(0x809800, x + (r6 - x) * c.k(0x3C63));
    c.wf(0x809802, z + (r7 - z) * c.k(0x3C64));
    c.wf(0x809801, c.rf(0x809801) - 10.0);
    c.sleep(1, 0x3C6F);
}

// --- 0x3CB3: textos piscando do fim de jogo ---------------------------------------------------------

/// `CALL $7660` em (256, y), RC 0xF, estilo `0x76BF` e só então flags |= 0x100.
void blinkText(ProcCtx& c, uint32_t txt, double y) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[2] = txt;
    cpu.r[2] = doubleToTms(256.0);
    cpu.r[3] = doubleToTms(y);
    cpu.rc = 0xF;
    c.call(0x7660);
    c.call(0x76BF);
    c.orField(cpu.ar[0], 8, 0x100);
}

/// Pisca os textos até o jogador apertar para pular (`[E631]`).
void blinkTexts(ProcCtx& c) {
    if (c.rd(0xE631) != 0) return c.die();
    blinkText(c, c.rd(0xB10B), 100.0);
    c.sleep(9, 0x3CC9);
}

// --- 0x864D: pódio do 1º lugar (chamada local) ----------------------------------------------------

/// 08649  som 0x8A com volume 0xFF (`0x4330`).
void podiumSound(ProcCtx& c) {
    c.cpu.r[2] = 0xFF;
    c.cpu.ar[2] = 0x8A;
    c.call(0x4330);
}

/// `AR5 −= [C92F]` (mínimo 0) seguido de `DBU AR5`: true se o laço continua.
bool countdown(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[5] -= c.rd(0xC92F);
    if (s(cpu.ar[5]) < 0) cpu.ar[5] = 0;
    return s(--cpu.ar[5]) >= 0;
}

/// Um terço do passo de subida: y −= 3, quadro da animação (só no 1º), som e 1 quadro.
void podiumRise(ProcCtx& c, bool frame, uint32_t resume) {
    const uint32_t a4 = c.cpu.ar[4];
    c.wf(a4 + 2, c.rf(a4 + 2) - 3.0);
    if (frame) c.call(0x86B5);
    podiumSound(c);
    c.sleep(1, resume);
}

/// Um passo da animação do pódio (`0x86B5`) com o som, a cada 3 quadros.
void podiumAnim(ProcCtx& c, uint32_t resume) {
    c.call(0x86B5);
    podiumSound(c);
    c.sleep(3, resume);
}

/// 08695  cria o processo da descida (`[B165]`, ID 0x500) com a tabela e volta ao chamador.
void podiumEnd(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[2] = c.rd(0xB165);
    cpu.r[2] = 0x500;
    c.call(0xA868);
    c.wr(cpu.ar[0] + 0xC, c.rd(cpu.ar[7] + 0xC));
    c.lret();
}

/// Um quadro da queda do objeto do pódio (y += 15).
void podiumFall(ProcCtx& c, uint32_t resume) {
    const uint32_t a4 = c.cpu.ar[4];
    c.wf(a4 + 2, c.rf(a4 + 2) + 15.0);
    c.sleep(1, resume);
}

} // namespace

void registerResultsBoards(SubroutineDispatcher& d) {
    addProc(d, 0x3E21, [](ProcCtx& c) {
        if (s(--c.cpu.ar[5]) >= 0) return slideInStep(c);  // DBU AR5
        // 03E22  posições finais
        Tms32031State& cpu = c.cpu;
        c.wf(c.rd(cpu.ar[7] + 0x16) + 3, 253.0);
        c.wf(c.rd(cpu.ar[7] + 0x17) + 3, 250.0);
        c.wf(c.rd(cpu.ar[7] + 0x18) + 3, 263.0);
        c.wf(c.rd(cpu.ar[7] + 0x19) + 3, 260.0);
        c.sleep(1, 0x3E30);
    });
    addProc(d, 0x3E30, slideHold);
    addProc(d, 0x3E4D, [](ProcCtx& c) {
        if (s(--c.cpu.ar[5]) >= 0) return slideOutStep(c);
        c.die();
    });

    addProc(d, 0x3DCF, averageSpeed);
    addProc(d, 0x3E4F, [](ProcCtx& c) { c.sleep(5, 0x3E51); });
    addProc(d, 0x3E51, trackRecord);
    addProc(d, 0x3ED8, [](ProcCtx& c) { c.sleep(5, 0x3EDA); });
    addProc(d, 0x3EDA, stageTime);

    addProc(d, 0x3F01, [](ProcCtx& c) {
        Tms32031State& cpu = c.cpu;
        cpu.ar[4] = (c.rd(0xE607) - 1) * 3 + c.rd(0xB1A5);
        cpu.r[5] = static_cast<uint32_t>(-0x3C);
        cpu.ar[6] = 0x28;
        placeMove(c, 78.0, 0x3F1B);
    });
    addProc(d, 0x3F1B, [](ProcCtx& c) {
        if (s(--c.cpu.ar[6]) >= 0) return placeMove(c, 78.0, 0x3F1B);
        c.cpu.ar[6] = 0x9E;
        placeHold(c);
    });
    addProc(d, 0x3F2A, [](ProcCtx& c) {
        if (c.rd(0xE483) == 0) return placeHold(c);
        c.cpu.ar[6] = 0x28;
        placeMove(c, -60.0, 0x3F40);
    });
    addProc(d, 0x3F40, [](ProcCtx& c) {
        if (s(--c.cpu.ar[6]) >= 0) return placeMove(c, -60.0, 0x3F40);
        c.die();
    });

    addProc(d, 0x3E82, [](ProcCtx& c) {
        c.cpu.r[4] = 0;
        c.sleep(5, 0x3E85);
    });
    addProc(d, 0x3E85, [](ProcCtx& c) { recordShow(c, 0xB114); });
    addProc(d, 0x3E87, [](ProcCtx& c) {
        c.cpu.r[4] = 0;
        c.sleep(5, 0x3E8A);
    });
    addProc(d, 0x3E8A, [](ProcCtx& c) { recordShow(c, 0xB115); });
    addProc(d, 0x3ECC, blink);
    addProc(d, 0x3EB1, [](ProcCtx& c) {
        if (c.rd(0xE483) == 0) return recordRise(c);
        recordFall(c);
    });
    addProc(d, 0x3EC7, recordFall);

    addProc(d, 0x39BF, linkBanner);
    addProc(d, 0x3A2C, releaseObject);
    addProc(d, 0x39CF, [](ProcCtx& c) {
        if (c.rd(0xE53E) == 0) return c.die();
        if (s(c.rd(0xC8E2)) < s(c.rd(0xE607))) return c.die();
        c.wr(0xE53B, 0);
        linkObj(c, 0xB0DA, 0x8C, 0x358, 0xB0DB, 0xE);
        linkObj(c, 0xB0DC, 0x50, 0x364, 0xB0DD, 0xC);
        linkObj(c, 0xB0DE, 0xC8, 0x364, 0xB0DF, 0xD);
        linkLoop(c);
    });
    addProc(d, 0x3A05, [](ProcCtx& c) {
        c.call(0x39BF);
        c.sleep(5, 0x3A08);
    });
    addProc(d, 0x3A08, [](ProcCtx& c) {
        linkShift(c, -5000.0);
        c.sleep(5, 0x3A17);
    });
    addProc(d, 0x3A17, [](ProcCtx& c) {
        linkShift(c, 5000.0);
        if (s(--c.cpu.ar[5]) >= 0) return linkLoop(c);  // DBU AR5
        // 03A25
        for (uint32_t slot : {0xCu, 0xDu, 0xEu}) {
            c.cpu.ar[2] = c.rd(c.cpu.ar[7] + slot);
            c.call(0x3A2C);
        }
        c.die();
    });
    // 03CDF  processo 0x555 (cross-country): nome da etapa e `[3CDD]` entram de −100, ficam e saem.
    addProc(d, 0x3CDF, [](ProcCtx& c) {
        stageLabel(c, c.rd(c.rd(0xE481) - 1 + c.rd(0x3953)), 300.0, 0xC);
        stageLabel(c, c.rd(0x3CDD), 275.0, 0xE);
        c.cpu.r[6] = doubleToTms(-100.0);
        c.cpu.ar[5] = 0x14;
        stageLabelStep(c, 256.0, 0x3D02, 0x3D10);
    });
    addProc(d, 0x3D10, [](ProcCtx& c) {
        if (s(--c.cpu.ar[5]) >= 0) return stageLabelStep(c, 256.0, 0x3D02, 0x3D10);
        stageLabelHold(c, 0x23, 0x3D1E);
    });
    addProc(d, 0x3D1E, [](ProcCtx& c) {
        c.cpu.ar[5] = 0x14;
        stageLabelStep(c, -100.0, 0x3D21, 0x3D2F);
    });
    addProc(d, 0x3D2F, [](ProcCtx& c) {
        if (s(--c.cpu.ar[5]) >= 0) return stageLabelStep(c, -100.0, 0x3D21, 0x3D2F);
        c.die();
    });

    // 03D31  `[B0F8]`: nome da próxima etapa e `[3CDE]` entram da direita (600 → 256, 25 %).
    addProc(d, 0x3D31, [](ProcCtx& c) { c.sleep(0x14, 0x3D33); });
    addProc(d, 0x3D33, [](ProcCtx& c) {
        stageLabel(c, c.rd(c.rd(0xE481) + c.rd(0x3953)), 50.0, 0xC);
        stageLabel(c, c.rd(0x3CDE), 25.0, 0xE);
        c.cpu.r[6] = doubleToTms(600.0);
        c.cpu.ar[5] = 0x10;
        stageLabelStep(c, 256.0, 0x3D55, 0x3D63);
    });
    addProc(d, 0x3D63, [](ProcCtx& c) {
        if (s(--c.cpu.ar[5]) >= 0) return stageLabelStep(c, 256.0, 0x3D55, 0x3D63);
        stageLabelHold(c, 0x32, 0x3D71);
    });
    addProc(d, 0x3D71, [](ProcCtx& c) {
        c.cpu.ar[5] = 0xD7;
        stageLabelStep(c, 600.0, 0x3D74, 0x3D82);
    });
    addProc(d, 0x3D82, [](ProcCtx& c) {
        if (s(--c.cpu.ar[5]) >= 0) return stageLabelStep(c, 600.0, 0x3D74, 0x3D82);
        c.die();
    });

    // 03C4A  `[B0E4]` (ID 0x5C3): câmera recua 7500 − [E86B] na direção do nó seguinte e sobe.
    addProc(d, 0x3C4A, [](ProcCtx& c) {
        Tms32031State& cpu = c.cpu;
        const uint32_t car = c.rd(c.rd(0xE878) + 0x37);
        const uint32_t node = c.rd(car + 0x21);
        cpu.r[2] = doubleToTms(c.rf(node + 1) - c.rf(car + 1));
        cpu.r[3] = doubleToTms(c.rf(node + 3) - c.rf(car + 3));
        c.call(0x931A);
        cpu.r[2] = doubleToTms(tmsToDouble(cpu.r[0]) - c.k(0x3C52));
        cpu.r[0] = doubleToTms(-7500.0 + c.rf(0xE86B));
        c.call(0x9076);
        cpu.r[6] = doubleToTms(tmsToDouble(cpu.r[0]) + c.rf(0x809800));
        cpu.r[7] = doubleToTms(tmsToDouble(cpu.r[1]) + c.rf(0x809802));
        cpu.ar[5] = 0x1E;
        cameraRiseStep(c);
    });
    addProc(d, 0x3C6F, [](ProcCtx& c) {
        if (s(--c.cpu.ar[5]) >= 0) return cameraRiseStep(c);
        c.die();
    });

    // 03CB3  `[B0E5]`: música 0xD8, som aleatório de `[3CAE]` e 8 piscadas de `[B10B]`/`[B10C|D]`.
    addProc(d, 0x3CB3, [](ProcCtx& c) {
        Tms32031State& cpu = c.cpu;
        cpu.ar[2] = 0xD8;
        c.call(0x91E0);
        cpu.ar[2] = 4;
        c.call(0x8EDB);
        c.sound(c.rd(cpu.r[0] + c.rd(0x3CAE)));
        cpu.ar[5] = 7;
        blinkTexts(c);
    });
    addProc(d, 0x3CC9, [](ProcCtx& c) {
        const uint32_t txt = c.item(0x1A) != 0 ? c.rd(0xB10C) : c.rd(0xB10D);
        blinkText(c, txt, 150.0);
        c.sleep(0xA, 0x3CDB);
    });
    addProc(d, 0x3CDB, [](ProcCtx& c) {
        if (s(--c.cpu.ar[5]) >= 0) return blinkTexts(c);
        c.die();
    });

    // 086B5  BR $C1019E: próximo quadro da tabela AR6 (negativo volta a `*+AR7(C)`) em `[AR4+D]`.
    addProc(d, 0x86B5, [](ProcCtx& c) {
        Tms32031State& cpu = c.cpu;
        for (;;) {
            const uint32_t v = c.rd(cpu.ar[6]++);
            if (s(v) >= 0) {
                cpu.r[0] = v;
                c.wr(cpu.ar[4] + 0xD, v);
                return;
            }
            cpu.ar[6] = c.rd(cpu.ar[7] + 0xC);
        }
    });
    addProc(d, 0x8649, podiumSound);

    // 0864D  chamada local do 1º lugar: espera 13 quadros, música 0xD8 e o troféu `[B163]` sobe
    // animando (`[B164]`); depois cria a descida `0x869E`.
    addProc(d, 0x864D, [](ProcCtx& c) {
        c.cpu.ar[5] = 0xC;
        c.sleep(1, 0x8650);
    });
    addProc(d, 0x8650, [](ProcCtx& c) {
        Tms32031State& cpu = c.cpu;
        if (countdown(c)) return c.sleep(1, 0x8650);
        cpu.ar[2] = 0xD8;
        c.call(0x91E0);
        c.call(0x7025);
        cpu.ar[4] = cpu.ar[0];
        const uint32_t o = cpu.ar[4];
        c.wf(o + 1, -50.0);
        c.wf(o + 2, 310.0);
        c.wf(o + 3, 368.0);
        c.orField(o, 0xE, 0x29);
        c.wr(o + 0xD, c.rd(0xB163));
        if (c.item(0x25) != 0) {
            cpu.ar[2] = o;
            c.call(0x7070);
        }
        cpu.ar[4] = o;
        cpu.ar[6] = c.rd(0xB164);
        c.wr(cpu.ar[7] + 0xC, cpu.ar[6]);
        cpu.ar[5] = 0xB;
        podiumRise(c, true, 0x8673);
    });
    addProc(d, 0x8673, [](ProcCtx& c) { podiumRise(c, false, 0x8679); });
    addProc(d, 0x8679, [](ProcCtx& c) { podiumRise(c, false, 0x867F); });
    addProc(d, 0x867F, [](ProcCtx& c) {
        if (s(--c.cpu.ar[5]) >= 0) return podiumRise(c, true, 0x8673);
        c.cpu.ar[5] = 0xD;
        podiumAnim(c, 0x8685);
    });
    addProc(d, 0x8685, [](ProcCtx& c) {
        if (s(--c.cpu.ar[5]) >= 0) return podiumAnim(c, 0x8685);
        c.sound(0x180);
        c.cpu.ar[5] = 0x1B;
        podiumAnim(c, 0x868D);
    });
    addProc(d, 0x868D, [](ProcCtx& c) {
        if (s(--c.cpu.ar[5]) >= 0) return podiumAnim(c, 0x868D);
        c.cpu.ar[5] = 0x31;
        c.sleep(1, 0x8691);
    });
    addProc(d, 0x8691, [](ProcCtx& c) {
        if (countdown(c)) return c.sleep(1, 0x8691);
        podiumEnd(c);
    });

    // 0869E  descida do troféu: 21 quadros a +15; silencia o canal contínuo e toca 0xDB.
    addProc(d, 0x869E, [](ProcCtx& c) {
        c.cpu.ar[5] = 6;
        podiumFall(c, 0x86A4);
    });
    addProc(d, 0x86A4, [](ProcCtx& c) { podiumFall(c, 0x86A9); });
    addProc(d, 0x86A9, [](ProcCtx& c) { podiumFall(c, 0x86AE); });
    addProc(d, 0x86AE, [](ProcCtx& c) {
        Tms32031State& cpu = c.cpu;
        if (s(--cpu.ar[5]) >= 0) return podiumFall(c, 0x86A4);
        cpu.r[0] = 0;
        cpu.r[1] = 0x64;
        c.call(0x9114);
        cpu.ar[2] = 0xDB;
        c.call(0x91E0);
        c.die();
    });
}

} // namespace rea::core::game
