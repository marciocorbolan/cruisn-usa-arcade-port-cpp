/**
 * @file test_rom_game.cpp
 * @brief Jogo com a ROM real: carros, colisões, efeitos, objetos, pistas especiais, partida, corrida, HUD e recordes.
 */
#include "test_support.hpp"

using namespace rea::core;
using namespace rea::test;

namespace {

/// Rodas e carroceria: 0x8F68 / 0x8FAE / 0x9481
void romCarWheels(const std::optional<std::filesystem::path>& dir) {
    {
        MemoryBus g;
        g.loadProgramRom(*dir, {"cusa-l41.u10", "cusa-l41.u11", "cusa-l41.u12", "cusa-l41.u13"});
        g.hardwareReset();
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        math::registerRoutines(d);
        math::registerFloatMath(d);
        math::registerTrig(d);
        math::registerMatrix(d);
        game::registerCarWheels(d);
        auto wf = [&](uint32_t a, double v) { g.writeWord(a, doubleToTms(v)); };
        auto rf = [&](uint32_t a) { return tmsToDouble(g.readWord(a)); };
        const uint32_t obj = 0x1F000, dd = 0x1F100, pcb = 0x1F200, p1 = 0x1F300, p2 = 0x1F340, p3 = 0x1F380;
        g.writeWord(0x5A, 0x1F400); g.writeWord(0x5B, 0x1F410); g.writeWord(0x5C, 0x1F420);
        g.writeWord(obj + 0x1B, dd); g.writeWord(obj + 0x1A, p1); g.writeWord(obj + 0x1C, 0); wf(obj + 0x15, 0.0);
        g.writeWord(p1, p2); g.writeWord(p1 + 0x12, 1);
        g.writeWord(p2, p3); g.writeWord(p2 + 0x12, 2);
        g.writeWord(p3, 0);  g.writeWord(p3 + 0x12, 0);
        wf(dd + 0x26, 10.0); wf(dd + 0x21, 0.2); wf(dd + 0x39, 0.0);
        g.writeWord(dd + 0x1F, 0); g.writeWord(dd + 0x20, 0);
        g.writeWord(0xE5EC, 0x5577); g.writeWord(0xC8C5, 0);
        Tms32031State c;
        c.ar[4] = obj; c.ar[7] = pcb;
        d.call(0x8F68, c, g);
        const double spin = rf(pcb + 0xE);
        bool slept = false;
        for (const auto& e : pend.calls) slept = slept || e.addr == 0xA874;
        check(std::fabs(spin - 0.2) < 1e-3 && std::fabs(rf(p1 + 7 + 4) - std::cos(spin)) < 1e-3 &&
                  rf(p1 + 7) == 1.0 && std::fabs(rf(p2 + 7) - std::cos(0.3)) < 1e-3 &&
                  std::fabs(rf(p3 + 7) - 1.0) < 1e-4 && rf(dd + 0x42) == 0.0 && rf(dd + 0x43) == 0.0 &&
                  slept && c.ar[2] == 1 && c.ret == 0x8F72,
              "0x8F68: giro/esterço das rodas (tipos 1 e 2) e carroceria parada; dorme 1");
        // freada: arfagem positiva limitada a 0,1 (nariz para baixo)
        wf(dd + 0x26, 0.0);
        pend.calls.clear();
        d.call(0x8F72, c, g);
        check(std::fabs(rf(dd + 0x42) - 0.1) < 1e-3, "0x8FAE: desaceleração inclina a carroceria, limitada a 0,1");
        check(game::addiFloatFields(doubleToTms(1.0), doubleToTms(0.0)) == doubleToTms(1.0) &&
                  game::addiFloatFields(0x01000000u, 0x00400000u) == 0x01400000u,
              "0x8FD4: ADDI soma só o campo sinal+mantissa e mantém o expoente");
    }
}

/// Colisão de caixas orientadas: 0x2863 / 0x28A8
void romBoxCollision(const std::optional<std::filesystem::path>& dir) {
    {
        MemoryBus g;
        g.loadProgramRom(*dir, {"cusa-l41.u10", "cusa-l41.u11", "cusa-l41.u12", "cusa-l41.u13"});
        g.hardwareReset();
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        track::registerGroundHeight(d);
        game::registerCarCollision(d);
        auto wf = [&](uint32_t a, double v) { g.writeWord(a, doubleToTms(v)); };
        const uint32_t A = 0x1F000, B = 0x1F100;
        for (uint32_t o : {A, B}) {
            const uint32_t dd = o + 0x80;
            g.writeWord(o + 0x1B, dd);
            wf(dd + 0x3B, -10.0); wf(dd + 0x3A, 10.0);
            wf(dd + 0x3D, 0.0);   wf(dd + 0x3C, 5.0);
            wf(dd + 0x3F, -20.0); wf(dd + 0x3E, 20.0);
            for (uint32_t i = 0; i < 9; ++i) wf(o + 4 + i, (i % 4) == 0 ? 1.0 : 0.0);
            wf(o + 1, 0.0); wf(o + 2, 0.0); wf(o + 3, 0.0);
        }
        Tms32031State c;
        auto collide = [&](double bx, double bz) {
            wf(B + 1, bx); wf(B + 3, bz);
            c.ar[0] = A; c.ar[1] = B; c.r[5] = 0x55;
            d.call(0x2863, c, g);
            return c.carry();
        };
        const bool near = collide(5.0, 5.0), far = collide(100.0, 0.0), front = collide(0.0, 35.0),
                   apart = collide(0.0, 45.0);
        check(near && !far && front && !apart && c.ar[0] == A && c.ar[1] == B && c.r[5] == 0x55 &&
                  tmsToDouble(g.readWord(0x809D00 + 24 + 21)) == 10.0,
              "0x2863: caixas sobrepostas → carry = 1; separadas → 0; cantos em mundo em [0x4F]+24");
    }
}

/// Resposta à colisão: 0x2645 (choque elástico, massas iguais)
void romCollisionResponse(const std::optional<std::filesystem::path>& dir) {
    {
        MemoryBus g;
        g.loadProgramRom(*dir, {"cusa-l41.u10", "cusa-l41.u11", "cusa-l41.u12", "cusa-l41.u13"});
        g.hardwareReset();
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        math::registerRoutines(d);
        math::registerFloatMath(d);
        math::registerTrig(d);
        prof::registerRoutines(d);
        game::registerCarDynamics(d);
        game::registerCarCollision(d);
        auto wf = [&](uint32_t a, double v) { g.writeWord(a, doubleToTms(v)); };
        auto rf = [&](uint32_t a) { return tmsToDouble(g.readWord(a)); };
        const uint32_t A = 0x1F000, B = 0x1F100, dA = 0x1F080, dB = 0x1F180, hit = 0x1F300;
        for (uint32_t o : {A, B}) {
            const uint32_t dd = o + 0x80;
            g.writeWord(o + 0x1B, dd); g.writeWord(o + 0xF, 0x216);
            for (uint32_t i = 1; i <= 0x13; ++i) wf(o + i, 0.0);
            wf(o + 8, 1.0);
            wf(dd + 0x2D, 0.0); wf(dd + 0x30, 1.0); g.writeWord(dd + 0x31, 0); wf(dd + 0x2E, 0.0);
        }
        wf(B + 3, 30.0);
        wf(A + 0x13, 50.0); wf(dA + 0x26, 50.0); wf(dB + 0x26, 0.0);
        wf(hit, 0.0); wf(hit + 1, 0.0); wf(hit + 2, 15.0);
        g.writeWord(0xE876, 0); g.writeWord(0xC92F, 1);
        Tms32031State c;
        c.ar[0] = A; c.ar[1] = B; c.ar[3] = hit + 1;
        d.call(0x2645, c, g);
        const double hB = rf(dB + 0x2D);
        check(std::fabs(rf(A + 3) + 25.0) < 0.01 && std::fabs(rf(B + 3) - 55.0) < 0.01 &&
                  std::fabs(rf(dA + 0x26) - 8.75) < 0.1 && std::fabs(rf(dB + 0x26) - 41.25) < 0.1 &&
                  std::fabs(hB) <= 0.34 && c.ar[0] == A && c.ar[1] == B,
              "0x2645: separa ±½·Δv, choque ¼ elástico + ¾ centro de massa ± 10·dir; rumo de B ± 0,333");
    }
}

/// Carro × objeto do cenário: 0x221F, 0x23D1 e o balanço 0x23B0
void romSceneryCollision(const std::optional<std::filesystem::path>& dir) {
    {
        MemoryBus g;
        g.loadProgramRom(*dir, {"cusa-l41.u10", "cusa-l41.u11", "cusa-l41.u12", "cusa-l41.u13"});
        g.hardwareReset();
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        math::registerRoutines(d);
        math::registerFloatMath(d);
        math::registerTrig(d);
        math::registerMatrix(d);
        prof::registerRoutines(d);
        game::registerCarDynamics(d);
        game::registerCarCollision(d);
        game::registerSceneryCollision(d);
        auto wf = [&](uint32_t a, double v) { g.writeWord(a, doubleToTms(v)); };
        auto rf = [&](uint32_t a) { return tmsToDouble(g.readWord(a)); };
        const uint32_t car = 0x1F000, dc = 0x1F080, obj = 0x1F200, cell = 0x1F300;
        // carro: caixa x ∈ [−10, 10], z ∈ [−20, 20], na origem (mesma orientação dos carros do jogo e do teste
        // da 0x2863; antes a caixa espelhada, +0x3B = +10, escondia o sinal invertido do teste de arestas)
        g.writeWord(car + 0x1B, dc);
        for (uint32_t i = 1; i <= 0x13; ++i) wf(car + i, 0.0);
        for (uint32_t i = 0; i < 9; ++i) wf(car + 4 + i, (i % 4) == 0 ? 1.0 : 0.0);
        wf(dc + 0x3B, -10.0); wf(dc + 0x3A, 10.0);
        wf(dc + 0x3D, 0.0);  wf(dc + 0x3C, 5.0);
        wf(dc + 0x3F, -20.0); wf(dc + 0x3E, 20.0);
        wf(dc + 0x2C, 0.0); wf(dc + 0x2D, 0.0); wf(dc + 0x2E, 0.0);
        g.writeWord(0xE876, car); g.writeWord(0xC92F, 1);
        auto place = [&](uint32_t type, double z) {
            for (uint32_t i = 1; i <= 0x21; ++i) g.writeWord(obj + i, 0);
            for (uint32_t i = 1; i <= 0x13; ++i) wf(obj + i, 0.0);
            for (uint32_t i = 0; i < 9; ++i) wf(obj + 4 + i, (i % 4) == 0 ? 1.0 : 0.0);
            g.writeWord(obj + 0xF, type);
            g.writeWord(obj + 0xE, 0x80000008u);
            wf(obj + 3, z);
            wf(car + 3, 0.0); wf(car + 0x13, 3.0); wf(dc + 0x26, 100.0);
        };
        Tms32031State c;
        auto hit = [&] {
            pend.calls.clear();
            c.ar[0] = car; c.ar[1] = obj;
            d.call(0x221F, c, g);
        };
        place(0x40, 30.0);
        hit();
        const bool outside = rf(car + 3) == 0.0 && rf(dc + 0x26) == 100.0 && pend.calls.empty();
        place(0x40, 5.0);
        hit();
        const uint32_t spin = g.readWord(dc + 0x31);
        check(outside && std::fabs(rf(car + 3) + 3.0) < 0.01 && std::fabs(rf(dc + 0x26) - 60.0) < 0.01 &&
                  (spin == 1 || spin == 0x3C) && c.ar[0] == car && c.ar[1] == obj,
              "0x221F sólido (0x40): fora da caixa nada muda; dentro empurra (Δv · dir), velocidade · 0,6, gira");

        // tombado (0x20): sai da lista [0x58], perde 20 % da velocidade, cria o processo 0x239C
        place(0x20, 5.0);
        g.writeWord(0x58, cell + 0x20);
        g.writeWord(cell + 0x20, obj);
        g.writeWord(obj + 0x20, 0);
        hit();
        bool created = false, sfx = false;
        for (const auto& e : pend.calls) {
            created = created || e.addr == 0xA868;
            sfx = sfx || e.addr == 0x9188;
        }
        check(g.readWord(cell + 0x20) == 0 && g.readWord(obj + 0xE) == (1u << 27) &&
                  std::fabs(rf(dc + 0x26) - 80.0) < 0.01 &&
                  std::fabs(tmsToDouble(c.r[7]) - 0.7) < 0.01 && created && sfx,
              "0x221F tombado (0x20): 0x23D1 tira da lista, bit 27, v − 20 %, R7 = min(v·0,03, 0,7), 0xA868");

        // balanço 0x23B0: R7, R7·−0,4 por 4 quadros, depois ·−0,6
        c.ar[4] = obj; c.r[7] = doubleToTms(0.5);
        d.call(0x23B0, c, g);
        const bool s0 = c.ret == 0x23B9;
        d.call(0x23B9, c, g);
        const bool s1 = c.ret == 0x23C4 && c.ar[6] == 3 && std::fabs(tmsToDouble(c.r[7]) + 0.2) < 0.001;
        for (int i = 0; i < 3; ++i) d.call(0x23C4, c, g);
        const bool s2 = std::fabs(tmsToDouble(c.r[7]) + 0.2) < 0.001 && c.ar[6] == 0;
        d.call(0x23C4, c, g);
        const bool s3 = c.ar[6] == 3 && std::fabs(tmsToDouble(c.r[7]) - 0.12) < 0.001;
        c.r[7] = doubleToTms(0.01);
        c.ar[6] = 0;
        g.writeWord(obj + 0xE, 1u << 27);
        pend.calls.clear();
        d.call(0x23C4, c, g);
        bool died = false;
        for (const auto& e : pend.calls) died = died || e.addr == 0xA896;
        check(s0 && s1 && s2 && s3 && died && g.readWord(obj + 0xE) == 8,
              "0x23B0: balanço R7 → ·−0,4 (4 quadros) → ·−0,6 até |R7| ≤ 0,01; volta a colidir (+8)");
    }
}

/// Fumaça dos carros: 0x8928 (filtros) e o processo 0x8860
void romSmoke(const std::optional<std::filesystem::path>& dir) {
    {
        MemoryBus g;
        g.loadProgramRom(*dir, {"cusa-l41.u10", "cusa-l41.u11", "cusa-l41.u12", "cusa-l41.u13"});
        g.hardwareReset();
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        game::registerSmokeEffects(d);
        Tms32031State c;
        const uint32_t car = 0x1F000, dc = 0x1F080;
        g.writeWord(car + 0x1B, dc); g.writeWord(dc + 0x37, 0x1234); g.writeWord(0xE876, car);
        // 0x894D (pendente aqui) deixa o carry como está: carry = 1 simula ≥ 2 efeitos ativos
        auto crash = [&](uint32_t mode, uint32_t a0, bool busy) {
            g.writeWord(0xC8C5, mode);
            pend.calls.clear();
            c.st = busy ? Tms32031State::kStCarry : 0;
            c.ar[0] = a0; c.ar[1] = 0x1F100; c.ar[3] = 0x55; c.r[5] = 0x77; c.ar[7] = 0x99;
            d.call(0x8928, c, g);
            bool created = false;
            for (const auto& e : pend.calls) created = created || e.addr == 0xA868;
            return created;
        };
        const bool attract = crash(2, car, false), other = crash(4, 0x1F200, false), many = crash(4, car, true);
        const bool ok = crash(4, car, false);
        check(!attract && !other && !many && ok && c.ar[0] == car && c.ar[3] == 0x55 && c.r[5] == 0x77 &&
                  c.ar[7] == 0x99,
              "0x8928: fumaça só do focado, fora do atrato e sem 2 efeitos ativos (0x894D); registradores preservados");

        // processo: nuvem cuja animação acabou é descartada e o efeito morre
        const uint32_t pcb = 0x1F500, puff = 0x1F600, frames = 0x1F700;
        g.writeWord(pcb + 0x11, dc); g.writeWord(pcb + 0x12, 1); g.writeWord(pcb + 0x13, puff);
        g.writeWord(puff + 0xE, 0x1000); g.writeWord(puff + 0xF, 0x766); g.writeWord(puff + 0x13, frames);
        g.writeWord(frames + 1, 0xFFFFFFFFu);
        pend.calls.clear();
        c.ar[7] = pcb;
        d.call(0x8860, c, g);
        bool freed = false, died = false;
        for (const auto& e : pend.calls) {
            freed = freed || e.addr == 0x7100;
            died = died || e.addr == 0xA896;
        }
        check(freed && died && g.readWord(pcb + 0x13) == 0 && c.ar[5] == dc,
              "0x8860: fim da animação → nuvem liberada (0x7100), slot zerado; sem nuvens o processo morre");
    }
}

/// Grupo 0x260/0x261 (tipos 0x470–0x476) e objetos 0x498
void romRoamingObjects(const std::optional<std::filesystem::path>& dir) {
    {
        MemoryBus g;
        g.loadProgramRom(*dir, {"cusa-l41.u10", "cusa-l41.u11", "cusa-l41.u12", "cusa-l41.u13"});
        g.hardwareReset();
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        math::registerRoutines(d);
        math::registerTrig(d);
        math::registerMatrix(d);
        math::registerFloatMath(d);
        game::registerRoamingObjects(d);
        const uint32_t obj = 0x1F000, pcb = 0x1F800;
        auto wf = [&](uint32_t a, double v) { g.writeWord(a, doubleToTms(v)); };
        auto rf = [&](uint32_t a) { return tmsToDouble(g.readWord(a)); };
        auto spawned = [&]() {
            uint32_t pc = 0;
            for (const auto& e : pend.calls) if (e.addr == 0xA844 || e.addr == 0xA868) pc = e.ar2;
            return pc;
        };
        Tms32031State c;
        wf(obj + 1, 100.0); wf(obj + 2, 0.0); wf(obj + 3, 200.0); wf(obj + 0x15, 0.0);
        g.writeWord(obj + 0xE, 0); g.writeWord(obj + 0x17, 0);
        g.writeWord(0xC8E3, 1);
        c.ar[4] = obj; c.ar[0] = pcb;
        d.call(0x9B43, c, g);
        check(spawned() == 0 && g.readWord(obj + 0x17) == 0, "0x9B43: com link ([C8E3] != 0) o grupo não é criado");
        g.writeWord(0xC8E3, 0);
        g.writeWord(0xE8E6, 7);
        c.r[0] = 0x11; c.r[1] = 0x22; c.r[2] = 0x33; c.r[3] = 0x44; c.ar[2] = 0x55;
        d.call(0x9B43, c, g);
        check(spawned() == 0x9B4E && g.readWord(obj + 0x17) == pcb && g.readWord(pcb + 0xC) == 1 &&
                  (g.readWord(obj + 0xE) & (1u << 23)) && g.readWord(0xE8E6) == 0 && rf(0xE8EA) == 240.0 &&
                  rf(0xE8E7) == 100.0 && std::fabs(rf(0xE8E9) - (200.0 + rf(0x9B42))) < 1e-3 &&
                  c.r[0] == 0 && c.r[1] == 0x22 && c.r[2] == 0x33 && c.r[3] == 0x44 && c.ar[2] == 0x55,
              "0x9B43 -> 0xC101A4: líder 0x9B4E (+C = 1), alvo = posição + rotY·(0, 0, [9B42]), velocidade 240; R0 = [C8E3]");
        pend.calls.clear();
        d.call(0x9B46, c, g);
        check(spawned() == 0x9B49 && g.readWord(pcb + 0xC) == 0, "0x9B46 -> 0xC101DA: seguidor 0x9B49 (+C = 0)");

        // 0x7EE9: objeto 0x300 mais perto em (x, z) na lista [E403] (encadeada por +20)
        const uint32_t r1 = 0x1F200, r2 = 0x1F240, r3 = 0x1F280;
        g.writeWord(0xE403, r1);
        g.writeWord(r1 + 0x20, r2); g.writeWord(r2 + 0x20, r3); g.writeWord(r3 + 0x20, 0);
        g.writeWord(r1 + 0xF, 0x305); wf(r1 + 1, 500.0); wf(r1 + 3, 200.0);
        g.writeWord(r2 + 0xF, 0x210); wf(r2 + 1, 101.0); wf(r2 + 3, 200.0);
        g.writeWord(r3 + 0xF, 0x300); wf(r3 + 1, 110.0); wf(r3 + 3, 190.0);
        d.call(0x7EE9, c, g);
        check(c.ar[0] == r3 && c.ar[2] == 0, "0x7EE9: AR0 = o objeto de tipo 0x30x mais perto (ignora 0x210)");

        // 0x676B: tira AR4 da lista [[57]] (+20) e decrementa [E5EE]
        const uint32_t head = g.readWord(0x57);
        g.writeWord(head, r1); g.writeWord(r1 + 0x20, obj); g.writeWord(obj + 0x20, r3); g.writeWord(r3 + 0x20, 0);
        g.writeWord(0xE5EE, 3);
        c.ar[4] = obj;
        d.call(0x676B, c, g);
        check(g.readWord(r1 + 0x20) == r3 && g.readWord(0xE5EE) == 2, "0x676B: desliga o objeto da lista e [E5EE]−−");

        // 0x9032: bloco de carro volta à lista livre [E3F6]
        g.writeWord(0xE3F6, 0x1F300); g.writeWord(0xE3F7, 5);
        c.ar[2] = 0x1F340;
        d.call(0x9032, c, g);
        check(g.readWord(0xE3F6) == 0x1F340 && g.readWord(0x1F340) == 0x1F300 && g.readWord(0xE3F7) == 4,
              "0x9032: dados de carro devolvidos à lista livre");

        // 0x9BEF: batida lenta (< 150) só marca +33; o carro gira π e fica com 80 no mínimo
        const uint32_t a = 0x1F400, b = 0x1F500, da = 0x1F600, db = 0x1F700;
        g.writeWord(a + 0x1B, da); g.writeWord(b + 0x1B, db);
        wf(da + 0x26, 100.0); wf(da + 0x2D, 0.5);
        g.writeWord(db + 0x33, 0);
        pend.calls.clear();
        c.ar[0] = a; c.ar[1] = b; c.ar[3] = 3; c.ar[4] = 4; c.ar[5] = 5;
        d.call(0x9BEF, c, g);
        check(g.readWord(db + 0x33) == 1 && rf(da + 0x26) == 100.0 && std::fabs(rf(da + 0x2D) - 3.6416) < 1e-3 &&
                  g.readWord(da + 0x31) == 1 && rf(da + 0x32) == 12.0 && spawned() == 0 &&
                  c.ar[0] == a && c.ar[1] == b && c.ar[3] == 3 && c.ar[4] == 4 && c.ar[5] == 5,
              "0x9BEF: batida a 100 só marca +33; carro gira π, roda 12 vezes; registradores preservados");
        // rápida: o objeto e o seguinte 0x260 da cadeia ganham o processo 0x9BF1; o carro perde metade
        g.writeWord(0xE876, a);
        wf(da + 0x26, 200.0);
        g.writeWord(b + 0xE, 0); g.writeWord(b + 0x20, r2); g.writeWord(r2 + 0xF, 0x260); g.writeWord(r2 + 0xE, 0);
        g.writeWord(r2 + 0x1B, db); g.writeWord(r2 + 0x20, 0);
        pend.calls.clear();
        d.call(0x9BEF, c, g);
        int bumps = 0;
        for (const auto& e : pend.calls) bumps += (e.addr == 0xA868 && e.ar2 == 0x9BF1);
        check(bumps == 2 && rf(da + 0x26) == 100.0 && rf(b + 0x12) <= 0.0 && rf(b + 0x12) >= -65.0 &&
                  (g.readWord(r2 + 0xE) & (1u << 23)),
              "0x9BEF: batida a 200 derruba os dois 0x260 encadeados (0x9BF1, impulso −65..0) e o carro cai para 100");

        // 0x4495: anda 150·quadros pela matriz do objeto até gastar 96000
        for (uint32_t i = 0; i < 9; ++i) wf(obj + 4 + i, (i % 4 == 0) ? 1.0 : 0.0);
        wf(obj + 1, 0.0); wf(obj + 2, 0.0); wf(obj + 3, 0.0);
        g.writeWord(0xC92F, 1);
        g.writeWord(obj + 0xE, 1u << 23);
        pend.calls.clear();
        c.ar[4] = obj; c.ar[7] = pcb;
        d.call(0x4495, c, g);
        int frames = 1;
        while (c.ret == 0x44B2 && frames < 1000) {
            c.ret = 0;
            d.call(0x44B2, c, g);
            ++frames;
        }
        check(frames == 641 && std::fabs(rf(obj + 3) - 96150.0) < 1.0 && (g.readWord(obj + 0xE) & (1u << 23)) == 0,
              "0x4495: 150 por quadro no eixo z do objeto por 641 quadros (96000 / 150 + 1); depois solta o objeto");
    }
}

/// Carro especial 0x270 da pista 2 (0x7CFA) e retas 2D (0x94B4/0x94C9)
void romSpecialCar(const std::optional<std::filesystem::path>& dir) {
    {
        MemoryBus g;
        g.loadProgramRom(*dir, {"cusa-l41.u10", "cusa-l41.u11", "cusa-l41.u12", "cusa-l41.u13"});
        g.hardwareReset();
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        math::registerRoutines(d);
        math::registerTrig(d);
        math::registerMatrix(d);
        math::registerFloatMath(d);
        game::registerRoamingObjects(d);
        game::registerSpecialCar(d);
        auto wf = [&](uint32_t a, double v) { g.writeWord(a, doubleToTms(v)); };
        auto rf = [&](uint32_t a) { return tmsToDouble(g.readWord(a)); };
        Tms32031State c;

        // 0x94C9: reta por b = (1, −1) com direção a − b = (3, 4); 0x94B4: distância de (4, 3) = 5
        const uint32_t pa = 0x1F000, pb = 0x1F010, ln = 0x1F020;
        wf(pa, 4.0); wf(pa + 1, 3.0); wf(pb, 1.0); wf(pb + 1, -1.0);
        c.ar[0] = pa; c.ar[1] = pb; c.ar[2] = ln; c.r[0] = 0x11; c.r[3] = 0x44;
        d.call(0x94C9, c, g);
        check(rf(ln) == 3.0 && rf(ln + 1) == 4.0 && rf(ln + 2) == 1.0 && c.r[0] == 0x11 && c.r[3] == 0x44,
              "0x94C9: reta (dx, dy, −(x1·dx + y1·dy)) por dois pontos; R0..R3 preservados");
        c.ar[0] = ln; c.ar[1] = pa; c.r[1] = 0x22; c.r[2] = 0x33;
        d.call(0x94B4, c, g);
        check(std::fabs(tmsToDouble(c.r[0]) - 5.0) < 1e-3 && c.r[1] == 0x22 && c.r[2] == 0x33,
              "0x94B4: distância com sinal do ponto à reta; R1 e R2 preservados");

        // 0x7E3F: nós das rodas da tabela [B11B] (2 nós) e processo [B11C] = 0x7E63 (ID 0x230)
        const uint32_t obj = 0x1F100, pcb = 0x1F200, wheels = 0x1F280;
        uint32_t nextNode = 0x1F400;
        d.add(0x9001, [&](Tms32031State& s, MemoryBus&, ISubroutines&) { s.ar[0] = nextNode; nextNode += 0x40; });
        d.add(0xA868, [&](Tms32031State& s, MemoryBus&, ISubroutines&) {
            pend.calls.push_back({0xA868, s.ar[2], s.r[0], s.r[2]});
            s.ar[0] = wheels;
        });
        g.writeWord(obj + 0xE, 0x10);
        c.ar[4] = obj; c.ar[7] = pcb;
        d.call(0x7E3F, c, g);
        const uint32_t n1 = g.readWord(obj + 0x1A), n2 = g.readWord(n1);
        bool spawnedWheels = false;
        for (const auto& e : pend.calls) spawnedWheels |= (e.addr == 0xA868 && e.ar2 == 0x7E63 && e.r1 == 0x230);
        check(g.readWord(obj + 0xE) == 0x810 && n1 == 0x1F400 && n2 == 0x1F440 && g.readWord(n2) == 0 &&
                  g.readWord(n1 + 2) == 0x07B20000 && g.readWord(n1 + 5) == 0x07B20000 && g.readWord(n1 + 0x10) == 3 &&
                  g.readWord(n2 + 0x10) == 0x5E && g.readWord(n1 + 0x11) == obj && spawnedWheels &&
                  g.readWord(pcb + 0x1E) == wheels && g.readWord(obj + 0x16) == wheels,
              "0x7E3F: bit 0x800, 2 nós de roda encadeados a partir de +1A, processo 0x7E63 (ID 0x230) em +1E/+16");

        // 0x7EC6: motor 0x135 no canal 1 com volume 128 + 127·(20000 − d)·[B11D]
        g.writeWord(0xE8FF, 0x135); g.writeWord(0xE908, 0);
        wf(pcb + 0x17, 10000.0);
        pend.calls.clear();
        d.call(0x7EC6, c, g);
        const double vol = std::floor(128.0 + 127.0 * 10000.0 * rf(0xB11D));
        check(pend.calls.size() == 1 && pend.calls[0].addr == 0x9114 && pend.calls[0].r0 == 1 &&
                  std::fabs(static_cast<double>(pend.calls[0].r1) - vol) <= 1.0,
              "0x7EC6: volume do som 0x135 pela distância (canal [E8FF] → R0 = 1)");
        g.writeWord(0xE8FF, 0); g.writeWord(0xE908, 0x135);
        pend.calls.clear();
        d.call(0x7EC6, c, g);
        check(pend.calls.size() == 1 && pend.calls[0].r0 == 2, "0x7EC6: canal [E908] → R0 = 2, sem tocar de novo (0x9188)");

        // 0x7E64: parado, rumo 0, volante 0, velocidade 100 → +13 = 0.1·100, z += 10·[C92F]
        g.writeWord(0x5A, 0x1F600); g.writeWord(0x5B, 0x1F610); g.writeWord(0x5D, 0x1F620);
        g.writeWord(0xC92F, 2);
        for (uint32_t off : {1u, 2u, 3u, 0x11u, 0x12u, 0x13u, 0x14u, 0x15u, 0x1Eu}) wf(obj + off, 0.0);
        wf(pcb + 0x26, 0.0); wf(pcb + 0x23, 20.0);
        c.r[2] = doubleToTms(0.0); c.r[3] = doubleToTms(100.0);
        d.call(0x7E64, c, g);
        check(std::fabs(rf(obj + 0x13) - 10.0) < 0.01 && std::fabs(rf(obj + 3) - 20.0) < 0.02 && rf(obj + 1) == 0.0 &&
                  rf(obj + 0x12) == 0.0 && rf(obj + 0x1E) == 0.0 && std::fabs(rf(pcb + 0x23) - 10.0) < 0.01 &&
                  rf(pcb + 0x24) == 20.0 && std::fabs(rf(obj + 0x14) - 0.1) < 0.001 && std::fabs(rf(obj + 4) - 1.0) < 1e-6,
              "0x7E64: velocidade suavizada (0.1), posição += v·[C92F], +14 = 0.2·(nova/antiga), matriz rotY·rotX·rotZ");
        // volante 0.01 → inclinação +1E = 0.1·25·0.01 = 0.025; rumo +15 = 0.01
        c.r[2] = doubleToTms(0.01); c.r[3] = doubleToTms(100.0);
        d.call(0x7E64, c, g);
        check(std::fabs(rf(obj + 0x1E) - 0.025) < 1e-4 && std::fabs(rf(obj + 0x15) - 0.01) < 1e-6,
              "0x7E64: volante soma ao rumo e inclina a carroceria (0.1·25·volante + 0.9·+1E)");

        // 0x7CFA com [E540] ligado: cria o carro e sai logo (0x7E29)
        d.add(0x7025, [&](Tms32031State& s, MemoryBus&, ISubroutines&) {
            s.ar[0] = obj;
            s.st &= ~Tms32031State::kStCarry;
        });
        nextNode = 0x1F400;
        g.writeWord(0xE540, 1);
        wf(obj + 0x1B, 0.0);
        g.writeWord(obj + 0x1B, 0x1F700);
        g.writeWord(0xE3F6, 0); g.writeWord(0xE3F7, 1);
        pend.calls.clear();
        c.ar[4] = 0; c.ar[7] = pcb;
        d.call(0x7CFA, c, g);
        std::vector<uint32_t> seq;
        for (const auto& e : pend.calls) seq.push_back(e.addr);
        const auto has = [&](uint32_t a, uint32_t ar2) {
            for (const auto& e : pend.calls) if (e.addr == a && e.ar2 == ar2) return true;
            return false;
        };
        check(g.readWord(obj + 0xD) == 0xC5F2C7 && rf(obj + 2) == -20.0 && rf(obj + 3) == 3500.0 &&
                  g.readWord(obj + 0xF) == 0x270 && g.readWord(pcb + 3) == 0x270 && g.readWord(obj + 0x17) == pcb &&
                  g.readWord(pcb + 0x2C) >= 1 && g.readWord(pcb + 0x2C) <= 2 && g.readWord(0xE3F6) == 0x1F700 &&
                  has(0x7100, obj) && has(0x91C1, 0x135) && has(0xA8AC, wheels) && !seq.empty() && seq.back() == 0xA896,
              "0x7CFA: carro 0x270 (modelo 0xC5F2C7) com [E540] ligado sai logo: dados, objeto, som 0x135 e rodas soltos");
        // falta de objeto (0x7025 com carry) → morre sem mexer em nada
        d.add(0x7025, [&](Tms32031State& s, MemoryBus&, ISubroutines&) { s.st |= Tms32031State::kStCarry; });
        pend.calls.clear();
        d.call(0x7CFA, c, g);
        check(pend.calls.size() == 2 && pend.calls[0].addr == 0xA91F && pend.calls[1].addr == 0xA896,
              "0x7CFA: sem objeto livre o processo morre");

        // 0x7E27: DBU AR5 — repete a saída de cena enquanto AR5 ≥ 0
        d.add(0x66BC, [](Tms32031State& s, MemoryBus&, ISubroutines&) { s.r[0] = doubleToTms(5000.0); });
        g.writeWord(0xE8FF, 0); g.writeWord(0xE908, 0);
        c.ar[4] = obj; c.ar[5] = 1; c.r[7] = doubleToTms(-10.0);
        wf(pcb + 0x26, 0.0); wf(pcb + 0x20, 1.0); wf(pcb + 0x21, 666.5); wf(pcb + 0x25, 0.0);
        pend.calls.clear();
        d.call(0x7E27, c, g);
        bool slept = false;
        for (const auto& e : pend.calls) slept |= (e.addr == 0xA874);
        check(c.ar[5] == 0 && tmsToDouble(c.r[7]) == -13.0 && rf(pcb + 0x26) == -13.0 &&
                  rf(pcb + 0x21) == 667.0 && slept && c.ret == 0x7E27,
              "0x7E27: DBU AR5 → mais um quadro de saída (R7 −3, altura-alvo cai, velocidade até 667)");

        // 0x7E63 → 0xC10000: roda gira 0.6·[C92F] por quadro
        g.writeWord(obj + 0x1A, 0x1F400);
        c.r[6] = doubleToTms(5.0);
        pend.calls.clear();
        d.call(0x7E63, c, g);
        check(std::fabs(tmsToDouble(c.r[6]) - 1.2) < 1e-3 && c.ret == 0xC1000E && std::fabs(rf(0x1F400 + 7) - std::cos(1.2)) < 0.01,
              "0x7E63 (0xC10000): R6 = 0 + 0.6·[C92F], matriz rotY do nó de roda, retoma em 0xC1000E");
    }
}

/// Encerramento (0x8481) e painéis (0xB07B–0xB07F)
void romEnding(const std::optional<std::filesystem::path>& dir) {
    {
        MemoryBus g;
        g.loadProgramRom(*dir, {"cusa-l41.u10", "cusa-l41.u11", "cusa-l41.u12", "cusa-l41.u13"});
        g.hardwareReset();
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        math::registerRoutines(d);
        math::registerTrig(d);
        math::registerMatrix(d);
        math::registerFloatMath(d);
        game::registerEnding(d);
        auto wf = [&](uint32_t a, double v) { g.writeWord(a, doubleToTms(v)); };
        auto rf = [&](uint32_t a) { return tmsToDouble(g.readWord(a)); };
        auto count = [&](uint32_t a, uint32_t ar2) {
            int n = 0;
            for (const auto& e : pend.calls) n += (e.addr == a && e.ar2 == ar2);
            return n;
        };
        Tms32031State c;
        const uint32_t pcb = 0x1F000, cam = 0x1F040;
        g.writeWord(0x45, cam);
        g.writeWord(0x5A, 0x1F060);
        g.writeWord(0xC92F, 1);

        // 0xB07F: copia RC + 1 palavras, preserva AR0/AR1
        for (uint32_t i = 0; i < 9; ++i) g.writeWord(0x1F100 + i, 0x100 + i);
        g.writeWord(0x1F129, 0);
        c.ar[0] = 0x1F100; c.ar[1] = 0x1F120; c.rc = 8;
        d.call(0xB07F, c, g);
        check(g.readWord(0x1F120) == 0x100 && g.readWord(0x1F128) == 0x108 && g.readWord(0x1F129) == 0 &&
                  c.ar[0] == 0x1F100 && c.ar[1] == 0x1F120,
              "0xB07F (0xC10194): copia RC + 1 palavras de AR0 para AR1");

        // 0x858C: percorre [0x40] e dispara as animações por tipo
        const uint32_t o1 = 0x1F200, o2 = 0x1F240, o3 = 0x1F280, head = 0x1F2C0;
        g.writeWord(0x40, head);
        g.writeWord(head, o1); g.writeWord(o1, o2); g.writeWord(o2, o3); g.writeWord(o3, 0);
        g.writeWord(o1 + 0xF, 0x621); g.writeWord(o2 + 0xF, 0x300); g.writeWord(o3 + 0xF, 0x6D0);
        c.ar[7] = pcb; c.ar[6] = 0x77;
        pend.calls.clear();
        d.call(0x858C, c, g);
        check(count(0xA844, 0x860F) == 2 && g.readWord(0xE5FC) == o1 && c.ar[6] == 0x77 &&
                  std::fabs(rf(o3 + 4) - 1.2) < 0.001 && std::fabs(rf(o3 + 0xC) - 1.2) < 0.001,
              "0x858C: 0x621 (som em [E5FC]) e 0x6D0 (escala 1.2) ganham 0x860F; 0x300 ignorado; AR6 preservado");

        // 0x860F: modelo da tabela AR6 a cada 9..11 quadros; −1 recomeça
        const uint32_t tab = 0x1F300;
        g.writeWord(tab, 0xAAA); g.writeWord(tab + 1, 0xFFFFFFFF);
        c.ar[4] = o1; c.ar[6] = tab;
        d.call(0x860F, c, g);
        const bool firstOk = g.readWord(o1 + 0xD) == 0xAAA && static_cast<int32_t>(c.r[4]) >= 9 && static_cast<int32_t>(c.r[4]) <= 11 && c.ret == 0x8618;
        c.r[4] = 1;
        d.call(0x8618, c, g);
        check(firstOk && g.readWord(o1 + 0xD) == 0xAAA && c.ar[5] == tab + 1 && static_cast<int32_t>(c.r[4]) >= 9,
              "0x860F: troca +D pela sequência e espera 9..11 quadros; no fim (−1) volta ao início");

        // 0x853F: volume do som 0xBA pela distância de [E5FC] à câmera
        wf(cam, 0.0); wf(cam + 2, 0.0);
        wf(o1 + 1, 1000.0); wf(o1 + 3, 0.0);
        pend.calls.clear();
        d.call(0x853F, c, g);
        const bool nearOk = !pend.calls.empty() && pend.calls.back().addr == 0xA874 && c.ret == 0x855E;
        uint32_t vNear = 0, vMid = 0, vFar = 0;
        for (const auto& e : pend.calls) if (e.addr == 0x4330) vNear = c.r[2];
        wf(o1 + 1, 7000.0);
        d.call(0x855E, c, g);
        vMid = c.r[2];
        wf(o1 + 1, 20000.0);
        d.call(0x853F, c, g);
        vFar = c.r[2];
        const uint32_t expMid = tmsFix((1.0 - 5000.0 * rf(0xB15A)) * 205.0);
        check(nearOk && count(0x4330, 0xBA) == 3 && vNear == 0xCD &&
                  vMid == std::min<uint32_t>(expMid, 0xB4) && vFar == 10,
              "0x853F: volume 0xBA = 205 até 2000, 205·(1 − (d − 2000)·[B15A]) (máx. 180) e 10 além de 12000");

        // 0x855F: sequência de sons (primeiro passo só espera 25; o último morre)
        pend.calls.clear();
        d.call(0x855F, c, g);
        check(c.ret == 0x8561 && count(0xA874, 25) == 1, "0x855F: espera 25 quadros e segue em 0x8561");
        pend.calls.clear();
        d.call(0x8569, c, g);
        check(count(0x91E0, 0xC) == 1 && count(0x9188, 0x8A) == 1 && c.ret == 0x856F, "0x8569: trilha 0xC e som 0x8A, 75 quadros");
        pend.calls.clear();
        d.call(0x8589, c, g);
        check(count(0x91E0, 0x1B) == 1 && pend.calls.back().addr == 0xA896, "0x8589: trilha 0x1B e o processo morre");

        // 0xB075 → 0xC1010A: painéis voltam (R7 = [B135]·0.91 por quadro, giro −0.2)
        uint32_t next = 0x1F400;
        d.add(0x7025, [&](Tms32031State& s, MemoryBus&, ISubroutines&) { s.ar[0] = next; next += 0x40; });
        pend.calls.clear();
        d.call(0xB075, c, g);
        check(g.readWord(0xE5F8) == 0 && count(0xA844, 0xB07D) == 1, "0xB075: [E5F8] = 0 e processo [B134] = 0xB07D (ID 0x5C0)");
        c.ar[7] = pcb;
        for (uint32_t i = 0; i < 4; ++i) { wf(0x1F400 + i * 0x40 + 1, 10.0 * (i + 1)); wf(0x1F400 + i * 0x40 + 2, 0.0); }
        pend.calls.clear();
        d.call(0xB07D, c, g);
        const double r7 = rf(0xB135) * 0.909912;
        const uint32_t p0 = g.readWord(pcb + 0xC), p1 = g.readWord(pcb + 0xD), p2 = g.readWord(pcb + 0xE);
        check(p0 == 0x1F400 && g.readWord(p0 + 0xD) == 0xC2FC80 && g.readWord(p0 + 0xE) == 0x8000021 &&
                  rf(p0 + 3) == 368.0 && count(0x7070, p0) == 1 && std::fabs(rf(p0 + 0x1E) - r7) < r7 * 1e-3 &&
                  std::fabs(rf(p1 + 0x1E) + r7) < r7 * 1e-3 && std::fabs(rf(p2 + 0x1F) + r7) < r7 * 1e-3 &&
                  c.ar[5] == 100 && c.ret == 0xC1014B,
              "0xB07D (0xC1010A): 4 painéis [B06B..] criados; afastamento ±[B135]·0.91 em +1E/+1F; 101 quadros");
        c.ar[5] = 0;
        d.call(0xC1014B, c, g);
        check(g.readWord(0xE5F8) == 1 && pend.calls.back().addr == 0xA896, "0xC1014B: fim do fechamento → [E5F8] = 1 e morre");

        // 0xB07C (0xC100B3): abre com fator 1.1 e solta os painéis depois de 65 quadros
        pend.calls.clear();
        next = 0x1F400;
        d.call(0xB07C, c, g);
        const bool openOk = std::fabs(rf(pcb + 0x13) - 1.1) < 0.001 && c.ret == 0xC100B8;
        const double before = rf(p0 + 0x1E);
        d.call(0xC100B8, c, g);
        check(openOk && std::fabs(rf(p0 + 0x1E) - (before + 2.2)) < 0.01 && c.ar[5] == 0x40 && c.ret == 0xC100FE,
              "0xB07C (0xC100B3): fator 1.1; cada quadro R7 ·= fator (2.2 no primeiro) e os painéis se afastam");
        c.ar[5] = 0;
        pend.calls.clear();
        d.call(0xC100FE, c, g);
        check(count(0x7100, p0) == 1 && count(0x7100, p2) == 1 && g.readWord(0xE5F8) == 1, "0xC100FE: solta os 4 painéis e liga [E5F8]");

        // 0x8481: câmera, cenário [E5FB], textos e o processo das legendas; volta pela pilha local
        d.add(0x7309, [&](Tms32031State& s, MemoryBus&, ISubroutines&) { s.ar[0] = 0x1F600; });
        d.add(0x7651, [&](Tms32031State& s, MemoryBus&, ISubroutines&) { s.ar[0] = 0x1F680; s.ar[1] = 0x1F6C0; });
        g.writeWord(0x1F600 + 0xE, 0); g.writeWord(0x47, 0x1F070);
        pend.calls.clear();
        d.call(0x8481, c, g);
        check(rf(cam + 1) == -600.0 && rf(cam + 2) == -12000.0 && g.readWord(0xE5FB) == 0x1F600 &&
                  g.readWord(0x1F600 + 0xE) == 0x29 && (g.readWord(0x1F680 + 8) & 0x100) && count(0xA844, 0x8632) == 1 &&
                  count(0xA3C2, g.readWord(0xB150)) == 2 && c.ret == 0x84E0 && (g.readWord(0xC8C5) & 0xC0) == 0,
              "0x8481: câmera em (0, −600, −12000), cenário [B153] em [E5FB], 2 textos, legendas 0x8632; espera 15");
        c.ar[5] = 0xB4;
        wf(cam + 2, -12000.0); wf(cam + 1, -600.0);
        d.call(0x84F5, c, g);
        check(std::fabs(rf(cam + 2) - (-12000.0 + 10500.0 * 0.016998)) < 1.0 && g.readWord(0xC940) == 1 && c.ret == 0x8507,
              "0x84F5: câmera se aproxima de (−4300, −1500) com fatores 0.04/0.017");
        const uint32_t stackTop = pcb + 0x2F;
        g.writeWord(pcb + 1, stackTop + 1); g.writeWord(stackTop, 0x3BF3);
        d.add(0x3BF3, [&](Tms32031State& s, MemoryBus&, ISubroutines&) { s.r[5] = 0x3BF3; });
        pend.calls.clear();
        d.call(0x852C, c, g);
        int kills = 0;
        for (const auto& e : pend.calls) kills += (e.addr == 0xA8CF && (e.r0 == 0x1234 || e.r0 == 0x850) && e.r1 == 0xFFFFFFFFu);
        check(c.r[5] == 0x3BF3 && kills == 2 && count(0x9DFE, g.readWord(0xB158)) == 1,
              "0x852C: descarrega, mata 0x1234/0x850, paletas [B158]/[B159] e volta pela pilha local (0x3BF3)");
    }
}

/// Eventos das pistas 9 e 11 (0x69A8–0x6C8B) e créditos finais (0xA1F1)
void romRoadsideEventsAndCredits(const std::optional<std::filesystem::path>& dir) {
    {
        MemoryBus g;
        g.loadProgramRom(*dir, {"cusa-l41.u10", "cusa-l41.u11", "cusa-l41.u12", "cusa-l41.u13"});
        g.hardwareReset();
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        math::registerRoutines(d);
        math::registerTrig(d);
        math::registerMatrix(d);
        math::registerFloatMath(d);
        game::registerRoadsideEvents(d);
        game::registerCredits(d);
        auto wf = [&](uint32_t a, double v) { g.writeWord(a, doubleToTms(v)); };
        auto rf = [&](uint32_t a) { return tmsToDouble(g.readWord(a)); };
        auto count = [&](uint32_t a, uint32_t ar2) {
            int n = 0;
            for (const auto& e : pend.calls) n += (e.addr == a && e.ar2 == ar2);
            return n;
        };
        Tms32031State c;
        const uint32_t pcb = 0x1F000;
        c.ar[7] = pcb;

        // 0x6C85 / 0x6C76 / 0x6C59: linha de pontos [AA2F] (x, y, z, trecho)
        const uint32_t path = g.readWord(0xAA2F);
        for (uint32_t i = 0; i < 12; ++i) {
            g.writeWord(path + i * 4, 0);
            g.writeWord(path + i * 4 + 1, 0);
            g.writeWord(path + i * 4 + 2, i * 1000);
            g.writeWord(path + i * 4 + 3, 10 + i);
        }
        c.r[2] = 13;
        d.call(0x6C85, c, g);
        check(c.ar[2] == path + 12, "0x6C85: AR2 = primeiro ponto com trecho >= R2 (13 → ponto 3)");
        c.ar[2] = path; c.r[2] = 1;
        d.call(0x6C76, c, g);
        const double head0 = tmsToDouble(c.r[2]);
        const uint32_t o = 0x1F100;
        wf(o + 1, 0.0); wf(o + 3, 3900.0);
        c.ar[4] = o; c.ar[2] = path; c.r[2] = 1;
        d.call(0x6C59, c, g);
        check(std::fabs(std::sin(head0)) < 1e-3 && c.ar[2] == path + 16 && c.r[2] == 4,
              "0x6C76: rumo ao longo de +z; 0x6C59: avança até o ponto mais perto do objeto (z 3900 → ponto 4, 4 passos)");

        // 0x6AD9: lança o objeto na direção do carro (≥ 100) e cria o processo de voo [2333] (ID 0x290)
        const uint32_t car = 0x1F200, ob = 0x1F300;
        wf(car + 0x2D, 0.0); wf(car + 0x26, 50.0);
        g.writeWord(ob + 0xE, 0); g.writeWord(0xE871, 0);
        c.ar[4] = ob; c.ar[5] = car;
        pend.calls.clear();
        d.call(0x6AD9, c, g);
        bool fly = false;
        for (const auto& e : pend.calls) fly |= (e.addr == 0xA868 && e.ar2 == g.readWord(0x2333));
        check(fly && rf(ob + 0x13) > 0.0 && rf(ob + 0x12) < 0.0 && rf(ob + 0x12) >= -27.0 * 1.5 &&
                  (g.readWord(ob + 0xE) & ((1u << 27) | (1u << 23) | 0x10u)) == ((1u << 27) | (1u << 23) | 0x10u) &&
                  g.readWord(ob + 0x1F) == 0,
              "0x6AD9: velocidade para a frente do carro, para cima (≥ −27·1.5); processo 0x290 e flags 1<<27|1<<23|0x10");

        // 0x6A66: estilhaça; registradores restaurados (0x9042/0x905C), carro com metade da velocidade
        const uint32_t hit = 0x1F400;
        g.writeWord(hit + 0xF, 0x751); g.writeWord(hit + 0xE, 0);
        wf(hit + 1, 10.0); wf(hit + 2, 20.0); wf(hit + 3, 30.0); wf(hit + 0x11, 0.0);
        wf(car + 0x26, 200.0);
        d.add(0x700A, [](Tms32031State& s, MemoryBus&, ISubroutines&) { s.st |= Tms32031State::kStCarry; });
        c.ar[1] = hit; c.ar[4] = 0x44; c.ar[6] = 0x66; c.r[6] = 0x123; c.r[7] = 0x77;
        pend.calls.clear();
        d.call(0x6A66, c, g);
        check(rf(car + 0x26) == 100.0 && g.readWord(hit + 0xF) == 0x750 && count(0xA868, 0x6AAC) == 1 &&
                  c.ar[4] == 0x44 && c.ar[6] == 0x66 && c.r[6] == 0x123 && c.r[7] == 0x77 && c.ar[1] == hit,
              "0x6A66: objeto vira 0x750, pedaços [C62B], processo 0x6AAC; carro a 50%; todos os registradores restaurados");
        g.writeWord(hit + 0xE, 0x10);
        d.call(0x6AD3, c, g);
        check(g.readWord(hit + 0xF) == 0x753 && (g.readWord(hit + 0xE) & 0x10) == 0,
              "0x6AD3: lança (0x6AD9) e solta o pedaço (0x6AA6: tipo 0x753, sem 0x10)");

        // 0x6BF5 → 0x6C0F: objeto de tela desce até z = 368 e anima
        const uint32_t dv = 0x1F500, tab = 0x1F540;
        d.add(0x700A, [&](Tms32031State& s, MemoryBus&, ISubroutines&) { s.ar[0] = dv; });
        g.writeWord(tab, 0xAAA); g.writeWord(tab + 1, 0xBBB); g.writeWord(tab + 2, 0xCCC); g.writeWord(tab + 3, 0);
        g.writeWord(dv + 0xE, 0);
        g.writeWord(0xE878, car); wf(car + 0x26, 1000.0); g.writeWord(0xC92F, 1);
        c.r[5] = tab;
        d.call(0x6BF5, c, g);
        const bool setupOk = g.readWord(dv + 0xF) == 0x103 && rf(dv + 3) == 2000.0 && (g.readWord(dv + 0xE) & 0xBB) == 0xBB &&
                             c.ret == 0x6C0F;
        int frames = 0;
        while (c.ret == 0x6C0F && frames < 50) { d.call(0x6C0F, c, g); ++frames; }
        check(setupOk && rf(dv + 3) == 368.0 && frames == 3 && g.readWord(dv + 0xD) == 0xBBB && c.ret == 0x6C28,
              "0x6BF5: objeto 0x103 em z = 2000 desce 800/quadro (até 368) e passa a animar pela tabela R5");

        // créditos: primeira linha "CRUISN USA" em y = 401, centralizada, subindo 1 por quadro
        const uint32_t t0 = 0x1F600, t1 = 0x1F640;
        d.add(0x7651, [&](Tms32031State& s, MemoryBus&, ISubroutines&) {
            pend.calls.push_back({0x7651, s.ar[2], s.r[2], s.r[3]});
            s.ar[0] = t0; s.ar[1] = t1;
        });
        g.writeWord(t0 + 8, 0); g.writeWord(t1 + 8, 0);
        pend.calls.clear();
        d.call(0xA205, c, g);
        const bool spawned = count(0xA844, 0xA1A1) == 1 && count(0xA844, 0xA179) == 1 && rf(0xE5F9) == 24.0;
        int waits = 0;
        while (count(0x7651, g.readWord(g.readWord(0xA2AB) + 2)) == 0 && waits < 100) { d.call(0xA218, c, g); ++waits; }
        uint32_t y = 0;
        for (const auto& e : pend.calls) if (e.addr == 0x7651) y = e.r1;
        check(spawned && waits == 43 && tmsToDouble(y) == 401.0 && (g.readWord(t0 + 8) & 0x100) && rf(t0 + 6) == -1.0 &&
                  count(0xA1E4, 0) == 0,
              "0xA1F1: anel 0xA1A1 e reciclagem 0xA179; 'CRUISN USA' nasce em y = 401 após 43 quadros, centralizada, vy = −1");

        // 0xA179: texto acima de y = −25 volta à lista livre
        const uint32_t listCell = g.readWord(0x763F), freeCell = g.readWord(0x763E);
        const uint32_t n1 = 0x1F700, n2 = 0x1F720;
        g.writeWord(listCell, n1); g.writeWord(n1, n2); g.writeWord(n2, 0);
        wf(n1 + 4, -30.0); wf(n2 + 4, 100.0);
        g.writeWord(freeCell, 0); g.writeWord(0xE91B, 5);
        d.call(0xA179, c, g);
        check(g.readWord(listCell) == n2 && g.readWord(freeCell) == n1 && g.readWord(0xE91B) == 6 && c.ret == 0xA194,
              "0xA179: texto em y = −30 sai da lista [763F] e volta para [763E]; [E91B]++");

        // 0xA26F: troca o texto em y ≤ 250
        c.ar[4] = t0; c.ar[5] = t1;
        wf(t0 + 4, 260.0);
        d.call(0xA26F, c, g);
        const bool waited = c.ret == 0xA26F;
        wf(t0 + 4, 250.0);
        pend.calls.clear();
        d.call(0xA26F, c, g);
        check(waited && g.readWord(t0 + 1) == g.readWord(0xA26C) && g.readWord(t1 + 1) == g.readWord(0xA26C) &&
                  pend.calls.back().addr == 0xA896,
              "0xA26D: em y = 250 os dois textos passam a [A26C] e o processo morre");
    }
}

/// Objetos animados do cenário: tratadores da tabela [4500] e processos
void romTrackProps(const std::optional<std::filesystem::path>& dir) {
    {
        MemoryBus g;
        g.loadProgramRom(*dir, {"cusa-l41.u10", "cusa-l41.u11", "cusa-l41.u12", "cusa-l41.u13"});
        g.hardwareReset();
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        math::registerRoutines(d);
        track::registerTrackLoader(d);
        game::registerTrackProps(d);
        const uint32_t obj = 0x1F000, list = 0x1F100;
        Tms32031State c;
        // 0x428C: lista [E8EC], tipo 0x731, R0 preservado
        g.writeWord(0xE8EC, 0);
        c.ar[4] = obj; c.r[0] = 0x1234;
        d.call(0x428C, c, g);
        check(g.readWord(obj + 0xF) == 0x731 && g.readWord(0xE8EC) == obj && c.r[0] == 0x1234 &&
                  (g.readWord(obj + 0xE) & (1u << 22)),
              "0x428C: objeto na lista [E8EC] (0xAF58) com tipo 0x731; R0 preservado");

        // 0x43DE: fase aleatória na lista [4341], processo [B0CF] = 0x4402, bit 23, limpa 0x400
        pend.calls.clear();
        g.writeWord(obj + 0xE, 0x400);
        c.ar[0] = 0x1F800; c.st = 0; c.ar[5] = 0x55; c.ar[6] = 0x66;
        d.call(0x43DE, c, g);
        uint32_t proc = 0;
        for (const auto& e : pend.calls) if (e.addr == 0xA844) proc = e.ar2;
        check(proc == 0x4402 && g.readWord(obj + 0x17) == 0x1F800 && g.readWord(obj + 0xE) == (1u << 23) &&
                  c.ar[5] == 0x55 && c.ar[6] == 0x66,
              "0x43DE: processo 0x4402 em [AR4+0x17], bit 23 ligado, bit 0x400 limpo; registradores preservados");

        // processo 0x4308: quadros 7, 8, fim (−1) → recomeça em 7; dorme 1 ou 2
        g.writeWord(list, 7); g.writeWord(list + 1, 8); g.writeWord(list + 2, 0xFFFFFFFFu);
        c.ar[4] = obj; c.ar[6] = list;
        uint32_t seq[4];
        bool sleeps = true;
        d.call(0x4308, c, g);
        seq[0] = g.readWord(obj + 0xD);
        for (int i = 1; i < 4; ++i) {
            sleeps = sleeps && c.ret == 0x4312 && (c.ar[2] == 1 || c.ar[2] == 2);
            d.call(0x4312, c, g);
            seq[i] = g.readWord(obj + 0xD);
        }
        check(seq[0] == 7 && seq[1] == 8 && seq[2] == 7 && seq[3] == 8 && sleeps,
              "0x4308: troca o modelo pela lista AR6 (negativo recomeça), 1 ou 2 quadros por troca");

        // 0x4330: som do canal contínuo 1 só ajusta o volume (0x9114, R0 = 1, R1 = volume)
        pend.calls.clear();
        g.writeWord(0xE8FF, 0xB7); g.writeWord(0xE908, 0);
        c.ar[2] = 0xB7; c.r[2] = 0x80;
        d.call(0x4330, c, g);
        const bool vol = pend.calls.size() == 1 && pend.calls[0].addr == 0x9114 && c.r[0] == 1 && c.r[1] == 0x80;
        pend.calls.clear();
        c.ar[2] = 0x123;
        d.call(0x4330, c, g);
        check(vol && pend.calls.size() == 1 && pend.calls[0].addr == 0x9188,
              "0x4330: som já no canal [E8FF] → só volume (0x9114); outro som → 0x9188");
    }
}

/// Liberação de objeto: 0x7100 / 0x7143
void romObjectFree() {
    {
        MemoryBus g;
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        uint64_t hangs = 0;
        scene::registerSceneObject(d, hangs);
        const uint32_t cell = 0x1F000, cell58 = 0x1F010, a = 0x1F100, bobj = 0x1F200, freeObj = 0x1F300;
        g.writeWord(0x40, cell); g.writeWord(0x58, cell58 + 0x20);
        g.writeWord(cell, a); g.writeWord(a, bobj); g.writeWord(bobj, 0);
        g.writeWord(cell58 + 0x20, bobj); g.writeWord(bobj + 0x20, 0);
        g.writeWord(0xC983, freeObj); g.writeWord(0xC989, 5);
        g.writeWord(a + 0xF, 0x766); g.writeWord(bobj + 0xF, 0x731);
        g.writeWord(bobj + 0xE, (1u << 31) | (1u << 23)); g.writeWord(bobj + 0x17, 0xEC00);
        g.writeWord(bobj + 0x1F, 9);
        Tms32031State c;
        c.ar[2] = bobj; c.r[0] = 0x11; c.ar[1] = 0x22;
        d.call(0x7100, c, g);
        bool killed = false;
        for (const auto& e : pend.calls) killed = killed || (e.addr == 0xA8AC && e.ar2 == 0xEC00);
        check(g.readWord(a) == 0 && g.readWord(cell58 + 0x20) == 0 && g.readWord(0xC983) == bobj &&
                  g.readWord(bobj) == freeObj && g.readWord(0xC989) == 6 && g.readWord(bobj + 0xE) == 0 &&
                  g.readWord(bobj + 0x1F) == 0 && killed && c.ar[2] == bobj && c.r[0] == 0x11 && c.ar[1] == 0x22,
              "0x7100: tira das listas marcadas (+0xE bit 31 → [0x58], bit 23 → processo) e da visível; volta à livre");
        c.r[0] = 0x766; c.r[1] = 0xFFF;
        d.call(0x7143, c, g);
        check(g.readWord(cell) == 0 && g.readWord(0xC983) == a && g.readWord(0xC989) == 7 && c.r[0] == 0x766,
              "0x7143: libera todos os objetos visíveis com (tipo & R1) = (R0 & R1)");
    }
}

/// Preço pelos DIPs (0x73B7) e aviso do ajuste 0x1A (processo 0x3C19)
void romPricingAndNotice(const std::optional<std::filesystem::path>& dir) {
    {
        MemoryBus g;
        g.loadProgramRom(*dir, {"cusa-l41.u10", "cusa-l41.u11", "cusa-l41.u12", "cusa-l41.u13"});
        g.hardwareReset();
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        math::registerRoutines(d);
        game::registerCreditsDisplay(d);
        game::registerRaceStart(d);
        Tms32031State c;
        g.setIoInput(0x992000, 0xF9FEF9FEu);
        c.dp = 0x80;
        d.call(0x73B7, c, g);
        // DIPs 0xF9 → ~ = 0x06 → invertidos 0x60: moeda 0, país 3 → registro [B083] + 3
        const uint32_t rec = g.readWord(g.readWord(0xB083) + 3);
        check(c.ar[2] == rec && c.r[2] == c.r[0] && c.r[0] * 11 + g.readWord(0xB080) == rec && c.dp == 0,
              "0x73B7: registro de preço pelos DIPs (bits invertidos), R0 = índice em [B080] (11 palavras)");

        g.writeWord(0xC8C5, 2);
        d.call(0x3C19, c, g);
        const bool wait5 = c.ret == 0x3C1B && c.ar[2] == 5;
        pend.calls.clear();
        d.call(0x3C1B, c, g);
        int texts = 0;
        for (const auto& e : pend.calls) texts += e.addr == 0x7651;
        const bool frame = texts == 2 && c.ret == 0x3C48 && c.ar[5] == 0x5A;
        int frames = 1;
        pend.calls.clear();
        for (;;) {
            const uint32_t before = static_cast<uint32_t>(pend.calls.size());
            d.call(0x3C48, c, g);
            if (pend.calls.size() > before && pend.calls.back().addr == 0xA896) break;
            ++frames;
        }
        check(wait5 && frame && frames == 0x5B, "0x3C19: espera 5 quadros, 2 textos por quadro durante 0x5B quadros e morre");
    }
}

/// Moedas (0x7314…0x7360), START (0x1D92) e fluxo da partida (0x1775)
void romCoinsStartAndFlow(const std::optional<std::filesystem::path>& dir) {
    {
        MemoryBus g;
        g.loadProgramRom(*dir, {"cusa-l41.u10", "cusa-l41.u11", "cusa-l41.u12", "cusa-l41.u13"});
        g.hardwareReset();
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        cmos::registerRoutines(d);
        game::registerCreditsDisplay(d);
        game::registerCoinStart(d);
        Tms32031State c;
        auto setItem = [&](uint32_t i, uint32_t v) { c.ar[2] = i; c.r[2] = v; d.call(0x99F4, c, g); };
        auto item = [&](uint32_t i) { c.ar[2] = i; d.call(0x99E9, c, g); return c.r[0]; };
        // registro de preços 7: 1 unidade por moeda e por crédito, começar = 3, 1 pulso no contador
        setItem(0x17, 1); setItem(0, 7); setItem(0x2D, 30); setItem(0x70, 0); setItem(0x71, 0); setItem(0xF7, 0);
        setItem(0x19, 0);
        g.writeWord(0xE573, 0);
        d.call(0x7314, c, g);
        bool snd = false;
        for (const auto& e : pend.calls) snd = snd || (e.addr == 0x91E0 && e.ar2 == 0x102);
        check(snd && item(0x71) == 1 && item(0x70) == 0 && g.readWord(0xE573) == 1 && pend.calls.back().addr == 0xA896,
              "0x7314: moeda 1 → som 0x102, 1 pulso no contador [E573], 1 crédito (0x7360) e morre");
        g.writeWord(0xC8C5, 2);
        d.call(0x1D7F, c, g);
        const bool notEnough = !c.carry();
        pend.calls.clear();
        g.writeWord(0xC8E6, 0);
        d.call(0x1D92, c, g);
        bool beep = false;
        for (const auto& e : pend.calls) beep = beep || (e.addr == 0x91E0 && e.ar2 == 0x105);
        check(notEnough && beep && g.readWord(0xC8E6) == 1 && g.readWord(0xC8C5) == 2,
              "0x1D92: sem créditos (1 < 3) só bipa 0x105 uma vez ([C8E6]) e morre");
        d.call(0x7314, c, g);
        d.call(0x7314, c, g);
        pend.calls.clear();
        c.jump = 0;
        d.call(0x1D92, c, g);
        bool start = false;
        for (const auto& e : pend.calls) start = start || (e.addr == 0x9503 && e.ar2 == 1);
        check(start && item(0x71) == 0 && (g.readWord(0xC8C5) & 0xF) == 3 && c.jump == 0x4C1B &&
                  c.sp == g.readWord(0x63),
              "0x1D92: com 3 créditos desconta o preço, entra no modo 3 e reinicia (0x9503, AR2 = 1) → 0x4C1B");
        c.jump = 0;
        g.writeWord(0xE631, 0);
        d.call(0x1D92, c, g);
        check(g.readWord(0xE631) == 1, "0x1D92: START durante o jogo (modo 3) só pede continuação ([E631] = 1)");
        setItem(0x71, 30);
        d.call(0x7350, c, g);
        check(item(0x71) == 30, "0x7350: crédito de serviço limitado ao item 0x2D");
    }
    {
        MemoryBus g;
        g.loadProgramRom(*dir, {"cusa-l41.u10", "cusa-l41.u11", "cusa-l41.u12", "cusa-l41.u13"});
        g.hardwareReset();
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        game::registerGameFlow(d);
        Tms32031State c;
        const uint32_t pcbAddr = 0x1F000;
        c.ar[7] = pcbAddr;
        g.writeWord(pcbAddr + 1, pcbAddr + 0x2F);
        g.writeWord(0xC8D1, 0);
        d.call(0x1775, c, g);
        const bool sync = g.readWord(pcbAddr + 1) == pcbAddr + 0x30 && g.readWord(pcbAddr + 0x2F) == 0x1783 &&
                          g.readWord(pcbAddr + 2) == 0x1722 && c.ret == 0x176D && g.readWord(0xC8C5) == 0x203 &&
                          g.readWord(0xE637) == 1;
        d.call(0x176D, c, g);
        bool course = false;
        for (const auto& e : pend.calls) course = course || e.addr == 0x6C8F;
        check(sync && course && g.readWord(0xE63D) == 1 && g.readWord(pcbAddr + 2) == 0x6C8F &&
                  g.readWord(pcbAddr + 1) == pcbAddr + 0x30 && g.readWord(pcbAddr + 0x2F) == 0x17C5,
              "0x1775: chamada local 0x1722 (pilha do PCB) → 0x1783 → chamada local 0x6C8F com retorno 0x17C5");
    }
}

/// Seleção de percurso (0x6C8F): pedal, volante e percurso final
void romCourseSelect(const std::optional<std::filesystem::path>& dir) {
    {
        MemoryBus g;
        g.loadProgramRom(*dir, {"cusa-l41.u10", "cusa-l41.u11", "cusa-l41.u12", "cusa-l41.u13"});
        g.hardwareReset();
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        math::registerFloatMath(d);
        game::registerCourseSelect(d);
        Tms32031State c;
        g.writeWord(0xE89A, doubleToTms(10.0));
        g.writeWord(0xC93B, 40);
        c.r[5] = 0;
        d.call(0x1C85, c, g);
        const bool edge = c.carry();
        c.r[5] = 1;
        d.call(0x1C85, c, g);
        const bool held = !c.carry();
        g.writeWord(0xC93B, 5);
        d.call(0x1C85, c, g);
        check(edge && held && !c.carry() && c.r[5] == 0,
              "0x1C85: carry só na borda de subida do acelerador (R5 = estado anterior)");
        g.writeWord(0xE638, 12);
        g.writeWord(0xE8A1, doubleToTms(120.0));
        g.writeWord(0xE89C, doubleToTms(0.0));
        g.writeWord(0xEA47, 6);
        g.writeWord(0xC8C5, 3);
        g.writeWord(0xC8C8, 66);
        d.call(0x1C62, c, g);
        const bool hyst = g.readWord(0xEA47) == 6;
        g.writeWord(0xC8C8, 100);
        d.call(0x1C62, c, g);
        const bool right = g.readWord(0xEA47) == 7;
        g.writeWord(0xEA47, 11);
        g.writeWord(0xC8C8, 200);
        d.call(0x1C62, c, g);
        check(hyst && right && g.readWord(0xEA47) == 11 && std::fabs(tmsToDouble(g.readWord(0xEA48)) - 115.0) < 0.01,
              "0x1C62: volante → faixa [EA47] (histerese de 0,6 faixa, limitada a [E638] − 1)");

        const uint32_t pcbAddr = 0x1F000;
        c.ar[7] = pcbAddr;
        g.writeWord(pcbAddr + 1, pcbAddr + 0x2F);
        g.writeWord(pcbAddr + 0x11, 0xFFFFFFFFu);
        g.writeWord(0xE636, 2);
        g.writeWord(0xE634, 3);
        d.call(0x6DB4, c, g);
        const uint32_t mapped = g.readWord(g.readWord(0x6FC5) + 3);
        check(g.readWord(0xE634) == mapped && g.readWord(0xE636) == 2 && g.readWord(0xEA2D) == 3 &&
                  g.readWord(pcbAddr + 0x2F) == g.readWord(0xC6F5),
              "0x6DB4: percurso do mapa → pista pela tabela [6FC5]; chamada local 0x6F2A com retorno 0x6DE6");
        g.writeWord(pcbAddr + 1, pcbAddr + 0x2F);
        g.writeWord(0xE636, 2);
        g.writeWord(0xE634, 5);
        d.call(0x6DB4, c, g);
        check(g.readWord(0xE634) == 0 && g.readWord(0xE636) == 1,
              "0x6DB4: percurso 5 → [E636] = 1 e começa no percurso 0");
    }
}

/// Garagem → pista (0x18A6): dados do carro e contador da largada
void romRaceEntry(const std::optional<std::filesystem::path>& dir) {
    {
        MemoryBus g;
        g.loadProgramRom(*dir, {"cusa-l41.u10", "cusa-l41.u11", "cusa-l41.u12", "cusa-l41.u13"});
        g.hardwareReset();
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        game::registerRaceEntry(d);
        Tms32031State c;
        const uint32_t car = 0x1E000;
        c.ar[0] = car;
        c.r[0] = 2;
        d.call(0x2939, c, g);
        const uint32_t src = g.readWord(0x2918) + 8;
        check(g.readWord(car + 0x23) == g.readWord(src) && g.readWord(car + 0x22) == g.readWord(src + 1) &&
                  g.readWord(car + 0x44) == g.readWord(src + 2) && g.readWord(car + 0x45) == g.readWord(src + 3),
              "0x2939: 4 palavras do carro R0 em [2918] → +0x23, +0x22, +0x44, +0x45");

        const uint32_t pcbAddr = 0x1F000;
        c.ar[7] = pcbAddr;
        g.writeWord(pcbAddr + 1, pcbAddr + 0x2F);
        g.writeWord(0xC8C5, 0);
        d.call(0x9CE3, c, g);
        const bool hold = g.readWord(0xE60F) == 0x96;
        g.writeWord(0xC8C5, 0x100);
        d.call(0x9CE5, c, g);
        check(hold && g.readWord(0xE60F) == 0x90 && c.ar[5] == 0x18,
              "0x9CE3: [E60F] = 150 até o bit 0x100 do modo; depois −6 por quadro");
    }
}

/// Carro do jogador: pedais, câmbio, superfície, câmera e vistas (0x29B8…)
void romPlayerCar(const std::optional<std::filesystem::path>& dir) {
    {
        MemoryBus g;
        g.loadProgramRom(*dir, {"cusa-l41.u10", "cusa-l41.u11", "cusa-l41.u12", "cusa-l41.u13"});
        g.hardwareReset();
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        math::registerFloatMath(d);
        game::registerCarDynamics(d);
        game::registerPlayerCar(d);
        auto wf = [&](uint32_t a, double v) { g.writeWord(a, doubleToTms(v)); };
        auto rf = [&](uint32_t a) { return tmsToDouble(g.readWord(a)); };
        Tms32031State c;
        const uint32_t obj = 0x1E000, dd = 0x1E100;
        c.ar[4] = obj;
        c.ar[5] = dd;

        wf(0xE89A, 20.0);
        wf(0xE89B, 220.0);
        g.writeWord(0xC93B, 120);
        d.call(0x2E57, c, g);
        const double half = tmsToDouble(c.r[0]);
        g.writeWord(0xC93B, 22);
        d.call(0x2E57, c, g);
        const double idle = tmsToDouble(c.r[0]);
        g.writeWord(0xC93B, 255);
        d.call(0x2E57, c, g);
        check(std::fabs(half - 0.5) < 1e-3 && idle == 0.0 && tmsToDouble(c.r[0]) == 1.0,
              "0x2E57: acelerador = ([C93B] − repouso) / curso, zona morta de 5, até 1");

        g.writeWord(dd + 0x56, 1);
        g.writeWord(0xC937, 0x1000);
        d.call(0x2E61, c, g);
        const uint32_t second = c.r[0];
        g.writeWord(0xC937, 0x2400);
        d.call(0x2E61, c, g);
        const uint32_t first = c.r[0];
        g.writeWord(dd + 0x56, 0);
        g.writeWord(0xE5EC, 0x33BB);
        g.writeWord(dd + 0x38, 3);
        d.call(0x2E61, c, g);
        check(second == 2 && first == 1 && c.r[0] == 3,
              "0x2E61: câmbio manual pelos bits de [C937] (0x2000 vence); automático parado no grid");

        const uint32_t ground = 0x1E300, road = 0x1E340, grass = 0x1E380;
        g.writeWord(ground + 0xF, 0x200);
        g.writeWord(road + 0xF, 0x305);
        g.writeWord(grass + 0xF, 0x910);
        auto contacts = [&](uint32_t odd) {
            for (uint32_t i = 0; i < 5; ++i) g.writeWord(dd + 5 + 6 * i, i == 2 ? odd : ground);
        };
        contacts(ground);
        g.writeWord(0xE86A, 11);
        d.call(0x2CD3, c, g);
        const uint32_t tolerant = c.r[0];
        g.writeWord(0xE86A, 5);
        d.call(0x2CD3, c, g);
        const uint32_t lost = c.r[0];
        contacts(grass);
        d.call(0x2CD3, c, g);
        const uint32_t edge = c.r[0];
        contacts(road);
        d.call(0x2CD3, c, g);
        check(tolerant == 11 && lost == 0 && edge == 5 && c.r[0] == 0x14 && g.readWord(0xE86A) == 0x14,
              "0x2CD3: asfalto renova [E86A] = 20; 0x9xx mantém; fora de tudo e ≤ 8 → 0 (reposição)");

        wf(0xE86C, 100.0); wf(0xE86B, 1000.0); wf(0xE86D, 2200.0);
        wf(0xE86E, -400.0); wf(0xE86F, -10.0); wf(0xE870, -700.0);
        g.writeWord(0xE871, 1);
        d.call(0x2EE6, c, g);
        const bool stepping = rf(0xE86B) == 1100.0 && rf(0xE86E) == -410.0 && rf(0xE86C) == 100.0;
        wf(0xE86B, 2150.0);
        d.call(0x2EE6, c, g);
        check(stepping && rf(0xE86B) == 2200.0 && rf(0xE86E) == -700.0 && rf(0xE86C) == 0.0,
              "0x2EE6: câmera anda [E86C]/[E86F] por quadro até [E86D]/[E870] e zera o passo");

        const uint32_t m = 0x1E400, out = 0x1E420;
        for (uint32_t i = 0; i < 9; ++i) wf(m + i, i % 4 == 0 ? 1.0 : 0.0);
        wf(m + 2, 0.5);
        c.ar[2] = m;
        wf(0x1E430, 0.0);
        c.r[2] = doubleToTms(4.0);
        c.r[3] = out;
        d.call(0x9099, c, g);
        check(rf(out) == 2.0 && rf(out + 1) == 0.0 && rf(out + 2) == 4.0 && c.ar[2] == m &&
                  tmsToDouble(c.r[2]) == 4.0,
              "0x9099: [R3] = matriz AR2 · (0, 0, R2), preserva AR2 e R2");

        wf(dd + 0x2F, 0.0); wf(dd + 0x21, 0.2); wf(dd + 0x27, 20.0);
        g.writeWord(dd + 0x1F, 0);
        g.writeWord(0xE873, 0);
        d.call(0x2D4A, c, g);
        const double roll = rf(dd + 0x2F);
        check(roll < -0.0499 && roll > -0.0501 && tmsToDouble(c.r[1]) == roll,
              "0x2D4A: inclinação anda no máximo velocidade(≤10)·0,005 por quadro rumo a −0,5·esterço");

        g.writeWord(0xE876, obj);
        g.writeWord(obj + 0xE, 0);
        g.writeWord(0xC8C5, 4);
        g.writeWord(0xE871, 0);
        g.writeWord(0xC8CD, 0x30);
        wf(0xE86B, 0.0); wf(0xE86E, -150.0); wf(0xE86C, 0.0);
        pend.calls.clear();
        d.call(0x2EB9, c, g);
        bool sound = false, relink = false, died = false;
        for (const auto& e : pend.calls) {
            if (e.addr == 0x9188) sound = true;
            if (e.addr == 0x7093) relink = true;
            if (e.addr == 0xA896) died = true;
        }
        check(g.readWord(0xE871) == 1 && rf(0xE86D) == 2200.0 && rf(0xE870) == -400.0 &&
                  std::fabs(rf(0xE86C) - 110.0) < 0.01 && rf(0xE86B) > 109.9 && g.readWord(0xC8CD) == 0x34 &&
                  sound && relink && died,
              "0x2EB9: botão da vista 1 sai da cabine (religa o carro), transição em 20 passos e morre");
    }
}

/// HUD e eventos da corrida: 0x60BD, 0x9E59, 0x5090, 0xACCA, 0x3998, 0x81D9
void romHudAndRaceEvents(const std::optional<std::filesystem::path>& dir) {
    {
        MemoryBus g;
        g.loadProgramRom(*dir, {"cusa-l41.u10", "cusa-l41.u11", "cusa-l41.u12", "cusa-l41.u13"});
        g.hardwareReset();
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        game::registerRaceHud(d);
        game::registerRaceEvents(d);
        video::registerSkyRenderer(d);
        auto wf = [&](uint32_t a, double v) { g.writeWord(a, doubleToTms(v)); };
        auto rf = [&](uint32_t a) { return tmsToDouble(g.readWord(a)); };
        Tms32031State c;

        c.r[0] = 3300 + 2 * 55 + 27;
        c.r[3] = 0x1234;
        d.call(0x60BD, c, g);
        check(c.r[2] == 1 && c.r[1] == 2 && c.r[0] >= 48 && c.r[0] <= 50 && c.r[3] == 0x1234,
              "0x60BD: quadros → minutos (÷3300), segundos (÷55) e centésimos; preserva R3");

        c.r[0] = 8; c.r[1] = 10; c.r[2] = 0x6A; c.r[3] = 0x1E;
        d.call(0x9E59, c, g);
        check(g.readWord(0xEA34) == 0x2008 && g.readWord(0xEA36) == 8 && g.readWord(0xEA39) == 0x72 &&
                  g.readWord(0xEA3C) == 0x72 && g.readWord(0xEA37) == 10 && g.readWord(0xEA40) == 0x28 &&
                  c.r[0] == 8 && c.r[2] == 0x6A,
              "0x9E59: retângulo (R0, R1, R2 × R3) no modelo de quad, modo 0x2008; preserva R0–R4");

        wf(0xE639, 1.2);
        c.r[0] = 1;
        d.call(0x5090, c, g);
        const double up = rf(0xE639);
        wf(0xE639, 0.82);
        c.r[0] = 3;
        d.call(0x5090, c, g);
        check(std::fabs(up - 1.25) < 1e-3 && std::fabs(rf(0xE639) - 0.8) < 1e-3,
              "0x5090: dificuldade [E639] +0,07 (1º) / −0,035, limitada a 0,8..1,25");

        g.writeWord(0xC8C5, 3);
        g.writeWord(0xE869, 0);
        g.writeWord(0xE63A, 0);
        g.writeWord(0xE604, 5);
        g.writeWord(0xE868, 40);
        c.ar[0] = 1;
        d.call(0xACCA, c, g);
        const bool cp = g.readWord(0xE869) == 1 && g.readWord(0xE63A) == 1 && g.readWord(0xE604) == 45;
        c.ar[0] = 2;
        d.call(0xACCA, c, g);
        const bool bit = g.readWord(0xC8C5) == 0x83;
        c.ar[0] = 0;
        d.call(0xACCA, c, g);
        check(cp && bit && g.readWord(0xC8C5) == 0x83,
              "0xACCA: evento 1 = checkpoint (+[E868] no tempo), 2 = [C8C5] |= 0x80, 0 = nada");

        d.call(0x3990, c, g);
        const bool idle = g.readWord(0xE480) == 0xCF && g.readWord(0xE481) == 3 && g.readWord(0xC8C5) == 0x83;
        g.writeWord(0xC8C5, 0x404);
        g.writeWord(0xC8DA, 0);
        g.writeWord(0xE607, 1);
        wf(0xE639, 1.0);
        pend.calls.clear();
        d.call(0x3996, c, g);
        bool spawned = false;
        for (const auto& e : pend.calls) spawned |= e.addr == 0xA868;
        check(idle && g.readWord(0xC8C5) == 5 && g.readWord(0xC8DA) == 0x800 && g.readWord(0xE481) == 1 &&
                  std::fabs(rf(0xE639) - 1.07) < 1e-3 && g.readWord(0xE5F4) == 0xB && spawned,
              "0x3998: chegada só no modo 4 → modo 5, [C8DA] |= 0x800, dificuldade e processo [B0D7]");

        const uint32_t node = 0x1E400, buf = 0x1E480, mat = 0x1E490, tab = 0x1E4A0;
        g.writeWord(0xE46F, node);
        g.writeWord(node + 0xE, 0x1000);
        wf(node + 1, 10.0); wf(node + 2, 7.0); wf(node + 3, 100.0);
        g.writeWord(node + 0x1C, 0x20);
        g.writeWord(node + 0x21, 0);
        for (uint32_t i = 0; i < 3; ++i) wf(0x809800 + i, 0.0);
        g.writeWord(0x5D, buf);
        g.writeWord(0x47, mat);
        const double m[6] = {1, 0, 0, 0, 1, 0};
        for (uint32_t i = 0; i < 6; ++i) wf(mat + i, m[i]);
        g.writeWord(0x52, tab);
        wf(tab + 2, 2.0);
        wf(0x53, 256.0);
        wf(0x54, 100.0);
        wf(0xE3FB, 5.0);
        c.r[6] = 0x77;
        d.call(0x81D9, c, g);
        check(rf(0xE3F9) == 119.0 && rf(0xE3FA) == 276.0 && c.r[6] == 0x77,
              "0x81D9: horizonte = menor y projetado (+[E3FB]) dos nós 0x1000; preserva R6");
    }
}

/// Batidas e efeitos de objetos: 0x1FB6, 0x5543, 0x23EF, 0x6822, 0x43AB
void romCrashesAndEffects(const std::optional<std::filesystem::path>& dir) {
    {
        MemoryBus g;
        g.loadProgramRom(*dir, {"cusa-l41.u10", "cusa-l41.u11", "cusa-l41.u12", "cusa-l41.u13"});
        g.hardwareReset();
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        math::registerFloatMath(d);
        math::registerRoutines(d);
        game::registerCarCollision(d);
        game::registerTrackProps(d);
        game::registerCarCrash(d);
        game::registerObjectEffects(d);
        auto wf = [&](uint32_t a, double v) { g.writeWord(a, doubleToTms(v)); };
        auto rf = [&](uint32_t a) { return tmsToDouble(g.readWord(a)); };
        Tms32031State c;
        const uint32_t me = 0x1E000, md = 0x1E100, other = 0x1E200, od = 0x1E300, pcb = 0x1E400;

        g.writeWord(me + 0x1B, md);
        g.writeWord(0xE403, 0);
        g.writeWord(0xE406, 0);
        g.writeWord(0x4C, 0x1E500);
        c.ar[4] = me;
        c.ar[5] = 0x1234;
        d.call(0x1FB6, c, g);
        check(!c.carry() && tmsToDouble(c.r[0]) == 20000.0 && c.ar[5] == 0x1234,
              "0x1FB6: sem chão nas listas [E403]/[E406] → carry = 0, R0 = 20000; preserva AR5");

        g.writeWord(0xE876, me);
        g.writeWord(0xE878, md);
        g.writeWord(0xE877, pcb);
        g.writeWord(0xE871, 2);
        g.writeWord(0xE873, 0);
        wf(md + 3, 100.0);
        wf(me + 2, -20.0);
        d.call(0x5543, c, g);
        check(g.readWord(0xE873) == 1 && rf(me + 0x12) == -60.0 && g.readWord(pcb + 0x21) == 2 &&
                  rf(pcb + 0x20) == 80.0 && c.ar[4] == me,
              "0x5543: capotagem do jogador: [E873] = 1, vertical −60, vista guardada no PCB");

        g.writeWord(other + 0x1B, od);
        g.writeWord(0xC8E3, 0);
        g.writeWord(0xE873, 1);
        g.writeWord(0xE871, 0);
        wf(0xE87E, 1.0);
        wf(md + 0x26, 100.0);
        wf(md + 0x2D, 0.5);
        wf(od + 0x30, 1.0);
        g.writeWord(other + 0xE, 0);
        g.writeWord(other + 0xD, 0);
        c.ar[0] = me;
        c.ar[1] = other;
        pend.calls.clear();
        d.call(0x23EF, c, g);
        bool launched = false;
        for (const auto& e : pend.calls) launched |= e.addr == 0xA868;
        check(rf(md + 0x26) == 50.0 && rf(od + 0x26) >= 62.5 && std::fabs(rf(md + 0x2D) - 3.6396) < 1e-3 &&
                  g.readWord(md + 0x31) == 0xF && rf(other + 0x12) < 0.0 && launched && c.ar[0] == me,
              "0x23EF: batida forte: jogador perde metade e dá meia-volta; o outro voa (processo 0x2478)");

        g.writeWord(0xE5F6, 5);
        pend.calls.clear();
        d.call(0x6822, c, g);
        bool alloc = false;
        for (const auto& e : pend.calls) alloc |= e.addr == 0x7025;
        check(!alloc && g.readWord(0xE5F6) == 5, "0x6822: no máximo 5 partículas vivas ([E5F6])");

        const uint32_t prop = 0x1E600;
        g.writeWord(prop + 0xE, 0x400);
        wf(prop + 2, 10.0);
        c.ar[4] = prop;
        c.r[0] = 0x77;
        d.call(0x43AB, c, g);
        check(g.readWord(prop + 0xF) == 0x767 && (g.readWord(prop + 0xE) & 0x408u) == 8u && c.r[0] == 0x77,
              "0x43AB: objeto 0x767 sem o bit 0x400, com a flag 8 e processo de animação [B0CD]");
    }
}

/// Recordes e pódio: 0x9B18, 0x9B28/0x9A7F, 0x32FB, 0x330E, 0x86B5
void romHiscoreAndPodium(const std::optional<std::filesystem::path>& dir) {
    {
        MemoryBus g;
        g.loadProgramRom(*dir, {"cusa-l41.u10", "cusa-l41.u11", "cusa-l41.u12", "cusa-l41.u13"});
        g.hardwareReset();
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        cmos::registerRoutines(d);
        game::registerHiscoreScene(d);
        game::registerHiscoreEntry(d);
        game::registerResultsBoards(d);
        Tms32031State c;
        d.call(0x9A99, c, g);  // tabelas padrão
        auto row = [&](uint32_t table, uint32_t r) {
            c.r[6] = table;
            c.r[7] = r;
            d.call(0x9AD2, c, g);
            d.call(0x9B06, c, g);
            return c.r[0];
        };
        const uint32_t t0 = row(3, 0), t9 = row(3, 9);
        c.r[0] = t9 + 1;
        c.r[1] = 3;
        d.call(0x9B18, c, g);
        const uint32_t worse = c.r[0];
        c.r[0] = t0 - 1;
        c.r[1] = 3;
        d.call(0x9B18, c, g);
        check(worse == 0xFFFFFFFFu && c.r[0] == 0 && t0 < t9,
              "0x9B18: tempo pior que a 10ª linha → −1; melhor que a 1ª → linha 0");

        const uint32_t old0 = row(3, 0), old8 = row(3, 8);
        c.r[0] = 5;
        c.r[1] = 'A';
        c.r[2] = 'B';
        c.r[3] = 'C';
        c.r[4] = 2;
        c.r[5] = 0;
        c.r[6] = 3;
        c.r[7] = 0;
        d.call(0x9B28, c, g);
        c.r[6] = 3; c.r[7] = 0;
        d.call(0x9AD2, c, g);
        d.call(0x9B06, c, g);
        const bool rowOk = c.r[0] == 5 && c.r[1] == 'A' && c.r[2] == 'B' && c.r[3] == 'C' && c.r[4] == 2;
        c.r[1] = 3;
        d.call(0x9A92, c, g);
        const uint32_t mark = c.r[0];
        check(rowOk && row(3, 1) == old0 && row(3, 9) == old8 && mark == 0,
              "0x9B28: insere na linha 0, empurra as outras e marca a linha nova (0x9A7F)");

        const uint32_t pcb = 0x1E400;
        c.ar[7] = pcb;
        c.r[2] = 'X' | ('Y' << 8);
        c.ar[2] = 0x33;
        d.call(0x99F4, c, g);
        d.call(0x32FB, c, g);
        const bool got = c.carry() && g.readWord(pcb + 0x13) == 'X' && g.readWord(pcb + 0x14) == 'Y' &&
                         g.readWord(pcb + 0x15) == 0x20;
        c.r[2] = 0xFFFFFFFFu;
        c.ar[2] = 0x33;
        d.call(0x99F4, c, g);
        d.call(0x32FB, c, g);
        check(got && !c.carry(), "0x32FB: iniciais guardadas no item 0x33 (−1 = nenhuma → carry 0)");

        const uint32_t tbl = g.readWord(0x3963);
        for (uint32_t i = 0; i < 13; ++i) g.writeWord(tbl + 4 * i, 100 + i);
        c.r[1] = 0x1234;
        d.call(0x330E, c, g);
        const uint32_t sum = c.r[0];
        g.writeWord(tbl + 4 * 7, 0);
        d.call(0x330E, c, g);
        check(sum == 13 * 100 + 78 && c.r[0] == 0 && c.r[1] == 0x1234,
              "0x330E: soma dos 13 tempos de [3963]; um tempo 0 → 0; preserva R1");

        const uint32_t anim = 0x1E600, obj = 0x1E700;
        g.writeWord(anim, 0x11);
        g.writeWord(anim + 1, 0x22);
        g.writeWord(anim + 2, 0xFFFFFFFFu);
        g.writeWord(pcb + 0xC, anim);
        c.ar[4] = obj;
        c.ar[6] = anim;
        uint32_t seq[3];
        for (uint32_t& v : seq) {
            d.call(0x86B5, c, g);
            v = g.readWord(obj + 0xD);
        }
        check(seq[0] == 0x11 && seq[1] == 0x22 && seq[2] == 0x11 && c.ar[6] == anim + 1,
              "0x86B5: próximo quadro da tabela AR6; o terminador negativo volta a *+AR7(C)");
    }
}

/// Rodas simples: 0xAEA8
void romSimpleWheels(const std::optional<std::filesystem::path>& dir) {
    {
        MemoryBus g;
        g.loadProgramRom(*dir, {"cusa-l41.u10", "cusa-l41.u11", "cusa-l41.u12", "cusa-l41.u13"});
        g.hardwareReset();
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        math::registerTrig(d);
        game::registerCarWheels(d);
        auto wf = [&](uint32_t a, double v) { g.writeWord(a, doubleToTms(v)); };
        const uint32_t obj = 0x1F000, dd = 0x1F100, pcb = 0x1F200, p1 = 0x1F300, p2 = 0x1F340;
        g.writeWord(obj + 0x1B, dd); g.writeWord(obj + 0x1A, p1); g.writeWord(obj + 0x1C, 0);
        g.writeWord(p1, p2); g.writeWord(p2, 0);
        for (uint32_t i = 0; i < 9; ++i) wf(p2 + 7 + i, 7.0);
        wf(dd + 0x26, 10.0); g.writeWord(0xE5EC, 0x5577);
        Tms32031State c;
        c.ar[4] = obj; c.ar[7] = pcb;
        d.call(0xAEA8, c, g);
        const double spin = tmsToDouble(g.readWord(pcb + 0xE));
        check(std::fabs(tmsToDouble(g.readWord(p1 + 7 + 4)) - std::cos(spin)) < 1e-3 && spin > 0.19 &&
                  tmsToDouble(g.readWord(p2 + 7)) == 7.0 && c.ar[2] == 3 && c.ret == 0xAEAD,
              "0xAEA8: rotX(giro) em todas as peças menos a última; dorme 3");
    }
}

/// Pisca de paleta: 0x1F2A / 0x1F31
void romPaletteBlink(const std::optional<std::filesystem::path>& dir) {
    {
        MemoryBus g;
        g.loadProgramRom(*dir, {"cusa-l41.u10", "cusa-l41.u11", "cusa-l41.u12", "cusa-l41.u13"});
        g.hardwareReset();
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        video::registerPaletteBlink(d);
        Tms32031State c;
        c.r[0] = 0x300;
        d.call(0x1F2A, c, g);
        const bool first = c.ar[2] == 2 && c.ar[4] == 0x1F44 && c.ar[6] == 0x300 && c.ret == 0x1F31;
        d.call(0x1F31, c, g);
        d.call(0x1F31, c, g);
        d.call(0x1F31, c, g);
        uint32_t sends = 0, lastDst = 0;
        for (const auto& e : pend.calls) if (e.addr == 0x9F5C) ++sends;
        lastDst = c.r[2];
        check(first && c.ar[2] == 4 && c.ar[4] == 0x1F3E && sends == 4 && lastDst == 0x300 + 0xFB,
              "0x1F2A: 4 passos de 5 cores (2/4/2/4 quadros) e volta ao início da tabela");
    }
}

/// Aritmética float da ROM: 0xA509, 0xA5F1, 0x931A, 0x9308, 0x941E
void romFloatMath(const std::optional<std::filesystem::path>& dir) {
    {
        MemoryBus g;
        g.loadProgramRom(*dir, {"cusa-l41.u10", "cusa-l41.u11", "cusa-l41.u12", "cusa-l41.u13"});
        g.hardwareReset();
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        math::registerFloatMath(d);
        Tms32031State c;
        auto div = [&](double a, double b) {
            c.r[0] = doubleToTms(a); c.r[1] = doubleToTms(b);
            d.call(0xA509, c, g);
            return tmsToDouble(c.r[0]);
        };
        const bool divOk = std::fabs(div(10, 4) - 2.5) < 1e-5 && std::fabs(div(-10, 4) + 2.5) < 1e-5 &&
                           std::fabs(div(10, -4) + 2.5) < 1e-5 && std::fabs(div(1, 3) - 1.0 / 3) < 1e-6;
        c.r[2] = doubleToTms(16.0); d.call(0xA5F1, c, g);
        const double r16 = tmsToDouble(c.r[0]);
        c.r[2] = doubleToTms(2.0); d.call(0xA5F1, c, g);
        const double r2 = tmsToDouble(c.r[0]);
        c.r[2] = doubleToTms(-1.0); d.call(0xA5F1, c, g);
        check(divOk && std::fabs(r16 - 4.0) < 1e-4 && std::fabs(r2 - std::sqrt(2.0)) < 1e-5 &&
                  tmsToDouble(c.r[0]) == -1.0,
              "0xA509/0xA5F1: divisão e raiz por Newton-Raphson");
        // RND arredonda ao mais próximo: x / 1 volta exatamente x (com truncamento saía x · 0,99999994), e 0 / 0 = 0.
        // A subida da placa do recorde (0x34BE) depende disso: o último passo divide a sobra por 0.
        check(div(1234.5678, 1.0) == tmsToDouble(doubleToTms(1234.5678)) && div(-7.25, 1.0) == -7.25 &&
                  div(0.0, 0.0) == 0.0 && tmsRound(1.0 - std::ldexp(1.0, -32)) == 1.0,
              "0xA509: x / 1 = x exato e 0 / 0 = 0 (RND ao mais próximo, não truncado)");
        double worst = 0.0, worstRef = 0.0;
        for (double a = -3.0; a <= 3.0; a += 0.5)
            for (double b = -3.0; b <= 3.0; b += 0.5) {
                if (a == 0.0 && b == 0.0) continue;
                c.r[2] = doubleToTms(a); c.r[3] = doubleToTms(b);
                d.call(0x931A, c, g);
                // compara pelo seno e cosseno (independe da convenção de faixa)
                const double got = tmsToDouble(c.r[0]);
                double e1 = std::fabs(std::sin(got) - std::sin(std::atan2(a, b))) + std::fabs(std::cos(got) - std::cos(std::atan2(a, b)));
                double e2 = std::fabs(std::sin(got) - std::sin(std::atan2(b, a))) + std::fabs(std::cos(got) - std::cos(std::atan2(b, a)));
                worst = std::max(worst, e1);
                worstRef = std::max(worstRef, e2);
            }
        std::printf("  atan2: erro máx. atan2(R2,R3)=%.4f atan2(R3,R2)=%.4f\n", worst, worstRef);
        check(std::min(worst, worstRef) < 0.02, "0x931A: atan2 pela tabela da ROM");
        c.r[2] = doubleToTms(4.0); d.call(0x9308, c, g);
        const double w1 = tmsToDouble(c.r[2]);
        c.r[2] = doubleToTms(-1.0); d.call(0x9313, c, g);
        check(std::fabs(w1 - (4.0 - 2 * kPi)) < 1e-3 && std::fabs(tmsToDouble(c.r[2]) - (2 * kPi - 1.0)) < 1e-3,
              "0x9308/0x9313: ângulo em (−π, π] e [0, 2π)");
        for (uint32_t i = 0; i < 9; ++i) g.writeWord(0x30000 + i, doubleToTms(static_cast<double>(i + 1)));
        g.writeWord(0x30010, doubleToTms(1.0)); g.writeWord(0x30011, doubleToTms(0.0)); g.writeWord(0x30012, doubleToTms(2.0));
        c.r[2] = 0x30000; c.ar[2] = 0x30010; c.r[3] = 0x30020;
        d.call(0x941E, c, g);
        check(tmsToDouble(g.readWord(0x30020)) == 7.0 && tmsToDouble(g.readWord(0x30021)) == 16.0 &&
                  tmsToDouble(g.readWord(0x30022)) == 25.0,
              "0x941E: matriz × vetor");
    }
}

} // namespace

void runRomGameTests(const std::optional<std::filesystem::path>& dir) {
    romCarWheels(dir);
    romBoxCollision(dir);
    romCollisionResponse(dir);
    romSceneryCollision(dir);
    romSmoke(dir);
    romRoamingObjects(dir);
    romSpecialCar(dir);
    romEnding(dir);
    romRoadsideEventsAndCredits(dir);
    romTrackProps(dir);
    romObjectFree();
    romPricingAndNotice(dir);
    romCoinsStartAndFlow(dir);
    romCourseSelect(dir);
    romRaceEntry(dir);
    romPlayerCar(dir);
    romHudAndRaceEvents(dir);
    romCrashesAndEffects(dir);
    romHiscoreAndPodium(dir);
    romSimpleWheels(dir);
    romPaletteBlink(dir);
    romFloatMath(dir);
}
