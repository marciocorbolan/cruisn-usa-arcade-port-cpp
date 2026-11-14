/**
 * @file test_service.cpp
 * @brief Menu de serviço do operador e corrotina da CPU.
 */
#include "test_support.hpp"

#include <chrono>
#include <thread>

using namespace rea::core;
using namespace rea::test;

namespace {

/// Menu de serviço (0x05F6–0x154C) e corrotina da CPU
void testServiceMenu() {
    {
        using namespace service;
        MemoryBus sb;
        RecordingSubroutines rec;
        SubroutineDispatcher d(rec);
        // ENTER apertada nos quadros 3–5: 0x05F6 volta no quadro 3, 0x05FA no 6
        ServiceFrames sf(sb, [](uint32_t f) { return (f >= 3 && f <= 5) ? keys::kEnter : 0u; }, 50);
        ServiceMenu menu(sf);
        menu.registerIn(d);
        Tms32031State c;
        d.call(0x05F6, c, sb);
        const uint32_t down_at = sf.frames;
        d.call(0x05FA, c, sb);
        check(down_at == 3 && sf.frames == 6 && (c.r[0] & keys::kEnter) != 0,
              "0x05F6/0x05FA: esperam ENTER apertada e solta pelo nível de [C8C9]");

        Ctx ctx(c, sb, d, sf);
        sb.writeWord(0x20000, 0x00434241);  // "ABC"
        c.ar[2] = 0x20000;
        const uint64_t text_cycles = ctx.drawCycles(0xA7F4);
        c.ar[2] = 10; c.r[3] = 19; c.r[2] = 5; c.rc = 6;
        check(text_cycles == 3 * cycles::kTextChar && ctx.drawCycles(0xAC23) == 10 * cycles::kLinePixel &&
                  ctx.drawCycles(0xA7E5) == 20 * cycles::kRectPixel && ctx.drawCycles(0x99E9) == 0,
              "Ctx::drawCycles: texto por caractere, linha pelo maior lado, retângulo pela área");

        bool aborted = false;
        try {
            d.call(0x05F6, c, sb);  // ENTER nunca mais é apertada: o limite de quadros interrompe
        } catch (const Aborted&) {
            aborted = true;
        }
        check(aborted, "espera interrompida (spin = false) lança service::Aborted");

        bool jumped = false;
        try {
            d.call(0x4B28, c, sb);
        } catch (const NonLocalJump& j) {
            jumped = j.pc == 0x4B28;
        }
        check(jumped, "0x4B28 (saída do menu) é um desvio não local para a reentrada");
    }
    {
        using namespace service;
        // 0x0E7D: VOLUME + alterna a opção, VOLUME − não faz nada, ENTER confirma (carry = R5 == 0)
        auto choice = [](std::function<uint32_t(uint32_t)> k) {
            MemoryBus sb;
            RecordingSubroutines rec;
            SubroutineDispatcher d(rec);
            ServiceFrames sf(sb, std::move(k), 200);
            ServiceMenu menu(sf);
            menu.registerIn(d);
            Tms32031State c;
            c.r[5] = 1;
            d.call(0x0E7D, c, sb);
            int toggles = 0;
            for (const auto& e : rec.calls) toggles += e.addr == 0x91E0 && e.ar2 == 0x025B;
            return std::make_pair((c.st & Tms32031State::kStCarry) != 0, toggles);
        };
        const auto up = choice([](uint32_t f) { return f >= 5 && f <= 6 ? keys::kUp : f >= 20 ? keys::kEnter : 0u; });
        const auto dn = choice([](uint32_t f) { return f >= 5 && f <= 6 ? keys::kDown : f >= 20 ? keys::kEnter : 0u; });
        check(up.first && up.second == 1 && !dn.first && dn.second == 0,
              "0x0E7D: VOLUME + troca a opção (som 0x25B); VOLUME − é ignorado (fiel ao ROM)");
    }
    {
        using namespace service;
        // 0x46A1 com o DIP de movimento desligado (bit 0x40 de [0x992000] >> 16): 0x4961 pisca o aviso 3 vezes
        MemoryBus sb;
        RecordingSubroutines rec;
        SubroutineDispatcher d(rec);
        io::registerSerialPrinter(d);
        ServiceFrames sf(sb, [](uint32_t) { return 0u; }, 1000);
        ServiceMenu menu(sf);
        menu.registerIn(d);
        sb.setIoInput(0x992000, 0x00400000u);
        sb.writeWord(0xC7D0, 0x111);
        sb.writeWord(0xC7D1, 0x222);
        Tms32031State c;
        d.call(0x46A1, c, sb);
        std::vector<uint32_t> texts;
        int beeps = 0;
        for (const auto& e : rec.calls) {
            if (e.addr == 0xA7F4) texts.push_back(e.ar2);
            beeps += e.addr == 0x9211 && e.ar2 == 0x03D8;
        }
        check(texts == std::vector<uint32_t>({0x111, 0x222, 0x111, 0x222, 0x111, 0x222}) && beeps == 3 &&
                  sf.frames >= 85 && sf.frames <= 90,
              "0x46A1 sem o DIP: 'WARNING MOTION DIP SWITCH OFF' pisca 3 vezes (0x4991: R0·5000 voltas)");

        // 0x47E8: posições 26000 + 17000·R; Y e X guardam R0 (STI R0), peculiaridade do ROM
        RecordingSubroutines rec2;
        SubroutineDispatcher d2(rec2);
        menu.registerIn(d2);
        Tms32031State p;
        p.r[0] = 0x1234;
        p.r[2] = doubleToTms(1.0);
        p.r[3] = doubleToTms(-1.0);
        p.r[4] = doubleToTms(0.0);
        d2.call(0x47E8, p, sb);
        int sends = 0;
        for (const auto& e : rec2.calls) sends += e.addr == 0x48D2;
        check(sb.readWord(0xEBB4) == 43000 && sb.readWord(0xEBB6) == 0x1234 && sb.readWord(0xEBB5) == 0x1234 &&
                  sends == 3,
              "0x47E8: Z = 43000 enviado; Y/X enviados guardando R0 em [EBB6]/[EBB5]");
    }
    {
        using namespace service;
        // 0xAB79: registro de erros com 2 entradas; VIEW 1 (IN1 bit 0x10) sai
        MemoryBus sb;
        RecordingSubroutines rec;
        SubroutineDispatcher d(rec);
        cmos::registerRoutines(d);
        sb.writeWord(0x9936, 0x9C0000);
        Tms32031State c;
        c.r[2] = 2;
        c.ar[2] = 0xDE;
        d.call(0x99F4, c, sb);
        ServiceFrames sf(sb, [](uint32_t f) { return f >= 20 && f <= 30 ? 0x10u << 16 : 0u; }, 400);
        ServiceMenu menu(sf);
        menu.registerIn(d);
        sb.writeWord(0xB13F, 0x7777);
        d.call(0xAB79, c, sb);
        int logs = 0, hex = 0;
        for (const auto& e : rec.calls) {
            logs += e.addr == 0xA7F4 && e.ar2 == 0x7777;
            hex += e.addr == 0xA7AC;
        }
        check(logs == 2 && hex == 32 && sf.frames >= 20 && sf.frames < 400,
              "0xAB79: 2 entradas do registro de erros ('LOG n' + 16 valores hex); VIEW 1 sai");
    }
    {
        using namespace service;
        // 0x0CC0: cada tela espera ENTER e grava a posição filtrada dos controles nos itens 1–7
        auto calibrate = [](uint32_t dips, std::vector<uint32_t>& items, int& screens) {
            MemoryBus sb;
            RecordingSubroutines rec;
            SubroutineDispatcher d(rec);
            cmos::registerRoutines(d);
            sb.writeWord(0x9936, 0x9C0000);
            sb.writeWord(0xC8CC, dips);
            Tms32031State c;
            for (uint32_t n = 1; n <= 7; ++n) {  // valores anteriores: 0x55
                c.r[2] = 0x55;
                c.ar[2] = n;
                d.call(0x99F4, c, sb);
            }
            // ENTER nos quadros 10k+5..10k+6; o volante e os pedais mudam entre as telas
            ServiceFrames sf(sb, [&sb](uint32_t f) {
                sb.writeWord(0xC8C8, f < 10 ? 0x80 : f < 20 ? 0x10 : 0xF0);
                sb.writeWord(0xC93B, f < 30 ? 0x02 : 0xE0);
                sb.writeWord(0xC93C, f < 40 ? 0x03 : 0xD0);
                return f % 10 >= 5 && f % 10 <= 6 ? keys::kEnter : 0u;
            }, 200);
            ServiceMenu menu(sf);
            menu.registerIn(d);
            d.call(0x0CC0, c, sb);
            items.clear();
            for (uint32_t n = 1; n <= 7; ++n) {
                c.ar[2] = n;
                d.call(0x99E9, c, sb);
                items.push_back(c.r[0]);
            }
            screens = 0;  // 0x8E79: uma da 0x0AE9 e uma entre cada tela
            for (const auto& e : rec.calls) screens += e.addr == 0x8E79;
        };
        std::vector<uint32_t> items;
        int clears = 0;
        calibrate(0, items, clears);
        check(items == std::vector<uint32_t>({0x02, 0xE0, 0x10, 0xF0, 0x80, 0x03, 0xD0}) && clears == 5,
              "0x0CC0: repouso (itens 1/6/5), volante à esquerda (3) e à direita (4), acelerador (2) e freio (7)");
        calibrate(0x20, items, clears);
        check(items[1] == 0xE0 && items[6] == 0x55 && clears == 4,
              "0x0CC0: com a DIP [C8CC] & 0x20 a tela do freio não aparece (item 7 mantido)");
    }
    {
        MemoryBus sb;
        sb.writeWord(0x995020, 0xC00);  // destrava a CMOS
        sb.writeWord(0x9C0004, 0x12000000);
        sb.writeWord(0x9C1FFF, 0xAB000000);
        const auto file = tempPath("cmos_test.nv");
        const bool saved = cmos::saveCmosFile(sb, file);
        MemoryBus other;
        const bool loaded = cmos::loadCmosFile(other, file);
        check(saved && loaded && std::filesystem::file_size(file) == 0x8000 && other.readWord(0x9C0004) == 0x12000000 &&
                  other.readWord(0x9C1FFF) == 0xAB000000 && !cmos::loadCmosFile(other, file.string() + ".x"),
              "save/cmos.nv: 0x2000 palavras em little-endian, gravadas e restauradas");
        std::filesystem::remove(file);
    }
    {
        std::vector<int> log;
        int step = 0;
        {
            CpuCoroutine co([&](CpuCoroutine& self) {
                for (;;) {
                    log.push_back(++step);
                    self.yield();
                }
            });
            co.resume();
            log.push_back(-1);
            co.resume();
            log.push_back(-2);
        }  // o destrutor encerra a corrotina (Stop) sem travar
        check(log == std::vector<int>({1, -1, 2, -2}), "CpuCoroutine: alternância estrita resume/yield e encerramento");
    }
    {
        // CPU que não devolve o controle (laço sem yield): resume() desiste no prazo.
        // A thread solta fica dormindo até o fim do processo: ela não pode mais tocar no objeto destruído.
        bool first = false, second = true, stalled = false;
        {
            CpuCoroutine co([](CpuCoroutine&) {
                for (;;) std::this_thread::sleep_for(std::chrono::seconds(1));
            });
            co.setStallLimit(std::chrono::milliseconds(50));
            first = co.resume();
            stalled = co.stalled();
            second = co.resume();
        }  // o destrutor solta a thread presa em vez de travar
        check(!first && !second && stalled, "CpuCoroutine: resume() desiste se a CPU não devolver o controle no prazo");
    }
}

} // namespace

void runServiceTests() {
    testServiceMenu();
}
