/**
 * @file hiscore_entry.cpp
 * @brief Tradução de `0x31A6`–`0x36B2`, `0x9A7F`–`0x9A91`, `0x9B18`–`0x9B27` e `0x9B28`–`0x9B41`.
 */
#include "core/game/hiscore_entry.hpp"

#include "core/game/process_ctx.hpp"

#include <cmath>

namespace rea::core::game {

namespace {

/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }

constexpr uint32_t kCamX = 0x809800;     ///< x da câmera.
constexpr uint32_t kCamY = 0x809801;     ///< y da câmera.
constexpr uint32_t kCamZ = 0x809802;     ///< z da câmera.
constexpr uint32_t kCamTilt = 0x809806;  ///< Inclinação da câmera.
constexpr uint32_t kSkip = 0xE631;   ///< ≠ 0: o jogador apertou para pular
constexpr uint32_t kWheel = 0xEA47;  ///< letra apontada pelo volante (`0x1C62`)
constexpr uint32_t kClock = 0xE604;  ///< relógio da entrada das iniciais

/// Liga ou desliga o bit C (carry) de ST, que a rotina original usa como retorno de sucesso ou falha.
void setCarry(Tms32031State& cpu, bool on) {
    if (on) cpu.st |= Tms32031State::kStCarry;
    else cpu.st &= ~Tms32031State::kStCarry;
}

/// Lê a palavra @p off do PCB (AR7 + @p off).
uint32_t pcb(ProcCtx& c, uint32_t off) { return c.rd(c.cpu.ar[7] + off); }
/// Grava @p v na palavra @p off do PCB.
void setPcb(ProcCtx& c, uint32_t off, uint32_t v) { c.wr(c.cpu.ar[7] + off, v); }
/// Lê o float @p off do PCB.
double pcbF(ProcCtx& c, uint32_t off) { return c.rf(c.cpu.ar[7] + off); }
/// Grava o float @p v na palavra @p off do PCB.
void setPcbF(ProcCtx& c, uint32_t off, double v) { c.wf(c.cpu.ar[7] + off, v); }
/// Soma @p d ao float em @p a.
void addF(ProcCtx& c, uint32_t a, double d) { c.wf(a, c.rf(a) + d); }
/// Lê o registrador R@p i como float TMS.
double fr(ProcCtx& c, int i) { return tmsToDouble(c.cpu.r[i]); }
/// Grava @p v, convertido para float TMS, no registrador R@p i.
void setR(ProcCtx& c, int i, double v) { c.cpu.r[i] = doubleToTms(v); }

/// `FLOAT b,R1 / FLOAT a,R0 / CALL $A509` → R0 = a / b.
double divide(ProcCtx& c, double a, double b) {
    setR(c, 1, b);
    setR(c, 0, a);
    c.call(0xA509);
    return fr(c, 0);
}

/// `LDI *+AR7(1),AR0 / STI AR5,*AR0++ / STI AR0,*+AR7(1)`.
void localPush(ProcCtx& c, uint32_t v) {
    const uint32_t sp = pcb(c, 1);
    c.wr(sp, v);
    setPcb(c, 1, sp + 1);
}
/// Desempilha uma palavra da pilha local do processo.
uint32_t localPop(ProcCtx& c) {
    const uint32_t sp = pcb(c, 1) - 1;
    setPcb(c, 1, sp);
    return c.rd(sp);
}

/// `R2 = [5A] / AR2 = AR7 + 2A / R3 = [45] / CALL $941E`: posição da câmera vista pela matriz `[5A]`.
void viewCamera(ProcCtx& c) {
    c.cpu.r[2] = c.rd(0x5A);
    c.cpu.ar[2] = c.cpu.ar[7] + 0x2A;
    c.cpu.r[3] = c.rd(0x45);
    c.call(0x941E);
}

/// Inclinação R2: matriz `[47]` com R2, `[5A]` com −R2 e reposiciona a câmera.
void tiltCamera(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[2] = c.rd(0x47);
    c.call(0x9377);
    setR(c, 2, -fr(c, 2));
    cpu.ar[2] = c.rd(0x5A);
    c.call(0x9377);
    viewCamera(c);
}

/// `LDI 1,AR2 / CALL $A874` com retomada em @p resume.
void frame(ProcCtx& c, uint32_t resume) { c.sleep(1, resume); }

// --- Auxiliares (chamadas comuns) ------------------------------------------------------------------

/// 032C7  AR4/AR5 = par de texto AR0/AR1 com flags |= 0x100.
void markPair(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[4] = cpu.ar[0];
    cpu.ar[5] = cpu.ar[1];
    c.orField(cpu.ar[4], 8, 0x100);
    c.orField(cpu.ar[5], 8, 0x100);
}

/// 032C1  letreiro `[B16A]` em (R2, 254), 9999 quadros.
void enterBanner(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[2] = c.rd(0xB16A);
    setR(c, 3, 254.0);
    cpu.rc = 0x270F;
    c.call(0x7651);
    markPair(c);
}

/// 032D0  relógio `[E604]` em (256, 343) por 1 quadro.
void showClock(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[2] = c.rd(kClock);
    cpu.ar[2] = c.rd(0x9CE0);
    c.call(0xA762);
    setR(c, 2, 256.0);
    setR(c, 3, 343.0);
    cpu.rc = 1;
    c.call(0x7651);
    const uint32_t ar4 = cpu.ar[4], ar5 = cpu.ar[5];
    markPair(c);
    cpu.ar[5] = ar5;
    cpu.ar[4] = ar4;
}

/// 032DD  grava o tempo `[E606]` com as iniciais `*+AR7(13..15)` na linha `*+AR7(1D)`.
void saveRecord(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[0] = c.rd(0xE606);
    cpu.r[1] = pcb(c, 0x13);
    cpu.r[2] = pcb(c, 0x14);
    cpu.r[3] = pcb(c, 0x15);
    cpu.r[4] = 0;
    cpu.r[5] = pcb(c, 0x1D);
    cpu.r[6] = pcb(c, 0x2D);
    c.call(0x9B28);
    cpu.r[1] = pcb(c, 0x2D);
}

/// 032FB  iniciais já guardadas no item 0x33 (−1 = nenhuma): copia para `*+AR7(13..15)`; carry = tem.
void storedInitials(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[2] = 0x33;
    c.call(0x99E9);
    if (cpu.r[0] == 0xFFFFFFFFu) return setCarry(cpu, false);
    cpu.r[1] = 0x20;
    setPcb(c, 0x14, cpu.r[1]);
    setPcb(c, 0x15, cpu.r[1]);
    cpu.ar[0] = cpu.ar[7] + 0x13;
    do {
        cpu.r[1] = cpu.r[0] & 0xFFu;
        if (cpu.r[1] == 0) break;
        c.wr(cpu.ar[0]++, cpu.r[1]);
        cpu.r[0] >>= 8;
    } while (cpu.r[0] != 0);
    setCarry(cpu, true);
}

/// 0330E  R0 = soma dos 13 tempos de `[3963]` (passo 4), ou 0 se algum é 0. Preserva R1, R2, AR0.
void totalTime(ProcCtx& c) {
    uint32_t a = c.rd(0x3963), sum = 0;
    for (int32_t n = 0xD; n > 0; --n, a += 4) {
        const uint32_t t = c.rd(a);
        if (t == 0) {
            c.cpu.r[0] = 0;
            return;
        }
        sum += t;
    }
    c.cpu.r[0] = sum;
}

/// 0355D  esconde a âncora 9, liga 0x81–0x83 e acha os objetos da cena por tipo.
void findSceneObjects(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[1] = 9;
    c.call(0x36B3);
    cpu.ar[2] = cpu.ar[0];
    c.call(0x715F);
    for (uint32_t t : {0x81u, 0x82u, 0x83u}) {
        cpu.r[1] = t;
        c.call(0x36B3);
        cpu.ar[2] = cpu.ar[0];
        c.call(0x7100);
    }
    cpu.ar[0] = c.rd(0x40);
    cpu.ar[5] = c.rd(cpu.ar[0]);
    while (cpu.ar[5] != 0) {
        cpu.ar[0] = cpu.ar[5];
        const uint32_t obj = cpu.ar[0], next = c.rd(obj);
        cpu.ar[5] = next;
        const uint32_t type = c.rd(obj + 0xF) & 0xFFu;
        if (type == 0x87) {  // pivô do pódio, em pé
            setPcb(c, 0x17, obj);
            setR(c, 2, c.k(0x3578));
            c.wr(obj + 0x14, cpu.r[2]);
            cpu.ar[2] = obj + 4;
            c.call(0x9377);
        } else if (type == 0x88) {
            setPcb(c, 0x18, obj);
        } else if (type == 0x8B) {  // carro
            setPcb(c, 0x16, obj);
            addF(c, obj + 3, 480.0);
        } else if (type == 0x89) {  // tampa
            setPcb(c, 0x1F, obj);
        } else if (type == 0x86) {  // cursor
            c.wr(obj + 0x10, pcb(c, 0x1E));
            setPcb(c, 0x19, obj);
        }
        cpu.ar[0] = obj;
        cpu.ar[5] = next;
    }
    c.call(0x35A4);
    c.call(0x359B);
}

/// 0359B  nome da etapa `[[3953] + *+AR7(2D)]` em letras 3D em (−24, −627, −341), tipo 0x8C.
void sceneTitle(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    setR(c, 3, -627.0);
    cpu.ar[2] = c.rd(pcb(c, 0x2D) + c.rd(0x3953));
    setR(c, 2, -24.0);
    setR(c, 4, -341.0);
    cpu.r[6] = 0x8C;
    c.call(0x3824);
}

/// 035A4  as faixas `*+AR7(18/19)` seguem o pivô `*+AR7(17)` num raio de 331.
void followPivot(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t piv = pcb(c, 0x17);
    cpu.ar[0] = piv;
    cpu.ar[2] = pcb(c, 0x18);
    cpu.r[2] = c.rd(piv + 0x14);
    cpu.ar[2] += 4;
    c.call(0x9377);
    cpu.r[2] = c.rd(piv + 0x14);
    cpu.ar[2] = pcb(c, 0x19) + 4;
    c.call(0x9377);
    const uint32_t a = pcb(c, 0x18), b = pcb(c, 0x19);
    cpu.ar[2] = a;
    cpu.ar[3] = b;
    const double r1 = 271.0 + 60.0;
    setR(c, 1, r1);
    cpu.r[2] = c.rd(piv + 0x14);
    c.wr(a + 0x14, cpu.r[2]);
    c.wr(b + 0x14, cpu.r[2]);
    c.call(0x92E2);
    double r3 = r1 * -fr(c, 0) + c.rf(piv + 3);
    c.wf(a + 3, r3);
    c.wf(b + 3, r3 + 1.0);
    c.call(0x92DF);
    r3 = r1 * fr(c, 0) + c.rf(piv + 2);
    if (r3 > 511.0) r3 = 511.0;
    c.wf(a + 2, r3);
    c.wf(b + 2, r3);
    setR(c, 3, r3);
    cpu.ar[0] = piv;
    cpu.ar[2] = a;
    cpu.ar[3] = b;
}

/// 035C4  as 3 letras `*+AR7(C..E)` acompanham o pivô (deslocamento (0, 344, −98) girado).
void followLetters(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t piv = pcb(c, 0x17), first = pcb(c, 0xC);
    cpu.ar[0] = piv;
    setR(c, 2, std::fabs(c.rf(piv + 0x14)) + c.k(0x35C8));
    c.wr(first + 0x14, cpu.r[2]);
    cpu.r[2] = c.rd(piv + 0x14);
    cpu.ar[2] = c.rd(0x5A);
    c.call(0x9377);
    const uint32_t v = c.rd(0x5D);
    c.wf(v, 0.0);
    c.wf(v + 1, 344.0);
    c.wf(v + 2, -98.0);
    cpu.ar[2] = c.rd(0x5D);
    cpu.r[2] = c.rd(0x5A);
    cpu.r[3] = cpu.ar[2];
    c.call(0x941E);
    cpu.ar[2] = first;
    cpu.ar[1] = c.rd(0x5D);
    double y = c.rf(piv + 2) + c.rf(cpu.ar[1] + 1);
    if (y > 524.0) y = 524.0;
    c.wf(first + 2, y);
    c.wf(first + 3, c.rf(piv + 3) + c.rf(cpu.ar[1] + 2));
    cpu.ar[0] = first;
    for (uint32_t i = 0; i < 3; ++i) {
        const uint32_t o = pcb(c, 0xC + i);
        c.wr(o + 2, c.rd(first + 2));
        c.wr(o + 3, c.rd(first + 3));
        cpu.r[2] = c.rd(first + 0x14);
        c.wr(o + 0x14, cpu.r[2]);
        cpu.ar[2] = o + 4;
        c.call(0x9377);
    }
    cpu.r[4] = 3;
    cpu.ar[0] = first;
}

/// 0361B  cursor `*+AR7(19)` desliza (50 %) até a letra `[EA47]` (x em `[B16B]` − 24); som 0x33 ao mudar.
void moveCursor(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t cur = pcb(c, 0x19);
    cpu.ar[2] = cur;
    cpu.r[2] = c.rd(kWheel);
    if (s(cpu.r[2]) > 0x25) cpu.r[2] = 0x25;
    cpu.ar[1] = c.rd(0xB16B) + cpu.r[2];
    const double x = static_cast<double>(s(c.rd(cpu.ar[1]))) + -24.0;
    c.wf(cur + 1, (x - c.rf(cur + 1)) * c.k(0x3624) + c.rf(cur + 1));
    cpu.r[2] = c.rd(kWheel);
    if (cpu.r[2] == c.rd(cur + 0x11)) return;
    c.wr(cur + 0x11, cpu.r[2]);
    c.sound(0x33);
}

/// 03646  AR2 = objeto do tipo 0x400 mais perto de x = R2.
void nearestLight(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t r3 = cpu.r[3];
    const double x = fr(c, 2);
    cpu.r[1] = 0x400;
    c.call(0x36DB);
    double best = 32000.0;
    cpu.ar[2] = cpu.ar[0];
    do {
        const double d = std::fabs(c.rf(cpu.ar[0] + 1) - x);
        if (d < best) {
            best = d;
            cpu.ar[2] = cpu.ar[0];
        }
        setR(c, 0, d);
        const uint32_t ar2 = cpu.ar[2];
        c.call(0x36EF);
        cpu.ar[2] = ar2;
    } while (!cpu.carry());
    cpu.r[3] = r3;
}

/// 0362E  luzes do chão: os pares a ±246·n da câmera (n = 0..6) ficam em z = 609 + n.
void placeLights(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const double camX = c.rf(kCamX);
    setR(c, 4, camX);
    for (int n = 0; n < 7; ++n) {
        const double r3 = n;
        setR(c, 3, r3);
        for (double side : {246.0, -246.0}) {
            setR(c, 2, side * r3 + camX);
            nearestLight(c);
            c.wf(cpu.ar[2] + 3, 620.0 + r3 + -11.0);
        }
    }
    setR(c, 3, 7.0);
}

/// 03669  as letras da linha `*+AR7(1D)` acompanham o carro `*+AR7(16)` e andam −R0 em x.
void lettersFollowCar(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t r6 = cpu.r[6];
    const double dx = fr(c, 0);
    cpu.r[1] = 1u << ((pcb(c, 0x1D) + 0x10) & 31u);
    c.call(0x36DB);
    if (!cpu.carry()) {
        const uint32_t car = pcb(c, 0x16);
        cpu.ar[1] = car;
        setR(c, 2, c.rf(car + 0x14) + c.k(0x3673));
        const double y = -12.0 + c.rf(car + 2), z = c.rf(car + 3) - 1.0;
        setR(c, 3, y);
        setR(c, 4, z);
        do {
            const uint32_t o = cpu.ar[0];
            c.wf(o + 1, c.rf(o + 1) - dx);
            c.wf(o + 2, y);
            c.wf(o + 3, z);
            if (c.rf(o + 0x14) != c.k(0x367E)) {
                cpu.ar[2] = o + 4;
                c.call(0x9377);
            }
            cpu.ar[0] = o;
            c.call(0x36EF);
        } while (!cpu.carry());
    }
    cpu.r[6] = r6;
}

/// 03657  gira as luzes (`+16` += R0), recua o carro 111·R0 e arrasta as letras.
void spinLights(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const double d = fr(c, 0);
    cpu.r[1] = 0x400;
    c.call(0x36DB);
    do {
        const uint32_t o = cpu.ar[0];
        setR(c, 2, c.rf(o + 0x16) + d);
        c.wr(o + 0x16, cpu.r[2]);
        cpu.ar[2] = o + 4;
        c.call(0x93C4);
        cpu.ar[0] = o;
        setR(c, 0, d);
        c.call(0x36EF);
    } while (!cpu.carry());
    const uint32_t car = pcb(c, 0x16);
    cpu.ar[0] = car;
    setR(c, 1, 111.0);
    setR(c, 0, d * 111.0);
    c.wf(car + 1, c.rf(car + 1) - fr(c, 0));
    lettersFollowCar(c);
}

/// 03688  iniciais gravadas da linha `*+AR7(1D)` em letras 3D junto ao carro.
void carInitials(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[6] = pcb(c, 0x2D);
    cpu.r[7] = pcb(c, 0x1D);
    c.call(0x9AD2);
    c.call(0x9B06);
    setPcb(c, 0xF, cpu.r[1]);
    setPcb(c, 0x10, cpu.r[2]);
    setPcb(c, 0x11, cpu.r[3]);
    cpu.ar[0] = pcb(c, 0x16);
    cpu.r[1] = pcb(c, 0x1D);
    c.call(0x37E6);
    setR(c, 0, 0.0);
    lettersFollowCar(c);
}

/// 03695  esconde (z = −2500) os 4 números da linha `*+AR7(1D)` (tipos 0x30 + 4·linha …).
void hideRowNumbers(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[1] = pcb(c, 0x1D) * 4 + 0x30;
    cpu.ar[5] = 3;
    do {
        c.call(0x36B3);
        if (cpu.carry()) return;
        c.wf(cpu.ar[0] + 3, -2500.0);
        cpu.r[1] += 1;
    } while (s(--cpu.ar[5]) >= 0);
}

// --- Processos filhos ------------------------------------------------------------------------------

/// 036A0  número da linha nova: aproxima z de 1000 (24 %) girando 0,0873 por quadro, 16 quadros.
void numberStep(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t o = cpu.ar[4];
    const double z = c.rf(o + 3);
    setR(c, 3, z);
    c.wf(o + 3, (1000.0 - z) * c.k(0x36A6) + z);
    setR(c, 2, c.rf(o + 0x16) + c.k(0x36AA));
    c.wr(o + 0x16, cpu.r[2]);
    cpu.ar[2] = o + 4;
    c.call(0x93C4);
    c.sleep(1, 0x36B1);
}

// --- 0x31A6: processo principal --------------------------------------------------------------------

void finish(ProcCtx& c);
void enterLoop(ProcCtx& c);
void podiumTurn(ProcCtx& c);
void lidRise(ProcCtx& c);
void shake(ProcCtx& c);
void lidLower(ProcCtx& c);
void carDrop(ProcCtx& c);
void carSlide(ProcCtx& c);
void carTurn(ProcCtx& c);
void carMove(ProcCtx& c);
void numbers(ProcCtx& c);
void rowsUpStep(ProcCtx& c);
void wrapUp(ProcCtx& c);
void closeDoors(ProcCtx& c);
void knownWait(ProcCtx& c);
void shakeDone(ProcCtx& c);
void turnStart(ProcCtx& c);
void rowsUp(ProcCtx& c);

/// 031B0  espera 4 quadros com `[E5CA]` = 0 (a contagem não zera quando volta a 1).
void waitQuiet(ProcCtx& c);

/// 031B8  monta a cena do pódio.
void sceneSetup(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    c.wr(0xC940, 0);
    c.call(0x9166);
    cpu.ar[2] = 0xF9;
    c.call(0x91E0);
    c.call(0x6FEB);
    c.call(0x7648);
    c.wf(0xE3FD, -512.0);
    c.wr(0xC8C5, (c.rd(0xC8C5) & ~0xC8Fu) | 0x66u);
    cpu.r[0] = 0x5C4;
    cpu.r[1] = 0xFFFFFFFFu;
    c.call(0xA8CF);
    c.wr(0xC938, 0);
    c.wf(kCamX, c.k(0x31CB));
    c.wf(kCamY, -45.0);
    c.wf(kCamZ, -1300.0);
    setR(c, 2, 0.0);
    c.wr(0x809807, cpu.r[2]);
    cpu.dp = 0;
    cpu.ar[2] = c.rd(0x47);
    c.call(0x938A);
    c.call(0x95F5);
    cpu.ar[2] = c.rd(0xB16E);
    c.call(0x9DF6);
    c.waitDma(1);
    c.waitDma(2);
    c.call(0x4EE3);
    c.wr(0xE5CB, 1);
    cpu.ar[2] = c.rd(0xB16F);
    c.call(0xA3C2);
    cpu.ar[2] = c.rd(0xB170);
    c.call(0x9F2F);
    setPcb(c, 0x1E, cpu.r[0]);
    cpu.ar[2] = c.rd(0xB171);
    c.call(0x440C);
    c.call(0x355D);
    c.call(0x7298);
    c.wr(0xC941, 0);
    c.wr(0xE638, 0x26);
    c.wr(kWheel, 0x12);
    c.wr(kClock, 0x14);
    cpu.ar[2] = c.rd(0xB172);
    c.call(0x9F2F);
    setPcb(c, 0x2E, cpu.r[0]);
    c.call(0x32FB);
    if (cpu.carry()) {
        // 0331F  iniciais já conhecidas: só mostra
        cpu.r[4] = pcb(c, 0x1E);
        double x = -150.0;
        for (uint32_t i = 0; i < 3; ++i) {
            cpu.ar[2] = pcb(c, 0x13 + i);
            c.call(0x3813);
            const uint32_t o = cpu.ar[0];
            setPcb(c, 0xC + i, o);
            c.wr(o + 0x10, cpu.r[4]);
            c.wf(o + 1, x);
            c.wf(o + 2, -115.0);
            c.wf(o + 3, -344.0);
            if (i < 2) x += 120.0;
            if (pcb(c, 0x13 + i) != 0x20) {
                cpu.ar[2] = o;
                c.call(0x7093);
            }
        }
        setR(c, 5, x);
        setR(c, 6, -115.0);
        setR(c, 7, -344.0);
        c.sound(0x2D);
        localPush(c, cpu.ar[5]);
        cpu.ar[5] = 0x1D;
        return knownWait(c);
    }
    // 031FC  entrada pelo volante: as 3 letras em branco e o letreiro (processo 0x32B2)
    cpu.r[4] = pcb(c, 0x1E);
    for (uint32_t i = 0x13; i <= 0x15; ++i) setPcb(c, i, 0x20);
    double x = -150.0;
    const uint32_t models[3] = {0xB173, 0xB174, 0xB175};
    for (uint32_t i = 0; i < 3; ++i) {
        cpu.ar[2] = c.rd(models[i]);
        c.call(0x700A);
        const uint32_t o = cpu.ar[0];
        setPcb(c, 0xC + i, o);
        c.wr(o + 0x10, cpu.r[4]);
        c.wf(o + 1, x);
        c.wf(o + 2, -115.0);
        c.wf(o + 3, -344.0);
        if (i < 2) x += 120.0;
        if (i == 0) {
            cpu.ar[2] = o;
            c.call(0x7093);
        }
    }
    setR(c, 5, x);
    setR(c, 6, -115.0);
    setR(c, 7, -344.0);
    cpu.ar[2] = c.rd(0xB176);
    cpu.r[2] = 0x800;
    c.call(0xA844);
    cpu.ar[5] = 0;
    c.wr(0xEA48, c.rd(0xE89E));
    c.call(0x1C62);
    cpu.r[4] = c.rd(kWheel);
    setPcb(c, 0x12, cpu.r[4]);
    c.wr(kSkip, 0);
    // 0322A  meio do curso do pedal
    cpu.r[1] = (tmsFix(c.rf(0xE89B) - c.rf(0xE89A)) >> 1) + tmsFix(c.rf(0xE89A));
    setPcb(c, 0x11, cpu.r[1]);
    cpu.ar[6] = 0;
    c.wr(0xE5FD, 1);
    enterLoop(c);
}

void waitQuiet(ProcCtx& c) {
    if (c.rd(0xE5CA) == 0) {
        c.cpu.r[4] += 1;
        if (s(c.cpu.r[4]) > 3) return sceneSetup(c);
    }
    frame(c, 0x31B7);
}

/// 032A1  relógio, cursor e 1 quadro.
void enterFrame(ProcCtx& c) {
    c.call(0x32D0);
    c.call(0x361B);
    frame(c, 0x32A5);
}

/// 032A6  fim da entrada: grava as iniciais no item 0x33.
void enterDone(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    if (cpu.r[4] == 0x25) {
        cpu.ar[2] = cpu.ar[1];
        c.call(0x715F);
    }
    cpu.r[2] = (((pcb(c, 0x15) << 8) | pcb(c, 0x14)) << 8) | pcb(c, 0x13);
    cpu.ar[2] = 0x33;
    c.call(0x99EE);
    finish(c);
}

/// 03235  um quadro da entrada das iniciais.
void enterLoop(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    if (cpu.ar[6] == 0 && (c.rd(0xC937) & c.rd(0xB177)) != 0) {
        c.wr(kClock, c.rd(kClock) - 1);
        cpu.ar[6] = 2;
    }
    if (cpu.ar[6] != 0) cpu.ar[6] -= 1;
    c.call(0x1C62);
    cpu.r[4] = c.rd(kWheel);
    if (s(cpu.r[4]) > 0x25) cpu.r[4] = 0x25;
    if (s(cpu.r[4]) < 0) cpu.r[4] = 0;
    cpu.r[1] = pcb(c, 0x12);
    setPcb(c, 0x12, cpu.r[4]);
    cpu.r[4] = (cpu.r[4] + cpu.r[1]) >> 1;  // média com o quadro anterior
    cpu.r[0] = s(cpu.r[4]) > 0x25 ? 0x25u : cpu.r[4];
    cpu.ar[1] = c.rd(cpu.r[0] + c.rd(0xB168));
    cpu.ar[0] = pcb(c, 0xC + cpu.ar[5]);
    c.wr(cpu.ar[0] + 0x10, pcb(c, 0x1E));
    c.wr(cpu.ar[0] + 0xD, cpu.ar[1]);
    // 03259  letra: 0x25 = espaço, 0x1B.. = dígitos, senão 'A'..
    cpu.ar[0] = cpu.ar[7] + 0x13 + cpu.ar[5];
    cpu.r[0] = cpu.r[4];
    if (s(cpu.r[0]) >= 0x25) cpu.r[0] = 0x20;
    else if (s(cpu.r[0]) >= 0x1B) cpu.r[0] += 0x15;
    else cpu.r[0] += 0x41;
    c.wr(cpu.ar[0], cpu.r[0]);
    // 03266  confirma pelo pedal (soltar e apertar de novo), pelo relógio ou pelo botão
    if (s(c.rd(kClock)) > 0 && c.rd(kSkip) == 0) {
        cpu.r[0] = c.rd(0xC93B);
        if (s(cpu.r[0]) < s(pcb(c, 0x11))) {
            c.wr(0xE5FD, 0);
            return enterFrame(c);
        }
        if (c.rd(0xE5FD) == 1) return enterFrame(c);
        c.wr(0xE5FD, 1);
    }
    // 03275
    if (c.rd(kClock) != 0) c.wr(kClock, 0x14);
    c.wr(kSkip, 0);
    cpu.ar[0] = cpu.ar[7] + 0xC + cpu.ar[5];
    if (cpu.r[4] == 0x1A) {
        if (s(c.rd(kClock)) > 0) {
            // 03289  apagar: volta uma letra
            if (cpu.ar[5] == 0) return enterFrame(c);
            cpu.ar[2] = c.rd(cpu.ar[0]);
            c.call(0x715F);
            cpu.ar[5] -= 1;
            return enterFrame(c);
        }
        cpu.r[4] = 0x25;
        cpu.ar[0] = cpu.ar[7] + 0x13 + cpu.ar[5];
        c.wr(cpu.ar[0], 0x20);
    }
    // 0328F  fixa a letra (paleta do cursor) e passa para a próxima
    cpu.ar[1] = pcb(c, 0xC + cpu.ar[5]);
    c.wr(cpu.ar[1] + 0x10, pcb(c, 0x2E));
    cpu.r[1] = c.rd(cpu.ar[1] + 0xD);
    if (cpu.r[4] == 0x25) return enterDone(c);
    cpu.ar[5] += 1;
    if (s(cpu.ar[5]) >= 3) return enterDone(c);
    cpu.ar[0] += 1;
    cpu.ar[2] = c.rd(cpu.ar[0]);
    c.wr(cpu.ar[2] + 0xD, cpu.r[1]);
    c.call(0x7093);
    c.sound(0x39);
    enterFrame(c);
}

/// 03351  mostra as iniciais conhecidas por 30 quadros (`[C92F]` acelera).
void knownWait(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    if (c.rd(kSkip) != 0) {
        cpu.ar[5] = localPop(c);
        return finish(c);
    }
    cpu.ar[5] -= c.rd(0xC92F);
    if (s(cpu.ar[5]) < 0) cpu.ar[5] = 0;
    frame(c, 0x3358);
}

/// 0335C  grava o recorde e monta a tabela atrás do pódio.
void finish(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[0] = 0x800;
    cpu.r[1] = 0xF00;
    c.call(0xA8CF);
    c.call(0x7648);
    c.wr(kSkip, 0);
    c.wr(0xC931, 0);
    c.sound(0xC6);
    cpu.r[1] = pcb(c, 0x2D);
    cpu.ar[0] = cpu.r[1] * 4 + c.rd(0x3963);
    cpu.r[0] = c.rd(cpu.ar[0]);
    if (cpu.r[1] == 0xE) c.call(0x330E);
    c.wr(0xE606, cpu.r[0]);
    c.call(0x32DD);
    c.wr(0xE606, cpu.r[0]);
    c.call(0x373C);
    c.wr(kClock, 0);
    c.call(0x362E);
    c.call(0x3930);
    setR(c, 2, -1950.0);
    cpu.r[1] = 0x200;
    c.call(0x371E);
    cpu.ar[2] = pcb(c, 0x1B);
    c.call(0x715F);
    c.call(0x3695);
    for (uint32_t t : {5u, 8u}) {
        cpu.r[1] = t;
        c.call(0x36B3);
        addF(c, cpu.ar[0] + 2, -2000.0);
    }
    cpu.ar[2] = pcb(c, 0x19);
    c.call(0x715F);
    for (;;) {
        cpu.r[1] = 0x8C;
        c.call(0x36B3);
        if (cpu.carry()) break;
        cpu.ar[2] = cpu.ar[0];
        c.call(0x7100);
    }
    podiumTurn(c);
}

/// Mostra os objetos do tipo 0x85 (a tampa) até não haver mais.
void showLid(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    for (;;) {
        cpu.r[1] = 0x85;
        c.call(0x36B3);
        if (cpu.carry()) return;
        cpu.ar[2] = cpu.ar[0];
        c.call(0x7100);
    }
}

/// 033C1  pódio deitado: tampa, som 0xD2 e 5 quadros.
void podiumClosed(ProcCtx& c) {
    showLid(c);
    c.sound(0xD2);
    c.sleep(5, 0x33CB);
}

/// 03390  câmera desce e recua; o pivô deita até −0,013 rad com as luzes girando.
void podiumTurn(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    addF(c, kCamY, divide(c, -55.0, 60.0));
    addF(c, kCamZ, divide(c, -800.0, 60.0));
    const double da = divide(c, c.k(0x339F), 59.0);
    const uint32_t piv = pcb(c, 0x17);
    cpu.ar[0] = piv;
    setR(c, 2, c.rf(piv + 0x14) - da);
    c.wr(piv + 0x14, cpu.r[2]);
    cpu.ar[2] = piv + 4;
    c.call(0x9377);
    const double dz = divide(c, -2000.0, 60.0);
    cpu.r[1] = 0x85;
    c.call(0x36B3);
    if (!cpu.carry()) {
        do {
            addF(c, cpu.ar[0] + 3, dz);
            c.call(0x36C7);
        } while (!cpu.carry());
    }
    c.call(0x35A4);
    c.call(0x35C4);
    cpu.ar[0] = pcb(c, 0x17);
    if (c.rf(cpu.ar[0] + 0x14) <= c.k(0x33B7)) return podiumClosed(c);
    divide(c, c.k(0x33BA), 60.0);
    c.call(0x3657);
    frame(c, 0x33BF);
}

/// 033E2  tampa aberta: sons 0x36/0x2A, esconde as letras e sacode a câmera 5 vezes.
void lidDone(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    c.sound(0x36);
    c.sound(0x2A);
    for (uint32_t i = 0; i < 3; ++i) {
        cpu.r[0] = pcb(c, 0x13 + i);
        cpu.ar[2] = pcb(c, 0xC + i);
        if (cpu.r[0] != 0x20) c.call(0x715F);
    }
    cpu.ar[5] = 4;
    shake(c);
}

/// 033CB  pivô e tampa sobem 24 por quadro até a tampa passar de y = 240.
void lidRise(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    if (c.rd(kSkip) != 0) return lidDone(c);
    const double d = divide(c, 240.0, 10.0);
    addF(c, pcb(c, 0x17) + 2, d);
    c.call(0x35A4);
    c.call(0x35C4);
    setR(c, 0, d);
    cpu.ar[0] = pcb(c, 0x1F);
    const double y = c.rf(cpu.ar[0] + 2) + d;
    c.wf(cpu.ar[0] + 2, y);
    setR(c, 2, y);
    if (y >= 240.0) return lidDone(c);
    frame(c, 0x33E1);
}

/// 033F6  sacode a câmera ±7 em y.
void shake(ProcCtx& c) {
    if (c.rd(kSkip) != 0) return shakeDone(c);
    addF(c, kCamY, 7.0);
    frame(c, 0x33FF);
}

/// 0340E  iniciais gravadas junto ao carro e a tampa desce.
void shakeDone(ProcCtx& c) {
    c.call(0x3688);
    lidLower(c);
}

/// 0340F  pivô e tampa descem 24 por quadro até a tampa chegar a y = 0.
void lidLower(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    if (c.rd(kSkip) != 0) return turnStart(c);
    c.call(0x35A4);
    const double d = divide(c, -240.0, 10.0);
    addF(c, pcb(c, 0x17) + 2, d);
    cpu.ar[0] = pcb(c, 0x1F);
    const double y = c.rf(cpu.ar[0] + 2) + d;
    c.wf(cpu.ar[0] + 2, y);
    setR(c, 2, y);
    if (y <= 0.0) return turnStart(c);
    frame(c, 0x3423);
}

/// 03424  guarda a câmera em `*+AR7(2A..2C)` e começa a descida do carro.
void turnStart(ProcCtx& c) {
    c.sound(0xC6);
    setPcb(c, 0x2A, c.rd(kCamX));
    setPcb(c, 0x2B, c.rd(kCamY));
    setPcb(c, 0x2C, c.rd(kCamZ));
    c.cpu.ar[6] = 0x27;
    carDrop(c);
}

/// 0342F  pivô, tampa, faixa e o par 0x8A descem 50 por quadro; a câmera inclina.
void carDrop(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    if (c.rd(kSkip) != 0) return carSlide(c);
    for (uint32_t slot : {0x17u, 0x1Fu, 0x18u}) addF(c, pcb(c, slot) + 2, -50.0);
    cpu.r[1] = 0x8A;
    c.call(0x36B3);
    addF(c, cpu.ar[0] + 2, -50.0);
    c.call(0x36C7);
    addF(c, cpu.ar[0] + 2, -50.0);
    const double d = divide(c, c.k(0x3448), 40.0);
    setR(c, 2, c.rf(kCamTilt) + d);
    c.wr(kCamTilt, cpu.r[2]);
    tiltCamera(c);
    frame(c, 0x345B);
}

/// 0345F  o carro corre para a esquerda (as luzes giram com R6); a câmera o segue.
void carSlide(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    if (c.rd(kSkip) != 0) return carTurn(c);
    setR(c, 0, fr(c, 6));
    setR(c, 6, fr(c, 6) + c.k(0x3462));
    c.call(0x3657);
    cpu.ar[0] = pcb(c, 0x16);
    double x = (c.rf(cpu.ar[0] + 1) - pcbF(c, 0x2A)) * c.k(0x3467) + pcbF(c, 0x2A);
    if (x < -4700.0) x = -4700.0;
    setPcbF(c, 0x2A, x);
    const double target = x >= -2500.0 ? -1300.0 : -2100.0;
    double z = (target - pcbF(c, 0x2C)) * c.k(0x3475) + pcbF(c, 0x2C);
    if (z > -1300.0) z = -1300.0;
    if (z < -2100.0) z = -2100.0;
    setPcbF(c, 0x2C, z);
    setR(c, 2, -c.rf(kCamTilt));
    cpu.ar[2] = c.rd(0x5A);
    c.call(0x9377);
    viewCamera(c);
    c.call(0x362E);
    frame(c, 0x348A);
}

/// 03497  o carro vira de frente (+0,157 rad por quadro até π/2).
void carTurn(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    if (c.rd(kSkip) != 0) return numbers(c);
    const uint32_t car = pcb(c, 0x16);
    double a = c.k(0x349A) + c.rf(car + 0x14);
    if (a > c.k(0x349C)) a = c.k(0x349D);
    setR(c, 2, a);
    c.wr(car + 0x14, cpu.r[2]);
    cpu.ar[2] = car + 4;
    c.call(0x9377);
    c.wf(car + 0x11, 0.0);
    c.wf(car + 0x12, 0.0);
    c.wf(car + 0x13, -156.0);
    cpu.r[2] = car + 4;
    cpu.ar[2] = car + 0x11;
    cpu.r[3] = cpu.ar[2];
    c.call(0x941E);
    cpu.ar[0] = car;
    c.wf(car + 3, 480.0 + c.rf(car + 0x13));
    c.wf(car + 2, 610.0 + c.rf(car + 0x12));
    setR(c, 0, 0.0);
    c.call(0x3669);
    frame(c, 0x34B7);
}

/// 034BE  o carro gira até 3π/2 e vai à linha `*+AR7(1B)` em AR5 + 1 passos; a câmera desce.
void carMove(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    double a = divide(c, c.k(0x34BF) - c.k(0x34C0), 40.0);
    const uint32_t car = pcb(c, 0x16);
    a += c.rf(car + 0x14);
    if (a > c.k(0x34C4)) a = c.k(0x34C5);
    setR(c, 0, a);
    c.wr(car + 0x14, cpu.r[0]);
    cpu.r[2] = cpu.r[0];
    cpu.ar[2] = car + 4;
    c.call(0x9377);
    const uint32_t dst = pcb(c, 0x1B);
    const double steps = static_cast<double>(s(cpu.ar[5]));
    double d = divide(c, c.rf(dst + 2) - c.rf(car + 2), steps);
    double y = d + c.rf(car + 2);
    c.wf(car + 2, y);
    if (y <= pcbF(c, 0x2B)) {
        double v = (y - pcbF(c, 0x2B)) * c.k(0x34D6) + pcbF(c, 0x2B);
        if (v < -2150.0) v = -2150.0;
        setPcbF(c, 0x2B, v);
    }
    d = divide(c, c.rf(dst + 3) - c.rf(car + 3), steps);
    c.wf(car + 3, d + c.rf(car + 3));
    d = divide(c, c.rf(dst + 1) - c.rf(car + 1), steps);
    c.wf(car + 1, d + c.rf(car + 1));
    cpu.ar[0] = car;
    cpu.ar[2] = dst;
    setR(c, 0, -d);
    c.call(0x3669);
    const double t = divide(c, c.k(0x34EF), 40.0);
    double tilt = c.rf(kCamTilt) + t;
    if (tilt > c.k(0x34F4)) tilt = 0.0;
    setR(c, 2, tilt);
    c.wr(kCamTilt, cpu.r[2]);
    tiltCamera(c);
    frame(c, 0x3504);
}

/// 03509  os 4 números da linha aparecem girando (processo `[B178]` = 0x36A0), 6 quadros cada.
void numbers(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    if (c.rd(kSkip) != 0) return rowsUp(c);
    cpu.r[1] = cpu.r[4];
    c.call(0x36B3);
    if (cpu.carry()) return rowsUp(c);
    cpu.ar[4] = cpu.ar[0];
    cpu.ar[2] = c.rd(0xB178);
    cpu.r[2] = 0x500;
    c.call(0xA844);
    c.sleep(6, 0x3514);
}

/// 03516  linhas 0..4: a câmera sobe 20 por quadro até y = −2150.
void rowsUp(ProcCtx& c) {
    if (c.rd(kSkip) != 0) return wrapUp(c);
    if (s(pcb(c, 0x1D)) > 4) return wrapUp(c);
    rowsUpStep(c);
}

/// Um quadro da subida das linhas; pular encerra a cena.
void rowsUpStep(ProcCtx& c) {
    if (c.rd(kSkip) != 0) return wrapUp(c);
    frame(c, 0x351F);
}

/// 03529  paletas `[B179..B17C]`; pisca a linha nova (`[B17D]` = 0x38CD) por 30 quadros, música 0x3C.
void wrapUp(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    for (uint32_t i = 0; i < 4; ++i) {
        cpu.ar[2] = c.rd(0xB179 + i);
        c.call(0x9F2F);
        setPcb(c, 0x12 + i, cpu.r[0]);
    }
    if (c.rd(kSkip) != 0) return closeDoors(c);
    cpu.r[4] = pcb(c, 0x2D);
    cpu.ar[2] = c.rd(0xB17D);
    cpu.r[2] = 0x500;
    c.call(0xA868);
    setPcb(c, 0x12, cpu.ar[0]);
    c.sleep(0x1E, 0x353E);
}

/// 03540  portas (`[B17E]` = 0x5DE0) e 20 quadros.
void closeDoors(ProcCtx& c) {
    c.cpu.ar[2] = c.rd(0xB17E);
    c.cpu.r[2] = 0x500;
    c.call(0xA844);
    c.sleep(0x14, 0x3545);
}

/// 03545  encerra o pisca, libera as paletas, restaura o modo e volta ao chamador.
void release(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[2] = pcb(c, 0x12);
    c.call(0xA8AC);
    cpu.ar[2] = c.rd(0xB17F);
    c.call(0x9DFE);
    for (uint32_t slot : {0x2Eu, 0x1Eu, 0x12u, 0x13u, 0x14u, 0x15u}) {
        cpu.ar[2] = pcb(c, slot);
        c.call(0x9EEC);
    }
    c.wr(0xC8C5, (c.rd(0xC8C5) & ~0x6Fu) | 5u);
    c.lret();
}

// --- Tabela de recordes na CMOS --------------------------------------------------------------------

/// 09B18  R0 = linha (0..9) em que o tempo R0 entra na tabela R1 (estritamente menor), ou −1.
void recordRow(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t t = cpu.r[0];
    cpu.r[6] = cpu.r[1];
    cpu.r[7] = 0;
    c.call(0x9AD2);
    cpu.r[6] = t;
    for (cpu.r[5] = 0; s(cpu.r[5]) < 10; ++cpu.r[5]) {
        c.call(0x9B06);
        if (s(cpu.r[6]) < s(cpu.r[0])) {
            cpu.r[0] = cpu.r[5];
            return;
        }
    }
    cpu.r[0] = 0xFFFFFFFFu;
}

/// 09B28  insere (R0 tempo, R1–R3 iniciais, R4 carro) na linha R5 da tabela R6, empurrando as
/// linhas R5..8 uma para baixo; depois marca a linha nova (`0x9A7F`).
void recordInsert(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t saved[6] = {cpu.r[0], cpu.r[1], cpu.r[2], cpu.r[3], cpu.r[4], cpu.r[5]};
    cpu.r[7] = 8;
    c.call(0x9AD2);
    if (s(cpu.r[5]) < 9) {
        do {
            c.call(0x9B06);
            c.call(0x9AEB);
            cpu.ar[2] -= 0x18;
            cpu.r[7] -= 1;
        } while (s(cpu.r[7]) >= s(cpu.r[5]));
    }
    cpu.r[7] = saved[5];
    c.call(0x9AD2);
    cpu.r[4] = saved[4];
    cpu.r[3] = saved[3];
    cpu.r[2] = saved[2];
    cpu.r[1] = saved[1];
    cpu.r[0] = saved[0];
    c.call(0x9AEB);
    c.call(0x9A7F);
}

/// 09A7F  grava a linha nova R7 da tabela R6 na marca `0x9AD2(0xF, 0)` + 4·R6.
void recordMark(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t ar2 = cpu.ar[2], r0 = cpu.r[0], r2 = cpu.r[2], r6 = cpu.r[6], r7 = cpu.r[7];
    cpu.r[0] = cpu.r[6];
    cpu.r[6] = 0xF;
    cpu.r[7] = 0;
    c.call(0x9AD2);
    cpu.r[0] *= 4;
    cpu.ar[2] += cpu.r[0];
    cpu.r[7] = r7;
    cpu.r[2] = cpu.r[7];
    c.call(0x9A0D);
    cpu.r[6] = r6;
    cpu.r[2] = r2;
    cpu.r[0] = r0;
    cpu.ar[2] = ar2;
}

} // namespace

void registerHiscoreEntry(SubroutineDispatcher& d) {
    addProc(d, 0x31A6, [](ProcCtx& c) {
        c.call(0x32E7);
        if (!c.cpu.carry()) return c.lret();
        setPcb(c, 0x1D, c.cpu.r[0]);
        c.wr(0xC941, 1);
        c.cpu.r[4] = 0;
        waitQuiet(c);
    });
    addProc(d, 0x31B7, waitQuiet);
    addProc(d, 0x32A5, enterLoop);
    addProc(d, 0x3351, knownWait);
    addProc(d, 0x3358, [](ProcCtx& c) {
        if (s(--c.cpu.ar[5]) >= 0) return knownWait(c);
        c.cpu.ar[5] = localPop(c);
        finish(c);
    });
    addProc(d, 0x33BF, [](ProcCtx& c) {
        if (c.rd(kSkip) == 0) return podiumTurn(c);
        podiumClosed(c);
    });
    addProc(d, 0x33CB, lidRise);
    addProc(d, 0x33E1, lidRise);
    addProc(d, 0x33FF, [](ProcCtx& c) {
        addF(c, kCamY, -7.0);
        frame(c, 0x3407);
    });
    addProc(d, 0x3407, [](ProcCtx& c) {
        if (s(--c.cpu.ar[5]) >= 0) return shake(c);
        c.wf(kCamY, -100.0);
        c.sleep(3, 0x340E);
    });
    addProc(d, 0x340E, shakeDone);
    addProc(d, 0x3423, lidLower);
    addProc(d, 0x3424, turnStart);
    addProc(d, 0x345B, [](ProcCtx& c) {
        if (s(--c.cpu.ar[6]) >= 0) return carDrop(c);
        c.sound(0xC9);
        setR(c, 6, 0.0);
        carSlide(c);
    });
    addProc(d, 0x348A, [](ProcCtx& c) {
        const uint32_t car = pcb(c, 0x16);
        c.cpu.ar[0] = car;
        if (c.rf(car + 1) < -2500.0) {
            double r6 = fr(c, 6) + c.k(0x348F);
            if (r6 < c.k(0x3490)) r6 = c.k(0x3491);
            setR(c, 6, r6);
        }
        if (c.rf(car + 1) >= -5000.0) return carSlide(c);
        carTurn(c);
    });
    addProc(d, 0x34B7, [](ProcCtx& c) {
        if (c.rf(pcb(c, 0x16) + 0x14) != c.k(0x34B9)) return carTurn(c);
        c.sound(0x84);
        c.cpu.ar[5] = 0x27;
        carMove(c);
    });
    addProc(d, 0x3504, [](ProcCtx& c) {
        if (s(--c.cpu.ar[5]) >= 0) return carMove(c);
        c.cpu.r[4] = pcb(c, 0x1D) * 4 + 0x30;
        c.cpu.ar[5] = 3;
        numbers(c);
    });
    addProc(d, 0x3509, numbers);
    addProc(d, 0x3514, [](ProcCtx& c) {
        c.cpu.r[4] += 1;
        if (s(--c.cpu.ar[5]) >= 0) return numbers(c);
        rowsUp(c);
    });
    addProc(d, 0x3516, rowsUp);
    addProc(d, 0x351F, [](ProcCtx& c) {
        double y = c.rf(kCamY) + -20.0;
        if (y < -2150.0) y = -2150.0;
        c.wf(kCamY, y);
        if (y != -2150.0) return rowsUpStep(c);
        wrapUp(c);
    });
    addProc(d, 0x353E, [](ProcCtx& c) {
        c.cpu.ar[2] = 0x3C;
        c.call(0x91E0);
        closeDoors(c);
    });
    addProc(d, 0x3545, release);

    // Processo 0x32B2 (ID 0x800): letreiro `[B16A]` entra de x = 616 (−6 por quadro) por 60 quadros.
    addProc(d, 0x32B2, [](ProcCtx& c) {
        setR(c, 2, 616.0);
        c.call(0x32C1);
        c.wf(c.cpu.ar[4] + 5, -6.0);
        c.wf(c.cpu.ar[5] + 5, -6.0);
        c.sleep(0x3C, 0x32B9);
    });
    addProc(d, 0x32B9, [](ProcCtx& c) {
        c.sound(0x2D);
        c.wf(c.cpu.ar[4] + 5, 0.0);
        c.wf(c.cpu.ar[5] + 5, 0.0);
        frame(c, 0x32C0);
    });
    addProc(d, 0x32C0, [](ProcCtx& c) { frame(c, 0x32C0); });
    addProc(d, 0x32C1, enterBanner);
    addProc(d, 0x32C7, markPair);
    addProc(d, 0x32D0, showClock);
    addProc(d, 0x32DD, saveRecord);
    addProc(d, 0x32FB, storedInitials);
    addProc(d, 0x330E, totalTime);

    addProc(d, 0x355D, findSceneObjects);
    addProc(d, 0x359B, sceneTitle);
    addProc(d, 0x35A4, followPivot);
    addProc(d, 0x35C4, followLetters);
    addProc(d, 0x361B, moveCursor);
    addProc(d, 0x362E, placeLights);
    addProc(d, 0x3646, nearestLight);
    addProc(d, 0x3657, spinLights);
    addProc(d, 0x3669, lettersFollowCar);
    addProc(d, 0x3688, carInitials);
    addProc(d, 0x3695, hideRowNumbers);
    addProc(d, 0x36A0, [](ProcCtx& c) {
        c.sound(0xCF);
        c.cpu.ar[5] = 0xF;
        numberStep(c);
    });
    addProc(d, 0x36B1, [](ProcCtx& c) {
        if (s(--c.cpu.ar[5]) >= 0) return numberStep(c);
        c.die();
    });

    addProc(d, 0x9A7F, recordMark);
    addProc(d, 0x9B18, recordRow);
    addProc(d, 0x9B28, recordInsert);
}

} // namespace rea::core::game
