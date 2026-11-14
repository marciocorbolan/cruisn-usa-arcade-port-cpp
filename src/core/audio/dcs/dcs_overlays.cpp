/**
 * @file dcs_overlays.cpp
 * @brief Overlays do DCS carregados na PM externa (`0x0800`…).
 *
 * @details
 * - **Inicialização** (banco 0 + 0x800, `0x0800`–`0x08B5`): zera a RAM, prepara a fila de comandos, o SPORT1
 *   (buffer `0x0400`, 480 palavras, I7/M5 em autobuffer), copia as tabelas de cosseno/seno (PM `0x0900`/`0x0980`
 *   → DM `0x0700`/`0x0780`), a janela de sobreposição (`0x062A`) e as tabelas de bandas e de Huffman do
 *   banco 3; lê o número de sons (banco 0x24, `0x2046`).
 * - **Sistema principal** (banco 2): `0x0803` prepara um fluxo; `0x0853` decodifica um quadro de um canal
 *   (fatores de escala por banda via Huffman `0x0A49`, coeficientes por tabela de códigos ou em PCM de n bits,
 *   escala pelo fator e pelo volume, soma em `0x3800`); `0x0A7E` avança as rampas, recalcula os volumes e conta
 *   os quadros dos roteiros.
 *
 * O fluxo de bits usa um reservatório de 2 palavras em `0x3902`/`0x3903` (L1 = 2), com a posição em `[063D]`;
 * os bytes vêm da janela da ROM (`[39FF]`, banco `[390F]`).
 */
#include "core/audio/dcs/dcs_board.hpp"
#include "core/audio/dcs/dcs_program.hpp"

namespace rea::core::audio::dcs {

namespace {
/// Formato dos operandos do MAC.
using Mul = AdspCore::Mul;
} // namespace

// =====================================================================================================
// Overlay de inicialização
// =====================================================================================================

void DcsProgram::o0800Init() {
    // 0800  MSTAT = $0010; L0–L6 = 0; M0 = 0; M1 = 1; M4 = 0; M5 = 1; I6 = $3400
    c.si = 0x0010;
    c.setMstat(c.si);
    for (int n = 0; n < 7; ++n) c.setL(n, 0);
    c.setM(0, 0);
    c.setM(1, 1);
    c.setM(4, 0);
    c.setM(5, 1);
    c.setI(6, 0x3400);
    c.ax0 = 0x0000;
    c.ax1 = 0x0001;
    // 0810  zera DM 0x0300–0x07FF e 0x3800–0x39FF
    c.setI(0, 0x0300);
    c.setCntr(0x0500);
    do c.dmw(0, 1, c.ax0);
    while (c.ce());
    c.setI(0, 0x3800);
    c.setCntr(0x0200);
    do c.dmw(0, 1, c.ax0);
    while (c.ce());
    // 0818  metade a escrever; fila; PCM
    c.si = 0x04F0;
    wr(0x0609, c.si);
    c.si = 0x3910;
    wr(0x03FC, c.si);
    wr(0x03FD, c.si);
    c.si = 0x4000;
    wr(0x05F7, c.si);
    c.si = 0x0000;
    wr(0x05F6, c.si);
    wr(0x05F8, c.si);
    wr(0x05F9, c.si);
    wr(0x05F4, c.si);
    wr(0x05F5, c.si);
    // 0825  SPORT1: buffer 0x0400 (480), SCLKDIV = 9, autobuffer TX em I7/M5
    c.setI(7, 0x0400);
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
    c.imask = 0x0020;  // só a IRQ2
    c.si = 0x04F0;
    wr(0x0609, c.si);
    c.setI(4, 0x3400);
    c.si = rd(0x3400);
    // 0838  tabelas da PM → DM
    auto copy = [&](uint32_t from, uint32_t to, uint32_t n) {
        c.setI(5, from);
        c.setI(4, to);
        c.setCntr(n);
        do {
            c.ar = c.pm(5, 5);
            c.dmw(4, 5, c.ar);
        } while (c.ce());
    };
    copy(0x0900, 0x0700, 0x80);  // seno
    copy(0x0980, 0x0780, 0x80);  // cosseno
    copy(0x08F0, 0x062A, 0x10);  // janela da sobreposição
    copy(0x08D5, 0x398C, 0x10);  // largura das bandas
    copy(0x08EC, 0x39F9, 0x04);  // fatores de escala finos
    copy(0x08E5, 0x0611, 0x07);  // bits por código (tabelas 0–6)
    // 085C  tabelas do banco 3 (índice no cabeçalho, quantidade, destino)
    struct T {
        uint16_t index, count;
        uint32_t dest;
    };
    for (const T& t : {T{0x00, 0x3D, 0x399C}, T{0x02, 0x04, 0x39DD}, T{0x04, 0x08, 0x39E1}, T{0x06, 0x20, 0x0300},
                       T{0x0E, 0x10, 0x39E9}, T{0x10, 0x10, 0x0320}, T{0x12, 0x10, 0x0330}, T{0x14, 0x10, 0x3920},
                       T{0x16, 0x10, 0x3930}, T{0x18, 0x10, 0x3940}, T{0x1A, 0x10, 0x3950}}) {
        c.ay1 = t.index;
        c.ax1 = t.count;
        c.setI(4, t.dest);
        o089FCopyTable();
    }
    // 0888  tabelas de códigos 1–3
    c.ay0 = 0x39DD;
    wr(0x39DA, c.ay0);
    c.ay0 = 0x39E1;
    wr(0x39DB, c.ay0);
    c.ay0 = 0x0300;
    wr(0x39DC, c.ay0);
    // 088E  nenhum dono nos 4 canais
    c.setI(0, 0x03B4);
    c.si = 0xFFFF;
    c.setCntr(0x0004);
    do c.dmw(0, 1, c.si);
    while (c.ce());
    // 0893  número de sons: banco 0x24, 0x2046/0x2047
    c.ar = 0x0024;
    wr(0x3000, c.ar);
    c.ay0 = 0x00FF;
    c.si = rd(0x2046);
    c.lshift(c.si, 8, false);
    c.ar = rd(0x2047);
    c.toAr(c.aluAnd(c.ar, c.ay0));
    c.lshift(c.ar, 0, false, true);
    wr(0x060C, c.sr0());
    // 089C  volume geral pedido = 0xE0
    c.si = 0x00E0;
    wr(0x05F0, c.si);
}

void DcsProgram::o089FCopyTable() {
    // 089F  banco 3: deslocamento = palavra no índice AY1; AX1 palavras (byte alto primeiro) → DM(I4)
    c.ay0 = 0x00FF;
    c.setMr0(0x0003);
    wr(0x3000, c.mr0());
    c.ar = 0x2000;
    c.toAr(c.aluAdd(c.ar, c.ay1));
    c.setI(0, c.ar);
    c.ar = c.dm(0, 1);
    c.lshift(c.ar, 8, false);
    c.ar = c.dm(0, 0);
    c.toAr(c.aluAnd(c.ar, c.ay0));
    c.lshift(c.ar, 0, false, true);
    c.setM(3, c.sr0());
    c.setI(0, 0x2000);
    c.modify(0, 3);
    c.setCntr(c.ax1);
    do {
        c.ar = c.dm(0, 1);
        c.lshift(c.ar, 8, false);
        c.ar = c.dm(0, 1);
        c.toAr(c.aluAnd(c.ar, c.ay0));
        c.lshift(c.ar, 0, false, true);
        c.dmw(4, 5, c.sr0());
    } while (c.ce());
}

// =====================================================================================================
// Overlay principal
// =====================================================================================================

void DcsProgram::o0803StreamStart() {
    // 0803  canal [03FF]: M2 = canal, M3 = 2 · canal
    c.si = rd(0x03FF);
    c.setM(2, c.si);
    c.lshift(c.si, 1, false);
    c.setM(3, c.sr0());
    // 0807  cabeçalho de 2 palavras do fluxo → 0x0686 + 2·canal (reservatório inicial de bits)
    c.setI(0, 0x0388);
    c.modify(0, 3);
    c.si = c.dm(0, 1);
    wr(0x39FD, c.si);
    c.si = c.dm(0, 0);
    wr(0x39FE, c.si);
    r0332PointerToBank();
    c.setI(3, 0x0686);
    c.modify(3, 3);
    r0351ReadWord();
    c.dmw(3, 1, c.ar);
    r0351ReadWord();
    c.dmw(3, 0, c.ar);
    // 0814  posição de bit = 0; ponteiro do fluxo avançado
    c.setI(0, 0x0682);
    c.modify(0, 2);
    c.dmw(0, 0, 0x0000);
    c.setI(0, 0x0388);
    c.modify(0, 3);
    r035CBankToPointer();
    c.si = rd(0x39FD);
    c.dmw(0, 1, c.si);
    c.si = rd(0x39FE);
    c.dmw(0, 0, c.si);
    // 081E  número de bandas: até 16 entradas da tabela de bandas, 0x7F encerra
    c.setI(0, 0x03B8);
    c.modify(0, 3);
    c.si = c.dm(0, 1);
    wr(0x060D, c.si);
    c.ar = c.dm(0, 0);
    c.setI(6, c.ar);
    wr(0x0604, c.ar);
    c.ay0 = 0x0010;
    c.toAr(c.aluAdd(c.ar, c.ay0));
    wr(0x060E, c.ar);
    for (;;) {
        // 0828
        c.ar = static_cast<uint16_t>(c.i[6]);
        c.ay0 = rd(0x060E);
        c.toAr(c.aluSub(c.ar, c.ay0));
        if (c.eq()) {
            c.ar = 0x0010;  // 0836
            break;
        }
        c.si = rd(0x390F);
        c.ar = rd(0x060D);
        wr(0x3000, c.ar);
        c.ar = c.dm(6, 5);
        wr(0x3000, c.si);
        c.ay0 = 0x007F;
        c.toAr(c.aluAnd(c.ar, c.ay0));
        c.toAf(c.aluSub(c.ar, c.ay0));
        if (c.eq()) {
            // 0838
            c.ax0 = static_cast<uint16_t>(c.i[6]);
            c.ay0 = rd(0x0604);
            c.toAf(c.aluSub(c.ax0, c.ay0));
            c.toAr(c.aluDec(c.af));
            break;
        }
    }
    // 083C  [3909 + canal] = bandas
    c.setI(0, 0x3909);
    c.modify(0, 2);
    c.dmw(0, 0, c.ar);
    // 083F  zera os 3 parâmetros (0x3960 + 3·canal) e os 16 fatores (0x0641 + 16·canal)
    c.ax1 = static_cast<uint16_t>(c.m[2]);
    c.ay0 = static_cast<uint16_t>(c.m[3]);
    c.toAr(c.aluAdd(c.ax1, c.ay0));
    c.setM(3, c.ar);
    c.setI(0, 0x3960);
    c.modify(0, 3);
    c.ax0 = static_cast<uint16_t>(c.m[0]);
    c.setCntr(0x0003);
    do c.dmw(0, 1, c.ax0);
    while (c.ce());
    c.si = static_cast<uint16_t>(c.m[2]);
    c.lshift(c.si, 4, false);
    c.setM(3, c.sr0());
    c.setI(0, 0x0641);
    c.modify(0, 3);
    c.ax0 = static_cast<uint16_t>(c.m[0]);
    c.setCntr(0x0010);
    do c.dmw(0, 1, c.ax0);
    while (c.ce());
}

void DcsProgram::o0853StreamFrame() {
    // 0853  canal [03FF]: M2 = canal, M3 = 2 · canal
    c.si = rd(0x03FF);
    c.setM(2, c.si);
    c.lshift(c.si, 1, false);
    c.setM(3, c.sr0());
    // 0857  tabela de bandas: banco [060D], início I6 = [0604], fim [060E] = +16
    c.setI(0, 0x03B8);
    c.modify(0, 3);
    c.si = c.dm(0, 1);
    wr(0x060D, c.si);
    c.ar = c.dm(0, 0);
    c.setI(6, c.ar);
    wr(0x0604, static_cast<uint16_t>(c.i[6]));
    c.ay0 = 0x0010;
    c.toAr(c.aluAdd(c.ar, c.ay0));
    wr(0x060E, c.ar);
    // 0861  bit 7 do 1º byte: [3908] = 0 (fatores por tabela 0x39E9/0x320/0x330) ou 1
    wr(0x3000, c.si);
    c.ar = c.dm(6, 5);
    c.ay0 = 0x0080;
    c.toAr(c.aluAnd(c.ar, c.ay0));
    if (c.eq()) wr(0x3908, static_cast<uint16_t>(c.m[1]));
    else wr(0x3908, static_cast<uint16_t>(c.m[0]));
    // 0869  [3904] = bit 7 do 2º byte · 2 + bit 7 do 3º (tabela de parâmetros 0x3920–0x3950)
    c.ar = c.dm(6, 5);
    c.toAr(c.aluAnd(c.ar, c.ay0));
    c.lshift(c.ar, 1, false);
    c.ar = c.dm(6, 4);
    c.toAr(c.aluAnd(c.ar, c.ay0));
    c.lshift(c.ar, 0, false, true);
    wr(0x3904, c.sr0());
    c.setI(6, rd(0x0604));
    // 0871  posição no fluxo
    c.setI(0, 0x0388);
    c.modify(0, 3);
    c.si = c.dm(0, 1);
    wr(0x39FD, c.si);
    c.si = c.dm(0, 0);
    wr(0x39FE, c.si);
    r0332PointerToBank();
    c.si = rd(0x390F);
    wr(0x3000, c.si);
    // 087A  posição de bit e reservatório (0x3902/0x3903)
    c.setI(0, 0x0682);
    c.modify(0, 2);
    c.si = c.dm(0, 0);
    wr(0x063D, c.si);
    c.si = 0x3902;
    wr(0x0640, c.si);
    c.setI(0, 0x0686);
    c.modify(0, 3);
    c.si = c.dm(0, 1);
    wr(0x3902, c.si);
    c.si = c.dm(0, 0);
    wr(0x3903, c.si);
    // 0886  MY1 = volume do canal
    c.setI(0, 0x03A0);
    c.modify(0, 2);
    c.my1 = c.dm(0, 0);
    // 0889  0x3905–0x3907 = (parâmetro + tabela[0x3920/30/40/50 + fator]) / 2
    c.ax1 = static_cast<uint16_t>(c.m[2]);
    c.ay0 = static_cast<uint16_t>(c.m[3]);
    c.toAr(c.aluAdd(c.ax1, c.ay0));
    c.setM(3, c.ar);
    c.setI(0, 0x3960);
    c.modify(0, 3);
    c.si = rd(0x03FF);
    c.lshift(c.si, 4, false);
    c.setM(3, c.sr0());
    c.setI(3, 0x0641);
    c.modify(3, 3);
    c.setI(1, 0x3905);
    c.ar = rd(0x3904);
    c.toAr(c.aluPass(c.ar));
    if (!c.gt()) {
        c.ax1 = 0x3920;
    } else {
        c.ay0 = 0x0001;
        c.toAr(c.aluSub(c.ar, c.ay0));
        if (!c.gt()) {
            c.ax1 = 0x3930;
        } else {
            c.ay0 = 0x0002;
            c.toAr(c.aluSub(c.ar, c.ay0));
            c.ax1 = c.gt() ? 0x3950 : 0x3940;
        }
    }
    c.setCntr(0x0003);
    do {
        c.ay0 = c.dm(3, 1);
        c.toAr(c.aluAdd(c.ax1, c.ay0));
        c.setI(2, c.ar);
        c.ay0 = c.dm(2, 0);
        c.ax0 = c.dm(0, 1);
        c.toAr(c.aluAdd(c.ax0, c.ay0));
        c.lshift(c.ar, -1, false);
        c.dmw(1, 1, c.sr0());
    } while (c.ce());
    // 08AF  diferenças dos fatores de escala por Huffman, uma por banda
    wr(0x0681, static_cast<uint16_t>(c.m[0]));
    c.setI(0, 0x3909);
    c.modify(0, 2);
    c.ar = c.dm(0, 0);
    c.setCntr(c.ar);
    do {
        o0A49Huffman();
        c.setI(0, rd(0x0681));
        c.modify(0, 1);
        wr(0x0681, static_cast<uint16_t>(c.i[0]));
    } while (c.ce());
    // 08B9
    c.ar = rd(0x3801);
    wr(0x063F, c.ar);
    c.setI(3, 0x3801);
    wr(0x0681, static_cast<uint16_t>(c.m[0]));

    for (;;) {
        // 08BD  próxima banda (fim da tabela ou 0x7F encerra)
        c.ar = static_cast<uint16_t>(c.i[6]);
        c.ay0 = rd(0x060E);
        c.toAr(c.aluSub(c.ar, c.ay0));
        if (c.eq()) break;
        c.si = rd(0x390F);
        c.ar = rd(0x060D);
        wr(0x3000, c.ar);
        c.ar = c.dm(6, 5);
        wr(0x3000, c.si);
        c.ay0 = 0x007F;
        c.toAr(c.aluAnd(c.ar, c.ay0));
        c.toAf(c.aluSub(c.ar, c.ay0));
        if (c.eq()) break;
        // 08CA  bit 6: banda intercalada (passo 2, metade dos coeficientes)
        c.lshift(c.ar, -6, false);
        wr(0x060F, c.sr0());
        wr(0x390D, c.ar);
        c.setI(0, 0x398C);
        c.setM(3, rd(0x0681));
        c.modify(0, 3);
        c.ar = c.dm(0, 0);
        c.setM(6, c.ar);
        c.ar = rd(0x060F);
        c.toAr(c.aluPass(c.ar));
        if (c.ne()) {
            c.setM(2, 0x0002);
            c.si = static_cast<uint16_t>(c.m[6]);
            c.lshift(c.si, -1, false);
            c.setM(6, c.sr0());
        } else {
            c.setM(2, 0x0001);
        }
        // 08DB  fator de escala da banda (0x0641 + 16·canal + banda); 0 = banda vazia
        c.si = rd(0x03FF);
        c.lshift(c.si, 4, false);
        c.setM(3, c.sr0());
        c.setI(0, 0x0641);
        c.modify(0, 3);
        c.setM(3, rd(0x0681));
        c.modify(0, 3);
        c.ar = c.dm(0, 0);
        c.toAr(c.aluPass(c.ar));
        if (c.eq()) {
            // 08E5  pula a banda
            c.setI(4, c.i[3]);
            c.modify(4, 6);
            c.setI(3, c.i[4]);
        } else {
            // 08E9  tabela de códigos [063E] e índice fino
            wr(0x063E, c.ar);
            c.setSr0(rd(0x390D));
            c.ar = rd(0x3908);
            c.toAr(c.aluPass(c.ar));
            if (c.eq()) {
                c.ar = rd(0x0681);
                c.ay0 = 0x0003;
                c.toAf(c.aluSub(c.ar, c.ay0));
                if (c.lt()) {
                    c.setM(3, c.ar);
                    c.setI(0, 0x3905);
                    c.modify(0, 3);
                    c.ay0 = c.dm(0, 0);
                    c.ax0 = rd(0x390D);
                    c.toAr(c.aluAdd(c.ax0, c.ay0));
                    wr(0x390D, c.ar);
                    c.ay0 = 0x39E9;
                } else {
                    c.ay0 = 0x0006;
                    c.toAf(c.aluSub(c.ar, c.ay0));
                    c.ay0 = c.ge() ? 0x0330 : 0x0320;
                }
                // 0901
                c.ax0 = rd(0x063E);
                c.toAr(c.aluAdd(c.ax0, c.ay0));
                c.setI(0, c.ar);
                c.ay0 = c.dm(0, 0);
                c.si = c.ay0;
                c.lshift(c.si, -8, false);
                wr(0x063E, c.sr0());
                c.ax0 = 0x00FF;
                c.toAr(c.aluAnd(c.ax0, c.ay0));
                c.ay1 = rd(0x390D);
                c.toAr(c.aluAdd(c.ar, c.ay1));
                c.setSr0(c.ar);
            }
            // 090D  MY0 = mantissa (0x39F9 + fator & 3) >> (15 − fator >> 2)
            c.ay1 = 0x0003;
            c.toAr(c.aluAnd(c.sr0(), c.ay1));
            c.ay1 = 0x39F9;
            c.toAr(c.aluAdd(c.ar, c.ay1));
            c.setI(0, c.ar);
            c.si = c.dm(0, 0);
            c.lshift(c.sr0(), 10, false);
            c.lshift(c.sr0(), -12, false);
            c.ay1 = 0x000F;
            c.toAr(c.aluSub(c.sr0(), c.ay1));
            c.setSe(c.ar);
            c.lshift(c.si, c.se, false);
            c.my0 = c.sr0();
            c.setI(2, 0x396C);
            c.ar = rd(0x063E);
            c.ay1 = 0x0006;
            c.toAf(c.aluSub(c.ar, c.ay1));
            if (c.gt()) {
                // 09C7  tabela > 6: coeficientes em PCM de [063E] bits com sinal
                c.setI(0, rd(0x39FF));
                c.setI(1, rd(0x0640));
                c.setI(4, rd(0x390F));
                wr(0x3000, static_cast<uint16_t>(c.i[4]));
                c.setM(3, 0x3000);
                c.setL(1, 0x0002);
                c.ay0 = rd(0x063E);
                c.toAr(c.aluDec(c.ay0));
                c.ay0 = c.ar;
                c.ar = 0x000F;
                c.toAr(c.aluSub(c.ay0, c.ar));
                {
                    const uint16_t t = c.ar;  // AR = AY0 + 1, MR2 = AR
                    c.toAr(c.aluInc(c.ay0));
                    c.setMr2(t);
                }
                c.ay0 = c.ar;
                c.ay1 = 0x0010;
                c.ax0 = 0x0008;
                c.ax1 = 0x00FF;
                c.setSe(rd(0x063D));
                c.ar = rd(0x063D);
                c.setCntr(static_cast<uint16_t>(c.m[6]));
                c.si = c.dm(1, 1);
                do {
                    {
                        c.lshift(c.si, c.se, true);  // SR = LSHIFT SI (HI), SI = DM(I1,M1)
                        c.si = c.dm(1, 1);
                    }
                    {
                        c.lshift(c.si, c.se, false, true);  // SR = SR OR LSHIFT SI (LO), SE = MR2
                        c.setSe(c.mr2());
                    }
                    c.ashift(c.sr1(), c.se, false);
                    c.dmw(2, 1, c.sr0());  // AR = AR + AY0, DM(I2,M1) = SR0
                    c.toAr(c.aluAdd(c.ar, c.ay0));
                    {
                        const uint16_t t = c.ar;  // AF = AR - AY1, SE = AR
                        c.toAf(c.aluSub(c.ar, c.ay1));
                        c.setSe(t);
                    }
                    if (!c.lt()) {
                        // 09E2  palavra esgotada: nova palavra da ROM no reservatório
                        c.setSe(c.ax0);
                        {
                            const uint16_t t = c.dm(0, 1);  // AF = AX1, SI = DM(I0,M1)
                            c.toAf(c.aluPass(c.ax1));
                            c.si = t;
                        }
                        {
                            c.lshift(c.si, c.se, false);  // SR = LSHIFT SI (LO), MR0 = AR
                            c.setMr0(c.ar);
                        }
                        c.ar = c.dm(0, 1);
                        c.toAr(c.aluAnd(c.ar, c.af));
                        c.lshift(c.ar, 0, false, true);
                        c.dmw(1, 1, c.sr0());  // AR = MR0 - AY1, DM(I1,M1) = SR0
                        c.toAr(c.aluSub(c.mr0(), c.ay1));
                        c.setSr0(static_cast<uint16_t>(c.i[0]));
                        c.lshift(c.sr0(), 3, false);
                        {
                            const uint16_t t = c.ar;  // AF = SR0, SE = AR
                            c.toAf(c.aluPass(c.sr0()));
                            c.setSe(t);
                        }
                        if (c.lt()) {
                            // 09ED  fim da janela: volta a 0x2000 no banco seguinte
                            c.modify(0, 3);
                            c.modify(4, 5);
                            wr(0x3000, static_cast<uint16_t>(c.i[4]));
                        }
                    }
                    c.si = c.dm(1, 1);  // 09F0
                } while (c.ce());
                c.modify(1, 1);
                c.setL(1, 0x0000);
                wr(0x063D, c.ar);
                wr(0x39FF, static_cast<uint16_t>(c.i[0]));
                wr(0x0640, static_cast<uint16_t>(c.i[1]));
                wr(0x390F, static_cast<uint16_t>(c.i[4]));
            } else {
                // 091F  tabela 1–6: códigos de [0618] bits; valor − [0619] (meio da faixa)
                c.si = rd(0x390F);
                wr(0x3000, c.si);
                c.setM(3, rd(0x063E));
                c.setI(0, 0x0611);
                c.modify(0, 3);
                c.ay0 = c.dm(0, 0);
                wr(0x0618, c.ay0);
                c.ar = static_cast<uint16_t>(c.m[3]);
                c.ay0 = 0x0001;
                {
                    const uint16_t t = c.ay0;  // AR = AR - AY0, MR0 = AY0
                    c.toAr(c.aluSub(c.ar, c.ay0));
                    c.setMr0(t);
                }
                c.setSe(c.ar);
                c.lshift(c.mr0(), c.se, false);
                wr(0x0619, c.sr0());
                c.setMr0(0x0003);
                wr(0x3000, c.mr0());
                c.ar = static_cast<uint16_t>(c.m[3]);
                c.ay0 = 0x0003;
                c.toAf(c.aluSub(c.ar, c.ay0));
                const bool wide = c.gt();  // tabelas 4–6: entradas de 2 bytes no banco 3
                if (wide) {
                    o0A6ETablePointer();
                } else {
                    c.setI(0, 0x39D9);
                    c.modify(0, 3);
                    c.si = c.dm(0, 0);
                    wr(0x0610, c.si);
                }
                // 0936/097B
                c.setI(1, rd(0x0640));
                c.setL(1, 0x0002);
                c.ay1 = 0x0010;
                c.ax0 = 0x0008;
                c.setSe(rd(0x063D));
                c.ay0 = rd(0x0618);
                c.ar = 0x0010;
                c.toAr(c.aluSub(c.ay0, c.ar));
                c.setMr2(c.ar);
                c.setI(4, rd(0x39FF));
                c.ar = c.seReg();
                c.si = c.dm(1, 1);
                c.setCntr(static_cast<uint16_t>(c.m[6]));
                do {
                    {
                        c.lshift(c.si, c.se, true);  // SR = LSHIFT SI (HI), SI = DM(I1,M1)
                        c.si = c.dm(1, 1);
                    }
                    {
                        c.lshift(c.si, c.se, false, true);  // SR = SR OR LSHIFT SI (LO), SE = MR2
                        c.setSe(c.mr2());
                    }
                    c.lshift(c.sr1(), c.se, false);
                    c.ay0 = 0x00FF;
                    if (wide) c.lshift(c.sr0(), 1, false);
                    c.setM(3, c.sr0());
                    c.setI(0, rd(0x0610));
                    c.setSe(c.ar);
                    c.modify(0, 3);
                    c.ar = c.dm(0, 1);
                    if (wide) {
                        // 0993  1º byte = bits consumidos, 2º = valor
                        const uint16_t t = c.dm(0, 0);  // AR = AR AND AY0, AX1 = DM(I0,M0)
                        c.toAr(c.aluAnd(c.ar, c.ay0));
                        c.ax1 = t;
                        const uint16_t u = c.ar;  // AR = AX1 AND AY0, SR0 = AR
                        c.toAr(c.aluAnd(c.ax1, c.ay0));
                        c.setSr0(u);
                    } else {
                        // 094D  palavra: byte alto = bits consumidos, baixo = valor
                        c.lshift(c.ar, 8, false);
                        c.toAr(c.aluAnd(c.ar, c.ay0));
                    }
                    c.ay0 = 0x0080;
                    c.toAf(c.aluAnd(c.ar, c.ay0));
                    const uint16_t used = wide ? c.sr0() : c.sr1();
                    if (!c.eq()) {
                        // 0952/0998  bit 7: par de zeros (consome duas posições do contador)
                        c.ar = static_cast<uint16_t>(c.m[0]);
                        c.dmw(2, 1, c.ar);
                        c.dmw(2, 1, c.ar);
                        c.ay0 = static_cast<uint16_t>(c.cntr);
                        c.toAr(c.aluDec(c.ay0));
                        c.ay0 = used;
                        c.owrCntr(c.ar);
                    } else {
                        // 0959/099F
                        c.ay0 = rd(0x0619);
                        c.toAr(c.aluSub(c.ar, c.ay0));
                        c.ay0 = used;
                        c.dmw(2, 1, c.ar);
                    }
                    // 095C/09A2  avança a posição de bit
                    c.ar = c.seReg();
                    c.toAr(c.aluAdd(c.ar, c.ay0));
                    {
                        const uint16_t t = c.ar;  // AF = AR - AY1, SE = AR
                        c.toAf(c.aluSub(c.ar, c.ay1));
                        c.setSe(t);
                    }
                    if (!c.lt()) {
                        // 0960/09A6  nova palavra da ROM no reservatório
                        c.setSe(c.ax0);
                        c.si = rd(0x390F);
                        wr(0x3000, c.si);
                        c.ay0 = 0x00FF;
                        c.si = c.dm(4, 5);
                        {
                            c.lshift(c.si, c.se, false);  // SR = LSHIFT SI (LO), MR0 = AR
                            c.setMr0(c.ar);
                        }
                        c.ar = c.dm(4, 5);
                        c.toAr(c.aluAnd(c.ar, c.ay0));
                        c.lshift(c.ar, 0, false, true);
                        c.dmw(1, 1, c.sr0());  // AR = MR0 - AY1, DM(I1,M1) = SR0
                        c.toAr(c.aluSub(c.mr0(), c.ay1));
                        c.si = 0x0003;
                        wr(0x3000, c.si);
                        c.setSr0(static_cast<uint16_t>(c.i[4]));
                        c.lshift(c.sr0(), 3, false);
                        {
                            const uint16_t t = c.ar;  // AF = SR0, SE = AR
                            c.toAf(c.aluPass(c.sr0()));
                            c.setSe(t);
                        }
                        if (c.lt()) {
                            // 0970  fim da janela: I4 volta a 0x2000; banco seguinte
                            c.setM(4, 0x3000);
                            c.modify(4, 4);
                            c.setM(4, 0x0000);
                            c.ay0 = rd(0x390F);
                            c.toAr(c.aluInc(c.ay0));
                            wr(0x390F, c.ar);
                            c.toAr(c.aluSub(c.mr0(), c.ay1));
                            c.setSe(c.ar);
                        }
                    }
                    c.si = c.dm(1, 1);  // 0978/09BE
                } while (c.ce());
                // 09BF
                c.modify(1, 1);
                wr(0x0640, static_cast<uint16_t>(c.i[1]));
                wr(0x063D, c.ar);
                wr(0x39FF, static_cast<uint16_t>(c.i[4]));
                c.si = rd(0x390F);
                wr(0x3000, c.si);
                c.setL(1, 0x0000);
            }
            // 09F7  coeficientes · MY0 (escala da banda), · MY1 (volume) somados em 0x3800 (passo M2)
            c.setI(2, 0x396C);
            c.setCntr(static_cast<uint16_t>(c.m[6]));
            do {
                {
                    const uint16_t t = c.dm(2, 1);  // AF = AR, AR = DM(I2,M1)
                    c.toAf(c.aluPass(c.ar));
                    c.ar = t;
                }
                c.mul(c.ar, c.my0, Mul::SU);
                c.mx0 = c.mr0();
                c.setMr1(c.dm(3, 0));
                c.mac(c.mx0, c.my1, Mul::SU);
                c.dmw(3, 2, c.mr1());
            } while (c.ce());
        }
        // 0A00  próxima banda
        c.setI(0, rd(0x0681));
        c.modify(0, 1);
        wr(0x0681, static_cast<uint16_t>(c.i[0]));
    }
    // 0A04  [3800] += [3801] antigo − [063F]; [3801] = [063F]
    c.setI(0, 0x3800);
    c.ay0 = c.dm(0, 1);
    c.ay1 = rd(0x063F);
    c.ar = c.dm(0, 0);
    c.dmw(0, 0, c.ay1);  // AR = AR - AY1, DM(I0,M0) = AY1
    c.toAr(c.aluSub(c.ar, c.ay1));
    c.toAr(c.aluAdd(c.ar, c.ay0));
    wr(0x3800, c.ar);
    // 0A0B  guarda posição de bit, ponteiro do fluxo e reservatório
    c.si = rd(0x03FF);
    c.setM(2, c.si);
    c.lshift(c.si, 1, false);
    c.setM(3, c.sr0());
    c.setI(0, 0x0682);
    c.modify(0, 2);
    c.si = rd(0x063D);
    c.dmw(0, 0, c.si);
    c.setI(3, 0x0388);
    c.modify(3, 3);
    r035CBankToPointer();
    c.si = rd(0x39FD);
    c.dmw(3, 1, c.si);
    c.si = rd(0x39FE);
    c.dmw(3, 0, c.si);
    c.setI(1, rd(0x0640));
    c.setL(1, 0x0002);
    c.setI(0, 0x0686);
    c.modify(0, 3);
    c.si = c.dm(1, 1);
    c.dmw(0, 1, c.si);
    c.si = c.dm(1, 0);
    c.dmw(0, 0, c.si);
    c.setL(1, 0x0000);
}

void DcsProgram::o0A27Bits() {
    // 0A27  AY0 bits do reservatório (I1, posição [063D] em I0); AR = MR1 & AY0
    c.ar = c.dm(0, 0);
    {
        const uint16_t t = c.dm(1, 0);  // AR = AR + AY0, SI = DM(I1,M0)
        c.toAr(c.aluAdd(c.ar, c.ay0));
        c.si = t;
    }
    c.setSe(c.ar);
    c.dmw(0, 0, c.ar);  // SR = LSHIFT SI (LO), DM(I0,M0) = AR
    c.lshift(c.si, c.se, false);
    {
        const uint16_t t = c.sr1();  // AR = AR - AY1, MR1 = SR1
        c.toAr(c.aluSub(c.ar, c.ay1));
        c.setMr1(t);
    }
    if (!c.lt()) {
        // 0A2D  palavra esgotada: próxima palavra da ROM no reservatório
        c.dmw(0, 0, c.ar);
        c.setL(1, 0x0002);
        c.setSe(0x0008);
        c.ay1 = 0x00FF;
        c.setI(0, rd(0x39FF));
        c.si = c.dm(0, 1);
        {
            c.lshift(c.si, c.se, false);  // SR = LSHIFT SI (LO), AR = DM(I0,M1)
            c.ar = c.dm(0, 1);
        }
        c.toAr(c.aluAnd(c.ar, c.ay1));
        c.lshift(c.ar, 0, false, true);
        c.dmw(1, 1, c.sr0());
        c.setSr0(static_cast<uint16_t>(c.i[0]));
        c.lshift(c.sr0(), 3, false);
        c.toAf(c.aluPass(c.sr0()));
        c.ay1 = 0x0010;
        if (c.lt()) {
            // 0A3C  fim da janela: 0x2000 do banco seguinte
            c.setM(3, 0x3000);
            c.modify(0, 3);
            c.setM(3, c.ay0);
            c.ay0 = rd(0x390F);
            c.toAr(c.aluInc(c.ay0));
            c.ay0 = static_cast<uint16_t>(c.m[3]);
            wr(0x390F, c.ar);
            wr(0x3000, c.ar);
            c.setL(1, 0x0000);
        }
        // 0A45
        wr(0x39FF, static_cast<uint16_t>(c.i[0]));
        wr(0x0640, static_cast<uint16_t>(c.i[1]));
    }
    // 0A47
    c.toAr(c.aluAnd(c.mr1(), c.ay0));
}

void DcsProgram::o0A49Huffman() {
    // 0A49  árvore em 0x399C: nó ≥ 0 = desvio do ramo 1; folha < 0 = valor
    c.setI(2, 0x399C);
    c.ax0 = c.dm(2, 0);
    c.ax1 = 0x00FF;
    c.ay1 = 0x0010;
    c.setI(1, rd(0x0640));
    c.ay0 = 0x0001;
    do {
        // 0A4F
        c.setI(0, 0x063D);
        o0A27Bits();
        if (c.eq()) {
            c.modify(2, 1);
        } else {
            c.setM(3, c.ax0);
            c.modify(2, 3);
        }
        c.ax0 = c.dm(2, 0);
        c.toAr(c.aluPass(c.ax0));
    } while (c.ge());
    // 0A59  folha: valor & 0xFF − 0x0F (até 0x1E) ou − 0x2E
    c.ay0 = c.ax1;
    c.toAr(c.aluAnd(c.ar, c.ay0));
    c.ay0 = 0x001E;
    c.toAf(c.aluSub(c.ar, c.ay0));
    c.ay0 = c.gt() ? 0x002E : 0x000F;
    c.toAr(c.aluSub(c.ar, c.ay0));
    // 0A63  fator da banda [0681] do canal += diferença
    c.si = rd(0x03FF);
    c.lshift(c.si, 4, false);
    c.setM(3, c.sr0());
    c.setI(0, 0x0641);
    c.modify(0, 3);
    c.setM(3, rd(0x0681));
    c.modify(0, 3);
    c.ay0 = c.dm(0, 0);
    c.toAr(c.aluAdd(c.ar, c.ay0));
    c.dmw(0, 0, c.ar);
}

void DcsProgram::o0A6ETablePointer() {
    // 0A6E  [0610] = 0x2000 + palavra no índice 2·M3 do banco 3
    c.setSr0(static_cast<uint16_t>(c.m[3]));
    c.lshift(c.sr0(), 1, false);
    c.setM(3, c.sr0());
    c.setI(0, 0x2000);
    c.modify(0, 3);
    c.ay0 = 0x00FF;
    c.ar = c.dm(0, 1);
    c.lshift(c.ar, 8, false);
    c.ar = c.dm(0, 1);
    c.toAr(c.aluAnd(c.ar, c.ay0));
    c.lshift(c.ar, 0, false, true);
    c.setM(3, c.sr0());
    c.setI(0, 0x2000);
    c.modify(0, 3);
    wr(0x0610, static_cast<uint16_t>(c.i[0]));
}

void DcsProgram::o0A7EFrameUpdate() {
    // 0A7E  16 rampas: passos > 0 → valor += incremento (±0x1FFF); no último passo valor = alvo
    c.setI(0, 0x0340);
    c.setI(1, 0x0350);
    c.setI(2, 0x0360);
    c.setI(3, 0x0370);
    c.setCntr(0x0010);
    do {
        c.ay0 = c.dm(0, 0);
        c.toAr(c.aluPass(c.ay0));
        if (!c.le()) {
            c.toAr(c.aluDec(c.ay0));
            c.dmw(0, 0, c.ar);
            if (c.eq()) {
                c.ar = c.dm(3, 0);
                c.dmw(1, 0, c.ar);
            } else {
                // 0A8D
                c.ar = c.dm(1, 0);
                c.ay0 = c.dm(2, 0);
                c.toAr(c.aluAdd(c.ar, c.ay0));
                c.ay0 = 0x1FFF;
                c.toAf(c.aluSub(c.ar, c.ay0));
                if (!c.le()) {
                    c.ar = 0x1FFF;
                } else {
                    c.ay0 = 0xE001;
                    c.toAf(c.aluSub(c.ar, c.ay0));
                    if (!c.ge()) c.ar = 0xE001;
                }
                c.dmw(1, 0, c.ar);
            }
        }
        // 0A9A
        c.modify(0, 1);
        c.modify(1, 1);
        c.modify(2, 1);
        c.modify(3, 1);
    } while (c.ce());
    // 0A9F  volume de cada canal = pedido [05FC+n] · 0x7C94^f(soma das 4 rampas)
    c.setI(0, 0x0350);
    c.setI(1, 0x03A0);
    c.setI(2, 0x05FC);
    c.setCntr(0x0004);
    do {
        c.ar = 0x0000;
        c.setCntr(0x0004);
        do {
            c.ay0 = c.dm(0, 1);
            c.toAr(c.aluAdd(c.ar, c.ay0));
        } while (c.ce());
        c.ay0 = 0x1FFF;
        c.toAf(c.aluSub(c.ar, c.ay0));
        if (!c.le()) {
            c.ar = 0x1FFF;
        } else {
            c.ay0 = 0xE001;
            c.toAf(c.aluSub(c.ar, c.ay0));
            if (!c.ge()) c.ar = 0xE001;
        }
        // 0AB2
        c.setSr0(c.dm(2, 1));
        c.lshift(c.sr0(), 7, false);
        c.mx0 = c.sr0();
        c.lshift(c.ar, -6, false);
        c.ay0 = 0x0080;
        c.toAr(c.aluAdd(c.sr0(), c.ay0));
        c.setSr1(c.ar);
        c.setCntr(0x0008);
        c.my0 = 0x7C94;
        c.setMstat(0x0008);
        do {
            c.lshift(c.sr1(), -1, true);
            c.toAf(c.aluPass(c.sr0()));
            c.setMr1(c.mx0);
            if (c.eq()) c.mul(c.mr1(), c.my0, Mul::UU);
            c.mx0 = c.mr1();
            c.setMr1(c.my0);
            {
                const uint16_t t = c.mx0;  // MR = MR1 * MY0 (UU), SR0 = MX0
                c.mul(c.mr1(), c.my0, Mul::UU);
                c.setSr0(t);
            }
            c.my0 = c.mr1();
        } while (c.ce());
        c.setMstat(0x0018);
        c.lshift(c.sr0(), 1, false);
        c.dmw(1, 1, c.sr0());
    } while (c.ce());
    // 0AC8  volume geral [05F1] = [05F0] · 0x7D98^…  (0 = mudo)
    c.setSr1(rd(0x05F0));
    {
        const uint16_t t = c.sr1();  // AF = SR1, SR0 = SR1
        c.toAf(c.aluPass(c.sr1()));
        c.setSr0(t);
    }
    if (!c.eq()) {
        c.setCntr(0x0008);
        c.mx0 = 0x3FFF;
        c.my0 = 0x7D98;
        c.setMstat(0x0008);
        do {
            c.lshift(c.sr1(), -1, true);
            c.toAf(c.aluPass(c.sr0()));
            c.setMr1(c.mx0);
            if (c.eq()) c.mul(c.mr1(), c.my0, Mul::UU);
            c.mx0 = c.mr1();
            c.setMr1(c.my0);
            {
                const uint16_t t = c.mx0;
                c.mul(c.mr1(), c.my0, Mul::UU);
                c.setSr0(t);
            }
            c.my0 = c.mr1();
        } while (c.ce());
        c.setMstat(0x0018);
        c.lshift(c.sr0(), 1, false);
    }
    wr(0x05F1, c.sr0());
    // 0ADB  quadros desde o último passo de cada roteiro
    c.setI(0, 0x03A8);
    c.setM(2, 0x3FFF);
    c.setCntr(0x0004);
    do {
        c.ay0 = c.dm(0, 0);
        c.toAr(c.aluInc(c.ay0));
        c.dmw(0, 1, c.ar);
        c.modify(2, 1);
    } while (c.ce());
    // 0AE4  quadros desde o último byte recebido (máx. 13)
    c.ay0 = rd(0x0608);
    c.toAr(c.aluInc(c.ay0));
    wr(0x0608, c.ar);
    c.ay0 = 0x000D;
    c.toAf(c.aluSub(c.ar, c.ay0));
    if (c.ge()) c.toAr(c.aluPass(c.ay0));
    wr(0x0608, c.ar);
}

} // namespace rea::core::audio::dcs
