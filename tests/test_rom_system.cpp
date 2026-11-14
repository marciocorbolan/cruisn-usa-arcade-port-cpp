/**
 * @file test_rom_system.cpp
 * @brief Sistema com a ROM real: nós da pista, trigonometria, telas de boot, autoteste, LZW, RESET, CMOS, INT0, entradas e partida completa.
 */
#include "test_support.hpp"

#include <algorithm>
#include <cctype>

using namespace rea::core;
using namespace rea::test;

namespace {

/// Nós da pista 0 (0xAA30 + 0xAA66, trechos 1..0x57)
void romTrackNodes(const std::optional<std::filesystem::path>& dir) {
    {
        MemoryBus g;
        g.loadProgramRom(*dir, {"cusa-l41.u10", "cusa-l41.u11", "cusa-l41.u12", "cusa-l41.u13"});
        g.hardwareReset();
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        math::registerFloatMath(d);
        math::registerTrig(d);
        math::registerRoutines(d);
        track::registerTrackLoader(d);
        g.loadGraphicsRoms(*dir);  // [A103] = 0xCD3923: dados da pista nas EPROMs u14..
        // [0x5A]/[0x5D] (rascunho em 0x8098xx) e [C60B] (pool) vêm da própria ROM
        Tms32031State c;
        d.call(0xAA30, c, g);
        c.ar[0] = 1;
        c.ar[1] = 0x57;
        d.call(0xAA66, c, g);
        const uint32_t base = g.readWord(track::track_vars::kNodes);
        uint32_t n = 0, prevIdx = 0;
        bool sorted = true, sane = true;
        int64_t maxAbs = 0;
        for (; n < 20000 && g.readWord(base + n * 4 + 3) != 0xFFFFFFFFu; ++n) {
            const uint32_t idx = g.readWord(base + n * 4 + 3);
            sorted = sorted && idx >= prevIdx;
            prevIdx = idx;
            for (uint32_t k = 0; k < 3; ++k) {
                const auto v = static_cast<int32_t>(g.readWord(base + n * 4 + k));
                maxAbs = std::max<int64_t>(maxAbs, std::llabs(v));
                sane = sane && v > -0x4000000 && v < 0x4000000;
            }
        }
        std::printf("  maior |coordenada| = %lld\n", static_cast<long long>(maxAbs));
        std::printf("  pista 0: %u nós; 1º índice 0x%X, último 0x%X; nó 0 = (%d, %d, %d)\n", n,
                    g.readWord(base + 3), prevIdx, static_cast<int32_t>(g.readWord(base)),
                    static_cast<int32_t>(g.readWord(base + 1)), static_cast<int32_t>(g.readWord(base + 2)));
        check(n > 50 && n < 20000 && sorted && sane && (g.readWord(base + 3) >> 8) == 1 &&
                  (prevIdx >> 8) == 0x5A && g.readWord(0xE856) == 0x58,
              "0xAA66: nós da pista 0 ordenados por índice, trechos 1..0x5A, fim −1");
        bool pending = false;
        for (const auto& e : pend.calls) pending = true, std::printf("  pendente 0x%X\n", e.addr);
        check(!pending, "0xAA66: sem rotinas pendentes");
    }
}

/// Seno/cosseno pela tabela da ROM (0x92DF / 0x92E2 / 0x938A)
void romTrig(const std::optional<std::filesystem::path>& dir) {
    {
        MemoryBus g;
        g.loadProgramRom(*dir, {"cusa-l41.u10", "cusa-l41.u11", "cusa-l41.u12", "cusa-l41.u13"});
        g.hardwareReset();
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        math::registerTrig(d);
        Tms32031State c;
        const double pi = 3.14159265358979;
        auto fn = [&](uint32_t addr, double a) {
            c.r[2] = doubleToTms(a);
            d.call(addr, c, g);
            return tmsToDouble(c.r[0]);
        };
        bool ok = true;
        double worst = 0;
        for (int i = -40; i <= 40; ++i) {
            const double a = i * 0.37;
            worst = std::max(worst, std::fabs(fn(0x92E2, a) - std::sin(a)));
            worst = std::max(worst, std::fabs(fn(0x92DF, a) - std::cos(a)));
        }
        ok = worst < 0.004 && fn(0x92E2, pi / 2) == 1.0 && fn(0x92DF, 0.0) == 1.0 && fn(0x92E2, 0.0) == 0.0;
        check(ok && pend.calls.empty(), "0x92E2/0x92DF: seno/cosseno pela tabela de 1/4 de onda (erro < 0,004 rad de passo)");
        c.ar[2] = 0x20000;
        c.r[2] = doubleToTms(pi / 2);
        c.r[0] = 0x55;
        d.call(0x938A, c, g);
        check(tmsToDouble(g.readWord(0x20006)) == 1.0 && tmsToDouble(g.readWord(0x20002)) == -1.0 &&
                  tmsToDouble(g.readWord(0x20004)) == 1.0 && std::fabs(tmsToDouble(g.readWord(0x20000))) < 0.004 &&
                  tmsToDouble(g.readWord(0x20001)) == 0.0 && c.r[0] == 0x55,
              "0x938A: matriz de rotação em Y (cos, 0, −sen; 0, 1, 0; sen, 0, cos)");
        math::registerRoutines(d);
        double worstPoly = 0;
        for (int i = -60; i <= 60; ++i) {
            const double a = i * 0.29;
            worstPoly = std::max(worstPoly, std::fabs(fn(0xB01A, a) - std::sin(a)));
            worstPoly = std::max(worstPoly, std::fabs(fn(0xB042, a) - std::cos(a)));
        }
        check(worstPoly < 5e-6 && pend.calls.empty(), "0xB01A/0xB042: seno/cosseno polinomiais (Cody-Waite), erro < 5e-6 (float de 24 bits)");
    }
}

/// Tela de carga do RESET (0x4F32) com a fonte da ROM
void romBootScreen(const std::optional<std::filesystem::path>& dir) {
    {
        MemoryBus g;
        g.loadProgramRom(*dir, {"cusa-l41.u10", "cusa-l41.u11", "cusa-l41.u12", "cusa-l41.u13"});
        g.hardwareReset();
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        video::registerRoutines(d);
        std::vector<std::string> shown;
        boot::registerBootScreens(d, shown);
        Tms32031State c;
        g.setIoInput(0x992000, 0xF9FEF9FE);  // Master
        d.call(0x4F32, c, g);
        std::size_t lit = 0;
        for (uint32_t i = 0; i < vunit::kVideoPageWords; ++i) lit += g.videoWord(i) != 0;
        check(shown.size() == 10 && shown[8] == "BOOT_LINK_MASTER" && lit > 3000 && g.readWord(0x9E0001) == 0xFFFFFFFFu &&
                  (g.readWord(vunit::kPageControl) & 1u) == 0,
              "0x4F32: paleta mínima, 10 linhas da fonte da ROM na página 0 exibida (Master)");
        check(pend.calls.empty(), "0x4F32: nada pendente (a moldura 0xABFC já é desenhada)");
    }
}

/// Mapa de memória
void romMemoryMap(MemoryBus& bus) {
    check(bus.readWord(vunit::kRomBase + 0x7456) == bus.readRom(0x7456), "ROM visível em 0xC00000");
    bus.hardwareReset();
    check(bus.readWord(0x0063) == 0x809C00u, "hardwareReset copia ROM->RAM (SP inicial em RAM[0x63])");
    check(bus.readWord(0x9E1000) == 0 && (bus.writeWord(0x9C0000, 7), bus.readWord(0x9C0000) == 0),
          "CMOS protegida após reset");
}

/// Cena de recordes 3D (0x3889) com a ROM real
void romHiscoreScene(const std::optional<std::filesystem::path>& dir) {
    {
        MemoryBus g;
        g.loadProgramRom(*dir, {"cusa-l41.u10", "cusa-l41.u11", "cusa-l41.u12", "cusa-l41.u13"});
        g.loadGraphicsRoms(*dir);
        g.hardwareReset();
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        boot::registerRoutines(d);
        cmos::registerRoutines(d);
        proc::Scheduler sched;
        proc::registerRoutines(d, sched);
        math::registerRoutines(d);
        math::registerTrig(d);
        video::registerRoutines(d);
        scene::registerRoutines(d);
        scene::registerObjectQuery(d);
        uint64_t hangs = 0;
        scene::registerSceneObject(d, hangs);
        game::registerHiscoreScene(d);
        Tms32031State c;
        d.call(0x4E35, c, g);  // BSS
        d.call(0x6FEB, c, g);  // pool de objetos
        d.call(0xA8F5, c, g);  // processos
        c.ar[2] = 0x3889; c.r[2] = 0x560; c.ar[6] = 0;  // rota 0: GOLDEN GATE PARK
        d.call(0xA844, c, g);
        const uint32_t scenePcb = c.ar[0];
        sched.run(c, g, d);
        auto count = [&](uint32_t cell) {
            uint32_t n = 0;
            for (uint32_t p = g.readWord(cell); p != 0 && n < 0x500; p = g.readWord(p)) ++n;
            return n;
        };
        const uint32_t vis = count(0xC982), far = count(0xC984), used = 0x44C - g.readWord(0xC989);
        // Título: 14 letras (sem os 2 espaços) com tipo 0; o objeto tipo 9 foi retirado
        uint32_t titleLetters = 0, digits = 0;
        for (uint32_t cell : {0xC982u, 0xC984u})
            for (uint32_t p = g.readWord(cell); p != 0; p = g.readWord(p)) {
                const uint32_t ty = g.readWord(p + 0xF);
                if (ty == 0 && tmsToDouble(g.readWord(p + 3)) == -301.0) ++titleLetters;
                if (ty == 0x200) ++digits;
            }
        std::printf("       (cena 0x3889: %u objetos usados, %u visíveis, %u distantes, %u letras do título, %u dígitos, travas %llu, pendentes:",
                    used, vis, far, titleLetters, digits, static_cast<unsigned long long>(hangs));
        std::map<uint32_t, int> pc;
        for (const auto& e : pend.calls) ++pc[e.addr];
        for (auto& [a, n] : pc) std::printf(" 0x%04X(%d)", a, n);
        std::printf(")\n");
        // Sem os carregadores 0x9F2F/0x9DF6 a tabela de texturas [0xEA4C] fica vazia: cada
        // objeto com modelo 0x2000 cairia na trava 0x701F (contada em `hangs`). Os objetos
        // dos tipos de [B16C] saem das listas (0x38F3), por isso vis + far < usados.
        check(titleLetters == 14 && digits == 80 && vis + far < used && g.readWord(scenePcb + proc::pcb::kPc) == 0x38B0,
              "0x3889: título 'GOLDEN GATE PARK' em 14 letras 3D, 10 tempos de 8 dígitos; retoma em 0x38B0");
        int frames = 1;
        while (g.readWord(scenePcb + proc::pcb::kPc) == 0x38B0 && frames < 200) { sched.run(c, g, d); ++frames; }
        check(g.readWord(scenePcb + proc::pcb::kPc) == 0x38B7 && frames > 10,
              "0x38B0: as linhas deslizam até z = 999 e a cena passa à espera de 60 quadros");
    }
}

/// Autoteste de energização: 0x62D5 / 0x6381
void romPowerOnTest(const std::optional<std::filesystem::path>& dir) {
    {
        MemoryBus g;
        g.loadProgramRom(*dir, {"cusa-l41.u10", "cusa-l41.u11", "cusa-l41.u12", "cusa-l41.u13"});
        g.loadGraphicsRoms(*dir);
        g.hardwareReset();
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        boot::registerRoutines(d);
        cmos::registerRoutines(d);
        video::registerRoutines(d);
        Tms32031State c;
        auto ramTest = [&](uint32_t start, uint32_t words, uint32_t mask) {
            c.ar[2] = start; c.r[2] = words; c.r[3] = mask; c.rc = 1; c.rs = 0x470; c.bk = 1;
            d.call(0x64FC, c, g);
            return c.r[0];
        };
        check(ramTest(0x400000, 16, 0xFFFFFFFFu) == 0 && g.readWord(0x400000) == 0xB4B4B4B6u,
              "0x64FE: x <- 2x | 2(b31^b30) a partir de [61C1] = 0x5A5A5A5A; RAM boa -> R0 = 0");
        check(ramTest(0xC00000, 16, 0xFFu) == 1, "0x64FE: ROM não guarda o padrão -> R0 = 1");
        c.ar[2] = 0xC00000; c.rc = 0x80000; c.r[2] = 0xFF; c.r[3] = 0;
        d.call(0x6552, c, g);
        check(c.r[0] == 0x444Fu && c.r[0] == g.readWord(0x656C) && c.rc == 0x80000 && c.ar[2] == 0xC00000,
              "0x6552: checksum de U10 (byte 0 do programa) = [656C]; preserva RC e AR2");
        c.ar[4] = 0x61D1;
        d.call(0x62F7, c, g);
        check(c.r[1] == 1 && c.r[2] == 0 && c.r[3] == 2, "0x62F7: rótulo \"U102\" -> 1, 0, 2 piscadas");

        pend.calls.clear();
        c = Tms32031State{};
        c.dp = 0;
        d.call(0x62D5, c, g);
        d.call(0x6381, c, g);
        bool results = true;
        for (uint32_t i = 0; i < 10; ++i) results &= g.readWord(0x809A7A + i) == 0;
        bool hang = false;
        for (const auto& e : pend.calls) hang |= e.addr == 0x631F;
        const uint32_t pal = g.readWord(0x64E);
        bool palette = true;
        for (uint32_t i = 0; i < pal; ++i) palette &= g.readWord(0x9E0000 + i) == g.readWord(0x64F + i);
        auto px = [&](uint32_t x, uint32_t y) { return g.readWord(0x900000 + y * 512 + x); };
        check(results && !hang, "0x62D5/0x6381: RAMs rápida, paleta e VRAM passam (resultados 0, sem trava)");
        check(palette, "0x065F: a paleta destruída pelo teste é restaurada da tabela [C7DF]");
        check(px(0xFC, 0xC5) == 4 && px(0xF2, 0x3E) == 4 && px(0x16A, 0xDE) == 4 && px(0x84, 0x7A) == 4,
              "0x6381: caixas verdes (cor 4) para U56, U26 (checksum), U69 (texturas) e U31 (CMOS)");
        check(px(0xFA, 0xC3) != 4 && px(0xFA, 0xC3) != 0, "0x63D2: moldura (0xABFC) em volta de cada caixa");
    }
}

/// Descompressor LZW com as EPROMs gráficas reais
void romLzwGraphics(const std::optional<std::filesystem::path>& dir) {
    {
        MemoryBus g;
        g.loadProgramRom(*dir, {"cusa-l41.u10", "cusa-l41.u11", "cusa-l41.u12", "cusa-l41.u13"});
        check(g.loadGraphicsRoms(*dir), "carrega as EPROMs gráficas cusa.u14-u29");
        g.hardwareReset();
        check(g.readWord(0xE80000) == 0 && g.readWord(0xC00000) == 0x4AD5u && g.readWord(0xD00000) != 0,
              "janela maindata: programa, bancos gráficos em 0xC80000-0xE7FFFF, depois 0");
        using namespace gfx::lzw_vars;
        // A RAM é cópia da ROM, e na BSS a ROM vale 0xFFFFFFFF; o jogo a zera em 0x4E35.
        g.writeWord(kActive, 0);
        g.writeWord(kBlocking, 0);
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        boot::registerRoutines(d);
        gfx::registerRoutines(d);
        Tms32031State c;

        // Modo cooperativo: 0xA2F9 só prepara; 0xA323 descomprime em fatias.
        c.ar[4] = 0xD00000;
        c.ar[5] = 0xA00000;
        d.call(0xA2F9, c, g);
        check(g.readWord(kActive) == 1 && g.textureByte(0) == 0, "[E5CB] = 0: 0xA2F9 só monta o contexto");
        int slices = 0;
        while (g.readWord(kActive) != 0 && slices < 1000) {
            d.call(0xA323, c, g);
            ++slices;
        }
        check(slices == 16, "218624 bytes; cede a vez no 1o 0x102 após >= 0x800 palavras: 16 chamadas de 0xA323");
        check(fnv1a(g, 0, 218624) == 0x70C229D6u, "fluxo 0xD00000 -> 0xA00000 igual à referência (FNV-1a)");

        // Modo bloqueante via 0xA3C2 com o descritor do RESET ([B092] = 0xB08C).
        pend.calls.clear();
        g.writeWord(kBlocking, 1);
        g.writeWord(0xE5E8, 0x110);
        g.writeWord(0xE5E9, 1);
        c.ar[2] = 0xB08C;
        d.call(0xA3C2, c, g);
        const bool anim = g.readWord(0xE5E8) != 0x110; // 0xA3A5 avança a barra por [E5E9]
        check(c.ar[2] == 0xB08E && g.readWord(kActive) == 0 && anim,
              "0xA3C2 bloqueante: lê {origem, destino}, termina e chama a animação 0xA3A5");
        {
            // 0xA3A5: quica em 300 (x = 0x12C → passo -1) e em 240.
            Tms32031State a{};
            g.writeWord(0xE5E8, 0x12B);
            g.writeWord(0xE5E9, 1);
            d.call(0xA3A5, a, g);
            const bool top = g.readWord(0xE5E8) == 0x12C && g.readWord(0xE5E9) == 0xFFFFFFFFu;
            g.writeWord(0xE5E8, 0xF1);
            d.call(0xA3A5, a, g);
            check(top && g.readWord(0xE5E8) == 0xF0 && g.readWord(0xE5E9) == 1,
                  "0xA3A5: barra de carregamento quica entre x = 240 e x = 300");
        }
        check(fnv1a(g, (0xBEFA00 - 0xA00000) * 2, 124160) == 0xF9CC2C23u,
              "fluxo 0xC15000 -> 0xBEFA00 igual à referência (FNV-1a)");

        // Ocupado: o pedido vira um processo (0xA844) com entrada [B12A] = 0xA3D7.
        g.writeWord(kActive, 1);
        pend.calls.clear();
        c.ar[2] = 0xB08C;
        c.r[2] = 0x55;
        d.call(0xA3C2, c, g);
        check(pend.calls.size() == 1 && pend.calls[0].addr == 0xA844 && pend.calls[0].ar2 == 0xA3D7 &&
                  c.r[2] == 0x55,
              "0xA3C2 com descompressão ativa: CALL $A844 (processo 0xA3D7), R2 preservado");
        g.writeWord(kActive, 0);
    }
}

/// Tabela de timing de vídeo -> taxa de quadros
void romVideoTiming(MemoryBus& bus) {
    const uint32_t table = bus.readRom(0x4ECB) - vunit::kRomBase;
    check(bus.readRom(table + 6) == vunit::kHTotal && bus.readRom(table + 11) == vunit::kVTotal,
          "htotal/vtotal da ROM = 666/432");
    check((bus.readRom(table + 0) & 0x1FFu) == vunit::kVblankScanline, "INT0 na scanline 399");
    check(std::fabs(vunit::kRefreshHz - 57.928) < 0.001, "taxa = 57,928 Hz");
}

/// RESET traduzido
void romReset(MemoryBus& bus) {
    Tms32031State cpu;
    boot::RecordingSubroutines subs;
    const uint32_t next = boot::ResetRoutine(bus, subs).run(cpu);
    check(cpu.sp == 0x809C00u, "RESET: SP = 0x809C00");
    check(cpu.ie == (Tms32031State::kIeInt0 | Tms32031State::kIeInt3), "RESET: IE = INT0|INT3");
    check((cpu.st & Tms32031State::kStGie) != 0, "RESET: interrupções ligadas (GIE)");
    check(bus.readWord(0x80A000 - 1) == 0 && bus.readWord(0x809800) == 0, "RESET: RAM interna zerada");
    check(bus.readWord(0x400000 + 0x63) == 0x809C00u, "RESET: RAM -> RAM rápida");
    check(bus.readWord(0xC8C5) == 1 && bus.readWord(0xC8C7) == 0xFF, "RESET: variáveis C8C5/C8C7");
    check(bus.readWord(0xC8CE) == 0x5A5A5A5Au, "RESET: marcador 0x5A5A5A5A");
    bool floats_zero = true;
    for (uint32_t a = 0xC947; a < 0xC947 + 48; ++a) floats_zero &= bus.readWord(a) == 0x80000000u;
    check(floats_zero, "RESET: 48 floats TMS zerados (0x80000000) em 0xC947");
    check(bus.readWord(vunit::kTimer0Control) == 0x201 && bus.readWord(vunit::kTimer0Period) == 0xFFFFFFFFu,
          "RESET: timer 0 programado");
    check(next == boot::ResetRoutine::kExitCmosCheckFailed, "RESET: sem CMOS válida segue para 0x5006");

    // Ordem dos CALLs comparada com o disassembly do unidasm (se gerado).
    std::ifstream asm_file(std::string(REA_SOURCE_DIR) + "/assets/rom_extracted/program.asm");
    if (asm_file) {
        std::vector<uint32_t> expected;
        std::string line;
        while (std::getline(asm_file, line)) {
            // Só linhas que começam com o endereço (5 dígitos hex); antes, uma linha em branco ou de cabeçalho
            // fazia o std::stoul lançar exceção e derrubar a suíte inteira.
            if (line.size() < 5 || !std::all_of(line.begin(), line.begin() + 5, [](unsigned char ch) { return std::isxdigit(ch); }))
                continue;
            const uint32_t pc = static_cast<uint32_t>(std::stoul(line.substr(0, 5), nullptr, 16));
            if (pc < 0x4AD5 || pc > 0x4BAE) continue;
            const auto at = line.find("CALL  $");
            if (at == std::string::npos) continue;
            const auto target = static_cast<uint32_t>(std::stoul(line.substr(at + 7), nullptr, 16));
            if (target != 0x4C33) expected.push_back(target); // 0x4C33 = "liga interrupções"
        }
        // Caminho completo: simula CMOS válida (ajuste 0xFA devolve 0x28, sem carry),
        // para que o último CALL ($995C, após o BNE) também seja alcançado.
        struct ValidCmos : boot::RecordingSubroutines {
            void call(uint32_t addr, Tms32031State& c, MemoryBus& b) override {
                RecordingSubroutines::call(addr, c, b);
                if (addr == 0x99E9 && c.ar[2] == 0xFA) c.r[0] = 0x28;
                if (addr == 0x995C) c.st &= ~Tms32031State::kStCarry;
            }
        } full;
        bus.hardwareReset();
        Tms32031State cpu2;
        const uint32_t next2 = boot::ResetRoutine(bus, full).run(cpu2);
        std::vector<uint32_t> got;
        for (const auto& c : full.calls) got.push_back(c.addr);
        check(got == expected, "RESET: ordem dos CALLs == disassembly (unidasm)");
        check(next2 == boot::ResetRoutine::kExitFallThrough, "RESET: CMOS válida segue para 0x4BAF");
    } else {
        std::printf("[SKIP] program.asm ausente (gere com unidasm)\n");
    }
}

/// CMOS (0x99E9/0x99F4/0x99DA): formato MSB-primeiro nos bits 24–31
void romCmosFormat(MemoryBus& bus) {
    {
        bus.hardwareReset();
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        cmos::registerRoutines(d);
        boot::registerRoutines(d);
        Tms32031State c;
        d.call(0x4E35, c, bus);  // zera a BSS como o RESET faz ([0xE572] viria com lixo da ROM)
        c.ar[2] = 0x10;
        c.r[2] = 0x12345678u;
        c.r[1] = 0xABCD;
        d.call(0x99F4, c, bus);                       // setItem(0x10, 0x12345678)
        const uint32_t item = 0x9C0000 + 0x10 * 4;
        check(c.ar[2] == item, "CMOS set: AR2 = endereço do item");
        check(c.r[1] == 0xABCD && c.r[2] == 0x12345678u, "CMOS set: R1/R2 preservados");
        check((bus.readWord(item) >> 24) == 0x12 && (bus.readWord(item + 1) >> 24) == 0x34 &&
              (bus.readWord(item + 2) >> 24) == 0x56 && (bus.readWord(item + 3) >> 24) == 0x78,
              "CMOS: bytes gravados MSB primeiro");
        bus.writeWord(item, 0);
        check((bus.readWord(item) >> 24) == 0x12, "CMOS travada após gravar");
        c.ar[2] = 0x10;
        d.call(0x99E9, c, bus);
        check(c.r[0] == 0x12345678u && c.ar[2] == item, "CMOS get: lê de volta 0x12345678");
        c.ar[2] = 0x10;
        d.call(0x99DA, c, bus);
        c.ar[2] = 0x10;
        d.call(0x99E9, c, bus);
        check(c.r[0] == 0x12345679u, "CMOS incrementa auditoria");
        check(pend.calls.empty(), "CMOS: nenhuma chamada pendente");
    }
}

/// Link / DIP switches (0x7F06, 0x7F18)
void romLinkDips(MemoryBus& bus) {
    {
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        boot::registerRoutines(d);
        Tms32031State c;
        bus.writeWord(0x8099DF, 0);
        bus.writeWord(boot::kDipSwitchesVar, 0x0001);  // Slave, link ligado
        d.call(0x7F18, c, bus);
        check((c.ie & Tms32031State::kIeInt2) != 0, "Slave com link habilita INT2");
        c = {};
        bus.writeWord(0x8099DF, 0);
        bus.writeWord(boot::kDipSwitchesVar, 0x0004);  // Master, link desligado
        d.call(0x7F18, c, bus);
        check((c.ie & Tms32031State::kIeInt2) == 0, "Master sem link não habilita INT2");
        d.call(0x7F06, c, bus);
        check(bus.readWord(0x997000) == 0xC0000000u && bus.readWord(0x997001) == 0x20000000u,
              "placa de link configurada como Master");
    }
}

/// RESET com as rotinas traduzidas executando
void romResetRunning(MemoryBus& bus) {
    {
        bus.hardwareReset();
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        boot::registerRoutines(d);
        cmos::registerRoutines(d);
        Tms32031State c;
        const uint32_t exit1 = boot::ResetRoutine(bus, d).run(c);
        check(bus.readWord(0x980020) == 0x58F && bus.readWord(0x980026) == 666 && bus.readWord(0x98002B) == 432,
              "RESET+4ECC: timing de vídeo programado");
        check(bus.readWord(0xC8C7) == 0xF7, "RESET+5029: watchdog alimentado (bit 3)");
        check(bus.readWord(vunit::kTimer0Control) == 0x2C1, "RESET+4EF9: timer 0 ligado (0x201|0xC0)");
        check(bus.readWord(0x1EFFE) == 0 && bus.readWord(0xC8D0) == 0, "RESET+4E35: BSS zerada");
        bool none_translated = true;
        for (const auto& e : pend.calls) none_translated &= !d.translated(e.addr);
        check(none_translated, "pendentes só contêm rotinas não traduzidas");
        check(exit1 == boot::ResetRoutine::kExitCmosCheckFailed, "CMOS vazia: item 0xFA != 0x28 -> 0x5006");

        // CMOS com o item 0xFA = 0x28 (como uma placa já configurada).
        bus.hardwareReset();
        Tms32031State w;
        w.ar[2] = 0xFA;
        w.r[2] = 0x28;
        d.call(0x99F4, w, bus);
        RecordingSubroutines pend2;
        SubroutineDispatcher d2(pend2);
        boot::registerRoutines(d2);
        cmos::registerRoutines(d2);
        Tms32031State c2;
        const uint32_t exit2 = boot::ResetRoutine(bus, d2).run(c2);
        bool hsPending = false;
        for (const auto& e : pend2.calls) hsPending = hsPending || e.addr == 0x9AA9 || e.addr == 0x9A99;
        Tms32031State h;
        h.r[6] = 0; h.r[7] = 0;
        d2.call(0x9AD2, h, bus);
        d2.call(0x9B06, h, bus);
        const bool table = h.r[0] == 0x1A78 && h.r[1] == 'T' && h.r[2] == 'V' && h.r[3] == 'G' && h.r[4] == 3;
        check(exit2 == boot::ResetRoutine::kExitFallThrough && !hsPending && table,
              "CMOS configurada: 0x995C valida os recordes (0x9AA9 → 0x9A99 recria: 1ª entrada TVG, carro 3) e segue");
        {
            // entrada válida é mantida; carro 11 (fora de 0..10) faz recriar as tabelas
            auto put = [&](uint32_t t, uint32_t e, uint32_t time, uint32_t car) {
                Tms32031State w2;
                w2.r[6] = t; w2.r[7] = e;
                d2.call(0x9AD2, w2, bus);
                w2.r[0] = time; w2.r[1] = 'A'; w2.r[2] = 'B'; w2.r[3] = ' '; w2.r[4] = car;
                d2.call(0x9AEB, w2, bus);
            };
            auto get = [&](uint32_t t, uint32_t e) {
                Tms32031State r;
                r.r[6] = t; r.r[7] = e;
                d2.call(0x9AD2, r, bus);
                d2.call(0x9B06, r, bus);
                return r;
            };
            Tms32031State v;
            put(3, 5, 0x100, 10);
            d2.call(0x9AA9, v, bus);
            const bool kept = get(3, 5).r[0] == 0x100 && get(3, 5).r[4] == 10;
            put(3, 5, 0x100, 11);
            d2.call(0x9AA9, v, bus);
            check(kept && get(3, 5).r[0] != 0x100 && get(3, 5).r[1] != 'A',
                  "0x9AA9: entrada válida mantida; carro fora de 0..10 recria as tabelas (0x9A99)");
        }
    }
}

/// INT0 / VBLANK
void romVblank(MemoryBus& bus) {
    {
        using namespace irq::vblank_vars;
        // Estado de partida: RESET completo com as rotinas traduzidas.
        bus.hardwareReset();
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        boot::registerRoutines(d);
        cmos::registerRoutines(d);
        Tms32031State c;
        boot::ResetRoutine(bus, d).run(c);
        irq::VblankHandler vb(bus, d);

        // Contexto do código interrompido deve voltar intacto.
        c.r[3] = 0x1234;
        c.ar[7] = 0x5678;
        c.dp = 0x42;
        bus.writeWord(kBackdropColor, 0x7C00);
        const Tms32031State before = c;
        pend.calls.clear();
        check(vb.run(c) == irq::VblankHandler::Result::Returned, "INT0: retorno normal");
        check(c.r == before.r && c.ar == before.ar && c.dp == 0x42 && c.sp == before.sp,
              "INT0: registradores restaurados");
        check(c.ie == (Tms32031State::kIeInt0 | Tms32031State::kIeInt1 | Tms32031State::kIeInt3),
              "INT0: IE = INT0|INT1|INT3 na saída");
        check((c.st & Tms32031State::kStGie) != 0 && (c.if_ & 1u) == 0, "INT0: RETI liga GIE, IF.INT0 limpo");
        check(bus.readWord(vunit::kPaletteBase) == 0x7C00, "INT0: [C938] -> paleta[0]");
        check(bus.readWord(kFrameCounter) == 1, "INT0: contador de quadros +1");
        std::vector<uint32_t> order;
        for (const auto& e : pend.calls) order.push_back(e.addr);
        const std::vector<uint32_t> expect = {0x9E97, 0x7F25, 0xAFC2, 0x92B3, 0x4CEF, 0x8EB7, 0x4E4F, 0x4FAE, 0x49B1};
        check(order == expect, "INT0: ordem das chamadas pendentes (0x75F1 já traduzida)");
        check(bus.readWord(0xC8C7) == 0xFF, "INT0: watchdog de hardware alimentado (bit 3 de volta a 1)");

        // Modo 1 (boot/atração): watchdog de software parado.
        check(bus.readWord(kGameMode) == 1 && bus.readWord(kSoftWatchdog) == 0, "INT0: modo 1 não conta watchdog");

        // Divisor de 57 quadros + auditorias da CMOS (modo 0: itens 0x76 e 0x77).
        bus.writeWord(kGameMode, 0);
        bus.writeWord(kSecondDivider, 0);
        auto audit = [&](uint32_t idx) {
            Tms32031State t;
            t.ar[2] = idx;
            d.call(0x99E9, t, bus);
            return t.r[0];
        };
        const uint32_t a76 = audit(0x76), a77 = audit(0x77);
        const double min0 = tmsToDouble(bus.readWord(kPlayMinutes));
        for (int i = 0; i < 57; ++i) {
            bus.writeWord(kSoftWatchdog, 0);  // papel do loop principal (0x4C02)
            vb.run(c);
        }
        check(bus.readWord(kSecondDivider) == 0 && audit(0x76) == a76 + 1 && audit(0x77) == a77 + 1,
              "INT0: 57 quadros = 1 s; auditorias 0x76/0x77 +1");
        check(std::fabs(tmsToDouble(bus.readWord(kPlayMinutes)) - min0 - 57.0 / 3420.0) < 1e-4,
              "INT0: minutos += 57/3420");

        // Watchdog de software: 300 quadros sem o loop principal -> RESET.
        bus.writeWord(kSoftWatchdog, 0);
        bool ok = true;
        for (uint32_t i = 0; i < irq::VblankHandler::kSoftWatchdogLimit; ++i)
            ok &= vb.run(c) == irq::VblankHandler::Result::Returned;
        check(ok && bus.readWord(kSoftWatchdog) == 300, "INT0: 300 quadros ainda toleráveis");
        pend.calls.clear();
        const uint32_t errs = audit(0xDE);
        check(vb.run(c) == irq::VblankHandler::Result::WatchdogReset, "INT0: quadro 301 dispara RESET");
        // 0xAB1A (traduzida): item 0xDE += 1 e a entrada nova começa com o código 0x800.
        Tms32031State e;
        e.ar[2] = errs * 0x40 + 0x4BF + 0x3F8 + bus.readWord(0x9936);
        d.call(0x99F9, e, bus);
        check(audit(0xDE) == errs + 1 && e.r[0] == 0x800,
              "INT0: registra o erro 0x800 na CMOS (0xAB1A) antes do RESET");

        // Evento pendente: alterna C942 e grava inteiro 0 no float C932 (= 1.0 TMS).
        bus.writeWord(kSoftWatchdog, 0);
        bus.writeWord(kEventFlag, 1);
        bus.writeWord(0xC941, 0);
        bus.writeWord(0xC942, 0);
        pend.calls.clear();
        vb.run(c);
        check(bus.readWord(kEventFlag) == 0 && bus.readWord(0xC942) == 1, "INT0: evento tratado, C942 alterna");
        check(pend.calls.size() > 3 && pend.calls[2].addr == 0x8E4E && pend.calls[3].addr == 0x8E6D,
              "INT0: evento chama 0x8E4E e 0x8E6D");
        check(tmsToDouble(bus.readWord(kFrameCountF)) == 2.0, "INT0: STI 0 em float = 1.0, +1.0 = 2.0 (fiel ao original)");
    }
}

/// Entradas: InputManager + 0x4CEF + INT3 (ADC0844)
void romInputs(const std::optional<std::filesystem::path>& dir) {
    {
        using namespace io;
        ArcadeInputState st;
        st.start = st.coin1 = true;
        st.gear = 2;
        check(InputManager::encodeIn0(st) == static_cast<uint16_t>(0xFFFF & ~0x0004 & ~0x0001 & ~0x1000),
              "IN0: start+ficha1+2a marcha, ativos em 0");
        st = {};
        st.gear = 0;
        st.radio = st.view3 = true;
        check(InputManager::encodeIn0(st) == 0xFFFF && InputManager::encodeIn1(st) == static_cast<uint16_t>(0xFFFF & ~0x2 & ~0x40),
              "IN1: rádio+vista3; neutro = nenhum bit de marcha");

        MemoryBus ibus;
        ibus.loadProgramRom(*dir, {"cusa-l41.u10", "cusa-l41.u11", "cusa-l41.u12", "cusa-l41.u13"});
        CabinetIo cab;
        ibus.attachDevice(&cab);
        ibus.hardwareReset();
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        boot::registerRoutines(d);
        cmos::registerRoutines(d);
        io::registerRoutines(d);
        Tms32031State c;
        boot::ResetRoutine(ibus, d).run(c);
        FakeInput fake;
        InputManager im(fake, cab);
        FrameRunner fr(ibus, d, cab);

        ArcadeInputState in;
        in.wheel = 0xFF;  // limitado a 0xF0 (faixa do potenciômetro)
        in.gas = 200;
        in.brake = 100;
        im.apply(in);
        fr.runFrame(c);
        check(fr.adcInterruptsLastFrame() == 3, "INT3: 3 conversões por quadro (volante, acelerador, freio)");
        check(ibus.readWord(irq::adc_vars::kWheel) == 21, "INT3 volante: Δ limitado a 65 -> floor(65*2703/8192) = 21");
        check(ibus.readWord(irq::adc_vars::kGas) == 65, "INT3 acelerador: floor(200*2703/8192) = 65");
        check(ibus.readWord(irq::adc_vars::kBrake) == 25, "INT3 freio: floor(100*0.25) = 25");
        check(ibus.readWord(input_vars::kDipSwitches) == kDefaultDsw, "0x4CEF: DIPs em [C8CC]");
        check(ibus.readWord(irq::adc_vars::kState) == 3, "INT3: estado final 3");
        for (int i = 0; i < 300; ++i) {
            ibus.writeWord(irq::vblank_vars::kSoftWatchdog, 0);
            fr.runFrame(c);
        }
        const uint32_t w = ibus.readWord(irq::adc_vars::kWheel), g = ibus.readWord(irq::adc_vars::kGas),
                       b = ibus.readWord(irq::adc_vars::kBrake);
        std::printf("       (regime: volante=%u/240, acelerador=%u/200, freio=%u/100)\n", w, g, b);
        check(w >= 230 && w < 240 && g >= 190 && g < 200 && b >= 95 && b < 100,
              "INT3: filtros convergem abaixo do valor real (piso + pesos < 1)");

        // Debounce + chave de teste (fora do modo 1).
        ibus.writeWord(input_vars::kGameMode, 0);
        auto frame = [&](const ArcadeInputState& s) {
            ibus.writeWord(irq::vblank_vars::kSoftWatchdog, 0);
            im.apply(s);
            fr.runFrame(c);
        };
        ArcadeInputState idle;
        ArcadeInputState test = idle;
        test.test = true;
        frame(idle);
        frame(idle);
        frame(test);
        check(ibus.readWord(input_vars::kTestRequest) == 1, "F2 (teste): borda alterna [C939] -> 1");
        frame(test);
        frame(test);
        check(ibus.readWord(input_vars::kTestRequest) == 1, "segurar F2 não repete a borda");
        frame(idle);
        frame(test);
        check(ibus.readWord(input_vars::kTestRequest) == 1, "debounce: precisa de 2 amostras soltas antes");
        frame(idle);
        frame(idle);
        frame(test);
        check(ibus.readWord(input_vars::kTestRequest) == 0, "nova borda válida alterna de volta -> 0");
        check((ibus.readWord(input_vars::kPressed) & 0x10u) != 0, "[C937]: nível invertido (1 = apertado)");

        // Volume +/− com mínimo da CMOS (item 0x20) e checksum (item 0x35).
        auto setItem = [&](uint32_t idx, uint32_t v) { Tms32031State t; t.ar[2] = idx; t.r[2] = v; d.call(0x99F4, t, ibus); };
        auto getItem = [&](uint32_t idx) { Tms32031State t; t.ar[2] = idx; d.call(0x99E9, t, ibus); return t.r[0]; };
        setItem(kCmosMinVolume, 10);
        setItem(kCmosVolume, 100);
        ArcadeInputState up = idle;
        up.volume_up = true;
        pend.calls.clear();
        for (int i = 0; i < 3; ++i) frame(up);
        check(getItem(kCmosVolume) == 103, "volume +: 3 quadros segurando = +3 (por nível)");
        Tms32031State t;
        d.call(0x99CA, t, ibus);
        check(getItem(cmos::kChecksumItem) == t.r[0], "checksum (item 0x35) = soma dos itens 0x00-0x2D");
        check(ibus.readWord(input_vars::kVolumeShown) == 0x3C, "volume: [E56D] = 60 quadros de exibição");
        bool sent = false;
        for (const auto& e : pend.calls) sent |= (e.addr == 0x910F && e.r1 == 103);
        check(sent, "volume enviado ao som: CALL 0x910F com R1 = 103");
        setItem(kCmosVolume, 11);
        ArcadeInputState down = idle;
        down.volume_down = true;
        for (int i = 0; i < 5; ++i) frame(down);
        check(getItem(kCmosVolume) == 10, "volume −: não passa do mínimo (item 0x20 = 10)");
    }
}

/// CMOS: validação de faixas (0x995C / 0x9981)
void romCmosValidation(MemoryBus& bus) {
    {
        bus.hardwareReset();
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        boot::registerRoutines(d);
        cmos::registerRoutines(d);
        Tms32031State c;
        d.call(0x4E35, c, bus);  // BSS zerada (máscara extra de proteção da CMOS)
        auto setI = [&](uint32_t idx, uint32_t v) { Tms32031State t; t.ar[2] = idx; t.r[2] = v; d.call(0x99F4, t, bus); };
        auto getI = [&](uint32_t idx) { Tms32031State t; t.ar[2] = idx; d.call(0x99E9, t, bus); return t.r[0]; };
        for (uint32_t i = 0; i < 0x2E; ++i) setI(i, 0);
        setI(0x08, 200);
        setI(0x2D, 99);
        setI(0x70, 50);
        setI(0x71, static_cast<uint32_t>(-5));
        c.st |= Tms32031State::kStCarry;
        c.r[3] = 0xABCD;
        d.call(0x995C, c, bus);
        check(getI(0x1D) == 20 && getI(0x27) == 5000 && getI(0x2D) == 30,
              "0x9981: fora da faixa -> padrão (1D: 0<10 -> 20; 27 -> 5000; 2D: 99>50 -> 30)");
        check(getI(0x08) == 200 && getI(0x00) == 0, "0x9981: valores dentro da faixa mantidos");
        Tms32031State t;
        d.call(0x99CA, t, bus);
        check(getI(cmos::kChecksumItem) == t.r[0], "0x9981: checksum (item 0x35) atualizado");
        check(getI(0x70) == 10 && getI(0x71) == 0, "0x995C: item 0x70 limitado a 10, 0x71 a 0");
        check(!c.carry(), "0x995C: carry sempre zerado (0x4FEF inalcançável)");
    }
}

/// Profiler e recíproco
void romProfiler(MemoryBus& bus) {
    {
        bus.hardwareReset();
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        boot::registerRoutines(d);
        prof::registerRoutines(d);
        Tms32031State c;
        c.r[0] = doubleToTms(4.0);
        d.call(0xA585, c, bus);
        check(tmsToDouble(c.r[0]) == 0.25, "0xA585: 1/4 = 0.25");
        c.r[0] = doubleToTms(-2.0);
        d.call(0xA585, c, bus);
        check(tmsToDouble(c.r[0]) == -0.5 && tmsToDouble(c.r[1]) == -0.5, "0xA585: 1/-2 = -0.5 (R1 = -|1/x|)");

        using namespace prof::prof_vars;
        bus.writeWord(kIterations, doubleToTms(0.0));
        bus.writeWord(kCursor, 0xC947);
        bus.writeWord(kResetRequest, 0);  // BSS: na RAM recém-copiada da ROM vale 0xFFFFFFFF
        for (uint32_t i = 0; i < 48; ++i) bus.writeWord(0xC947 + i, doubleToTms(0.0));
        for (int it = 0; it < 2; ++it) {
            bus.writeWord(0x808024, 1000);
            c.dp = 0x12;
            d.call(0x4F27, c, bus);
            bus.writeWord(0x808024, 3000);
            d.call(0x4F27, c, bus);
            d.call(0x4F08, c, bus);
        }
        check(c.dp == 0x12 && tmsToDouble(bus.readWord(kIterations)) == 2.0, "0x4F08: [C944] conta iterações (2.0); DP preservado");
        check(tmsToDouble(bus.readWord(0xC957)) == 2000.0 && tmsToDouble(bus.readWord(0xC958)) == 6000.0,
              "0x4F08: somas acumuladas em C957+");
        check(tmsToDouble(bus.readWord(0xC967)) == 1000.0 && tmsToDouble(bus.readWord(0xC968)) == 3000.0,
              "0x4F08: médias em C967+ = soma / iterações");
        check(bus.readWord(kCursor) == 0xC947, "0x4F08: cursor [C945] volta a 0xC947");
        bus.writeWord(kResetRequest, 1);
        d.call(0x4F08, c, bus);
        check(bus.readWord(kResetRequest) == 0 && bus.readWord(0xC967) == 0x80000000u &&
                  bus.readWord(kIterations) == 0x80000000u,
              "0x4F08: [C946] != 0 zera o bloco (0.0 = 0x80000000)");
    }
}

/// Partida completa: RESET -> 0x5006 -> reentrada 0x4B28 -> 0x4BAF
void romFullBoot(const std::optional<std::filesystem::path>& dir) {
    {
        MemoryBus bbus;
        bbus.loadProgramRom(*dir, {"cusa-l41.u10", "cusa-l41.u11", "cusa-l41.u12", "cusa-l41.u13"});
        bbus.loadGraphicsRoms(*dir);
        io::CabinetIo cab;
        bbus.attachDevice(&cab);
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        boot::registerRoutines(d);
        cmos::registerRoutines(d);
        io::registerRoutines(d);
        prof::registerRoutines(d);
        gfx::registerRoutines(d);
        scene::registerRoutines(d);
        Tms32031State c;
        boot::BootSequence boot(bbus, d);
        const uint32_t pc = boot.run(c);
        check(pc == 0x4BAF && boot.exits().size() == 2 && boot.exits()[0] == 0x5006,
              "CMOS vazia: RESET -> 0x5006 -> reentrada em 0x4B28 -> 0x4BAF");
        check(boot.messagesShown().size() == 2 && boot.messagesShown()[0] == "BOOT_VERSION_UPDATED",
              "tela 'VERSION UPDATED / FACTORY SETTINGS RESTORED'");
        int texts = 0, dce = 0;
        for (const auto& e : pend.calls) {
            texts += (e.addr == 0xA7F4);
            dce += (e.addr == 0x0E62) + (e.addr == 0x0CC0);
        }
        Tms32031State q;
        q.ar[2] = 0x70;
        d.call(0x99E9, q, bbus);
        // 87 textos na tela "CPU BOARD TEST" (0x6381): 38 rótulos de moldura + CMOS, 3 títulos,
        // 10 + 4 resultados da 0x644D, CMOS, "CPU BOARD TEST", 28 resultados de checksum/texturas e CMOS;
        // só no 1º RESET (a reentrada em 0x4B28 não passa pela 0x6381).
        check(texts == 9 * 101 * 2 + 87 && dce == 2 && q.r[0] == 0,
              "recuperação: 9 x 101 x 2 textos (0xA7F4) + 87 do autoteste; 0x0DCE restaura a CMOS e chama 0x0E62/0x0CC0");
        check(fnv1a(bbus, 0, 218624) == 0x70C229D6u && bbus.readWord(gfx::lzw_vars::kActive) == 0,
              "RESET 0x4B76: as 5 cargas bloqueantes (0xA3C2) preenchem a RAM de texturas");
        Tms32031State t;
        t.ar[2] = boot::kVersionItem;
        d.call(0x99E9, t, bbus);
        check(t.r[0] == 0x28, "assinatura de versão gravada (item 0xFA = 0x28)");
        check(bbus.readWord(0x8099E1) == 8, "reentrada 0x4B28: [0x8099E1] = 8 (auditoria 0x78 não conta de novo)");
        check(bbus.readWord(0xC8C5) == 1, "após a reentrada: modo 1 (o fundo 0x4210 de 0x5020 é apagado pela BSS)");
        const uint32_t pc2 = boot.run(c);
        check(pc2 == 0x4BAF && boot.exits().size() == 1 && boot.messagesShown().empty(),
              "segunda partida (CMOS válida): direto para 0x4BAF");

        // --- Loop principal ---------------------------------------------------------
        using namespace mainloop;
        using namespace mainloop::main_vars;
        FrameRunner fr(bbus, d, cab);
        MainLoop ml(bbus, d);
        ml.reset();
        ml.step(c);
        check(ml.iterations() == 0 && ml.pc() == Point::WaitFlip && bbus.readWord(kFlipRequest) == 1,
              "1a chamada: init (0x4BAF), 0x7187 pede a troca de página e o loop espera em 0x4BD4");
        Tms32031State t21;
        t21.ar[2] = kItem21;
        d.call(0x99E9, t21, bbus);
        check(static_cast<int32_t>(t21.r[0]) > 0, "0x4BAF: item 0x21 > 0 após a validação");

        bbus.writeWord(kGameMode, 0);  // fora do modo 1: a INT0 conta o watchdog
        bool reset = false;
        for (int i = 0; i < 400 && !reset; ++i) {
            reset = fr.runFrame(c) == FrameRunner::Result::WatchdogReset;
            ml.step(c);
        }
        check(!reset && ml.iterations() == 400, "400 quadros fora do modo 1: o loop zera o watchdog (sem RESET)");
        check(bbus.readWord(kFrameDelta) == 1, "[C931] = 0: troca de página a cada VBLANK, [C92F] = 1 quadro por iteração");

        bbus.writeWord(kFrameLimit, 2);
        const uint64_t before = ml.iterations();
        for (int i = 0; i < 100; ++i) {
            fr.runFrame(c);
            ml.step(c);
        }
        check(ml.iterations() - before == 34 && bbus.readWord(kFrameDelta) == 3,
              "limitador [C931] = 2: [C931] + 1 = 3 quadros por iteração (34 em 100 quadros)");
        bbus.writeWord(kFrameLimit, 0);

        bbus.writeWord(kFlipRequest, 1);
        const uint64_t it0 = ml.iterations();
        ml.step(c);
        check(ml.pc() == Point::WaitFlip && ml.iterations() == it0, "[C93F] != 0: espera a troca de página (0x4BD4)");
        fr.runFrame(c);
        ml.step(c);
        check(ml.iterations() == it0 + 1 && ml.pc() == Point::WaitFlip && bbus.readWord(kFlipRequest) == 1,
              "INT0 faz a troca, o loop completa a iteração e a 0x7187 pede a próxima");

        auto calls4834 = [&](uint32_t mode) {
            bbus.writeWord(kGameMode, mode);
            pend.calls.clear();
            fr.runFrame(c);
            ml.step(c);
            for (const auto& e : pend.calls) if (e.addr == 0x4834) return true;
            return false;
        };
        check(!calls4834(4) && calls4834(3), "0x4BF8: CALL $4834 exceto no modo 4");
        bbus.writeWord(0x980082, 1);
        fr.runFrame(c);
        ml.step(c);
        check(ml.pc() == Point::WaitDma0, "[0x980082] bit 0: espera o DMA (0x4BC1)");
        bbus.writeWord(0x980082, 0);
    }
}

} // namespace

void runRomSystemTests(const std::optional<std::filesystem::path>& dir, MemoryBus& bus) {
    romTrackNodes(dir);
    romTrig(dir);
    romBootScreen(dir);
    romMemoryMap(bus);
    romHiscoreScene(dir);
    romPowerOnTest(dir);
    romLzwGraphics(dir);
    romVideoTiming(bus);
    romReset(bus);
    romCmosFormat(bus);
    romLinkDips(bus);
    romResetRunning(bus);
    romVblank(bus);
    romInputs(dir);
    romCmosValidation(bus);
    romProfiler(bus);
    romFullBoot(dir);
}
