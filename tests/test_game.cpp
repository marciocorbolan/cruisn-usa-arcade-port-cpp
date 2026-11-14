/**
 * @file test_game.cpp
 * @brief Jogo (sem ROM): proximidade, carros da CPU, dinâmica e IA, posição, tráfego, física, chão, trechos, demo e reinício.
 */
#include "test_support.hpp"

using namespace rea::core;
using namespace rea::test;

namespace {

/// Proximidade dos carros: 0x260E (pares) e 0x2207 (carro × objetos)
void testCarProximity() {
    {
        MemoryBus pb;
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        game::registerCarProximity(d);
        std::vector<std::pair<uint32_t, uint32_t>> pairs;
        bool hit = false;
        d.add(0x2863, [&](Tms32031State& t, MemoryBus&, ISubroutines&) {
            pairs.push_back({t.ar[0], t.ar[1]});
            if (hit) t.st |= Tms32031State::kStCarry; else t.st &= ~Tms32031State::kStCarry;
        });
        auto node = [&](uint32_t a, uint32_t next, double x, double z, int r, uint32_t flags) {
            pb.writeWord(a + 0x20, next); pb.writeWord(a + 1, doubleToTms(x)); pb.writeWord(a + 3, doubleToTms(z));
            pb.writeWord(a + 0x1D, static_cast<uint32_t>(r)); pb.writeWord(a + 0xE, flags);
        };
        // carros: A(0,0,r10) → B(15,0,r10) → C(100,0,r10) → D(5,0, sem colisão)
        node(0x1000, 0x1100, 0, 0, 10, 0); node(0x1100, 0x1200, 15, 0, 10, 0);
        node(0x1200, 0x1300, 100, 0, 10, 0); node(0x1300, 0, 5, 0, 10, 0x10);
        pb.writeWord(0xE404, 0x1000);
        Tms32031State c;
        d.call(0x260E, c, pb);
        const bool onlyNear = pairs == std::vector<std::pair<uint32_t, uint32_t>>{{0x1000, 0x1100}};
        hit = true; pairs.clear();
        d.call(0x260E, c, pb);
        bool response = false;
        for (const auto& e : pend.calls) response |= e.addr == 0x2645;
        check(onlyNear && response && pairs.size() == 1, "0x260E: só pares com círculos sobrepostos; 1ª colisão → 0x2645");

        // objetos: célula 0x2000 → O1(3,4) → O2(50,0) ; carro A raio 10
        pb.writeWord(0x2000, 0x3000); node(0x3000, 0x3100, 3, 4, 0, 0); node(0x3100, 0, 50, 0, 0, 0);
        pend.calls.clear();
        c.ar[0] = 0x1000; c.ar[1] = 0x2000;
        d.call(0x2207, c, pb);
        int collisions = 0;
        for (const auto& e : pend.calls) collisions += e.addr == 0x221F;
        check(collisions == 1, "0x2207: carro × objetos dentro do raio do carro → 0x221F");
    }
}

/// Carros da CPU: 0x9076 (vetor no rumo) e 0x536C (nó da pista pelo índice)
void testDroneCarHelpers() {
    {
        MemoryBus mb;
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        game::registerDroneCar(d);
        // seno/cosseno exatos no lugar da tabela da ROM
        d.add(0x92DF, [](Tms32031State& t, MemoryBus&, ISubroutines&) { t.r[0] = doubleToTms(std::cos(tmsToDouble(t.r[2]))); });
        d.add(0x92E2, [](Tms32031State& t, MemoryBus&, ISubroutines&) { t.r[0] = doubleToTms(std::sin(tmsToDouble(t.r[2]))); });
        Tms32031State c;
        c.r[0] = doubleToTms(100.0);
        c.r[2] = doubleToTms(kPi / 2);
        c.r[3] = 0x1234;
        d.call(0x9076, c, mb);
        check(std::fabs(tmsToDouble(c.r[0]) + 100.0) < 0.01 && std::fabs(tmsToDouble(c.r[1])) < 0.01 && c.r[3] == 0x1234,
              "0x9076: R0 = −d·sen(R2), R1 = d·cos(R2)");
        mb.writeWord(0xAA2F, 0x5000);
        for (uint32_t i = 0; i < 6; ++i) mb.writeWord(0x5000 + i * 4 + 3, 0x100 + i * 2);
        c.ar[7] = 0x6000;
        c.r[2] = 0x103;
        d.call(0x536C, c, mb);
        check(mb.readWord(0x6000 + 0x12) == 0x5000 + 2 * 4 && mb.readWord(0x6000 + 0x13) == 0x104,
              "0x536C: [AR7+0x12] = nó de índice ≥ R2, [AR7+0x13] = índice");
    }
}

/// Dinâmica/IA/largada: 0x2E73, 0x2C92, 0x94A7, 0x66A7, 0x8EDB, 0x1CFB
void testDynamicsAiStart() {
    {
        MemoryBus mb;
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        math::registerRoutines(d);
        game::registerCarDynamics(d);
        game::registerDroneAi(d);
        game::registerRaceStart(d);
        Tms32031State c;
        auto wf = [&](uint32_t a, double v) { mb.writeWord(a, doubleToTms(v)); };
        const uint32_t dd = 0x3200;
        c.ar[5] = dd;
        // marcha 2, acelerador 1,0: limites 23 e 41 de rotação
        wf(dd + 0x24, 1.0); mb.writeWord(dd + 0x38, 2);
        wf(dd + 0x39, 45.0);
        d.call(0x2E73, c, mb);
        const uint32_t up = c.r[0];
        wf(dd + 0x39, 10.0);
        d.call(0x2E73, c, mb);
        const uint32_t down = c.r[0];
        mb.writeWord(dd + 0x38, 1);
        d.call(0x2E73, c, mb);
        check(up == 3 && down == 1 && c.r[0] == 1, "0x2E73: troca de marcha pela rotação, limitada a 1..4");

        // rotação = velocidade × [[2C46] + marcha], até 47
        mb.writeWord(0x2C46, 0x3300); wf(0x3302, 0.5);
        mb.writeWord(dd + 0x38, 2); wf(dd + 0x26, 60.0);
        d.call(0x2C92, c, mb);
        const double rpm = tmsToDouble(mb.readWord(dd + 0x39));
        wf(dd + 0x26, 200.0);
        d.call(0x2C92, c, mb);
        check(rpm == 30.0 && tmsToDouble(mb.readWord(dd + 0x39)) == 47.0, "0x2C92: rotação = velocidade · fator da marcha, máx. 47");

        // 0x94A7: diferença de ângulo dobrada em ±π
        wf(0x9303, 3.14159265); wf(0x9304, 6.2831853);
        c.r[0] = doubleToTms(3.0); c.r[2] = doubleToTms(-3.0);
        d.call(0x94A7, c, mb);
        check(std::fabs(tmsToDouble(c.r[0]) - (6.0 - 6.2831853)) < 1e-4, "0x94A7: R0 − R2 dobrado em (−π, π]");

        // 0x66A7: trecho 7 na janela com o bit 7 → bloqueado
        mb.writeWord(0x3F69, 0x5000); mb.writeWord(0xE46C, 1);
        mb.writeWord(0x5000 + 4, 3); mb.writeWord(0x5005 + 4, 7); mb.writeWord(0x5005 + 3, 0x80);
        c.ar[2] = 0x6000; mb.writeWord(0x6000 + 0x1E, 0x705);
        d.call(0x66A7, c, mb);
        const uint32_t blocked = c.r[0];
        mb.writeWord(0x6000 + 0x1E, 0x905);
        d.call(0x66A7, c, mb);
        check(blocked == 1 && c.r[0] == 0, "0x66A7: bit 7 do trecho na janela; fora da janela = 0");

        // 0x8EDB: 0 ≤ R0 < AR2
        bool inRange = true;
        for (int i = 0; i < 50; ++i) {
            c.ar[2] = 7;
            d.call(0x8EDB, c, mb);
            inRange = inRange && c.r[0] < 7;
        }
        check(inRange, "0x8EDB: aleatório em [0, AR2)");

        // 0x1CFB: "GO" libera o grid e morre
        mb.writeWord(0xE5EC, 0x33BB); mb.writeWord(0xC8C5, 2); mb.writeWord(0xB223, 0); mb.writeWord(0xC8E3, 0);
        c.ar[6] = 0x7000; c.r[5] = 0x12;
        pend.calls.clear();
        d.call(0x1CFB, c, mb);
        bool died = false;
        for (const auto& e : pend.calls) died = died || e.addr == 0xA896;
        check(mb.readWord(0xE5EC) == 0x5577 && mb.readWord(0xC8C5) == 0x202 && died,
              "0x1CFB: [E5EC] = 0x5577, [C8C5] = R5 | 0x200 sem 0x10 e o processo morre");
    }
}

/// Posição na corrida e numeração: 0x65A5, 0x6628
void testRacePosition() {
    {
        MemoryBus mb;
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        game::registerRaceTraffic(d);
        Tms32031State c;
        auto wf = [&](uint32_t a, double v) { mb.writeWord(a, doubleToTms(v)); };
        const uint32_t me = 0x4000, myD = 0x4100, n1 = 0x4200, n2 = 0x4240, n0 = 0x4280, n3 = 0x42C0, x = 0x4300;
        mb.writeWord(myD + 0x37, n1); mb.writeWord(n1 + 0x1E, 0x105); mb.writeWord(n1 + 0x21, n2);
        mb.writeWord(n0 + 0x1E, 0x104); mb.writeWord(n3 + 0x1E, 0x106);
        wf(n2 + 1, 0.0); wf(n2 + 3, 100.0);
        wf(me + 1, 0.0); wf(me + 3, 0.0); wf(myD + 0x46, 0.0);
        mb.writeWord(x + 0x25, 0);
        // carros: A à frente (nó seguinte), B atrás, C no mesmo nó mais perto do nó seguinte, D outro tipo
        const uint32_t cars[4] = {0x5000, 0x5100, 0x5200, 0x5300};
        const uint32_t nodes[4] = {n3, n0, n1, n1};
        const int32_t dists[4] = {100, -6000, 0, 40000};
        for (int i = 0; i < 4; ++i) {
            const uint32_t o = cars[i], dd = o + 0x80;
            mb.writeWord(o + 0x20, i < 3 ? cars[i + 1] : 0);
            mb.writeWord(o + 0xF, i < 3 ? 0x216 : 0x300);
            mb.writeWord(o + 0x1B, dd); mb.writeWord(o + 0x17, x);
            mb.writeWord(o + 0x1C, static_cast<uint32_t>(dists[i]));
            mb.writeWord(dd + 0x58, 0); mb.writeWord(dd + 0x37, nodes[i]); wf(dd + 0x46, 0.0);
            wf(o + 1, 0.0); wf(o + 3, 50.0);
        }
        mb.writeWord(0xE404, cars[0]);
        c.ar[4] = me; c.ar[5] = myD;
        d.call(0x65A5, c, mb);
        check(mb.readWord(0xE607) == 3 && mb.readWord(0xE5F1) == 2,
              "0x65A5: posição = 1 + carros 0x216 à frente; [E5F1] = carros a −5000..30000");
        mb.writeWord(0xE5F5, 0x7F); mb.writeWord(0xC8CC, 0);
        c.ar[5] = 0x6000;
        d.call(0x6628, c, mb);
        check(mb.readWord(0xE5F5) == 0xA && mb.readWord(0x6000 + 0x59) == 0x8A && mb.readWord(0x6000 + 0x58) == 0,
              "0x6628: número do carro volta a 10 depois de 0x7F; bit 0x80 sem o link");
    }
}

/// Tráfego: 0x9891 (janela de nós) e 0x68F4 (à frente do focado)
void testTrafficWindow() {
    {
        MemoryBus mb;
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        game::registerTrafficCars(d);
        Tms32031State c;
        auto wf = [&](uint32_t a, double v) { mb.writeWord(a, doubleToTms(v)); };
        const uint32_t first = 0x4000, last = 0x4040, mid = 0x4080, pcb = 0x4100;
        mb.writeWord(0xE46E, first); mb.writeWord(0xE470, last);
        mb.writeWord(first + 0x1E, 0x100); mb.writeWord(last + 0x1E, 0x500);
        c.ar[7] = pcb;
        auto win = [&](uint32_t node, uint32_t idx) {
            mb.writeWord(pcb + 0x10, node); mb.writeWord(pcb + 0x13, idx);
            d.call(0x9891, c, mb);
            return c.r[0];
        };
        check(win(mid, 0x300) == 1 && win(first, 0x300) == 0 && win(mid, 0x100) == 0 && win(mid, 0x500) == 0,
              "0x9891: índice estritamente entre o primeiro e o último nó carregado");
        // focado no nó 0x305, carro no 0x306 → carro à frente; carro no 0x304 → atrás
        const uint32_t fObj = 0x5000, fD = 0x5100, nF = 0x5200, nA = 0x5240, nB = 0x5280, car = 0x5300, cd = 0x5380;
        mb.writeWord(0xE876, fObj); mb.writeWord(0xE878, fD); mb.writeWord(fD + 0x37, nF);
        mb.writeWord(nF + 0x1E, 0x305); mb.writeWord(nA + 0x1E, 0x306); mb.writeWord(nB + 0x1E, 0x304);
        c.ar[4] = car; c.ar[5] = cd; c.r[0] = 0x77;
        mb.writeWord(cd + 0x37, nA);
        d.call(0x68F4, c, mb);
        const bool ahead = c.carry();
        mb.writeWord(cd + 0x37, nB);
        d.call(0x68F4, c, mb);
        const bool behind = !c.carry();
        // mesmo nó: decide pela distância ao 2º nó seguinte
        const uint32_t n1 = 0x5400, n2 = 0x5440;
        mb.writeWord(cd + 0x37, nF); mb.writeWord(nF + 0x21, n1); mb.writeWord(n1 + 0x21, n2);
        wf(n2 + 1, 0.0); wf(n2 + 3, 100.0);
        wf(car + 1, 0.0); wf(car + 3, 60.0); wf(fObj + 1, 0.0); wf(fObj + 3, 10.0);
        d.call(0x68F4, c, mb);
        check(ahead && behind && c.carry() && c.r[0] == 0x77, "0x68F4: carry = carro à frente do focado (nó ou distância)");
    }
}

/// Física do carro: 0x67E3 (distância lateral) e 0x1FF6 (pontos de contato)
void testCarPhysics() {
    {
        MemoryBus mb;
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        math::registerFloatMath(d);
        prof::registerRoutines(d);
        track::registerGroundHeight(d);
        game::registerCarPhysics(d);
        Tms32031State c;
        auto wf = [&](uint32_t a, double v) { mb.writeWord(a, doubleToTms(v)); };
        auto rf = [&](uint32_t a) { return tmsToDouble(mb.readWord(a)); };
        // reta do nó (0,0,0) → (0,0,100)
        const uint32_t car = 0x3100, dd = 0x3200, n0 = 0x3400, n1 = 0x3440;
        wf(n0 + 1, 0.0); wf(n0 + 3, 0.0); wf(n1 + 1, 0.0); wf(n1 + 3, 100.0);
        mb.writeWord(n0 + 0x21, n1);
        mb.writeWord(dd + 0x37, n0);
        c.ar[4] = car; c.ar[5] = dd;
        wf(car + 1, 50.0); wf(car + 3, 30.0);
        d.call(0x67E3, c, mb);
        const bool right = c.carry() && std::fabs(tmsToDouble(c.r[0]) + 50.0) < 0.01;
        wf(car + 1, -20.0);
        d.call(0x67E3, c, mb);
        check(right && !c.carry() && std::fabs(tmsToDouble(c.r[0]) - 20.0) < 0.01 && c.ar[5] == dd,
              "0x67E3: R0 = distância com sinal à reta do nó, carry se < 0");

        // chão plano y = 0 (quad ±1000), carro em y = −50 com 3 pontos à frente/atrás
        mb.writeWord(0x4F, 0x5000); mb.writeWord(0x4C, 0x5101); mb.writeWord(0x51, 0x5111);
        mb.writeWord(0x1F86, 0xE577); mb.writeWord(0x1F87, 0xE57B); mb.writeWord(0x1F8A, 0xE584);
        const uint32_t g = 0x3000;
        for (int i = 0; i < 9; ++i) wf(g + 4 + i, (i % 4 == 0) ? 1.0 : 0.0);
        mb.writeWord(g + 0xD, 0x4000); mb.writeWord(g + 0x1D, 1000); mb.writeWord(g + 0x20, 0);
        mb.writeWord(g + 0xF, 0x300);
        wf(g + 1, 0.0); wf(g + 2, 0.0); wf(g + 3, 0.0);
        mb.writeWord(0x4001, 3);
        const int v[4][2] = {{-1000, -1000}, {1000, -1000}, {1000, 1000}, {-1000, 1000}};
        for (int i = 0; i < 4; ++i) {
            mb.writeWord(0x4002 + 2 * i, static_cast<uint32_t>(v[i][0]) & 0xFFFFu);
            mb.writeWord(0x4003 + 2 * i, static_cast<uint32_t>(v[i][1]));
        }
        mb.writeWord(0x400B, 0x00010203);
        mb.writeWord(0xE403, g); mb.writeWord(0xE406, 0);
        mb.writeWord(0xC92F, 1);
        for (int i = 0; i < 9; ++i) wf(car + 4 + i, (i % 4 == 0) ? 1.0 : 0.0);
        wf(car + 1, 0.0); wf(car + 2, -50.0); wf(car + 3, 0.0); mb.writeWord(car + 0x1D, 100);
        const double off[5][3] = {{0, 0, 100}, {-50, 0, -100}, {50, 0, -100}, {-50, 0, 100}, {50, 0, 100}};
        for (int i = 0; i < 5; ++i)
            for (int k = 0; k < 3; ++k) wf(dd + 0x47 + 3 * i + k, off[i][k]);
        for (int i = 0; i < 5; ++i) wf(dd + 6 * i + 4, 0.0);
        c.ar[4] = car;
        c.r[3] = dd;
        d.call(0x1FF6, c, mb);
        const double m11 = rf(car + 4 + 4);
        check(std::fabs(rf(car + 2) + 30.0) < 0.01 && std::fabs(rf(dd + 3) - 50.0) < 0.01 && mb.readWord(dd + 5) == g && mb.readWord(dd + 0x1E) == 0x300 &&
                  std::fabs(rf(dd + 4) - 20.0) < 0.01 && mb.readWord(dd + 0x20) == 0 && std::fabs(std::fabs(m11) - 1.0) < 0.01 &&
                  c.ar[4] == car,
              "0x1FF6: pontos caem 4·[C92F] (+16·[C92F] fora de 0x330) rumo ao chão, chão do ponto 0 em +5, y do objeto e matriz plana");
        check(pend.calls.empty(), "0x1FF6/0x67E3: sem rotinas pendentes");
    }
}

/// Altura do chão: 0x1F8B (lista [E403]) → 0x20B4 → 0x20FF/0x9466
void testGroundHeight() {
    {
        MemoryBus mb;
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        math::registerFloatMath(d);
        track::registerGroundHeight(d);
        Tms32031State c;
        auto wf = [&](uint32_t a, double v) { mb.writeWord(a, doubleToTms(v)); };
        mb.writeWord(0x4F, 0x5000); mb.writeWord(0x4C, 0x5101); mb.writeWord(0x51, 0x5111);
        mb.writeWord(0x1F86, 0xE577); mb.writeWord(0x1F87, 0xE57B);
        // objeto em (100, 50, 200), matriz identidade, raio 1000
        const uint32_t o = 0x3000;
        wf(o + 1, 100.0); wf(o + 2, 50.0); wf(o + 3, 200.0);
        for (int i = 0; i < 9; ++i) wf(o + 4 + i, (i % 4 == 0) ? 1.0 : 0.0);
        mb.writeWord(o + 0xD, 0x4000); mb.writeWord(o + 0x1D, 1000); mb.writeWord(o + 0x20, 0);
        // quad inclinado: y = 10 + z/10
        mb.writeWord(0x4001, 3);
        const int v[4][3] = {{-100, 0, -100}, {100, 0, -100}, {100, 20, 100}, {-100, 20, 100}};
        for (int i = 0; i < 4; ++i) {
            mb.writeWord(0x4002 + 2 * i, (static_cast<uint32_t>(v[i][1]) << 16) | (static_cast<uint32_t>(v[i][0]) & 0xFFFFu));
            mb.writeWord(0x4003 + 2 * i, static_cast<uint32_t>(v[i][2]));
        }
        mb.writeWord(0x400B, 0x00010203);
        mb.writeWord(0xE403, o); mb.writeWord(0xE406, 0);
        wf(0x6000, 100.0); wf(0x6001, 0.0); wf(0x6002, 200.0);
        c.ar[4] = 0x6000;
        d.call(0x1F8B, c, mb);
        check(c.carry() && std::fabs(tmsToDouble(c.r[0]) - 60.0) < 0.01 && c.ar[4] == 0x6000,
              "0x1F8B: ponto sobre o quad → carry = 1, R0 = altura relativa do plano");
        wf(0x6000, 250.0);
        d.call(0x1F8B, c, mb);
        check(!c.carry() && tmsToDouble(c.r[0]) == 0.0, "0x1F8B: fora do polígono → carry = 0, R0 = 0");
        check(pend.calls.empty(), "0x1F8B: sem rotinas pendentes");
    }
}

/// Fluxo de trechos: lista ordenada 0x4197 e liberação 0x41D1
void testTrackStream() {
    {
        MemoryBus mb;
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        track::registerTrackStream(d);
        Tms32031State c;
        mb.writeWord(0x3F68, 0xE407);
        mb.writeWord(0xE407, 0);
        const uint32_t nodes[] = {0x2000, 0x2100, 0x2200};
        const uint32_t idx[] = {0x305, 0x301, 0x303};
        for (int i = 0; i < 3; ++i) {
            mb.writeWord(nodes[i] + 0x1E, idx[i]);
            c.ar[4] = nodes[i];
            d.call(0x4197, c, mb);
        }
        std::vector<uint32_t> order;
        for (uint32_t n = mb.readWord(0xE407); n != 0 && order.size() < 5; n = mb.readWord(n + 0x21)) order.push_back(n);
        check(order == std::vector<uint32_t>{0x2100, 0x2200, 0x2000} && mb.readWord(0x2200 + 0x17) == 0x2100 &&
                  mb.readWord(0x2100 + 0x17) == 0,
              "0x4197: nós do trecho em ordem de índice (+0x21 próximo, +0x17 anterior)");

        // 0x41D1: lista [0x40] = A(marca 0x5AA) → B(0x6AA) → C(0x5AA); [0x41] vazia
        mb.writeWord(0x40, 0x3000); mb.writeWord(0x41, 0x3010);
        mb.writeWord(0x3000, 0x4000); mb.writeWord(0x4000, 0x4100); mb.writeWord(0x4100, 0x4200);
        mb.writeWord(0x4200, 0); mb.writeWord(0x3010, 0);
        mb.writeWord(0x4000 + 0x1F, 0x5AA); mb.writeWord(0x4100 + 0x1F, 0x6AA); mb.writeWord(0x4200 + 0x1F, 0x5AA);
        mb.writeWord(0x4000 + 0xE, 1u << 23);  // processo associado → 0x70FB
        mb.writeWord(0xC983, 0); mb.writeWord(0xC989, 10);
        c.ar[2] = 0x5AA;
        d.call(0x41D1, c, mb);
        bool killed = false;
        for (const auto& e : pend.calls) killed |= e.addr == 0x70FB;
        check(mb.readWord(0x3000) == 0x4100 && mb.readWord(0x4100) == 0 && mb.readWord(0xC989) == 12 &&
                  mb.readWord(0xC983) == 0x4200 && mb.readWord(0x4200) == 0x4000 && killed && c.ar[2] == 0x5AA,
              "0x41D1: libera os objetos do trecho (pool [C983], contador [C989], bit 23 → 0x70FB)");
    }
}

/// Demo do atrato: 0x58E8, 0x559E (roteiro), 0x5943 (câmera), 0x585A/0x57F0
void testAttractDemo() {
    {
        MemoryBus mb;
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        game::registerAttractDemo(d);
        game::registerDemoCamera(d);
        Tms32031State c;
        auto wrap = [&](double v) { c.r[0] = doubleToTms(v); d.call(0x58E8, c, mb); return tmsToDouble(c.r[0]); };
        check(std::fabs(wrap(4.0) - (4.0 - 6.279297)) < 1e-4 && std::fabs(wrap(-4.0) - (-4.0 + 6.279297)) < 1e-4 &&
                  wrap(1.0) == 1.0,
              "0x58E8: ângulo em (−π, π] por ±6,279");

        // roteiro: [passo A: início 0x6000, duração 2, por quadro 0x6001] [terminador]
        std::vector<uint32_t> log;
        for (uint32_t f : {0x6000u, 0x6001u})
            d.add(f, [&log, f](Tms32031State&, MemoryBus&, ISubroutines&) { log.push_back(f); });
        d.add(0x6001, [&log](Tms32031State& t, MemoryBus& b, ISubroutines&) {
            log.push_back(0x6001);
            t.ar[5] -= b.readWord(0xC92F);
        });
        int sleeps = 0;
        d.add(0xA874, [&](Tms32031State&, MemoryBus&, ISubroutines&) { ++sleeps; });
        d.add(0x5943, [](Tms32031State&, MemoryBus&, ISubroutines&) {});
        const uint32_t script = 0x2000;
        const uint32_t words[] = {0x6000, 2, 0x6001, 0, 0};
        for (uint32_t i = 0; i < 5; ++i) mb.writeWord(script + i, words[i]);
        mb.writeWord(0xC92F, 1);
        mb.writeWord(0xC941, 3);
        c = Tms32031State{};
        c.ar[7] = 0x3000;
        // 1º quadro: início + duração; quadros seguintes: por quadro até AR5 ≤ 0; terminador repete
        mb.writeWord(0xE876, 0x3100); mb.writeWord(0x3100 + 0xA, 0x3200); mb.writeWord(0x3100 + 9, 0x3300);
        mb.writeWord(0x3200 + 0xA, 0x3400);
        d.add(0x5620, [script](Tms32031State& t, MemoryBus&, ISubroutines&) { t.ar[6] = script; });
        d.call(0x55B4, c, mb);                          // foco + 1º passo
        const bool first = log == std::vector<uint32_t>{0x6000} && c.ar[5] == 2 && c.ret == 0x55C5 &&
                           mb.readWord(0xE876) == 0x3300 && mb.readWord(0xE878) == 0x3400;
        for (int i = 0; i < 3; ++i) d.call(0x55C5, c, mb); // 2 quadros e a troca (terminador → repete)
        check(first && log == std::vector<uint32_t>{0x6000, 0x6001, 0x6001, 0x6000} && c.ar[6] == script + 2 &&
                  mb.readWord(0xC941) == 0,
              "0x559E: roteiro roda início, por quadro até AR5 ≤ 0, terminador repete o passo");

        // 0x5943: matriz suavizada 0,2·antiga + 0,8·nova; com [AR7+D] direto
        d.add(0x5943, game::applyDemoCamera);
        mb.writeWord(game::demo_vars::kCamPosPtr, 0x4000);
        mb.writeWord(game::demo_vars::kCamMatPtr, 0x4100);
        for (uint32_t i = 0; i < 9; ++i) {
            mb.writeWord(0x4100 + i, doubleToTms(10.0));
            mb.writeWord(0x3000 + 0x1D + i, doubleToTms(20.0));
        }
        mb.writeWord(0x3000 + 0x17, doubleToTms(5.0));
        mb.writeWord(0x3000 + 0xD, 0);
        c.ar[7] = 0x3000;
        d.call(0x5943, c, mb);
        const double smooth = tmsToDouble(mb.readWord(0x4100));
        mb.writeWord(0x3000 + 0xD, 1);
        d.call(0x5943, c, mb);
        check(std::fabs(smooth - 18.0) < 0.01 && tmsToDouble(mb.readWord(0x4100)) == 20.0 &&
                  tmsToDouble(mb.readWord(0x4000)) == 5.0 && mb.readWord(0x3000 + 0xD) == 0,
              "0x5943: câmera do processo; matriz suavizada (0,2/0,8) ou direta com [AR7+0xD]");

        // pista: nós (x,y,z,índice) em [AA2F]; 0x585A acha o 1º índice ≥ R2
        mb.writeWord(0xAA2F, 0x5000);
        for (uint32_t i = 0; i < 8; ++i) mb.writeWord(0x5000 + i * 4 + 3, i * 10);
        c.r[2] = 25;
        d.call(0x585A, c, mb);
        check(c.ar[4] == 0x5000 + 3 * 4, "0x585A: nó da pista pelo índice");

        // 0x57F0: carros (tipo 0x216) em [0x57]; nó via [[car+0x17]+0x12] (+0x25 ≠ 0)
        auto car = [&](uint32_t a, uint32_t next, uint32_t node) {
            mb.writeWord(a + 0x20, next); mb.writeWord(a + 0xF, 0x216); mb.writeWord(a + 0x17, a + 0x40);
            mb.writeWord(a + 0x40 + 0x12, node); mb.writeWord(a + 0x40 + 0x25, 1);
        };
        car(0x6000, 0x6100, 0x5010); car(0x6100, 0x6200, 0x5030); car(0x6200, 0, 0x5020);
        mb.writeWord(0x57, 0x5800);   // cabeça da lista: [[0x57]] = 1º objeto
        mb.writeWord(0x5800, 0x6000);
        c.r[2] = 2;
        d.call(0x57F0, c, mb);
        check(c.ar[4] == 0x6200 && mb.readWord(0x3000 + 0x2C) == 0x6100 && mb.readWord(0x3000 + 0x2D) == 0x6200,
              "0x57F0: o 2º carro mais adiantado na pista");
    }
}

/// (Re)início do jogo (0x9503) e casos
void testGameRestart() {
    {
        using namespace proc::proc_vars;
        namespace pcb = proc::pcb;
        MemoryBus gb;
        gb.writeWord(kActiveHeadPtr, kActiveHead);
        gb.writeWord(kFreeHeadPtr, kFreeHead);
        gb.writeWord(kPoolPtr, 0xEBE7);
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        proc::Scheduler sched;
        proc::registerRoutines(d, sched);
        game::registerRoutines(d);
        Tms32031State c;
        d.call(0xA8F5, c, gb);
        auto setup = [&]() {
            const uint32_t tbl[8] = {0x9563, 0x9572, 0x95B6, 0x95B4, 0x9596, 0x95A4, 0x9587, 0x9579};
            for (uint32_t i = 0; i < 8; ++i) gb.writeWord(0x955B + i, tbl[i]);
            const uint32_t pcs[15] = {0x903B, 0x154D, 0x3859, 0x9563, 0x9563, 0xA93B, 0xA133, 0xA9C8,
                                      0x9563, 0x559E, 0x1E26, 0x9563, 0xA1EA, 0xA133, 0x1775};
            for (uint32_t i = 0; i < 15; ++i) gb.writeWord(0xC701 + i, pcs[i]);
        };
        auto pcsActive = [&]() {
            std::vector<uint32_t> v;
            for (uint32_t p = gb.readWord(kActiveHead); p != 0 && v.size() < 0x100; p = gb.readWord(p))
                v.push_back(gb.readWord(p + pcb::kPc));
            return v;
        };
        setup();
        gb.writeWord(0x809900, 0x1234);
        gb.writeWord(0x1000, 0xABCD);
        gb.writeWord(0xE575, 0x77);
        gb.writeWord(0xC8CD, 0xFF);
        c.ar[2] = 0xFFFFFFFFu;
        c.ret = 0x4BA3;
        d.call(0x9503, c, gb);
        auto pending = [&](uint32_t a) {
            for (const auto& e : pend.calls) if (e.addr == a) return true;
            return false;
        };
        // 0x4E35 está pendente aqui: a RAM principal (inclusive a tabela) segue como estava
        check(c.ar[7] == 0x4BA3 && gb.readWord(0x809900) == 0 && gb.readWord(0x401000) == 0xABCD &&
                  gb.readWord(0xE575) == 0x77 && pending(0x9166) && gb.readWord(0xC8D1) == 1 && pending(0x95F5),
              "0x9503: AR7 = retorno; zera a RAM interna; copia 0x0000-0x10000 para 0x400000; preserva E575");
        check(gb.readWord(0x809812) == doubleToTms(0.70703125) && gb.readWord(0x809800) == doubleToTms(0.0) &&
                  gb.readWord(0xE3FD) == doubleToTms(35.0) && gb.readWord(0xC93F) == 1,
              "0x9503: câmera zerada, 0,70703 em 809812/13, [E3FD] = 35,0 e pede troca de página");
        check(pcsActive() == std::vector<uint32_t>{0x9563, 0x3859, 0x903B} && gb.readWord(0xC8C5) == 0x22 &&
                  gb.readWord(0xC943) == 0xFFFFFFFFu && gb.readWord(0xC8CD) == 0xF1,
              "0x9503 com AR2 = -1 (RESET): processos 0x903B, 0x3859 e 0x9563; modo 0x22; [C8CD] &= ~0xE");

        // Processo 0x9563: espera o outro gabinete nos modos 5-7
        uint32_t peer = 0;
        for (uint32_t p = gb.readWord(kActiveHead); p != 0; p = gb.readWord(p))
            if (gb.readWord(p + pcb::kPc) == 0x9563) peer = p;
        gb.writeWord(0xC8D6, 0x13);
        gb.writeWord(0xC8DD, 0);
        sched.run(c, gb, d);
        check(gb.readWord(peer + pcb::kPc) == 0x9565 && gb.readWord(peer + pcb::kSleep) == 1,
              "0x9563: adormece 1 quadro e retoma em 0x9565");
        gb.writeWord(0xC8D6, 0x16);
        pend.calls.clear();
        sched.run(c, gb, d);
        check(gb.readWord(0xC8E8) == static_cast<uint32_t>(-7) && c.jump == 0x4C1B &&
                  pcsActive() == std::vector<uint32_t>{0x154D, 0x903B} && pending(0x9166),
              "0x9565: outro gabinete no modo 6 -> [C8E8] = -7; 0x1E17 reinicia no caso -7 (processo 0x154D) e salta para 0x4C1B");
        c.jump = 0;
        // 0x1E1C: ciclo de atração -1 -> -2 -> -3 -> -4 -> -1
        gb.writeWord(0xC8E8, static_cast<uint32_t>(-4));
        d.call(0x1E1C, c, gb);
        check(gb.readWord(0xC8E8) == 0xFFFFFFFFu && c.jump == 0x4C1B && gb.readWord(0xC8C5) == 0x22,
              "0x1E1C: [C8E8] = -4 - 1 < -4 -> volta a -1 (atração) e salta para 0x4C1B");
        c.jump = 0;
        d.call(0x1E1C, c, gb);
        check(gb.readWord(0xC8E8) == static_cast<uint32_t>(-2) && gb.readWord(0xC8C5) == 2,
              "0x1E1C: -1 -> -2 (caso 0x9587, modo 2)");
        c.jump = 0;

        // AR2 = 1: 0x95C4 cria 0x1775 com ID 0x110
        d.call(0xA8F5, c, gb);
        setup();
        c.ar[2] = 1;
        d.call(0x9503, c, gb);
        check(pcsActive() == std::vector<uint32_t>{0x1775, 0x903B} && gb.readWord(0xC8E9) == 1 && pending(0x92AD),
              "0x9503 com AR2 = 1: 0x95C4 (0x92AD, [C8E9] = 1, processo 0x1775)");
    }
}

} // namespace

void runGameTests() {
    testCarProximity();
    testDroneCarHelpers();
    testDynamicsAiStart();
    testRacePosition();
    testTrafficWindow();
    testCarPhysics();
    testGroundHeight();
    testTrackStream();
    testAttractDemo();
    testGameRestart();
}
