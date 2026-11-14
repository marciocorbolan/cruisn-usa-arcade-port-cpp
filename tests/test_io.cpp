/**
 * @file test_io.cpp
 * @brief Entradas e saídas do gabinete: textos e contadores de moedas, créditos, motor do volante, lâmpadas e UART do controlador de movimento.
 */
#include "test_support.hpp"

using namespace rea::core;
using namespace rea::test;

namespace {

/// Criação de textos (0x7660/0x7651), contadores de moedas (0x75F1), link ocioso (0x7F25)
void testTextsAndCoinCounters() {
    {
        MemoryBus xb;
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        scene::registerNodePools(d);
        video::registerTextOverlay(d);
        xb.writeWord(0x763E, 0xE919);
        xb.writeWord(0x763F, 0xE91A);
        xb.writeWord(0xE919, 0x30000);
        xb.writeWord(0x30000, 0x30010);
        xb.writeWord(0x30010, 0);
        xb.writeWord(0xE91B, 2);
        xb.writeWord(0x7644, 0x55);
        xb.writeWord(0x7645, 0x66);
        Tms32031State c;
        c.ar[2] = 0x40000; c.r[2] = doubleToTms(100.0); c.r[3] = doubleToTms(50.0); c.rc = 1;
        d.call(0x7651, c, xb);
        const uint32_t front = c.ar[1], shadow = c.ar[0];
        const bool created = front == 0x30000 && shadow == 0x30010 && xb.readWord(0xE91A) == shadow &&
                             xb.readWord(shadow) == front && xb.readWord(0xE91B) == 0 &&
                             xb.readWord(front + 1) == 0x40000 && xb.readWord(front + 8) == 0 &&
                             xb.readWord(shadow + 8) == 0x800 && tmsToDouble(xb.readWord(shadow + 3)) == 102.0 &&
                             tmsToDouble(xb.readWord(shadow + 4)) == 52.0 && xb.readWord(front + 7) == 1 &&
                             xb.readWord(front + 9) == 0x11 && xb.readWord(front + 0xB) == 0x55 &&
                             xb.readWord(front + 0xA) == 0x66 && c.ar[2] == xb.readWord(0xC6CC);
        check(created, "0x7651: texto (AR1) e sombra (AR0: 0x800, +2, +2) com o estilo 0x76F0");
        bool styled = false;
        for (const auto& e : pend.calls) styled |= e.addr == 0x9ECD;
        check(styled, "0x76F0: paleta pela 0x9ECD");

        cmos::registerRoutines(d);
        xb.writeWord(0xE575, 7); xb.writeWord(0xE576, 1); xb.writeWord(0xE573, 1); xb.writeWord(0xE574, 0);
        xb.writeWord(0xE572, 0xF0F);
        c.st = 0;
        d.call(0x75F1, c, xb);
        const bool pulse = xb.readWord(0xE576) == 0 && xb.readWord(0xE573) == 0 && xb.readWord(0xE574) == 0 &&
                           xb.readWord(0xE572) == 0x10F && (c.st & Tms32031State::kStGie) != 0;
        for (int i = 0; i < 8; ++i) d.call(0x75F1, c, xb);
        check(pulse && xb.readWord(0xE576) == 1 && xb.readWord(0xE572) == 0x00F,
              "0x75F1: a cada 8 quadros, pulso 0x100 por moeda pendente e depois desliga");

        xb.writeWord(0xE591, 0); xb.writeWord(0xC8CC, 0);
        xb.writeWord(0x7F17, 0x50000);
        xb.writeWord(0x50000, 0x04000000u);
        c.st = Tms32031State::kStGie; c.ie = 0xF; c.if_ = 4;
        link::linkIdle(c, xb);
        check(xb.readWord(0x50000) == 0xC4000000u && c.ie == 0xB && c.if_ == 0 && !(c.st & Tms32031State::kStGie),
              "0x7F25: pulso 0xC800|b → 0xC000|b na porta; INT2 e GIE desligados");
    }
}

/// Textos de créditos: 0x77E7 (concatenação) e 0x745E (jogo grátis)
void testCreditTexts() {
    {
        MemoryBus cb;
        auto put = [&](uint32_t a, const char* t) {
            for (uint32_t w = 0;; ++w) {
                uint32_t v = 0;
                bool end = false;
                for (uint32_t k = 0; k < 4; ++k) {
                    const char ch = end ? 0 : t[w * 4 + k];
                    if (ch == 0) end = true;
                    v |= static_cast<uint32_t>(static_cast<uint8_t>(ch)) << (8 * k);
                }
                cb.writeWord(a + w, v);
                if (end) break;
            }
        };
        auto get = [&](uint32_t a) {
            std::string out;
            for (uint32_t w = 0; w < 16; ++w)
                for (uint32_t k = 0; k < 4; ++k) {
                    const char ch = static_cast<char>((cb.readWord(a + w) >> (8 * k)) & 0xFF);
                    if (ch == 0) return out;
                    out += ch;
                }
            return out;
        };
        put(0x100, "12");
        put(0x200, " CREDITS");
        Tms32031State c;
        c.ar[0] = 0x100; c.ar[1] = 0x200;
        game::appendString(c, cb);
        put(0x300, "ABCD");
        cb.writeWord(0x301, 0);
        c.ar[0] = 0x300; c.ar[1] = 0x200;
        game::appendString(c, cb);
        check(get(0x100) == "12 CREDITS" && get(0x300) == "ABCD CREDITS" && c.ar[0] == 0x300,
              "0x77E7: concatena textos de 4 caracteres por palavra");

        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        game::registerCreditsDisplay(d);
        d.add(0x99E9, [](Tms32031State& t, MemoryBus&, ISubroutines&) { t.r[0] = t.ar[2] == 0x19 ? 1u : 0u; });
        d.add(0x7651, [](Tms32031State& t, MemoryBus&, ISubroutines&) { t.ar[0] = 0x500; t.ar[1] = 0x510; });
        cb.writeWord(0x745A, 0x777);
        cb.writeWord(0xE56C, 0);
        cb.writeWord(0xC92F, 1);
        cb.writeWord(0x992000, 0x00400000u);  // sem o aviso "MOTION OFF"
        d.call(0x745E, c, cb);
        check(cb.readWord(0xC8CD) == 1 && c.ar[2] == 0x777 && cb.readWord(0x508) == 0x100 &&
              tmsToDouble(c.r[3]) == 360.0 && cb.readWord(0xE56C) == 0xFFFFFFFFu,
              "0x745E: jogo grátis → \"PRESS START\" centralizado em y = 360, piscando");
    }
}

/// Saídas do gabinete: motor do volante (0xAFC2) e lâmpadas (0x4E4F)
void testCabinetOutputs() {
    {
        MemoryBus ob;
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        io::registerCabinetOutputs(d);
        d.add(0x99E9, [](Tms32031State& t, MemoryBus&, ISubroutines&) { t.r[0] = 0; });
        Tms32031State c;
        ob.writeWord(0xC8C5, 5);
        ob.writeWord(0xEA48, doubleToTms(128.0));
        ob.writeWord(0xC8C8, 125);
        ob.writeWord(0xC8EA, doubleToTms(0.0));
        ob.writeWord(0xC8EC, doubleToTms(0.0));
        d.call(0xAFC2, c, ob);
        // erro 3 · ganho 2 − 2,2 · 2,5 · (0,45 · clamp(125 − 0, ±22)): velocidade = atual − anterior (0AFE8)
        const double force = tmsToDouble(ob.readWord(0xEA49));
        check(force > -48.6 && force < -48.3 && c.r[0] == (static_cast<uint32_t>(-49) & 0xFFu) && tmsToDouble(ob.readWord(0xC8EB)) == 2.0 &&
                  tmsToDouble(ob.readWord(0xC8EA)) == 125.0,
              "0xAFC2: força = ganho·erro − amortecimento·velocidade filtrada (modo 5)");

        ob.writeWord(0xC8C5, 0);
        ob.writeWord(0xC8CD, 1);
        ob.writeWord(0x8099E2, 0);
        ob.writeWord(0xC943, 0);
        d.call(0x4E4F, c, ob);
        const bool lit = ob.readWord(0xC943) == 1 && ob.readWord(0x8099E2) == 1 && c.r[0] == (0xFF00u | 0xC1u);
        c.r[0] = 0;
        d.call(0x4E4F, c, ob);
        check(lit && c.r[0] == 1 && ob.readWord(0x8099E2) == 2, "0x4E4F: START aceso (com 0xC0); sem mudança não reescreve");

        ob.writeWord(0xC8C5, 2);
        ob.writeWord(0xC97D, 0);
        ob.writeWord(0xC97C, 2);
        ob.writeWord(0x4FD7, 0x4FD8);
        ob.writeWord(0x4FD8, 2);
        for (int i = 0; i < 60; ++i) d.call(0x4E4F, c, ob);
        check(ob.readWord(0xC97C) == 0 && ob.readWord(0xC97D) == 0 && c.r[0] == (0xFF00u | 0xC2u),
              "0x4E4F: atrato sem créditos troca o padrão a cada 60 quadros");
        ob.writeWord(0x4995, 0x10); ob.writeWord(0x991030, 0x10); ob.writeWord(0xEBB7, 0);
        ob.writeWord(0x992000, 0x00400000u);
        d.call(0x49B1, c, ob);
        const bool absent = ob.readWord(0xEBB7) == 0;
        ob.writeWord(0x992000, 0);
        d.call(0x49B1, c, ob);
        check(absent && ob.readWord(0xEBB7) == 1, "0x49B1: pedido do controlador de movimento só com a placa presente");
    }
}

/// Impressora serial: 0x4A41, 0x77D1, 0x48D2
void testMotionUart() {
    {
        MemoryBus mb;
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        io::registerSerialPrinter(d);
        Tms32031State c;
        auto hex = [&](uint32_t ch) {
            c.r[1] = ch;
            d.call(0x4A41, c, mb);
            return c.carry() ? static_cast<int>(c.r[1]) : -1;
        };
        check(hex('7') == 7 && hex('A') == 10 && hex('F') == 15 && hex('G') == -1 && hex('a') == -1 && hex('/') == -1,
              "0x4A41: dígito hexadecimal ASCII (só maiúsculas)");
        {
            // 0x4530: sem o DIP do modem (bit 0x40 de [0x992000] >> 16 ligado) só grava a CMOS e volta
            auto modem = [&](uint32_t dips) {
                mb.setIoInput(0x992000, dips);
                mb.writeWord(0xEBB7, 7);
                pend.calls.clear();
                d.call(0x4530, c, mb);
                std::vector<uint32_t> seq;
                for (const auto& e : pend.calls) seq.push_back(e.addr == 0x99F4 ? 0x99F40000u | e.ar2 : e.addr);
                return seq;
            };
            const auto off = modem(0xF9FEF9FEu);
            const auto on = modem(0xF9BEF9BEu);
            check(off == std::vector<uint32_t>{0x4C33, 0x99F40034u, 0x99F40080u} && on.size() == 4 && on[3] == 0x4539 &&
                      mb.readWord(0xEBB7) == 0 && c.dp == 0,
                  "0x4530: [EBB7] = 0, itens 0x34/0x80 da CMOS; só segue para o modem (0x4539) com o DIP 0x40 desligado");
        }
        // "HELLO" empacotado: 'H','E','L','L' | 'O',0
        mb.writeWord(0x3000, 0x4C4C4548); mb.writeWord(0x3001, 0x0000004F);
        c.ar[0] = 0x3000; c.ar[1] = 0x3100;
        d.call(0x77D1, c, mb);
        const bool copied = mb.readWord(0x3100) == 0x4C4C4548 && mb.readWord(0x3101) == 0x4F && c.ar[0] == 0x3000;
        c.ar[2] = 0x3000; c.r[5] = 0x55;
        d.call(0x48D2, c, mb);
        check(copied && mb.readWord(0x995000) == (0xFFFFFF00u | 'O') && c.ar[2] == 0x3001 && c.r[5] == 0x55,
              "0x77D1/0x48D2: copia o texto e envia caractere a caractere (0xFF00|c, 0xFB00|c, 0xFF00|c)");
    }
}

} // namespace

void runIoTests() {
    testTextsAndCoinCounters();
    testCreditTexts();
    testCabinetOutputs();
    testMotionUart();
}
