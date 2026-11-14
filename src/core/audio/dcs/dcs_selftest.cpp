/**
 * @file dcs_selftest.cpp
 * @brief Autoteste do DCS: página de boot do banco 0 (`0x0000`–`0x0176`).
 *
 * @details Sequência:
 * 1. `0x0022`: registradores, vetores (PM `0x1C`–`0x21` → `0x04`…`0x18`), IRQ2 ligada.
 * 2. `0x0040`: espera 250 × 10 000 ciclos (~0,25 s). Um byte da CPU principal nesse tempo (IRQ2 → `[39FC]` = 1)
 *    pula o teste e faz o boot do banco 1 (`0x00F4`). É o que a CPU principal faz ao reiniciar a placa no jogo.
 * 3. `0x0049`: limpa a RAM, liga o SPORT1 (buffer de bipe em `0x3800`, 480 palavras) e a interrupção dele.
 * 4. `0x0069`/`0x00A4`: enche a RAM externa com k · 0x1001 e confere em ordem pseudoaleatória (passo 0x1D).
 * 5. `0x0073`/`0x0079`: soma as ROMs pela tabela do banco 0x24 (pares contagem/banco e soma esperada).
 * 6. `0x00C0`: 16 passadas de padrões 00/55/AA/FF em DM e PM, e endereço = dado.
 * 7. `0x00F2`: 1 bipe (`0x0130`) e boot do banco 1. Falhas: N bipes (2 + índice da ROM, ou 10 para a RAM) e
 *    o DSP para em `0x0153`.
 *
 * Os laços de espera viram ciclos gastos (`burn`), contados instrução a instrução.
 */
#include "core/audio/dcs/dcs_board.hpp"
#include "core/audio/dcs/dcs_program.hpp"

namespace rea::core::audio::dcs {

namespace {
/// Registradores guardados na entrada de uma interrupção.
struct IsrFrame {
    uint16_t astat;  ///< ASTAT.
    uint16_t mstat;  ///< MSTAT.
    uint16_t imask;  ///< IMASK.
};
/// Entrada numa interrupção: guarda ASTAT, MSTAT e IMASK.
IsrFrame enterIsr(AdspCore& c) { return {c.astat, c.mstat, c.imask}; }
/// `RTI`: desempilha ASTAT/MSTAT/IMASK (volta ao banco de registradores primário).
void rti(AdspCore& c, const IsrFrame& f) {
    c.setMstat(f.mstat);
    c.astat = f.astat;
    c.imask = f.imask;
}
} // namespace

void DcsProgram::burn(uint64_t cycles) { b.burn(cycles); }

void DcsProgram::irq2() {
    if (vectors_ == Vectors::SelfTest) st016EIrq2();
    else if (vectors_ == Vectors::Resident) r0383Irq2();
}

void DcsProgram::sportTx() {
    if (vectors_ == Vectors::SelfTest) st0154BeepFill();
}

/// `CNTR = n; DO … UNTIL CE` com só `NOP` no corpo, repetido @p outer vezes com 2 instruções a mais por volta.
void DcsProgram::stNopLoop(uint32_t outer, uint32_t inner) {
    c.setCntr(outer);
    do {
        c.setCntr(inner);
        burn(2 + inner);
        c.owrCntr(1);
        (void)c.ce();
        burn(1);
    } while (c.ce());
}

void DcsProgram::selfTest() {
    // 0000  JUMP $0022
    // 0022  SI = $0010; DM($3FFF) = SI; SI = $2280; DM($3FFE) = SI
    c.si = 0x0010;
    wr(ctrl::kSysControl, c.si);
    c.si = 0x2280;
    wr(ctrl::kWaitStates, c.si);
    // 0026  L0–L6 = 0; M0 = 0; M1 = 1; M4 = 0; M5 = 1
    for (int n = 0; n < 7; ++n) c.setL(n, 0);
    c.setM(0, 0);
    c.setM(1, 1);
    c.setM(4, 0);
    c.setM(5, 1);
    // 0031  DM($39FB) = M0; DM($39FC) = M0
    wr(0x39FB, static_cast<uint16_t>(c.m[0]));
    wr(0x39FC, static_cast<uint16_t>(c.m[0]));
    // 0033  vetores: PM(0x1C…0x21) → PM(0x04, 0x08, …, 0x18)
    c.setI(5, 0x001C);
    c.setI(4, 0x0004);
    c.setM(6, 4);
    c.setCntr(6);
    do {
        c.ar = c.pm(5, 5);
        c.pmw(4, 6, c.ar);
    } while (c.ce());
    vectors_ = Vectors::SelfTest;
    // 003A  I4 = $3400; SI = DM(I4,M4)  (descarta o trinco)
    c.setI(4, 0x3400);
    c.si = c.dm(4, 4);
    // 003C  ICNTL = 0; IMASK = $0020; MSTAT = $0010
    c.icntl = 0;
    c.imask = 0x0020;
    c.si = 0x0010;
    c.setMstat(c.si);
    burn(0x3F - 0x22);

    // 0040  250 voltas de 10 000 NOPs; um byte recebido ([39FC] ≠ 0) pula o teste
    c.setCntr(0x00FA);
    do {
        c.setCntr(0x2710);
        burn(2 + 0x2710);
        c.owrCntr(1);
        (void)c.ce();
        c.ar = rd(0x39FC);
        c.toAf(c.aluPass(c.ar));
        burn(3);
        if (c.ne()) {
            // 00F4  IMASK = 0; banco 1; DM($3FFF) = $0208 → boot do residente
            c.imask = 0;
            c.si = 0x0001;
            wr(0x3000, c.si);
            c.si = 0x0208;
            wr(ctrl::kSysControl, c.si);  // lança Reboot
        }
        burn(1);
    } while (c.ce());

    // 0049  limpa DM 0x0200–0x07FF e 0x3800–0x39FF
    c.ax0 = 0x0000;
    c.ax1 = 0x0001;
    c.setI(0, 0x0200);
    c.setCntr(0x0600);
    do c.dmw(0, 1, c.ax0);
    while (c.ce());
    c.setI(0, 0x3800);
    c.setCntr(0x0200);
    do c.dmw(0, 1, c.ax0);
    while (c.ce());
    burn(0x600 + 0x200 + 8);
    // 0053  SPORT1: buffer do bipe em 0x3800 (480 palavras), 31 250 Hz, autobuffer em I7/M5
    c.si = 0x38F0;
    wr(0x39FD, c.si);
    c.setI(7, 0x3800);
    c.setL(7, 0x01E0);
    c.ar = 0x0009;
    wr(ctrl::kSclkdiv1, c.ar);
    c.ar = 0x4A9F;
    wr(ctrl::kSport1Ctrl, c.ar);
    c.ar = 0x0C10;
    wr(ctrl::kSysControl, c.ar);
    b.clearInterrupts(0x3F);  // IFC = $003F
    c.ar = 0x0E82;
    wr(ctrl::kAutobuffer, c.ar);
    c.ax0 = c.dm(7, 5);
    b.writeTx1(c.ax0);  // TX1 = AX0
    c.icntl = 0;
    c.imask = 0x0024;  // IRQ2 + SPORT1 TX
    c.si = 0x38F0;
    wr(0x39FD, c.si);
    c.setI(4, 0x3400);
    c.si = c.dm(4, 4);
    c.si = c.dm(4, 4);
    burn(0x69 - 0x53);

    // 0069  RAM externa: PM(0x800 + k) = k · 0x1001 (MR0 nos bits 8–23, MR1 nos 0–7)
    c.setI(4, 0x0800);
    c.mul(0, 0, AdspCore::Mul::SS);  // MR = 0
    c.mx0 = 0x0001;
    c.my0 = 0x1001;
    c.enable(AdspCore::kMMode);
    c.setCntr(0x0800);
    do {
        c.px = static_cast<uint8_t>(c.mr1());
        c.pmw(4, 5, c.mr0());
        c.mac(c.mx0, c.my0, AdspCore::Mul::UU);
    } while (c.ce());
    burn(0x800 * 3 + 6);

    // 0073  25 palavras do banco 0x24 → DM 0x39E2…: pares (contagem, banco inicial) e somas esperadas
    c.setI(0, 0x2000);
    c.setI(1, 0x39E2);
    c.setCntr(0x0019);
    do st011BReadWord();
    while (c.ce());
    burn(0x19 * 12);

    // 0079  soma de cada grupo de bancos: bytes pares e ímpares em separado
    c.setI(2, 0x39E2);
    c.si = 0x0000;
    wr(0x39E1, c.si);
    for (;;) {
        // 007C
        c.setMr1(c.dm(2, 1));
        c.toAf(c.aluPass(c.mr1()));
        if (c.eq()) break;  // → 00A4
        c.si = c.dm(2, 1);
        c.setI(0, c.si);
        c.ay0 = 0x00FF;
        c.setSr1(0);
        c.setSr0(0);
        c.setCntr(c.mr1());
        do {
            // 0086  DM($3000) = I0; 4 KB do banco
            wr(0x3000, static_cast<uint16_t>(c.i[0]));
            c.setI(1, 0x2000);
            c.setCntr(0x0800);
            do {
                c.ar = c.dm(1, 1);
                c.toAf(c.aluAnd(c.ar, c.ay0));
                c.toAr(c.aluAdd(c.sr1(), c.af));
                c.setSr1(c.ar);
                c.ar = c.dm(1, 1);
                c.toAf(c.aluAnd(c.ar, c.ay0));
                c.toAr(c.aluAdd(c.sr0(), c.af));
                c.setSr0(c.ar);
            } while (c.ce());
            burn(0x800 * 9 + 4);
            c.modify(0, 1);
        } while (c.ce());
        // 0094  soma = (ímpares & 0xFF) << 8 + (pares & 0xFF)
        c.toAr(c.aluAnd(c.sr1(), c.ay0));
        c.toAf(c.aluAnd(c.sr0(), c.ay0));
        c.lshift(c.ar, 8, false);
        c.toAr(c.aluAdd(c.sr0(), c.af));
        c.ay1 = c.dm(2, 1);
        c.toAf(c.aluSub(c.ar, c.ay1));
        burn(12);
        if (c.ne()) {
            // 009B  código 2 + índice do grupo
            c.ar = rd(0x39E1);
            c.ay0 = 0x0002;
            c.toAr(c.aluAdd(c.ar, c.ay0));
            stFail(c.ar);
        }
        // 00A0
        c.ay0 = rd(0x39E1);
        c.toAr(c.aluInc(c.ay0));
        wr(0x39E1, c.ar);
    }

    // 00A4  confere PM 0x800–0xFFF em saltos de 0x1D (buffer circular de 0x800 palavras)
    c.ay0 = 0x001D;
    c.ay1 = 0x07FF;
    c.ar = c.ay1;
    c.setI(4, 0x0800);
    c.setL(4, 0x0800);
    c.setSe(0x000C);
    c.setCntr(0x1000);
    do {
        c.setCntr(c.ay0);
        do c.setMr0(c.pm(4, 5));
        while (c.ce());
        c.toAr(c.aluAdd(c.ar, c.ay0));
        c.toAr(c.aluAnd(c.ar, c.ay1));
        c.lshift(c.ar, 0, false);
        c.lshift(c.ar, c.se, false, true);
        {
            const uint16_t t = c.ar;  // AF = MR0, AX0 = AR
            c.toAf(c.aluPass(c.mr0()));
            c.ax0 = t;
        }
        c.toAf(c.aluSub(c.sr0(), c.af));
        if (c.ne()) stFail(0x000A);
        c.ar = c.px;
        c.toAf(c.aluPass(c.ar));
        c.toAf(c.aluSub(c.sr1(), c.af));
        if (c.ne()) stFail(0x000A);
        c.ar = c.ax0;
    } while (c.ce());
    burn(0x1000 * (0x1D + 14));
    // 00BB
    c.setL(4, 0);

    // 00C0  16 passadas: padrões em DM e PM, depois endereço = dado
    c.ar = 0x0000;
    wr(0x39FE, c.ar);
    c.setM(6, 0x3FFF);
    for (;;) {
        // 00C2
        c.setI(4, 0x0800);
        c.setCntr(0x0800);
        do {
            for (uint16_t p : {0x00, 0x55, 0xAA, 0xFF}) {
                c.si = p;
                if (!st00FCTestDm()) stFail(0x000A);
            }
            for (uint16_t p : {0x00, 0x55, 0xAA, 0xFF}) {
                c.si = p;
                if (!st0108TestPm()) stFail(0x000A);
            }
            c.modify(4, 5);
        } while (c.ce());
        burn(0x800 * 100);
        // 00D7  PM(k) = k (PX = k & 0xFF)
        c.setI(4, 0x0800);
        c.setCntr(0x0800);
        do {
            c.ar = static_cast<uint16_t>(c.i[4]);
            c.px = static_cast<uint8_t>(c.ar);
            c.pmw(4, 5, c.ar);
        } while (c.ce());
        // 00DD  confere
        c.setI(4, 0x0800);
        c.setCntr(0x0800);
        do {
            c.ar = c.pm(4, 4);
            c.ay0 = static_cast<uint16_t>(c.i[4]);
            c.toAf(c.aluSub(c.ar, c.ay0));
            if (c.ne()) stFail(0x000A);  // 00F9
            c.ay0 = 0x00FF;
            c.ar = c.px;
            c.toAf(c.aluAnd(c.ar, c.ay0));
            c.ar = static_cast<uint16_t>(c.i[4]);
            c.toAr(c.aluAnd(c.ar, c.ay0));
            c.toAf(c.aluSub(c.ar, c.af));
            if (c.ne()) stFail(0x000A);
            c.modify(4, 5);
        } while (c.ce());
        burn(0x800 * 16 + 10);
        // 00EC
        c.ay0 = rd(0x39FE);
        c.toAr(c.aluInc(c.ay0));
        c.ay0 = 0x0010;
        wr(0x39FE, c.ar);
        c.toAr(c.aluSub(c.ar, c.ay0));
        if (c.eq()) break;
    }
    // 00F2  um bipe: tudo certo
    c.ar = 0x0001;
    st0130Report(c.ar);
    // 00F4  IMASK = 0; banco 1; DM($3FFF) = $0208
    c.imask = 0;
    c.si = 0x0001;
    wr(0x3000, c.si);
    c.si = 0x0208;
    wr(ctrl::kSysControl, c.si);  // lança Reboot
    for (;;) burn(1000);
}

[[noreturn]] void DcsProgram::stFail(uint16_t code) {
    // 00BD/00F9/0105/0118  AR = código; CALL $0130; 0153: JUMP $0153
    c.ar = code;
    st0130Report(c.ar);
    for (;;) burn(10000);
}

bool DcsProgram::st00FCTestDm() {
    // 00FC  SR0 = SI; SR = SR OR LSHIFT SI BY 8 (LO); DM(I4,M5) = SR0
    c.setSr0(c.si);
    c.lshift(c.si, 8, false, true);
    c.dmw(4, 5, c.sr0());
    // 00FF  AR = NOT SR0; PX = AR; DM(I4,M6) = AR (endereço seguinte, volta ao atual)
    c.toAr(c.aluNot(c.sr0()));
    c.px = static_cast<uint8_t>(c.ar);
    c.dmw(4, 6, c.ar);
    // 0102  AY0 = DM(I4,M4); AF = SR0 - AY0; IF EQ RTS
    c.ay0 = c.dm(4, 4);
    c.toAf(c.aluSub(c.sr0(), c.ay0));
    return c.eq();
}

bool DcsProgram::st0108TestPm() {
    // 0108  SR0 = SI; SR = SR OR LSHIFT SI BY 8 (LO); PX = SI; PM(I4,M5) = SR0
    c.setSr0(c.si);
    c.lshift(c.si, 8, false, true);
    c.px = static_cast<uint8_t>(c.si);
    c.pmw(4, 5, c.sr0());
    // 010C  AR = NOT SR0; PX = AR; PM(I4,M6) = AR
    c.toAr(c.aluNot(c.sr0()));
    c.px = static_cast<uint8_t>(c.ar);
    c.pmw(4, 6, c.ar);
    // 010F  AY0 = PM(I4,M4); AF = SR0 - AY0; IF NE → falha
    c.ay0 = c.pm(4, 4);
    c.toAf(c.aluSub(c.sr0(), c.ay0));
    if (c.ne()) return false;
    // 0112  AY0 = SR0; SR0 = PX; SR = SR OR LSHIFT SR0 BY 8 (LO); AF = SR0 - AY0
    c.ay0 = c.sr0();
    c.setSr0(c.px);
    c.lshift(c.sr0(), 8, false, true);
    c.toAf(c.aluSub(c.sr0(), c.ay0));
    return c.eq();
}

void DcsProgram::st011BReadWord() {
    // 011B  banco 0x24; palavra de 16 bits (byte alto primeiro) de DM(I0) → DM(I1)
    c.si = 0x0024;
    wr(0x3000, c.si);
    c.setSr0(c.dm(0, 1));
    c.lshift(c.sr0(), 8, false);
    c.si = c.sr0();
    c.setSr0(c.dm(0, 1));
    c.lshift(c.sr0(), 8, false);
    c.lshift(c.sr0(), -8, false);
    c.lshift(c.si, 0, false, true);
    c.dmw(1, 1, c.sr0());
}

void DcsProgram::st0130Report(uint16_t code) {
    // 0130  trinco de saída = 0x79; ~0,15 s; trinco de saída = código
    c.setI(4, 0x3400);
    c.ay0 = 0x0079;
    wr(0x3400, c.ay0);
    stNopLoop(0x0096, 0x2710);
    wr(0x3400, code);
    // 013A  N bipes: 50 ms de silêncio, depois 750 ms de onda quadrada decaindo (×0,996 por ms)
    c.setCntr(code);
    wr(0x39E0, static_cast<uint16_t>(c.m[0]));
    do {
        stNopLoop(0x0032, 0x2710);
        c.si = 0x3FFF;
        c.lshift(c.si, -3, false);
        wr(0x39E0, c.sr0());
        c.setCntr(0x02EE);
        do {
            c.setCntr(0x2710);
            burn(2 + 0x2710);
            c.owrCntr(1);
            (void)c.ce();
            // 014B  MSTAT = 0; [39E0] = [39E0] · 0x7F80 (UU, fracionário)
            c.setMstat(0);
            c.mx0 = rd(0x39E0);
            c.my0 = 0x7F80;
            c.mul(c.mx0, c.my0, AdspCore::Mul::UU);
            wr(0x39E0, c.mr1());
            burn(6);
        } while (c.ce());
        burn(1);
    } while (c.ce());
}

void DcsProgram::st0154BeepFill() {
    // 0154  ENA SEC_REG; preenche 0x3800–0x39DF: 3 × (80 × −A, 80 × +A), A = [39E0]
    const IsrFrame f = enterIsr(c);
    c.enable(AdspCore::kSecReg);
    c.mx1 = static_cast<uint16_t>(c.i[0]);
    c.ax1 = rd(0x39E0);
    c.toAr(c.aluSub(0, c.ax1));
    c.ax0 = c.ar;
    c.setI(0, 0x3800);
    c.setSr0(0x0001);
    c.ar = 0x0003;
    c.ay0 = 0x0001;
    do {
        // 015D
        c.ay1 = 0x0050;
        c.toAf(c.aluPass(c.ay1));
        do {
            c.dmw(0, 1, c.ax0);
            c.toAf(c.aluSub(c.af, c.sr0()));
        } while (c.ne());
        c.ay1 = 0x0050;
        c.toAf(c.aluPass(c.ay1));
        do {
            c.dmw(0, 1, c.ax1);
            c.toAf(c.aluSub(c.af, c.sr0()));
        } while (c.ne());
        c.toAr(c.aluSub(c.ar, c.ay0));
    } while (c.ne());
    // 0169  [39FB] += 1
    c.ay0 = rd(0x39FB);
    c.toAr(c.aluInc(c.ay0));
    wr(0x39FB, c.ar);
    c.setI(0, c.mx1);
    rti(c, f);
}

void DcsProgram::st016EIrq2() {
    // 016E  ENA SEC_REG; lê o trinco; [39FC] = 1
    const IsrFrame f = enterIsr(c);
    c.enable(AdspCore::kSecReg);
    c.mx1 = c.px;
    c.mx0 = static_cast<uint16_t>(c.i[4]);
    c.setI(4, 0x3400);
    c.ar = rd(0x3400);
    wr(0x39FC, static_cast<uint16_t>(c.m[1]));
    c.px = static_cast<uint8_t>(c.mx1);
    c.setI(4, c.mx0);
    rti(c, f);
}

} // namespace rea::core::audio::dcs
