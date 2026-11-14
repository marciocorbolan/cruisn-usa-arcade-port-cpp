/**
 * @file test_link.cpp
 * @brief Link entre gabinetes: placa, pacotes, mensagens, carro do outro jogador e abertura do link.
 */
#include "test_support.hpp"

#include <atomic>

#include "platform/link_shm.hpp"

using namespace rea::core;
using namespace rea::test;

namespace {

/// Link entre gabinetes (0x7F5D)
void testLinkBoard() {
    {
        using namespace link::link_vars;
        constexpr uint32_t kGie = Tms32031State::kStGie;
        // DIP Linking desligada (padrão de fábrica): nada acontece.
        {
            MemoryBus lb;
            io::CabinetIo cab;
            lb.attachDevice(&cab);
            FakeFrames clk(lb, 1000);
            link::LinkRoutines lr(clk);
            Tms32031State c;
            lb.writeWord(kDipShadow, 0x0004);
            lr.exchange(c, lb);
            check(lb.readWord(kBusy) == 0 && clk.frames == 0, "0x7F5D: DIP Linking desligada -> RETS (0x7F67)");
        }
        // Gabinete sozinho (loopback do MAME): o Master estoura o tempo.
        {
            MemoryBus lb;
            io::CabinetIo cab;
            io::LinkPort port;
            lb.attachDevice(&cab);
            lb.attachDevice(&port);
            FakeFrames clk(lb, 1000);
            link::LinkRoutines lr(clk);
            Tms32031State c;
            c.st = kGie;
            setupLinkMachine(lb, false, std::vector<uint8_t>(10, 0x5A));
            lr.exchange(c, lb);
            check(lb.readWord(kTimedOut) == 1 && lb.readWord(kMasterErrors) == 1 && lb.readWord(kRxPairs) == 0 &&
                      lb.readWord(kBusy) == 0 && lb.readWord(0xC930) == 0x78 && (c.st & kGie),
                  "0x7F5D Master sozinho: timeout de 120 quadros ([C930]), erro em [0x809A16], GIE religado");
            lr.exchange(c, lb);
            check(lb.readWord(kMasterErrors) == 2 && clk.frames == 0x78,
                  "0x7F5D: após timeout, o limite cai para [C930] + 1 (máx. 4) -> erro imediato");
            lb.writeWord(kTxLength, 0x240);
            lr.exchange(c, lb);
            check(lb.readWord(kMasterErrors) == 3, "0x7F5D: [C8D3] >= 0x240 -> erro (0x8084)");
        }
        // FrameBusyWait: com GIE desligado ninguém alimenta o watchdog de hardware.
        {
            MemoryBus wb;
            io::CabinetIo cab;
            wb.attachDevice(&cab);
            RecordingSubroutines pend;
            SubroutineDispatcher d(pend);
            FrameRunner fr(wb, d, cab);
            FrameBusyWait bw(fr);
            Tms32031State c;  // GIE = 0
            uint32_t spins = 0;
            while (bw.spin(c, vunit::kInstructionsPerFrame) && spins < 1000) ++spins;
            check(spins == 1000 && !bw.resetRequested(), "FrameBusyWait: watchdog de hardware desligado por padrão (como no MAME)");
            bw.setHardwareWatchdog(FrameBusyWait::kMameDefaultWatchdogFrames);
            bw.takeFrames();
            spins = 0;
            while (bw.spin(c, vunit::kInstructionsPerFrame) && spins < 1000) ++spins;
            check(bw.resetRequested() && spins == FrameBusyWait::kMameDefaultWatchdogFrames && bw.takeFrames() == spins + 1,
                  "FrameBusyWait: com prazo de 174 quadros (3 s) sem borda no bit 3 de 0x994000 -> pede RESET");
            // O próprio código inverte o bit 3 (como a 0x503C no autoteste, com o GIE desligado): sem RESET.
            bw.clearReset();
            uint32_t bit = 0;
            spins = 0;
            for (; spins < 1000; ++spins) {
                bit ^= 0x8u;
                wb.writeWord(vunit::kControl, bit);
                if (!bw.spin(c, vunit::kInstructionsPerFrame)) break;
            }
            check(spins == 1000 && !bw.resetRequested(),
                  "FrameBusyWait: bordas de subida do bit 3 de 0x994000 alimentam o watchdog mesmo com o GIE desligado");
        }
        // 0x632F com pausas: cada piscada = 2 × RPTS 0x7A1200, cada dígito + RPTS 0x2160EC0 (LED em 0x994000).
        {
            struct CountWait : IBusyWait {
                uint64_t total = 0;
                uint64_t limit = UINT64_MAX;
                bool spin(Tms32031State&, uint32_t n) override {
                    total += n;
                    return total < limit;
                }
            } cw;
            MemoryBus pb;
            RecordingSubroutines pend;
            SubroutineDispatcher d(pend);
            boot::registerPowerOnTest(d);
            boot::registerPowerOnPauses(d, cw);
            Tms32031State c;
            c.r[1] = 1;
            c.r[2] = 0;
            c.r[3] = 2;
            d.call(0x632F, c, pb);
            const uint64_t expected = 3ull * 2ull * 0x7A1201ull + 3ull * 0x2160EC1ull;
            check(cw.total == expected && (pb.readWord(0xC8C7) & 0x80u) && c.rc == 0xFFFFFFFFu,
                  "0x632F: código 1-0-2 no LED com as pausas de NOPs (~5,9 s de quadros), LED aceso no fim");
            cw.total = 0;
            cw.limit = 0x7A1201ull * 5;  // RESET no meio: a 0x62D5 com falha deixa de piscar
            c.r[1] = 9;
            d.call(0x632F, c, pb);
            check(cw.total == cw.limit, "0x632F: espera interrompida encerra as piscadas");
        }
        // Dois gabinetes: Master <-> Slave pelo cabo, ambos traduzidos.
        {
            MemoryBus mb, sb;
            io::CabinetIo mcab, scab;
            io::LinkPort mport, sport;
            io::LinkCable cable;
            mb.attachDevice(&mcab);
            sb.attachDevice(&scab);
            mb.attachDevice(&mport);
            sb.attachDevice(&sport);
            cable.attach(mport);
            cable.attach(sport);
            std::vector<uint8_t> mtx(37), stx(300);
            for (std::size_t i = 0; i < mtx.size(); ++i) mtx[i] = static_cast<uint8_t>(0xA0 + 7 * i);
            for (std::size_t i = 0; i < stx.size(); ++i) stx[i] = static_cast<uint8_t>(i * 13 + 1);
            setupLinkMachine(mb, false, mtx);
            setupLinkMachine(sb, true, stx);
            LinkCoSim sim;
            LinkCoSim::Side mside(sim, 0, mb), sside(sim, 1, sb);
            link::LinkRoutines mlink(mside), slink(sside);
            Tms32031State mc, sc;
            mc.st = sc.st = kGie;
            std::function<void()> fm = [&] { mlink.exchange(mc, mb); };
            std::function<void()> fs = [&] { slink.exchange(sc, sb); };
            sim.run(fm, fs);

            auto received = [](MemoryBus& b, const std::vector<uint8_t>& sent) {
                const std::size_t pairs = (sent.size() + 1) / 2;
                for (std::size_t i = 0; i < 2 * pairs; ++i) {
                    const uint32_t expect = i < sent.size() ? sent[i] : 0u;  // pad com 0 (0x7F7A)
                    if (b.readWord(0x1F258 + static_cast<uint32_t>(i)) != (expect << 16)) return false;
                }
                return b.readWord(kRxPairs) == pairs - 1;
            };
            check(mb.readWord(kMasterOk) == 1 && sb.readWord(kSlaveOk) == 1 &&
                      mb.readWord(kMasterErrors) == 0 && sb.readWord(kSlaveErrors) == 0,
                  "link co-simulado: Master e Slave concluem sem erro ([0x809A17] / [0x809A19])");
            check(received(sb, mtx), "Slave recebe os 37 bytes do Master (19 pares, byte << 16) e [C8D4] = 18");
            check(received(mb, stx), "Master recebe os 300 bytes do Slave (150 pares) e [C8D4] = 149");
            check(mb.readWord(kBusy) == 0 && sb.readWord(kBusy) == 0 && (mc.st & kGie) && (sc.st & kGie) &&
                      mb.readWord(kConnected) == 1 && sb.readWord(kConnected) == 1,
                  "fim da troca: [E591] = 0, conectados, RETIU religa o GIE nos dois");
        }
    }
}

/// Pacote de envio do link (0x7989 / 0x798F / 0x7998 / 0x79AF / 0x7A41)
void testLinkPacket() {
    {
        using namespace link::link_vars;
        MemoryBus pb;
        pb.writeWord(kTxBufferPtr, 0x1F000);
        pb.writeWord(link::packet::kScratchPtr, 0x809A1B);
        pb.writeWord(0xC8C5, 0x12345678);
        pb.writeWord(0xC8DA, 0xABCD);
        pb.writeWord(0xC8E4, 5);
        pb.writeWord(0xE607, 6);
        pb.writeWord(0xC8E8, 7);
        pb.writeWord(0xE639, doubleToTms(-0.0625));  // x100 = -6.25 -> FIX = -7 (floor)
        RecordingSubroutines none;
        SubroutineDispatcher d(none);
        link::registerPacketRoutines(d);
        Tms32031State c;
        c.r[0] = 0x77;

        pb.writeWord(kDipShadow, 0x0004);  // Linking desligada
        pb.writeWord(kTxLength, 9);
        d.call(0x7989, c, pb);
        check(pb.readWord(kTxLength) == 0 && pb.readWord(0x1F000) == 0 && c.rc == 10 && c.r[0] == 0x77,
              "0x7989 com o link desligado: [C8D3] = 0 e nada é anexado (RC = 10)");

        pb.writeWord(kDipShadow, 0);
        d.call(0x7989, c, pb);
        const std::vector<uint32_t> expect{1, 0x12345678, 0x123456, 0x1234, 0x12, 0xABCD, 0xAB, 5, 6, 7,
                                           static_cast<uint32_t>(-7)};
        bool same = true;
        for (std::size_t i = 0; i < expect.size(); ++i) same = same && pb.readWord(0x1F000 + static_cast<uint32_t>(i)) == expect[i];
        check(same && pb.readWord(kTxLength) == 11, "0x7A41: mensagem 1 = tipo, [C8C5] x4, [C8DA] x2, [C8E4], [E607], [C8E8], FIX(x100)");
        check(c.rc == 0xFFFFFFFFu && c.ar[2] == 0x809A1B && c.r[0] == 0x77,
              "0x7998: RC = -1 após o RPTB (não restaurado); AR2 = início do rascunho; R0 preservado");
        c.ar[2] = 0x1234;
        d.call(0x798F, c, pb);
        check(pb.readWord(kTxLength) == 12 && pb.readWord(0x1F00B) == 0 && c.ar[2] == 0x1234,
              "0x798F: termina o pacote com a palavra 0; AR2 preservado");

        pb.writeWord(kTxLength, 0x250);
        c.ar[2] = 0x809A1B;
        c.rc = 10;
        d.call(0x7998, c, pb);
        check(pb.readWord(kTxLength) == 0x250 && c.rc == 10, "0x7998: 0x250 + 11 > 0x258 -> descarta (RC intacto)");
        pb.writeWord(kTxLength, 0x257);
        c.ar[2] = 0xBEEF;
        d.call(0x79AF, c, pb);
        d.call(0x79AF, c, pb);
        check(pb.readWord(kTxLength) == 0x258 && pb.readWord(0x1F257) == 0xBEEF, "0x79AF: anexa até 0x258 palavras e para");
    }
}

/// Mensagens do link (0x794C, tratadores, emissores, 0x793A)
void testLinkMessages() {
    {
        using namespace link::link_vars;
        using namespace link::msg_vars;
        // Máquina "fake" com a tabela 0x7967 da ROM (33 entradas).
        const uint32_t table[0x21] = {0x79BF, 0x79E7, 0x7ABE, 0x7AA3, 0x7AB2, 0x7A6B, 0x7A8A, 0x7B58, 0x7A8A, 0x79DD, 0x79E2,
                                      0x7A5C, 0x7B49, 0x5278, 0x7BC3, 0x7BB0, 0x992B, 0x98F2, 0x7B43, 0x258F, 0x25AD, 0x7B43,
                                      0x79C8, 0x79CB, 0x79CE, 0x7BD1, 0x7BFA, 0x7BFA, 0x7BFA, 0x7BFA, 0x7BFD, 0x7C0F, 0x7C14};
        auto setupRx = [&](MemoryBus& b) {
            b.writeWord(kHandlerTablePtr, 0x7967);
            for (uint32_t i = 0; i < 0x21; ++i) b.writeWord(0x7967 + i, table[i]);
            b.writeWord(kRxBufferPtr, 0x1F258);
            b.writeWord(kTxBufferPtr, 0x1F000);
            b.writeWord(link::packet::kScratchPtr, 0x809A1B);
            b.writeWord(kScratch3Ptr, 0x809A1B);
            b.writeWord(kSlotTablePtr, 0xE8A3);
            b.writeWord(kDipShadow, 0);
            b.writeWord(kConnected, 1);
        };
        // Simula o cabo: palavras do pacote de A → bytes em bits 16–23 no buffer de B.
        auto deliver = [](MemoryBus& from, MemoryBus& to) {
            const uint32_t n = from.readWord(kTxLength);
            for (uint32_t i = 0; i < n; ++i) to.writeWord(0x1F258 + i, (from.readWord(0x1F000 + i) & 0xFFu) << 16);
            if (n & 1u) to.writeWord(0x1F258 + n, 0);
            to.writeWord(kRxPairs, (n + 1) / 2 - 1);
        };

        MemoryBus a, b;
        setupRx(a);
        setupRx(b);
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        link::registerPacketRoutines(d);
        link::registerMessageRoutines(d);
        Tms32031State c;

        // 0x793A
        b.writeWord(kVarE592, 0x55);
        for (uint32_t v : {kLocalC8DA, kPeerC8DA, kPeerMode, kRaceFlag, kVarE594, kVarC8DD, kVarE404}) b.writeWord(v, 9);
        for (uint32_t i = 0; i < 11; ++i) b.writeWord(0xE8A3 + i, 9);
        d.call(0x793A, c, b);
        bool zero = b.readWord(kVarE592) == 0 && b.readWord(kVarE596) == 0x55 && b.readWord(0xE8A3 + 10) == 9;
        for (uint32_t v : {kLocalC8DA, kPeerC8DA, kPeerMode, kRaceFlag, kVarE594, kVarC8DD, kVarE404}) zero = zero && b.readWord(v) == 0;
        for (uint32_t i = 0; i < 10; ++i) zero = zero && b.readWord(0xE8A3 + i) == 0;
        check(zero && b.readWord(kVarC8D7) == 0xFFFFFFFFu && b.readWord(kVarC8D8) == 0xFFFFFFFFu,
              "0x793A: [E596] = [E592]; zera variáveis e 10 palavras de [0x508E]; [C8D7] = [C8D8] = -1");

        // Mensagem 1 (status) de A lida por B
        a.writeWord(0xC8C5, 0x12345674);
        a.writeWord(0xC8DA, 0x06CD);  // bit 0x800 desligado
        a.writeWord(0xC8E4, 5);
        a.writeWord(0xE607, 6);
        a.writeWord(0xC8E8, static_cast<uint32_t>(-3));
        a.writeWord(0xE639, doubleToTms(0.5));
        d.call(0x7989, c, a);
        d.call(0x798F, c, a);
        deliver(a, b);
        b.writeWord(kPeerC8E8, 42);
        b.writeWord(kGameMode, 4);
        b.writeWord(kLocalC8DA, 0x0300);  // nosso 0x300 < dele 0x600 -> CALL $AD00
        c.r[0] = 0x11; c.r[1] = 0x22; c.r[2] = 0x33;
        pend.calls.clear();
        d.call(0x794C, c, b);
        check(b.readWord(kPeerMode) == 0x12345674 && b.readWord(kPeerC8DA) == 0x06CD && b.readWord(kPeerC8E4) == 5 &&
                  b.readWord(kPeerE607) == 6 && b.readWord(kPeerC8E8) == static_cast<uint32_t>(-3) &&
                  b.readWord(kPeerC8E8Prev) == 42,
              "0x794C + 0x79E7: B reconstrói o status de A (bytes LSB primeiro, [C8E8] com sinal, anterior em [C8DC])");
        check(std::fabs(tmsToDouble(b.readWord(kPeerE639)) - 50 * 0.0099983215) < 1e-6,
              "0x79E7: [C8D5] = FLOAT(byte) x 0,009998 (0x923D)");
        check(pend.calls.size() == 1 && pend.calls[0].addr == 0xAD00 && b.readWord(kRxPairs) == 0 &&
                  c.r[0] == 0 && c.r[1] == 0x22 && c.r[2] == 0x33 && c.ar[2] == 0x1F258 + 12,
              "0x79E7: modo 4 dos dois e nosso 0xF00 < dele -> CALL $AD00; R1/R2 preservados; 0x7963 zera R0 e [C8D4]");

        // Outro no modo 2 -> [C8E3] = 0, [C8D7] = [C8D8] = -1
        a.writeWord(0xC8C5, 2);
        d.call(0x7989, c, a);
        d.call(0x798F, c, a);
        deliver(a, b);
        b.writeWord(kRaceFlag, 1);
        b.writeWord(kVarC8D7, 0);
        d.call(0x794C, c, b);
        check(b.readWord(kRaceFlag) == 0 && b.readWord(kVarC8D7) == 0xFFFFFFFFu, "0x79E7: outro no modo 2 encerra a corrida");

        // Guardas: Linking desligada / sem pares
        deliver(a, b);
        b.writeWord(kDipShadow, 4);
        d.call(0x794C, c, b);
        check(b.readWord(kRxPairs) == 5, "0x794C: Linking desligada -> não lê nem zera [C8D4]");
        b.writeWord(kDipShadow, 0);

        // Mensagens curtas: 2, 3, 4, 0x1F, 0x20 em um só pacote
        a.writeWord(0xC8C5, 0);
        a.writeWord(kVarE604, 0x1234);
        a.writeWord(kVarC93E, 0x56);
        a.writeWord(0xEA30, static_cast<uint32_t>(-2));
        a.writeWord(0xE636, 7);
        a.writeWord(0xE634, 8);
        a.writeWord(0xE640, 9);
        d.call(0x7989, c, a);
        for (uint32_t r : {0x7A8D, 0x7A98, 0x7AB7, 0x7C12}) d.call(r, c, a);
        a.writeWord(kVar9A6C, 2);
        a.writeWord(kDipShadow, 0x0001);  // Slave -> 0xA0
        d.call(0x7C1C, c, a);
        check(a.readWord(kVar9A6C) == 3 && a.readWord(0x1F000 + a.readWord(kTxLength) - 1) == 0xA1,
              "0x7C1C: [9A6C] += 1; envia [C8CC] | 0xA0 (o OR usa R0 = DIPs, bug preservado)");
        a.writeWord(kDipShadow, 0);
        d.call(0x798F, c, a);
        deliver(a, b);
        b.writeWord(kVarE597, 0);
        d.call(0x794C, c, b);
        check(b.readWord(kVarE604) == 0x1234 && b.readWord(kVarC93E) == (0x56u << 16),
              "0x7ABE: [E604] em 2 bytes; [C93E] = byte << 16 (sem deslocar)");
        check(b.readWord(kVarC8DF) == static_cast<uint32_t>(-2) && b.readWord(kVarC8E0) == 7 &&
                  b.readWord(kVarC8D7) == 8 && b.readWord(kVarC8D8) == 9,
              "0x7AA3 / 0x7AB2: [C8DF] com sinal, [C8E0], [C8D7], [C8D8]");
        check(b.readWord(kVarE597) == 1 && b.readWord(kVar9A6B) == 0xFFFFFFA1u && b.readWord(kVar9A6C) == 2,
              "0x7C0F / 0x7C14: [E597] = 1; [9A6B] = byte com sinal; [9A6C] = (byte & 0xF) + 1 pela resposta");
        check(b.readWord(kTxLength) == 2 && b.readWord(0x1F000) == 0x20 && b.readWord(0x1F001) == 0xB0,
              "0x7C14: B responde com a mensagem 0x20 (Master -> 0xB0)");

        // Tratador pendente: a leitura para (o payload não vira tipo)
        b.writeWord(0x1F258, 0x10u << 16);
        b.writeWord(0x1F259, 0x1Fu << 16);  // seria lido como tipo 0x1F
        b.writeWord(0x1F25A, 0x1Fu << 16);
        b.writeWord(0x1F25B, 0);
        b.writeWord(kRxPairs, 1);
        b.writeWord(kVarE597, 0);
        pend.calls.clear();
        d.call(0x794C, c, b);
        check(b.readWord(kVarE597) == 0 && pend.calls.empty() && b.readWord(kRxPairs) == 0,
              "0x794C: tipo sem tratador registrado (0x10 sem o tráfego) interrompe a leitura sem dessincronizar");
        // Sem a consulta (comportamento cru): despacha para o pendente e segue
        b.writeWord(kRxPairs, 1);
        link::parseReceived(c, b, d);
        check(pend.calls.size() == 1 && pend.calls[0].addr == 0x992B && b.readWord(kVarE597) == 1,
              "parseReceived sem consulta: chama 0x992B e lê o payload seguinte como tipo 0x1F");

        // Estado de carro (0x7ADB → tipo 7 → 0x7B58; 0x7ACF → tipo 0x12 → 0x7B43)
        {
            for (MemoryBus* m : {&a, &b}) {
                m->writeWord(0x7B32, 0xF103126Fu);  // ≈ 1/32000 (cópia da ROM na RAM)
                m->writeWord(0x7B33, 0xF603126Fu);  // ≈ 1/1000
            }
            constexpr uint32_t kObj = 0xF000, kData = 0xF100, kPcb = 0xF200;
            const double pos[3] = {1234.0, -50.5, 98765.0};
            const double mat[9] = {0.5, -0.25, 1.0, 0.0, -1.0, 0.125, 0.75, -0.5, 0.999};
            for (int i = 0; i < 3; ++i) a.writeWord(kObj + 1 + i, doubleToTms(pos[i]));
            for (int i = 0; i < 9; ++i) a.writeWord(kObj + 4 + i, doubleToTms(mat[i]));
            a.writeWord(kObj + 0x1B, kData);
            a.writeWord(kData + 0x2C, doubleToTms(0.5));
            a.writeWord(kData + 0x21, doubleToTms(-1.0));
            a.writeWord(kData + 0x2D, doubleToTms(2.0));
            a.writeWord(kData + 0x26, doubleToTms(-300.0));
            a.writeWord(kData + 0x1F, 3);
            a.writeWord(kData + 0x20, 4);
            a.writeWord(kData + 0x2B, doubleToTms(0.375));
            a.writeWord(kData + 0x57, 0x123456);
            a.writeWord(kData + 0x59, 7);
            a.writeWord(kPcb + 0x0E, 2);
            a.writeWord(kPcb + 0x13, 0x0A0B0C);
            c.ar[4] = kObj;
            c.ar[5] = kData;
            c.ar[7] = kPcb;
            d.call(0x7989, c, a);
            pend.calls.clear();
            d.call(0x7ADB, c, a);
            const bool angles_via_9308 = pend.calls.size() == 3 && pend.calls[0].addr == 0x9308;
            check(a.readWord(kTxLength) >= 46 && angles_via_9308,
                  "0x7ADB: mensagem de 46 palavras (tipo 7) anexada; ângulos normalizados por 0x9308");
            d.call(0x7ACF, c, a);
            d.call(0x798F, c, a);
            check(a.readWord(kData + 0x57) == 0x0A0B0C, "0x7ACF: *+AR5(57) = *+AR7(13) antes do envio");

            // B: carro do link [E592] (tipo 7) e o carro com +59 = 7 na lista [E404] (tipo 0x12)
            constexpr uint32_t kObjB = 0xF300, kDataB = 0xF400, kObjC = 0xF500, kDataC = 0xF600;
            b.writeWord(kVarE592, kObjB);
            b.writeWord(kVarE593, 0);
            b.writeWord(kObjB + 0x1B, kDataB);
            b.writeWord(kVarE404, kObjC);
            b.writeWord(kObjC + 0x1B, kDataC);
            b.writeWord(kObjC + 0x20, 0);
            b.writeWord(kDataC + 0x59, 7);
            deliver(a, b);
            d.call(0x794C, c, b);
            auto near = [](double x, double y, double tol) { return std::fabs(x - y) <= tol; };
            bool ok = tmsToDouble(b.readWord(kObjB + 1)) == 1234.0 && tmsToDouble(b.readWord(kObjB + 2)) == -51.0 &&
                      tmsToDouble(b.readWord(kObjB + 3)) == 98765.0;
            for (int i = 0; i < 9; ++i) ok = ok && near(tmsToDouble(b.readWord(kObjB + 4 + i)), mat[i], 1e-4);
            check(ok, "0x7B58: posição (FIX: -50,5 -> -51) e matriz (16 bits · 1/32000) do carro [E592]");
            check(near(tmsToDouble(b.readWord(kDataB + 0x2C)), 0.5, 2e-3) &&
                      near(tmsToDouble(b.readWord(kDataB + 0x21)), -1.0, 2e-3) &&
                      near(tmsToDouble(b.readWord(kDataB + 0x2D)), 2.0, 2e-3) &&
                      tmsToDouble(b.readWord(kDataB + 0x26)) == -300.0 && b.readWord(kDataB + 0x1F) == 3 &&
                      b.readWord(kDataB + 0x20) == 4 && near(tmsToDouble(b.readWord(kDataB + 0x2B)), 0.37, 1e-3) &&
                      b.readWord(kDataB + 0x57) == 0x123456 && b.readWord(kDataB + 0x5A) == 1,
                  "0x7B58: ângulos (· 1/1000), +26, +1F, +20, +2B (37 · 0,009998), +57 (24 bits) e +5A = 1");
            check(tmsToDouble(b.readWord(kObjC + 3)) == 98765.0 && b.readWord(kDataC + 0x57) == 0x0A0B0C &&
                      b.readWord(kDataC + 0x5A) == 1,
                  "0x7B43: tipo 0x12 acha em [E404] o carro com +59 = 7 e aplica o estado");
            // [E593] ≠ 0: payload pulado sem dessincronizar (a mensagem 0x1F seguinte ainda é lida)
            d.call(0x7989, c, a);
            d.call(0x7ADB, c, a);
            d.call(0x7C12, c, a);
            d.call(0x798F, c, a);
            b.writeWord(kVarE593, 1);
            b.writeWord(kVarE597, 0);
            b.writeWord(kObjB + 1, 0);
            deliver(a, b);
            d.call(0x794C, c, b);
            check(b.readWord(kObjB + 1) == 0 && b.readWord(kVarE597) == 1,
                  "0x7BAE: com [E593] o estado é pulado (AR2 += 0x2D) e a mensagem seguinte é lida");

            // Batida pelo link: 0x2597 (tipo 0x14) e 0x2587 (tipo 0x13)
            for (uint32_t addr : {0x2595u, 0x2596u, 0x25D3u, 0x25D4u, 0x25D5u})
                b.writeWord(addr, addr == 0x2595 ? 0x2560 : addr == 0x2596 ? 0x255E : addr == 0x25D3 ? 0x257B
                                  : addr == 0x25D4 ? 0x2478 : 0x24CF);
            constexpr uint32_t kPcbA = 0xF700, kPcbC = 0xF800, kWinHi = 0xF900, kWinLo = 0xF910;
            for (uint32_t i = 0; i < 6; ++i) a.writeWord(kPcbA + 0x0C + i, 0x11223344u + i * 0x01010101u);
            b.writeWord(kObjC + 0x17, kPcbC);
            b.writeWord(0xE470, kWinHi);
            b.writeWord(kWinHi + 0x1E, 100);
            b.writeWord(0xE46E, kWinLo);
            b.writeWord(kWinLo + 0x1E, 10);
            b.writeWord(kVarE593, 0);
            c.ar[5] = kData;
            c.ar[7] = kPcbA;
            c.r[4] = 1;
            d.call(0x7989, c, a);
            d.call(0x2597, c, a);
            d.call(0x798F, c, a);
            deliver(a, b);
            b.writeWord(kDataC + 0x57, 50);  // dentro da janela 10..100
            b.writeWord(kDataC + 0x58, 9);
            d.call(0x794C, c, b);
            bool same = true;
            for (uint32_t i = 0; i < 6; ++i) same = same && b.readWord(kPcbC + 0x0C + i) == 0x11223344u + i * 0x01010101u;
            check(same && b.readWord(kPcbC + 2) == 0x24CF && b.readWord(kDataC + 0x58) == 0,
                  "0x2597 -> 0x25AD: 6 palavras de 32 bits copiadas; dentro da janela com R4 = 1 retoma em 0x24CF");
            c.ar[5] = kData;  // o 0x25AD de B trocou AR5/AR7 (como no original)
            d.call(0x7989, c, a);
            d.call(0x2587, c, a);
            d.call(0x798F, c, a);
            deliver(a, b);
            b.writeWord(kPcbC + 2, 0);
            d.call(0x794C, c, b);
            check(b.readWord(kPcbC + 2) == 0x2560, "0x2587 -> 0x258F: o processo do carro retoma em [2595] = 0x2560");

            // Carro de tráfego passado ao outro gabinete: 0x5255 (tipo 0xD) -> 0x5278
            a.writeWord(0xE8AD, 10);
            a.writeWord(0xE8AE, 90);
            a.writeWord(kPcbA + 0x13, 40);
            a.writeWord(kPcbA + 0x0E, 3);
            c.ar[7] = kPcbA;
            c.r[0] = 1;  // 0x5268 fica pendente no teste: R1 − R0 = voltas do focado − do carro [E592]
            c.r[1] = 2;
            d.call(0x7989, c, a);
            const bool handed = link::trafficHandOff(c, a, d, true);
            c.r[0] = 2;
            c.r[1] = 2;
            const bool same_lap = link::trafficHandOff(c, a, d, true);
            d.call(0x798F, c, a);
            check(handed && !same_lap && a.readWord(0x1F000 + 11) == 0x0D && a.readWord(0x1F000 + 12) == 3,
                  "0x5255: focado à frente nas voltas e +13 em [E8AD]..[E8AE] -> envia 0xD com *+AR7(E) e segue para 0xA718");
            constexpr uint32_t kObjD = 0xFA00, kPcbD = 0xFA40, kDataD = 0xFA80;
            for (uint32_t addr : {0x52A5u, 0x52A6u, 0x52A7u})
                b.writeWord(addr, addr == 0x52A5 ? 0x5208 : addr == 0x52A6 ? 0x5225 : 0x52A8);
            b.writeWord(0xE8A3 + 3, kObjD);
            b.writeWord(kObjD + 0x17, kPcbD);
            b.writeWord(kObjD + 0x1B, kDataD);
            b.writeWord(kDataD + 0x57, 50);
            b.writeWord(kDataD + 0x58, 9);
            b.writeWord(kWinLo + 0x21, kWinLo);  // [[[[E46E]+21]+21]+21] = a própria janela baixa (+1E = 10)
            b.writeWord(kVarE593, 0);
            deliver(a, b);
            d.call(0x794C, c, b);
            check(b.readWord(kPcbD + 2) == 0x52A8 && b.readWord(kPcbD + 0x13) == 50 && b.readWord(kPcbD + 0x23) == 0x4B &&
                      b.readWord(kPcbD + 0x25) == 1 && tmsToDouble(b.readWord(kPcbD + 0x0F)) == 1.0 &&
                      b.readWord(kDataD + 0x58) == 0,
                  "0x5278: o carro do slot 3 volta (+13 = +57, +23 = 0x4B, +25 = 1) e retoma em 0x52A8");

            // Tipos 0x10 / 0x11 (tratadores do tráfego): 0x9924 / 0x98E4 -> 0x992B / 0x98F2
            {
                RecordingSubroutines pend2;
                SubroutineDispatcher d2(pend2);
                link::registerPacketRoutines(d2);
                link::registerMessageRoutines(d2);
                game::registerTrafficCars(d2);
                b.writeWord(0x9902, 0x9903);
                b.writeWord(0x9935, 0x98A9);
                b.writeWord(kPcbC + 2, 0);
                c.r[0] = 7;  // carro +59 = 7
                d2.call(0x7989, c, a);
                d2.call(0x9924, c, a);
                d2.call(0x798F, c, a);
                deliver(a, b);
                d2.call(0x794C, c, b);
                check(b.readWord(kPcbC + 2) == 0x98A9, "0x9924 -> 0x992B (tipo 0x10): o carro retoma em [9935] = 0x98A9");
                // 0x11: carro, +59 e +F & 0x20 (como a 0x98E4 monta)
                d2.call(0x7989, c, a);
                c.ar[2] = a.readWord(0x7939);
                a.writeWord(c.ar[2], 0x11);
                a.writeWord(c.ar[2] + 1, 7);
                a.writeWord(c.ar[2] + 2, 0x20);
                c.rc = 2;
                d2.call(0x7998, c, a);
                d2.call(0x798F, c, a);
                deliver(a, b);
                d2.call(0x794C, c, b);
                check(b.readWord(kPcbC + 2) == 0x9903 && b.readWord(kPcbC + 5) == (0x20u << 16),
                      "0x98F2 (tipo 0x11): retoma em 0x9903 com PCB+5 (R4) = palavra de flags (0x20 << 16)");
            }
        }

        // Tipo 5 com [E594] = 1: início de corrida
        b.writeWord(kVarE594, 1);
        b.writeWord(0xE636, 1);
        b.writeWord(kTxLength, 0);
        c.ar[2] = 0x1F259;
        pend.calls.clear();
        d.call(0x7A6B, c, b);
        check(b.readWord(kRaceFlag) == 1 && b.readWord(kVarC8DD) == 0 && b.readWord(kVarE594) == 0 &&
                  b.readWord(kVarC8D8) == 0xFFFFFFFFu && c.ar[2] == 0x809A1B,
              "0x7A6B: [C8E3] = 1, zera [C8DD]/[E594]; AR2 não é salvo (0xF8, depois o rascunho da 0x7A98)");
        check(pend.calls.size() == 2 && pend.calls[0].addr == 0x99DA && pend.calls[0].ar2 == 0xF8 &&
                  pend.calls[1].addr == 0x91E0 && pend.calls[1].ar2 == 0xF3 && b.readWord(0x1F000) == 9 &&
                  b.readWord(0x1F001) == 3 && b.readWord(kTxLength) == 5,
              "0x7A6B: auditoria 0xF8, envia 9, CALL $91E0 (AR2 = 0xF3), envia a mensagem 3");
        b.writeWord(kVarE594, 0);
        b.writeWord(kTxLength, 0);
        d.call(0x7A6B, c, b);
        check(b.readWord(kVarC8DD) == 1 && b.readWord(0x1F000) == 0x0A, "0x7A6B sem [E594]: [C8DD] = 1 e envia 0xA");

        // 0x7A61 (envia 5) e 0x7A36
        b.writeWord(kTxLength, 0);
        b.writeWord(kLocalC8DA, 0x7);
        d.call(0x7A61, c, b);
        check(b.readWord(0x1F000) == 5 && b.readWord(kVarE594) == 1 && b.readWord(kLocalC8DA) == 0,
              "0x7A61: envia 5, zera [C8DA]/[C8E3], [E594] = 1");
        b.writeWord(kVarE63A, 2);
        d.call(0x7A36, c, b);
        d.call(0x7A3D, c, b);
        check(b.readWord(kLocalC8DA) == 0x0C00, "0x7A36 / 0x7A3D: [C8DA] |= (1 << [E63A]) << 8 e |= 0x800");

        // 0x7BD1: 2 valores de 24 bits
        c.ar[2] = 0x1F300;
        const uint32_t w[6] = {0x11u << 16, 0x22u << 16, 0x33u << 16, 0x44u << 16, 0x55u << 16, 0x66u << 16};
        for (uint32_t i = 0; i < 6; ++i) b.writeWord(0x1F300 + i, w[i]);
        d.call(0x7BD1, c, b);
        check(b.readWord(kVarE8AD) == 0x332211 && b.readWord(kVarE8AE) == 0x665544 && c.ar[2] == 0x1F306,
              "0x7BD1: [E8AD]/[E8AE] = b0 + b1<<8 + b2<<16");
    }
}

/// 0xA6F8: objeto [C98B] acima do carro do outro gabinete
void testLinkOpponentMarker() {
    {
        MemoryBus m;
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        game::registerLinkCar(d);
        Tms32031State c;
        constexpr uint32_t kCar = 0xF000, kMark = 0xF100, kTable = 0xF200;
        m.writeWord(0xC98B, kMark);
        m.writeWord(0xE592, kCar);
        m.writeWord(0x0052, kTable);
        m.writeWord(kCar + 0x1C, 0x35);           // >> 4 = 3
        m.writeWord(kTable + 3, doubleToTms(32.0));  // 0xA585 pendente: R0 fica 32 (>= 16)
        m.writeWord(kCar + 1, doubleToTms(100.0));
        m.writeWord(kCar + 2, doubleToTms(-50.0));
        m.writeWord(kCar + 3, doubleToTms(7000.0));
        m.writeWord(kCar, 0xF300);                 // próximo da lista
        m.writeWord(kCar + 0x0E, 0x3001);
        m.writeWord(kMark + 0x0E, 0x0482);
        d.call(0xA6F8, c, m);
        check(tmsToDouble(m.readWord(kMark + 4)) == 32.0 && tmsToDouble(m.readWord(kMark + 0xC)) == 32.0 &&
                  tmsToDouble(m.readWord(kMark + 1)) == 100.0 && tmsToDouble(m.readWord(kMark + 2)) == -50.0 - 35.0 * 32.0 - 20.0 &&
                  m.readWord(kCar) == kMark && m.readWord(kMark) == 0xF300 && m.readWord(kMark + 0x0E) == 0x3482 &&
                  pend.calls.size() == 2 && pend.calls[0].addr == 0xA585 && pend.calls[1].addr == 0x715F,
              "0xA6F8: escala [[0x52] + (+1C >> 4)] (mín. 16), 35·escala + 20 acima do carro, encadeado logo depois dele");
        m.writeWord(kTable + 3, doubleToTms(2.0));
        d.call(0xA6F8, c, m);
        check(tmsToDouble(m.readWord(kMark + 8)) == 16.0, "0xA6F8: escala abaixo de 16 vira 16");
    }
}

/// 0x154D: abertura do link (caso −7) e 0xA1EA (caso −6)
void testLinkIntroAndCredits() {
    {
        MemoryBus m;
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        game::registerLinkIntro(d);
        Tms32031State c;
        auto lost = [&](uint32_t c8d1, uint32_t c8d6, uint32_t c8dd) {
            m.writeWord(0xC8D1, c8d1);
            m.writeWord(0xC8D6, c8d6);
            m.writeWord(0xC8DD, c8dd);
            c.st = 0;
            d.call(0x15D4, c, m);
            return c.carry();
        };
        check(lost(0, 5, 1) && !lost(1, 0x36, 0) && lost(1, 2, 1) && lost(1, 3, 0) && !lost(1, 3, 1),
              "0x15D4: link caído sem link, no modo 2 ou com [C8DD] = 0 fora dos modos 5..7");
        constexpr uint32_t kPcb = 0xF000, kC = 0xF100, kD = 0xF200, kE = 0xF300;
        c.ar[7] = kPcb;
        m.writeWord(kPcb + 0x0C, kC);
        m.writeWord(kPcb + 0x0D, kD);
        m.writeWord(kPcb + 0x0E, kE);
        m.writeWord(0xB1CF, doubleToTms(1000.0));
        m.writeWord(0x158C, 0x0A61C4CDu);  // MPYF 0,100006,R1
        m.writeWord(0xC8D1, 1);
        m.writeWord(0xC8D6, 5);
        c.ar[5] = 3;
        c.ar[6] = 0;
        c.r[6] = doubleToTms(100.0);
        c.r[7] = doubleToTms(2000.0);
        d.call(0x1593, c, m);
        const double r7 = tmsToDouble(c.r[7]);
        check(c.ar[5] == 2 && tmsToDouble(m.readWord(kD + 2)) == 100.0 && tmsToDouble(m.readWord(kC + 2)) == -100.0 &&
                  tmsToDouble(m.readWord(kE + 3)) == 2000.0 && tmsToDouble(c.r[6]) == 96.0 && r7 > 1899.0 && r7 < 1901.0 &&
                  !pend.calls.empty() && pend.calls.back().addr == 0xA874 && c.ret == 0x1593,
              "0x1582: peças C/D em ∓R6, E em R7; R6 − 4, R7 − (R7 − [B1CF])·0,1; dorme 1 quadro");
        pend.calls.clear();
        m.writeWord(0xC8D1, 0);
        c.ar[5] = 3;
        d.call(0x1593, c, m);
        check(m.readWord(0xC8E8) == 0xFFFFFFFEu && !pend.calls.empty() && pend.calls.front().addr == 0x793A,
              "0x154D: link caído → 0x793A, [C8E8] = −2 e reinício");
    }
}

/// Cabo de link por arquivo: reabrir um lado com o outro rodando não congela o outro
void testShmLinkRestart() {
    const auto file = tempPath("link_test.shm").string();
    std::error_code ec;  // limpeza sem exceção: no Windows um arquivo ainda mapeado não pode ser apagado
    std::filesystem::remove(file, ec);
    bool ok_a = false, ok_b2 = false;
    long a_seen = 0, b_until_a = 0;
    {  // os dois lados fecham (desfazem o mapeamento) antes de o arquivo ser apagado
        rea::platform::ShmLinkWire a(file, 0);
        {
            rea::platform::ShmLinkWire b(file, 1);
            std::thread tb([&] { for (int i = 0; i < 2000; ++i) b.advance(1000); });
            for (int i = 0; i < 2000; ++i) a.advance(1000);
            tb.join();
        }  // B fecha (sair do executável)
        rea::platform::ShmLinkWire b2(file, 1);  // B reabre com A rodando
        std::atomic<bool> stop{false};
        std::atomic<long> a_steps{0};
        std::thread ta([&] {
            while (!stop) {
                a.advance(1000);
                ++a_steps;
            }
        });
        long b_steps = 0;
        while (a_steps < 10 && b_steps < 5000) {
            b2.advance(1000);
            ++b_steps;
        }
        b_until_a = b_steps;  // passos de B até A voltar a andar
        stop = true;
        while (b_steps < b_until_a + 1000) {  // solta o A, se ele estiver esperando
            b2.advance(1000);
            ++b_steps;
        }
        ta.join();
        ok_a = a.ok();
        ok_b2 = b2.ok();
        a_seen = a_steps;
    }
    std::filesystem::remove(file, ec);
    // Antes, B começava em 0 e A só andava depois de B refazer os 2000 passos (b_until_a ≈ 2010).
    check(ok_a && ok_b2 && a_seen >= 10 && b_until_a < 500, "ShmLinkWire: lado reaberto começa no relógio do outro (o outro não congela)");
}

} // namespace

void runLinkTests() {
    testShmLinkRestart();
    testLinkBoard();
    testLinkPacket();
    testLinkMessages();
    testLinkOpponentMarker();
    testLinkIntroAndCredits();
}
