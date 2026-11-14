/**
 * @file dcs_resident.cpp
 * @brief Residente do DCS: página de boot do banco 1 (`0x0000`–`0x03FE`).
 *
 * @details Quadro de áudio (240 amostras, 7,68 ms), laço `0x0021`:
 * 1. `0x0025` comandos da fila: o som n aponta (tabela do banco 0x24 + 0x40, 3 bytes por som) para um
 *    roteiro de canal; `0x0364` instala o roteiro no canal indicado.
 * 2. `0x004F` roteiros dos 4 canais (`0x01E3`): esperas em quadros e operações (`0x0304`, 15 códigos).
 * 3. `0x0068` volume: soma dos volumes dos canais ativos × volume geral → expoente de escala `[390E]`.
 * 4. `0x008E` fluxo de cada canal (`0x01AC` → overlay `0x0803`/`0x0853`): coeficientes em `0x3800`.
 * 5. `0x0095`–`0x0114` transformada inversa de 128 pontos (pré-rotação, FFT de 64 pontos em bit-reverse,
 *    pós-rotação) e escala.
 * 6. `0x0115` espera a metade livre do buffer de saída (I7 do autobuffer).
 * 7. `0x0126` sobreposição com a cauda do quadro anterior (janela `0x062A`), soma do canal PCM (`0x0158`) e
 *    escrita das 240 amostras; guarda a nova cauda em `0x061A`.
 * 8. `0x0156` atualização por quadro (overlay `0x0A7E`).
 */
#include "core/audio/dcs/dcs_board.hpp"
#include "core/audio/dcs/dcs_program.hpp"

namespace rea::core::audio::dcs {

namespace {
/// Formato dos operandos do MAC.
using Mul = AdspCore::Mul;
/// Registradores guardados na entrada de uma interrupção.
struct IsrFrame {
    uint16_t astat;  ///< ASTAT.
    uint16_t mstat;  ///< MSTAT.
    uint16_t imask;  ///< IMASK.
};
} // namespace

void DcsProgram::waitI7(bool wantLow) {
    // 0117/011B/0121  AR = I7; AF = AR - $04F0; repete até a metade pedida
    for (;;) {
        c.ar = static_cast<uint16_t>(c.i[7]);
        c.ay0 = 0x04F0;
        c.toAf(c.aluSub(c.ar, c.ay0));
        if (wantLow ? c.lt() : c.ge()) return;
        b.burnToNextSample();
    }
}

void DcsProgram::resident() {
    // 0000  JUMP $0008 (vetor da IRQ2 em 0x0004: JUMP $0383)
    vectors_ = Vectors::Resident;
    // 0008  SYSCONTROL = $0010; estados de espera = $2280
    c.si = 0x0010;
    wr(ctrl::kSysControl, c.si);
    c.si = 0x2280;
    wr(ctrl::kWaitStates, c.si);
    // 000C  L0 = L1 = L4 = 0; M1 = 1; M4 = 0; M5 = 1
    c.setL(0, 0);
    c.setL(1, 0);
    c.setL(4, 0);
    c.setM(1, 1);
    c.setM(4, 0);
    c.setM(5, 1);
    // 0012  overlay de inicialização: banco 0 + 0x800, 0x200 palavras → PM 0x800; CALL $0800
    c.si = 0x0000;
    c.setI(0, 0x2800);
    c.ax0 = 0x0200;
    r03F1LoadOverlay();
    o0800Init();
    // 0017  overlay principal: banco 2, 0x300 palavras → PM 0x800
    c.si = 0x0002;
    c.setI(0, 0x2000);
    c.ax0 = 0x0300;
    r03F1LoadOverlay();
    // 001B  volumes pedidos dos canais = 0xFF
    c.setI(0, 0x05FC);
    c.ax0 = 0x00FF;
    for (int k = 0; k < 4; ++k) c.dmw(0, 1, c.ax0);
    r0021MainLoop();
}

[[noreturn]] void DcsProgram::r0021MainLoop() {
    for (;;) {
        // 0021  limpa os coeficientes 0x3800–0x3901
        c.setCntr(0x0102);
        c.setI(0, 0x3800);
        do c.dmw(0, 1, 0x0000);
        while (c.ce());
        r0025Commands();

        // 004F  roteiros: cada canal uma vez (um canal reiniciado por outro roda de novo)
        c.ar = 0x0000;
        wr(0x0600, c.ar);
        wr(0x03FF, static_cast<uint16_t>(c.m[0]));
        do {
            // 0052
            c.ay0 = rd(0x0600);
            c.setSe(rd(0x03FF));
            c.ar = 0x0001;
            c.lshift(c.ar, c.se, false);
            c.toAr(c.aluAnd(c.sr0(), c.ay0));
            if (c.eq()) {
                r01E3ChannelScript();
                c.setSr0(rd(0x0600));
                c.setSe(rd(0x03FF));
                c.ar = 0x0001;
                c.lshift(c.ar, c.se, false, true);
                wr(0x0600, c.sr0());
            }
            // 005E  próximo canal (módulo 4)
            c.ay0 = rd(0x03FF);
            c.toAr(c.aluInc(c.ay0));
            c.ay0 = 0x0004;
            c.toAf(c.aluSub(c.ar, c.ay0));
            if (c.eq()) c.toAr(c.aluPass(0));
            wr(0x03FF, c.ar);
            c.ar = rd(0x0600);
            c.ay0 = 0x000F;
            c.toAf(c.aluSub(c.ar, c.ay0));
        } while (c.ne());

        // 0068  MR = Σ volume do canal · [05F1] (canais com fluxo ativo)
        c.setI(0, 0x0388);
        c.setI(1, 0x03A0);
        c.setCntr(0x0004);
        c.mul(c.mx0, 0, Mul::SS);  // MR = 0
        c.my0 = rd(0x05F1);
        do {
            c.ax0 = c.dm(0, 1);
            c.ay0 = c.dm(0, 1);
            c.mx0 = c.dm(1, 1);
            c.toAr(c.aluOr(c.ax0, c.ay0));
            if (c.ne()) c.mac(c.mx0, c.my0, Mul::UU);
        } while (c.ce());
        // 0073  expoente da soma → SE em −8…0; [390E] = 0x8000 >> −SE
        c.lshift(c.mr2(), 14, true);
        c.lshift(c.mr1(), -2, true, true);
        c.expHi(c.sr1());
        c.ay0 = c.seReg();
        c.ar = 0x0003;
        c.toAr(c.aluAdd(c.ar, c.ay0));
        if (c.gt()) c.toAr(c.aluPass(0));
        c.ay0 = 0xFFF8;
        c.toAf(c.aluSub(c.ar, c.ay0));
        if (c.lt()) c.toAr(c.aluPass(c.ay0));
        c.setSe(c.ar);
        c.setSr0(0x8000);
        c.lshift(c.sr0(), c.se, false);
        wr(0x390E, c.sr0());
        c.toAr(c.aluSub(0, c.ar));
        c.setSe(c.ar);
        // 0083  volumes dos canais · [05F1], deslocados por −SE (fracionário)
        c.setMstat(0x0000);
        c.setI(1, 0x03A0);
        c.setCntr(0x0004);
        do {
            c.mx0 = c.dm(1, 0);
            c.mul(c.mx0, c.my0, Mul::UU);
            c.lshift(c.mr1(), c.se, true);
            c.lshift(c.mr0(), c.se, false, true);
            c.dmw(1, 1, c.sr1());
        } while (c.ce());
        c.setMstat(0x0018);
        // 008D  fluxo de cada canal
        c.ar = 0x0000;
        for (;;) {
            wr(0x03FF, c.ar);
            r01ACChannelStream();
            c.ay0 = rd(0x03FF);
            c.toAr(c.aluInc(c.ay0));
            c.ay0 = 0x0004;
            c.toAf(c.aluSub(c.ar, c.ay0));
            if (c.eq()) break;
        }

        // 0095  pré-processamento de 0x3880/0x3881
        c.setMstat(0x0008);
        c.setI(0, 0x3880);
        c.my1 = 0x8000;
        c.ar = c.dm(0, 0);
        c.mul(c.ar, c.my1, Mul::SS);
        c.dmw(0, 1, c.mr1());
        c.ay0 = c.dm(0, 0);
        c.toAr(c.aluNeg(c.ay0));
        c.mul(c.ar, c.my1, Mul::SS);
        c.dmw(0, 1, c.mr1());
        // 009F  borboletas entre 0x3800… e 0x3900… (escala 1/2)
        c.setCntr(0x0040);
        c.setI(0, 0x3800);
        c.setI(1, 0x3900);
        c.setM(2, 0x3FFF);
        c.setM(3, 0x3FFD);
        c.my1 = 0x4000;
        do {
            c.ax0 = c.dm(0, 0);
            c.ay0 = c.dm(1, 0);
            c.toAr(c.aluAdd(c.ax0, c.ay0));
            c.mul(c.ar, c.my1, Mul::SS);
            c.dmw(0, 1, c.mr1());
            c.toAr(c.aluSub(c.ax0, c.ay0));
            c.mul(c.ar, c.my1, Mul::SS);
            c.dmw(1, 1, c.mr1());
            c.ax0 = c.dm(0, 0);
            c.ay0 = c.dm(1, 0);
            c.toAr(c.aluAdd(c.ax0, c.ay0));
            c.mul(c.ar, c.my1, Mul::SS);
            c.dmw(1, 3, c.mr1());
            c.toAr(c.aluSub(c.ax0, c.ay0));
            c.mul(c.ar, c.my1, Mul::SS);
            c.dmw(0, 1, c.mr1());
        } while (c.ce());
        // 00B5  pré-rotação complexa com as tabelas 0x0780/0x0700 (endereços em bit-reverse)
        c.setI(0, 0x0078);
        c.setI(1, 0x0038);
        c.setI(4, 0x3800);
        c.setI(5, 0x3901);
        c.setMstat(0x0008);
        c.setM(2, 0x0080);
        c.setM(6, 0x3FFF);
        c.setM(7, 0x0002);
        c.enable(AdspCore::kBitRev);
        c.setCntr(0x0040);
        do {
            c.my0 = c.dm(0, 2);
            c.my1 = c.dm(1, 2);
            c.mx1 = c.dm(5, 6);
            c.mul(c.mx1, c.my1, Mul::SS);
            c.mx0 = c.dm(5, 5);
            c.msubRnd(c.mx0, c.my0);
            c.ay0 = c.dm(4, 5);
            {
                const uint16_t t = c.mr1();  // MR = MX1 * MY0 (SS), AX0 = MR1
                c.mul(c.mx1, c.my0, Mul::SS);
                c.ax0 = t;
            }
            c.macRnd(c.mx0, c.my1);
            c.ay1 = c.dm(4, 4);
            c.toAr(c.aluAdd(c.ax0, c.ay1));
            c.dmw(4, 6, c.ar);
            c.toAr(c.aluSub(c.ax0, c.ay1));
            c.dmw(5, 6, c.ar);
            c.toAr(c.aluAdd(c.mr1(), c.ay0));
            c.dmw(4, 7, c.ar);
            c.toAr(c.aluSub(c.ay0, c.mr1()));
            c.dmw(5, 6, c.ar);
        } while (c.ce());
        c.disable(AdspCore::kBitRev);
        c.disable(AdspCore::kMMode);
        // 00CF  primeira etapa da FFT (pares 0x3800/0x3880)
        c.setI(0, 0x3800);
        c.setI(2, 0x3880);
        c.setM(2, 0x3FFF);
        c.setM(3, 0x3FFD);
        c.setCntr(0x0040);
        do {
            c.ax0 = c.dm(0, 1);
            c.ay0 = c.dm(2, 0);
            c.toAr(c.aluAdd(c.ax0, c.ay0));
            c.ax1 = c.dm(0, 2);
            c.dmw(0, 1, c.ar);
            c.toAr(c.aluSub(c.ax0, c.ay0));
            c.dmw(2, 1, c.ar);
            c.ay1 = c.dm(2, 0);
            c.toAr(c.aluAdd(c.ax1, c.ay1));
            c.dmw(0, 1, c.ar);
            c.toAr(c.aluSub(c.ax1, c.ay1));
            c.dmw(2, 1, c.ar);
        } while (c.ce());
        // 00DF  6 etapas restantes (grupos ×2, borboletas ÷2 a cada etapa)
        c.ar = 0x0002;
        wr(0x063A, c.ar);
        c.si = 0x0040;
        wr(0x063B, c.si);
        c.lshift(c.si, -1, false);
        wr(0x063C, c.sr0());
        c.setM(0, 0x3FFF);
        c.setCntr(0x0006);
        do {
            c.setI(4, 0x0780);
            c.setI(5, 0x0700);
            c.setI(0, 0x3800);
            c.setI(1, 0x3800);
            c.ay0 = rd(0x063B);
            c.setM(2, c.ay0);
            c.modify(1, 2);
            c.setI(2, c.i[1]);
            c.toAr(c.aluDec(c.ay0));
            c.setM(3, c.ar);
            c.setCntr(rd(0x063A));
            do {
                c.setCntr(rd(0x063C));
                c.my0 = c.dm(4, 5);
                c.my1 = c.dm(5, 5);
                c.mx0 = c.dm(1, 1);
                do {
                    c.mul(c.mx0, c.my0, Mul::SS);
                    c.mx1 = c.dm(1, 1);
                    c.msubRnd(c.mx1, c.my1);
                    c.ay0 = c.dm(0, 1);
                    {
                        const uint16_t t = c.mr1();
                        c.mul(c.mx1, c.my0, Mul::SS);
                        c.ax0 = t;
                    }
                    c.macRnd(c.mx0, c.my1);
                    c.ay1 = c.dm(0, 0);
                    c.toAr(c.aluSub(c.ay0, c.ax0));
                    c.mx0 = c.dm(1, 1);
                    c.dmw(0, 1, c.ar);
                    c.toAr(c.aluAdd(c.ax0, c.ay0));
                    c.dmw(2, 1, c.ar);
                    c.toAr(c.aluSub(c.ay1, c.mr1()));
                    c.dmw(0, 1, c.ar);
                    c.toAr(c.aluAdd(c.mr1(), c.ay1));
                    c.dmw(2, 1, c.ar);
                } while (c.ce());
                c.modify(2, 2);
                c.modify(1, 3);
                c.modify(0, 2);
            } while (c.ce());
            // 0105
            c.si = rd(0x063A);
            c.lshift(c.si, 1, false);
            wr(0x063A, c.sr0());
            c.si = rd(0x063C);
            wr(0x063B, c.si);
            c.lshift(c.si, -1, false);
            wr(0x063C, c.sr0());
        } while (c.ce());
        // 010C  escala pelo expoente [390E] (saturando)
        c.setM(0, 0x0000);
        c.setI(0, 0x3800);
        c.setCntr(0x0100);
        c.my0 = rd(0x390E);
        do {
            c.mx0 = c.dm(0, 0);
            c.mul(c.mx0, c.my0, Mul::SU);
            c.satMr();
            c.dmw(0, 1, c.mr1());
        } while (c.ce());

        // 0115  banco 0x800; espera a metade livre do buffer de saída
        c.ar = 0x0800;
        wr(0x3000, c.ar);
        c.ar = static_cast<uint16_t>(c.i[7]);
        c.ay0 = 0x04F0;
        c.toAf(c.aluSub(c.ar, c.ay0));
        if (c.lt()) {
            waitI7(false);  // 0121: tocando a 1ª metade → espera a 2ª e escreve na 1ª
            c.ar = 0x0400;
            wr(0x0609, c.ar);
        } else {
            waitI7(true);  // 011B: tocando a 2ª → espera a 1ª e escreve na 2ª
            c.ar = 0x04F0;
            wr(0x0609, c.ar);
        }
        ++b.frames_decoded_;

        // 0126  sobreposição: 8 pares nas bordas com a janela 0x062A e a cauda 0x061A
        c.setI(0, 0x0007);
        c.setI(1, 0x2007);
        c.setM(2, 0x0040);
        c.enable(AdspCore::kBitRev);
        c.setI(5, 0x062A);
        c.setI(6, 0x0639);
        c.setI(4, 0x061A);
        c.setM(6, 0x3FFF);
        c.setM(7, 0x0002);
        c.setCntr(0x0008);
        do {
            c.my0 = c.dm(5, 5);
            c.mx0 = c.dm(0, 0);
            c.mul(c.mx0, c.my0, Mul::SU);
            c.mx0 = c.dm(4, 5);
            c.my0 = c.dm(6, 6);
            c.mac(c.mx0, c.my0, Mul::SU);
            c.macRnd(c.mx0, 0);
            c.dmw(0, 2, c.mr1());
            c.my0 = c.dm(5, 5);
            c.mx0 = c.dm(1, 0);
            c.mul(c.mx0, c.my0, Mul::SU);
            c.mx0 = c.dm(4, 5);
            c.my0 = c.dm(6, 6);
            c.mac(c.mx0, c.my0, Mul::SU);
            c.macRnd(c.mx0, 0);
            c.dmw(1, 2, c.mr1());
        } while (c.ce());
        // 013F  240 amostras: transformada + canal PCM → buffer de saída
        c.setI(4, rd(0x0609));
        c.setI(2, 0x0007);
        c.setI(1, 0x2007);
        c.setCntr(0x0078);
        do {
            r0158PcmSample();
            c.ax0 = c.dm(2, 2);
            c.toAr(c.aluAdd(c.ax0, c.ay0));
            c.dmw(4, 5, c.ar);
            r0158PcmSample();
            c.ax0 = c.dm(1, 2);
            c.toAr(c.aluAdd(c.ax0, c.ay0));
            c.dmw(4, 5, c.ar);
        } while (c.ce());
        // 014C  nova cauda (16 amostras) → 0x061A
        c.setI(0, c.i[2]);
        c.setI(4, 0x061A);
        c.setCntr(0x0008);
        do {
            c.ar = c.dm(0, 2);
            c.dmw(4, 5, c.ar);
            c.ar = c.dm(1, 2);
            c.dmw(4, 5, c.ar);
        } while (c.ce());
        // 0154
        c.disable(AdspCore::kBitRev);
        c.setMstat(0x0018);
        o0A7EFrameUpdate();  // CALL $0802
    }
}

void DcsProgram::r0025Commands() {
    for (;;) {
        // 0025  fila vazia?
        c.ax0 = rd(0x03FC);
        c.ay0 = rd(0x03FD);
        c.toAf(c.aluSub(c.ax0, c.ay0));
        if (c.eq()) return;  // → 004F
        c.setL(0, 0x0010);
        c.setI(0, rd(0x03FD));
        c.ar = c.dm(0, 1);
        wr(0x03FD, static_cast<uint16_t>(c.i[0]));
        c.setL(0, 0x0000);
        c.ay0 = rd(0x060C);
        c.toAf(c.aluSub(c.ar, c.ay0));
        if (c.ge()) continue;  // número fora da tabela
        // 0031  ponteiro da tabela (banco 0x24, deslocamento 0x40) + 3 · n
        c.mx0 = c.ar;
        c.si = 0x0024;
        wr(0x390F, c.si);
        c.si = 0x2040;
        wr(0x39FF, c.si);
        r0358ReadPointer();
        c.setMr1(c.px);
        c.setMr0(c.ar);
        c.my0 = 0x0003;
        c.mac(c.mx0, c.my0, Mul::UU);
        wr(0x39FD, c.mr1());
        wr(0x39FE, c.mr0());
        r0332PointerToBank();
        // 003E  roteiro do som (0xFF no 1º byte = som vazio)
        r0358ReadPointer();
        c.ax0 = c.px;
        c.ay0 = 0x00FF;
        c.toAf(c.aluSub(c.ax0, c.ay0));
        if (c.eq()) continue;
        c.si = c.px;
        wr(0x39FD, c.si);
        wr(0x39FE, c.ar);
        r0332PointerToBank();
        r033EReadByte();
        r033EReadByte();
        c.setMr0(c.ar);  // canal
        r035CBankToPointer();
        c.px = static_cast<uint8_t>(rd(0x39FD));
        c.ar = rd(0x39FE);
        r0364StartScript();
    }
}

void DcsProgram::r0158PcmSample() {
    // 0158  canal PCM direto ([05F5] ≠ 0): AY0 = próxima amostra · [05F4] · [05F1]; senão AY0 = 0
    c.disable(AdspCore::kBitRev);
    c.setMstat(0x0010);
    c.ar = rd(0x05F5);
    c.toAr(c.aluPass(c.ar));
    if (c.eq()) {
        // 019B
        c.ay0 = 0x0000;
    } else {
        // 015D  passo = (n & 0x7F) + 0x7F em [05FB]; módulo [05FA] = 0xFF (n < 0x80) ou 0x7F
        c.ay1 = 0x007F;
        c.setSr0(rd(0x05F5));
        c.lshift(c.sr0(), -7, true);
        wr(0x05F9, c.sr1());
        c.setSr0(rd(0x05F5));
        c.toAr(c.aluAnd(c.sr0(), c.ay1));
        c.toAr(c.aluAdd(c.ar, c.ay1));
        wr(0x05FB, c.ar);
        c.toAr(c.aluPass(c.sr1()));
        if (c.ne()) {
            wr(0x05FA, c.ay1);
        } else {
            c.ax0 = 0x00FF;
            wr(0x05FA, c.ax0);
        }
        // 016B  amostra de 16 bits (little-endian) em [05F6]:[05F7]
        c.ar = rd(0x05F6);
        wr(0x39FD, c.ar);
        c.ar = rd(0x05F7);
        wr(0x39FE, c.ar);
        r0332PointerToBank();
        r0351ReadWord();
        c.setSr0(c.ar);
        c.lshift(c.sr0(), -8, true);
        c.si = c.sr0();
        c.lshift(c.si, 0, true, true);
        wr(0x05F3, c.sr1());
        // 0176  acumulador de fase: avança 2 bytes a cada [05FA] de [05F8]
        c.ay0 = rd(0x05FB);
        c.ax0 = rd(0x05F8);
        c.toAr(c.aluAdd(c.ax0, c.ay0));
        wr(0x05F8, c.ar);
        for (;;) {
            // 017A
            c.ax0 = rd(0x05F8);
            c.ay0 = rd(0x05FA);
            c.toAr(c.aluSub(c.ax0, c.ay0));
            if (c.lt()) break;  // → 019A → 019D
            wr(0x05F8, c.ar);
            c.ay0 = rd(0x05F7);
            c.ax0 = 0x0002;
            c.toAr(c.aluAdd(c.ax0, c.ay0));
            wr(0x05F7, c.ar);
            if (c.acFlag()) {
                // 0185
                c.ay0 = rd(0x05F6);
                c.toAr(c.aluInc(c.ay0));
                wr(0x05F6, c.ar);
            }
            // 0188  no fim (ponteiro 0x1:D368…) volta ao início 0x0:4000
            c.ay0 = rd(0x05F6);
            c.ax0 = 0x0001;
            c.toAr(c.aluSub(c.ax0, c.ay0));
            if (c.ne()) continue;
            c.ay0 = rd(0x05F7);
            c.ax0 = 0xD368;
            c.toAr(c.aluAbs(c.ax0));
            c.toAr(c.aluSub(c.ax0, c.ay0));
            bool wrap;
            if (c.neg()) wrap = !c.lt();  // 0194
            else wrap = c.le();           // 0192
            if (wrap) {
                // 0195
                c.ay0 = 0x0000;
                wr(0x05F6, c.ay0);
                c.ay0 = 0x4000;
                wr(0x05F7, c.ay0);
            }
        }
        // 019D  AY0 = ((amostra · [05F4]) >> 8) · ([05F1] >> 8) >> 7
        c.mx0 = rd(0x05F3);
        c.my0 = rd(0x05F4);
        c.mul(c.mx0, c.my0, Mul::SU);
        c.lshift(c.mr1(), -8, true);
        c.lshift(c.mr0(), -8, false, true);
        c.my0 = c.sr0();
        c.setMr1(rd(0x05F1));
        c.lshift(c.mr1(), -8, true);
        c.mul(c.sr1(), c.my0, Mul::US);
        c.lshift(c.mr1(), -7, true);
        c.lshift(c.mr0(), -7, false, true);
        c.ay0 = c.sr0();
    }
    // 01A9
    c.enable(AdspCore::kBitRev);
    c.setMstat(0x000A);
}

void DcsProgram::r01ACChannelStream() {
    // 01AC  AR = canal; M3 = 2 · canal; nada a fazer sem fluxo em 0x388
    c.lshift(c.ar, 1, false);
    c.setM(3, c.sr0());
    c.setI(0, 0x0388);
    c.modify(0, 3);
    c.ax1 = c.dm(0, 1);
    c.ay1 = c.dm(0, 0);
    c.toAf(c.aluOr(c.ax1, c.ay1));
    if (c.eq()) return;
    // 01B4  ponteiro atual == inicial → começa o fluxo (0x0803)
    c.setI(0, 0x0390);
    c.modify(0, 3);
    {
        const uint16_t t = c.dm(0, 1);  // AF = AX1, AX1 = DM(I0,M1)
        c.toAf(c.aluPass(c.ax1));
        c.ax1 = t;
    }
    {
        const uint16_t t = c.dm(0, 0);  // AF = AX1 - AF, AX1 = DM(I0,M0)
        c.toAf(c.aluSub(c.ax1, c.af));
        c.ax1 = t;
    }
    if (c.eq()) {
        c.toAf(c.aluSub(c.ax1, c.ay1));
        if (c.eq()) o0803StreamStart();
    }
    // 01BB  um quadro do fluxo
    o0853StreamFrame();
    c.si = rd(0x03FF);
    c.setM(2, c.si);
    c.lshift(c.si, 1, false);
    c.setM(3, c.sr0());
    // 01C0  quadros restantes do fluxo
    c.setI(0, 0x03AC);
    c.modify(0, 2);
    c.ay1 = c.dm(0, 0);
    c.toAr(c.aluDec(c.ay1));
    c.dmw(0, 0, c.ar);
    if (c.ne()) return;
    // 01C6  fim: recarrega a contagem e volta ao início do fluxo
    c.setI(1, 0x03B0);
    c.modify(1, 2);
    c.ar = c.dm(1, 0);
    c.dmw(0, 0, c.ar);
    c.setI(0, 0x0390);
    c.modify(0, 3);
    c.setI(1, 0x0388);
    c.modify(1, 3);
    c.si = c.dm(0, 1);
    c.dmw(1, 1, c.si);
    c.si = c.dm(0, 0);
    c.dmw(1, 0, c.si);
    // 01D2  repetições: 0 = infinito; 1 = última → desliga o fluxo
    c.setI(0, 0x0398);
    c.modify(0, 2);
    c.ay1 = c.dm(0, 0);
    c.toAr(c.aluPass(c.ay1));
    if (c.eq()) return;
    c.toAr(c.aluDec(c.ay1));
    if (!c.eq()) {
        c.dmw(0, 0, c.ar);
        return;
    }
    // 01DB
    c.setI(0, 0x0388);
    c.modify(0, 3);
    c.dmw(0, 1, 0x0000);
    c.dmw(0, 0, 0x0000);
    c.setI(0, 0x03B4);
    c.modify(0, 2);
    c.dmw(0, 0, 0xFFFF);
}

void DcsProgram::r01E3ChannelScript() {
    // 01E3  ponteiro do roteiro do canal [03FF]
    c.ax0 = 0x0380;
    c.ay0 = rd(0x03FF);
    c.toAr(c.aluAdd(c.ax0, c.ay0));
    c.toAr(c.aluAdd(c.ar, c.ay0));
    c.setI(0, c.ar);
    c.ar = c.dm(0, 1);
    wr(0x39FD, c.ar);
    c.ay0 = c.dm(0, 0);
    wr(0x39FE, c.ay0);
    c.toAr(c.aluOr(c.ar, c.ay0));
    if (c.eq()) return;
    c.ar = c.ay0;
    r0332PointerToBank();
    for (;;) {
        // 01F0  espera (em quadros) antes da próxima operação; 0xFFFF = fim (fica parado)
        r0351ReadWord();
        c.ay1 = c.ar;
        c.toAf(c.aluInc(c.ay1));
        bool execute = false;
        if (!c.eq()) {
            c.ax0 = 0x03A8;
            c.ay0 = rd(0x03FF);
            c.toAr(c.aluAdd(c.ax0, c.ay0));
            c.setI(0, c.ar);
            c.ar = c.dm(0, 0);
            c.toAf(c.aluSub(c.ar, c.ay1));
            execute = c.eq();
        }
        if (!execute) {
            // 01FB  volta 2 bytes (relê a espera no próximo quadro)
            r035CBankToPointer();
            c.ax0 = 0x0380;
            c.ay0 = rd(0x03FF);
            c.toAr(c.aluAdd(c.ax0, c.ay0));
            c.toAr(c.aluAdd(c.ar, c.ay0));
            c.setI(0, c.ar);
            c.setMr1(rd(0x39FD));
            c.setMr0(rd(0x39FE));
            c.mx0 = 0xFFFE;
            c.my0 = 0x0001;
            c.mac(c.mx0, c.my0, Mul::SS);
            c.dmw(0, 1, c.mr1());
            c.dmw(0, 0, c.mr0());
            return;
        }
        // 0209  zera o contador e despacha a operação (tabela 0x0304)
        c.dmw(0, 0, 0x0000);
        r033EReadByte();
        c.ay0 = 0x0304;
        {
            const uint16_t t = c.ar;  // AR = AR + AY0, AY1 = AR
            c.toAr(c.aluAdd(c.ar, c.ay0));
            c.ay1 = t;
        }
        c.setI(4, c.ar);
        c.ar = rd(0x03FF);
        c.setM(2, c.ar);
        c.lshift(c.ar, 1, false);
        c.setM(3, c.sr0());
        switch (c.ay1) {
        case 0x0: {
            // 0213  fim do roteiro: libera o canal
            c.setI(0, 0x0380);
            c.modify(0, 3);
            c.dmw(0, 1, 0x0000);
            c.dmw(0, 0, 0x0000);
            c.setI(0, 0x0388);
            c.modify(0, 3);
            c.dmw(0, 1, 0x0000);
            c.dmw(0, 0, 0x0000);
            c.setI(0, 0x03F0);
            c.modify(0, 2);
            c.dmw(0, 0, 0x0000);
            c.setMr0(static_cast<uint16_t>(c.m[2]));
            r0325ClearRamps();
            return;
        }
        case 0x1: {
            // 0221  toca um fluxo no canal MR1: ponteiro de 3 bytes → cabeçalho
            r033EReadByte();
            c.setMr1(c.ar);
            r0358ReadPointer();
            c.si = rd(0x390F);
            wr(0x0606, c.si);
            c.si = rd(0x39FF);
            wr(0x0607, c.si);
            c.si = c.px;
            wr(0x39FD, c.si);
            wr(0x39FE, c.ar);
            r0332PointerToBank();
            r0351ReadWord();  // duração em quadros
            c.si = c.ar;
            c.ay0 = 0x03AC;
            c.toAr(c.aluAdd(c.mr1(), c.ay0));
            c.setI(0, c.ar);
            c.dmw(0, 0, c.si);
            c.ay0 = 0x03B0;
            c.toAr(c.aluAdd(c.mr1(), c.ay0));
            c.setI(0, c.ar);
            c.dmw(0, 0, c.si);
            // 0236  tabela de bandas do fluxo = posição atual
            c.ay0 = 0x03B8;
            c.toAf(c.aluAdd(c.mr1(), c.ay0));
            c.toAr(c.aluAdd(c.mr1(), c.af));
            c.setI(0, c.ar);
            c.si = rd(0x390F);
            c.dmw(0, 1, c.si);
            c.si = rd(0x39FF);
            c.dmw(0, 0, c.si);
            // 023E  pula 16 bytes; o fluxo começa depois deles
            r033EReadByte();
            c.setCntr(0x000F);
            do r033EReadByte();
            while (c.ce());
            r035CBankToPointer();
            c.ay0 = 0x0388;
            c.toAf(c.aluAdd(c.mr1(), c.ay0));
            c.toAr(c.aluAdd(c.mr1(), c.af));
            c.setI(1, c.ar);
            c.ay0 = 0x0390;
            c.toAf(c.aluAdd(c.mr1(), c.ay0));
            c.toAr(c.aluAdd(c.mr1(), c.af));
            c.setI(2, c.ar);
            c.si = rd(0x39FD);
            c.ar = rd(0x39FE);
            c.dmw(1, 1, c.si);
            c.dmw(1, 0, c.ar);
            c.dmw(2, 1, c.si);
            c.dmw(2, 0, c.ar);
            // 0252  volta ao roteiro; repetições
            c.si = rd(0x0606);
            wr(0x390F, c.si);
            c.si = rd(0x0607);
            wr(0x39FF, c.si);
            r033EReadByte();
            c.si = c.ar;
            c.ay0 = 0x0398;
            c.toAr(c.aluAdd(c.mr1(), c.ay0));
            c.setI(1, c.ar);
            c.dmw(1, 0, c.si);
            // 025C  outro dono do canal: zera as rampas do dono anterior naquele canal
            c.ay0 = 0x03B4;
            c.toAr(c.aluAdd(c.mr1(), c.ay0));
            c.setI(0, c.ar);
            c.ar = c.dm(0, 0);
            c.toAf(c.aluPass(c.ar));
            if (!c.lt()) {
                c.ay1 = rd(0x03FF);
                c.toAf(c.aluSub(c.ar, c.ay1));
                if (!c.eq()) {
                    c.my0 = 0x0004;
                    c.setMr0(c.ar);
                    c.mac(c.mr1(), c.my0, Mul::UU);
                    c.si = 0x0000;
                    c.ay0 = 0x0350;
                    c.toAr(c.aluAdd(c.mr0(), c.ay0));
                    c.setI(1, c.ar);
                    c.dmw(1, 0, c.si);
                    c.ay0 = 0x0340;
                    c.toAr(c.aluAdd(c.mr0(), c.ay0));
                    c.setI(1, c.ar);
                    c.dmw(1, 0, c.si);
                }
            }
            // 0271
            c.ay0 = rd(0x03FF);
            c.dmw(0, 0, c.ay0);
            break;
        }
        case 0x2: {
            // 0274  para o canal MR0 (fluxo e roteiro)
            r033EReadByte();
            c.setMr0(c.ar);
            c.ay0 = 0x0388;
            c.toAf(c.aluAdd(c.mr0(), c.ay0));
            c.toAr(c.aluAdd(c.mr0(), c.af));
            c.setI(1, c.ar);
            c.ar = c.dm(1, 1);
            c.toAr(c.aluPass(c.ar));
            bool active = c.ne();
            if (!active) {
                c.ar = c.dm(1, 1);
                c.toAr(c.aluPass(c.ar));
                active = c.ne();
            }
            if (active) {
                // 0280
                c.ay0 = 0x0388;
                c.toAf(c.aluAdd(c.mr0(), c.ay0));
                c.toAr(c.aluAdd(c.mr0(), c.af));
                c.setI(1, c.ar);
                c.si = 0x0000;
                c.dmw(1, 1, c.si);
                c.dmw(1, 1, c.si);
                r0325ClearRamps();
            }
            // 0288
            c.si = 0x0000;
            c.ay0 = 0x0380;
            c.toAf(c.aluAdd(c.mr0(), c.ay0));
            c.toAr(c.aluAdd(c.mr0(), c.af));
            c.setI(1, c.ar);
            c.dmw(1, 1, c.si);
            c.dmw(1, 1, c.si);
            // 028F  o próprio roteiro foi parado?
            c.ar = rd(0x03FF);
            c.ay0 = 0x0380;
            c.toAf(c.aluAdd(c.ar, c.ay0));
            c.toAr(c.aluAdd(c.ar, c.af));
            c.setI(1, c.ar);
            c.ar = c.dm(1, 1);
            c.toAr(c.aluPass(c.ar));
            if (c.eq()) {
                c.ar = c.dm(1, 0);
                c.toAr(c.aluPass(c.ar));
                if (c.eq()) return;
            }
            break;
        }
        case 0x3:
            // 029B  põe outro comando na fila
            r0351ReadWord();
            r0313Enqueue();
            break;
        case 0x4:
            // 029E  byte para o trinco de saída
            r033EReadByte();
            c.mx0 = c.ar;
            c.setI(4, 0x3400);
            c.dmw(4, 4, c.ar);
            break;
        case 0x5:
        case 0x6:
            // 02A3
            break;
        case 0x7:
        case 0x8:
        case 0x9:
        case 0xA:
        case 0xB:
        case 0xC: {
            // 02A4/02AA  rampa do parâmetro [0601] do canal: tipo [0603] (0 = absoluto, 1 = +relativo,
            // 2 = −relativo), valor [0602], passos AX0 (0 = imediato)
            const bool timed = c.ay1 >= 0xA;
            c.ar = timed ? 0x000A : 0x0007;
            c.toAr(c.aluSub(c.ay1, c.ar));
            wr(0x0603, c.ar);
            r031BRampParams();
            if (timed) {
                r0351ReadWord();  // 0322
                c.ax0 = c.ar;
            } else {
                c.ax0 = 0x0000;
            }
            // 02AF
            c.mx0 = rd(0x0601);
            c.my0 = 0x0004;
            c.setMr0(rd(0x03FF));
            c.mac(c.mx0, c.my0, Mul::UU);
            c.setM(2, c.mr0());
            c.setI(0, 0x0340);
            c.modify(0, 2);
            c.dmw(0, 0, c.ax0);
            c.setI(1, 0x0350);
            c.modify(1, 2);
            c.setI(2, 0x0370);
            c.modify(2, 2);
            c.ax1 = rd(0x0603);
            c.ar = rd(0x0602);
            {
                const uint16_t t = c.dm(1, 0);  // AF = AX1, AY1 = DM(I1,M0)
                c.toAf(c.aluPass(c.ax1));
                c.ay1 = t;
            }
            if (!c.eq()) {
                c.toAf(c.aluDec(c.af));
                if (c.ne()) c.toAr(c.aluSub(0, c.ar));
                {
                    const uint16_t t = c.ar;  // AR = AR + AY1, AY0 = AR
                    c.toAr(c.aluAdd(c.ar, c.ay1));
                    c.ay0 = t;
                }
                c.ay1 = 0x1FFF;
                c.toAf(c.aluSub(c.ar, c.ay1));
                if (c.gt()) c.toAr(c.aluPass(c.ay1));
                c.ay1 = 0xE001;
                c.toAf(c.aluSub(c.ar, c.ay1));
                if (c.lt()) c.toAr(c.aluPass(c.ay1));
            } else {
                // 02C9
                {
                    const uint16_t t = c.ar;  // AR = AR - AY1, AX1 = AR
                    c.toAr(c.aluSub(c.ar, c.ay1));
                    c.ax1 = t;
                }
                {
                    const uint16_t t = c.ar;  // AR = AX1, AY0 = AR
                    c.toAr(c.aluPass(c.ax1));
                    c.ay0 = t;
                }
            }
            // 02CB  alvo; sem passos → valor = alvo
            c.dmw(2, 0, c.ar);
            c.toAf(c.aluPass(c.ax0));
            if (c.eq()) {
                c.dmw(1, 0, c.ar);
                break;
            }
            // 02CF  incremento = (diferença · 2) / passos (DIVS + 15 DIVQ)
            c.setSr0(c.ay0);
            c.ashift(c.sr0(), 1, false);
            c.toAr(c.aluAbs(c.sr0()));
            {
                const uint16_t t = c.ar;  // AF = 0, AY0 = AR
                c.toAf(c.aluPass(0));
                c.ay0 = t;
            }
            c.divs(c.af, c.ax0);
            c.setCntr(0x000F);
            do c.divq(c.ax0);
            while (c.ce());
            c.ar = c.ay0;
            if (c.neg()) c.toAr(c.aluSub(0, c.ar));
            c.setI(2, 0x0360);
            c.modify(2, 2);
            c.dmw(2, 0, c.ar);
            break;
        }
        case 0xD:
            // 02DD  início de laço: repetições = próximo byte; empilha (repetições, posição)
            r033EReadByte();
            c.si = c.ar;
            c.setSr1(rd(0x390F));
            c.setSr0(rd(0x39FF));
            loopStack(0x000C);  // 02F2
            c.setM(3, 0x0004);
            c.dmw(1, 3, c.si);
            c.dmw(1, 3, c.sr1());
            c.dmw(1, 3, c.sr0());
            break;
        case 0xE:
            // 02E2  fim de laço: desempilha; repete enquanto restarem voltas (0 = para sempre)
            loopStack(0xFFF4);
            c.setM(2, 0x3FFC);
            c.modify(1, 2);
            c.setSr0(c.dm(1, 2));
            c.setSr1(c.dm(1, 2));
            c.si = c.dm(1, 0);
            c.ay0 = c.si;
            c.toAf(c.aluDec(c.ay0));
            if (c.eq()) break;  // era a última volta
            c.toAf(c.aluPass(c.ay0));
            if (!c.eq()) {
                c.toAr(c.aluDec(c.ay0));
                c.si = c.ar;
            }
            // 02F0  volta ao início do laço e reempilha
            wr(0x390F, c.sr1());
            wr(0x39FF, c.sr0());
            loopStack(0x000C);  // 02F2
            c.setM(3, 0x0004);
            c.dmw(1, 3, c.si);
            c.dmw(1, 3, c.sr1());
            c.dmw(1, 3, c.sr0());
            break;
        default:
            // Código > 0x0E: o original salta para fora da tabela (0x0313 em diante). Não ocorre na ROM.
            return;
        }
        // → 01F0
    }
}

void DcsProgram::loopStack(uint16_t delta) {
    // 02F9  I1 = 0x3C0 + topo + canal (entradas intercaladas de 4 em 4); topo += AX0
    c.ax0 = delta;
    c.setM(2, rd(0x03FF));
    c.setI(0, 0x03F0);
    c.modify(0, 2);
    c.ay0 = c.dm(0, 0);
    c.ar = 0x03C0;
    c.toAr(c.aluAdd(c.ar, c.ay0));
    c.setI(1, c.ar);
    c.modify(1, 2);
    c.toAr(c.aluAdd(c.ax0, c.ay0));
    c.dmw(0, 0, c.ar);
}

void DcsProgram::r0313Enqueue() {
    // 0313  IMASK = 0; fila [03FC] (circular, 16) ← AR; IMASK = $0020
    c.imask = 0;
    c.setI(0, rd(0x03FC));
    c.setL(0, 0x0010);
    c.dmw(0, 1, c.ar);
    c.setL(0, 0x0000);
    wr(0x03FC, static_cast<uint16_t>(c.i[0]));
    c.imask = 0x0020;
}

void DcsProgram::r031BRampParams() {
    // 031B  [0601] = parâmetro; [0602] = próximo byte << 6 (com sinal)
    r033EReadByte();
    wr(0x0601, c.ar);
    r033EReadByte();
    c.lshift(c.ar, 8, false);
    c.ashift(c.sr0(), -2, false);
    wr(0x0602, c.sr0());
}

void DcsProgram::r0325ClearRamps() {
    // 0325  zera as 4 rampas (passos e valor) do canal MR0
    c.ay0 = 0x0350;
    c.toAr(c.aluAdd(c.mr0(), c.ay0));
    c.setI(0, c.ar);
    c.ay0 = 0x0340;
    c.toAr(c.aluAdd(c.mr0(), c.ay0));
    c.setI(1, c.ar);
    c.setM(2, 0x0004);
    c.si = 0x0000;
    c.setCntr(0x0004);
    do {
        c.dmw(0, 2, c.si);
        c.dmw(1, 2, c.si);
    } while (c.ce());
}

void DcsProgram::r0332PointerToBank() {
    // 0332  ponteiro de 24 bits [39FD]:[39FE] → banco [390F] e endereço 0x2000 + (p & 0xFFF) em [39FF]
    c.si = rd(0x39FD);
    c.lshift(c.si, 9, true);
    c.si = rd(0x39FE);
    c.lshift(c.si, -7, true, true);
    c.lshift(c.sr1(), -5, false);
    wr(0x390F, c.sr0());
    c.lshift(c.si, 4, false);
    c.lshift(c.sr0(), -4, false);
    c.si = 0x2000;
    c.lshift(c.si, 0, false, true);
    wr(0x39FF, c.sr0());
}

void DcsProgram::r033EReadByte() {
    // 033E  AR = byte em [39FF] do banco [390F]; avança (troca de banco no fim da janela)
    c.ay0 = rd(0x390F);
    wr(0x3000, c.ay0);
    c.ay1 = rd(0x39FF);
    c.setI(0, c.ay1);
    c.ar = c.dm(0, 1);
    c.lshift(c.ar, 8, false);
    c.lshift(c.sr0(), -8, false);
    c.ar = c.sr0();
    c.ax1 = static_cast<uint16_t>(c.i[0]);
    wr(0x39FF, c.ax1);
    c.ay1 = 0x3000;
    c.toAf(c.aluSub(c.ax1, c.ay1));
    if (c.lt()) return;
    c.ar = 0x2000;
    wr(0x39FF, c.ar);
    c.toAr(c.aluInc(c.ay0));
    wr(0x390F, c.ar);
    c.ar = c.sr0();
}

void DcsProgram::r0351ReadWord() {
    // 0351  palavra de 16 bits, byte alto primeiro
    r033EReadByte();
    c.setMr0(c.ar);
    r033EReadByte();
    c.setSr0(c.ar);
    c.lshift(c.mr0(), 8, false, true);
    c.ar = c.sr0();
}

void DcsProgram::r0358ReadPointer() {
    // 0358  ponteiro de 3 bytes: PX = byte alto, AR = os outros dois
    r033EReadByte();
    c.px = static_cast<uint8_t>(c.ar);
    r0351ReadWord();
}

void DcsProgram::r035CBankToPointer() {
    // 035C  banco [390F] + endereço [39FF] → ponteiro [39FD]:[39FE]
    c.setSr0(rd(0x39FF));
    c.lshift(c.sr0(), 4, false);
    c.lshift(c.sr0(), -4, false);
    c.si = rd(0x390F);
    c.lshift(c.si, 12, false, true);
    wr(0x39FD, c.sr1());
    wr(0x39FE, c.sr0());
}

void DcsProgram::r0364StartScript() {
    // 0364  roteiro PX:AR no canal MR0; zera contador, fluxo e pilha; o canal roda neste quadro
    c.si = c.ar;
    c.ay0 = 0x03A8;
    c.toAr(c.aluAdd(c.mr0(), c.ay0));
    c.setI(0, c.ar);
    c.dmw(0, 0, 0x0000);
    c.ay0 = 0x0380;
    c.toAf(c.aluAdd(c.mr0(), c.ay0));
    c.toAr(c.aluAdd(c.mr0(), c.af));
    c.setI(0, c.ar);
    c.ar = c.px;
    c.dmw(0, 1, c.ar);
    c.dmw(0, 0, c.si);
    c.ay0 = 0x03F0;
    c.toAr(c.aluAdd(c.mr0(), c.ay0));
    c.setI(0, c.ar);
    c.dmw(0, 0, 0x0000);
    c.ay0 = 0x0388;
    c.toAf(c.aluAdd(c.mr0(), c.ay0));
    c.toAr(c.aluAdd(c.mr0(), c.af));
    c.setI(0, c.ar);
    c.dmw(0, 1, 0x0000);
    c.dmw(0, 0, 0x0000);
    c.setSe(c.mr0());
    c.ar = 0x0001;
    c.lshift(c.ar, c.se, false);
    c.toAf(c.aluNot(c.sr0()));
    c.ar = rd(0x0600);
    c.toAr(c.aluAnd(c.ar, c.af));
    wr(0x0600, c.ar);
    r0325ClearRamps();
}

void DcsProgram::r0383Irq2() {
    // 0383  ENA SEC_REG; recebe um byte do trinco
    const IsrFrame f{c.astat, c.mstat, c.imask};
    c.enable(AdspCore::kSecReg);
    c.mx0 = static_cast<uint16_t>(c.i[6]);
    c.mx1 = c.px;
    c.setI(6, 0x3400);
    // 0387  13 quadros sem bytes → recomeça no estado 0
    c.ar = rd(0x0608);
    c.ay0 = 0x000D;
    c.toAr(c.aluSub(c.ar, c.ay0));
    if (!c.lt()) wr(0x03FE, static_cast<uint16_t>(c.m[4]));
    auto readByte = [&] {
        c.setSr0(c.dm(6, 4));
        c.lshift(c.sr0(), 8, false);
        c.lshift(c.sr0(), -8, false);
    };
    bool reset_state = true;  // 03EC: estado ← 0
    // 038C
    c.ay0 = rd(0x03FE);
    c.ar = 0x0004;
    c.toAr(c.aluSub(c.ay0, c.ar));
    if (c.eq()) {
        // 03BC  0x55CC: 1º byte → [05F5] (passo do PCM)
        readByte();
        wr(0x05F5, c.sr0());
        c.ar = 0x0005;
        wr(0x03FE, c.ar);
        reset_state = false;
    } else {
        c.ar = 0x0005;
        c.ay0 = rd(0x03FE);
        c.toAr(c.aluSub(c.ay0, c.ar));
        if (c.eq()) {
            // 03C3  2º byte → [05F4] (volume do PCM)
            readByte();
            wr(0x05F4, c.sr0());
        } else {
            c.ay0 = rd(0x03FE);
            c.toAr(c.aluPass(c.ay0));
            if (c.eq()) {
                // 0397  estado 0: byte alto
                c.setSr0(c.dm(6, 4));
                c.lshift(c.sr0(), 8, false);
                wr(0x060A, c.sr0());
                wr(0x03FE, static_cast<uint16_t>(c.m[5]));
                reset_state = false;
            } else {
                c.toAr(c.aluDec(c.ay0));
                if (c.eq()) {
                    // 039E  estado 1: palavra completa
                    readByte();
                    c.setSr1(rd(0x060A));
                    c.lshift(c.sr1(), 0, false, true);
                    c.ay0 = 0x0000;
                    c.toAr(c.aluAdd(c.sr0(), c.ay0));
                    if (!c.lt()) {
                        c.ay0 = 0x55AA;
                        c.toAr(c.aluSub(c.sr0(), c.ay0));
                        if (!c.ge()) {
                            // 03B1  comando comum → fila
                            c.setI(6, rd(0x03FC));
                            c.setL(6, 0x0010);
                            c.dmw(6, 5, c.sr0());
                            c.setL(6, 0x0000);
                            wr(0x03FC, static_cast<uint16_t>(c.i[6]));
                            c.setI(6, 0x3400);
                            wr(0x03FE, static_cast<uint16_t>(c.m[4]));
                            reset_state = false;
                        } else {
                            // 03AA
                            c.ay0 = 0x55CC;
                            c.toAr(c.aluSub(c.sr0(), c.ay0));
                            if (c.eq()) {
                                // 03B9  PCM: mais 2 bytes
                                c.ar = 0x0004;
                                wr(0x03FE, c.ar);
                                reset_state = false;
                            } else {
                                c.ay0 = 0x55AE;
                                c.toAr(c.aluSub(c.sr0(), c.ay0));
                                if (c.le()) {
                                    // 03C8  0x55AA–0x55AE: volume (geral ou do canal n)
                                    c.ay0 = 0x55AA;
                                    c.toAr(c.aluSub(c.sr0(), c.ay0));
                                    wr(0x05F2, c.ar);
                                    c.ar = 0x0002;
                                    wr(0x03FE, c.ar);
                                    reset_state = false;
                                }
                            }
                        }
                    }
                } else {
                    // 03CE
                    c.ar = 0x0002;
                    c.toAr(c.aluSub(c.ay0, c.ar));
                    if (c.eq()) {
                        // 03D1  estado 2: valor do volume
                        readByte();
                        wr(0x060A, c.sr0());
                        c.ar = 0x0003;
                        wr(0x03FE, c.ar);
                        reset_state = false;
                    } else {
                        // 03D8  estado 3: complemento do valor; confere e grava
                        readByte();
                        c.ay0 = rd(0x060A);
                        {
                            const uint16_t t = c.ay0;  // AR = SR0 XOR AY0, AX0 = AY0
                            c.toAr(c.aluXor(c.sr0(), c.ay0));
                            c.ax0 = t;
                        }
                        c.ay0 = 0x00FF;
                        c.toAr(c.aluSub(c.ar, c.ay0));
                        if (c.eq()) {
                            c.ay0 = rd(0x05F2);
                            c.toAr(c.aluPass(c.ay0));
                            if (c.eq()) {
                                wr(0x05F0, c.ax0);  // 03EB  volume geral
                            } else {
                                c.ay0 = 0x05FC;
                                c.ar = rd(0x05F2);
                                c.toAr(c.aluAdd(c.ar, c.ay0));
                                c.ay0 = 0x0001;
                                c.toAr(c.aluSub(c.ar, c.ay0));
                                c.setI(6, c.ar);
                                c.dmw(6, 4, c.ax0);  // volume do canal
                            }
                        }
                    }
                }
            }
        }
    }
    if (reset_state) wr(0x03FE, static_cast<uint16_t>(c.m[4]));  // 03EC
    // 03ED  [0608] = 0; restaura I6 e PX; RTI
    wr(0x0608, static_cast<uint16_t>(c.m[4]));
    c.setI(6, c.mx0);
    c.px = static_cast<uint8_t>(c.mx1);
    c.setMstat(f.mstat);
    c.astat = f.astat;
    c.imask = f.imask;
}

void DcsProgram::r03F1LoadOverlay() {
    // 03F1  banco SI; AX0 palavras de 3 bytes (pula o 4º) de DM(I0) → PM(0x800…)
    wr(0x3000, c.si);
    c.setM(2, 0x0002);
    c.setI(4, 0x0800);
    c.ay0 = 0x00FF;
    c.setSe(0x0008);
    c.setCntr(c.ax0);
    do {
        c.si = c.dm(0, 1);
        {
            c.lshift(c.si, c.se, false);
            c.ar = c.dm(0, 1);
        }
        c.toAr(c.aluAnd(c.ar, c.ay0));
        c.ax0 = c.dm(0, 2);
        c.lshift(c.ar, 0, false, true);
        c.px = static_cast<uint8_t>(c.ax0);
        c.pmw(4, 5, c.sr0());
    } while (c.ce());
}

} // namespace rea::core::audio::dcs
