/**
 * @file test_scene.cpp
 * @brief Cena: lista de objetos, visibilidade, pools, buscas por tipo, objetos de cena, matrizes e pipeline 3D.
 */
#include "test_support.hpp"

using namespace rea::core;
using namespace rea::test;

namespace {

/// Lista de objetos (0x7187 / 0x71B3 / 0x71BF / 0x71E4)
void testObjectList() {
    {
        MemoryBus ob;
        ObjectListFixture f{ob};
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        d.add(0x7187, scene::sortObjects);  // só a ordenação: o resto fica registrado
        Tms32031State c;
        for (int i = 0; i < 6; ++i) f.set(i, 0x400, std::vector<int>{5, 9, 1, 7, -3, 9}[i]);
        f.build({0, 1, 2, 3, 4, 5});
        d.call(0x7187, c, ob);
        check(f.order() == std::vector<int>({1, 5, 3, 0, 2, 4}) && ob.readWord(0xC93F) == 1,
              "0x7187: [C93F] = 1 e ordem decrescente pela chave +0x1C (estável: 9a antes de 9b)");
        std::vector<uint32_t> calls;
        for (const auto& e : pend.calls) calls.push_back(e.addr);
        check(calls == std::vector<uint32_t>({0x71B3, 0x71E4, 0xAF75, 0x71BF, 0x87B8, 0xAE05}),
              "0x71A9: [C8C5] & 0x20 == 0 -> 71B3, 71E4, AF75, 71BF, 87B8, AE05");
        pend.calls.clear();
        ob.writeWord(0xC8C5, 0x20);
        d.call(0x7187, c, ob);
        check(pend.calls.empty(), "0x71A9: [C8C5] & 0x20 -> RETS");
        ob.writeWord(0xC8C5, 0);
        // Lista cíclica: o orçamento de passos faz o papel do VBLANK.
        f.set(0, 0x400, 1); f.set(1, 0x400, 2);
        f.build({0, 1});
        ob.writeWord(ObjectListFixture::obj(1), ObjectListFixture::obj(0));
        d.call(0x7187, c, ob);
        check(true, "0x7187: lista cíclica termina pelo orçamento de passos");

        // 0x71B3: retira o primeiro tipo 0x100.
        f.set(0, 0x400, 9); f.set(1, 0x100, 8); f.set(2, 0x100, 7); f.set(3, 0x215, 6, 1500.0);
        f.build({0, 1, 2, 3});
        scene::detachType100(c, ob);
        check(ob.readWord(0xC98C) == ObjectListFixture::obj(1) && f.order() == std::vector<int>({0, 2, 3}),
              "0x71B3: retira o 1o tipo 0x100 e guarda em [C98C]");
        // 0x71BF: devolve [C98C] ao fim com os 0x21x próximos (chave <= e |dx| <= 2000).
        f.set(1, 0x100, 10, 100.0);
        f.set(2, 0x210, 20, 100.0);   // chave maior: fica
        f.set(3, 0x215, 5, 1500.0);   // |dx| = 1400: vai junto
        f.set(4, 0x212, 3, 2101.0);   // |dx| = 2001: fica
        f.set(5, 0x214, 4, -1900.5);  // |dx| = 2000.5 -> FIX = 2000: vai junto
        f.build({0, 2, 3, 4, 5});
        scene::reattachDetached(c, ob);
        check(f.order() == std::vector<int>({0, 2, 4, 1, 3, 5}), "0x71BF: [C98C] + 0x21x próximos no fim da lista");
        ob.writeWord(0xC98C, 0);
        f.build({0, 2});
        scene::reattachDetached(c, ob);
        check(f.order() == std::vector<int>({0, 2}), "0x71BF: [C98C] = 0 não mexe na lista");

        // 0x71E4: reinsere 0x2xx a partir do vizinho de menor chave.
        // Lista: 0(k90) 1(k70) 2(k50) 3(k30) 4(k10); objeto 6 (0x200, k40) com vizinhos {2, 0, nulo, 3, 1}.
        for (int i = 0; i < 5; ++i) f.set(i, 0x400, 90 - 20 * i);
        f.set(6, 0x205, 40);
        const uint32_t nb = 0x18000;
        ob.writeWord(ObjectListFixture::obj(6) + 0x1B, nb);
        const std::vector<int> slots{2, 0, -1, 3, 1};
        for (int k = 0; k < 5; ++k)
            ob.writeWord(nb + 5 + 6 * static_cast<uint32_t>(k), slots[k] < 0 ? 0u : ObjectListFixture::obj(slots[k]));
        f.build({6, 0, 1, 2, 3, 4});
        scene::regroupType200(c, ob);
        // Menor chave entre os vizinhos = 3 (k30): a busca parte dele; o seguinte (4, k10) tem chave <= 40.
        check(f.order() == std::vector<int>({0, 1, 2, 3, 6, 4}),
              "0x71E4: âncora = vizinho de menor chave; insere antes do 1o com chave <= a dele");
        // Âncora não encontrada (vizinhos fora da lista) -> procura desde a cabeça.
        for (int k = 0; k < 5; ++k) ob.writeWord(nb + 5 + 6 * static_cast<uint32_t>(k), k == 0 ? 0x1F000u : 0u);
        ob.writeWord(0x1F000 + 0x1C, 1);
        ob.writeWord(0x1F000 + 0x0F, 0x400);
        f.build({0, 1, 6, 2, 3, 4});
        scene::regroupType200(c, ob);
        check(f.order() == std::vector<int>({0, 1, 2, 6, 3, 4}),
              "0x71E4: âncora fora da lista -> desde a cabeça (k40 entre k50 e k30)");
        // 1o vizinho tipo 0x300: só contam tipos < 0x310 ou 0x6xx.
        ob.writeWord(0x1F000 + 0x0F, 0x300);
        f.set(3, 0x450, 30);           // não elegível: passa
        f.set(4, 0x620, 10);           // 0x6xx: elegível
        f.build({0, 1, 2, 3, 4, 6});
        scene::regroupType200(c, ob);
        check(f.order() == std::vector<int>({0, 1, 2, 3, 6, 4}), "0x71E4: vizinho 0x300 -> só tipos < 0x310 ou 0x6xx");
    }
}

/// Listas de visibilidade (0x726F / 0x7254)
void testVisibilityLists() {
    {
        MemoryBus vb;
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        scene::registerRoutines(d);
        Tms32031State c;
        const uint32_t nearCell = 0xC982, farCell = 0xC984;
        vb.writeWord(0x0040, nearCell);
        vb.writeWord(0x0041, farCell);
        vb.writeWord(0x0045, 0x809800);
        vb.writeWord(0x0047, 0x809809);
        vb.writeWord(0x7252, 75000);
        vb.writeWord(0x7253, 80000);
        for (uint32_t i = 0; i < 3; ++i) vb.writeWord(0x809800 + i, doubleToTms(0.0));
        for (uint32_t i = 0; i < 9; ++i) vb.writeWord(0x809809 + i, doubleToTms(0.0));
        vb.writeWord(0x809809 + 8, doubleToTms(1.0));  // eixo de profundidade = +z
        // 3 objetos distantes: z = 1000 (perto), z = 100000 (longe), z = -9000 (atrás)
        const uint32_t o[3] = {0x20000, 0x20040, 0x20080};
        const double z[3] = {1000.0, 100000.0, -9000.0};
        for (uint32_t i = 0; i < 3; ++i) {
            vb.writeWord(o[i], i < 2 ? o[i + 1] : 0);
            vb.writeWord(o[i] + 1, doubleToTms(0.0));
            vb.writeWord(o[i] + 2, doubleToTms(0.0));
            vb.writeWord(o[i] + 3, doubleToTms(z[i]));
            vb.writeWord(o[i] + 0x0E, 0x1000);
            vb.writeWord(o[i] + 0x1D, 50);
        }
        vb.writeWord(farCell, o[0]);
        vb.writeWord(nearCell, 0);
        d.call(0x726F, c, vb);
        check(vb.readWord(nearCell) == o[0] && vb.readWord(o[0]) == 0 && vb.readWord(farCell) == o[1] &&
                  vb.readWord(o[1]) == o[2] && vb.readWord(o[0] + 0x0E) == 0x2000 && vb.readWord(o[0] + 0x1C) == 1000 &&
                  vb.readWord(o[1] + 0x1C) == 100000 && vb.readWord(o[2] + 0x1C) == static_cast<uint32_t>(-9000),
              "0x726F: profundidade em +0x1C; z = 1000 passa para a lista visível (+0xE ^= 0x3000)");
        // Afasta o objeto visível: 0x7254 o devolve (usa a profundidade gravada)
        vb.writeWord(o[0] + 0x1C, 80051);
        d.call(0x7254, c, vb);
        check(vb.readWord(nearCell) == 0 && vb.readWord(farCell) == o[0] && vb.readWord(o[0]) == o[1] &&
                  vb.readWord(o[0] + 0x0E) == 0x1000,
              "0x7254: profundidade − 80000 − raio > 0 -> volta ao início da lista distante");
        vb.writeWord(farCell, o[1]);
        vb.writeWord(nearCell, o[0]);
        vb.writeWord(o[0], 0);
        vb.writeWord(o[0] + 0x1C, 80050);
        d.call(0x7254, c, vb);
        check(vb.readWord(nearCell) == o[0], "0x7254: profundidade − 80000 − raio = 0 -> continua visível");
        vb.writeWord(o[0] + 0x1C, static_cast<uint32_t>(-5000));
        d.call(0x7254, c, vb);
        check(vb.readWord(nearCell) == 0 && vb.readWord(farCell) == o[0], "0x7254: profundidade ≤ −5000 -> rebaixa");
    }
}

/// Pool de objetos (0x6FEB) e modo de atração (0x3859)
void testObjectPoolAndAttract() {
    {
        MemoryBus ab;
        ab.writeWord(0x6FE9, 0xC983);
        ab.writeWord(0x6FEA, 0x10555);
        ab.writeWord(0xC982, 0x1234);
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        scene::registerRoutines(d);
        Tms32031State c;
        c.r[0] = 5; c.ar[0] = 6; c.ar[1] = 7;
        d.call(0x6FEB, c, ab);
        uint32_t n = 0, last = 0;
        for (uint32_t p = ab.readWord(0xC983); p != 0 && n < 0x2000; p = ab.readWord(p)) { ++n; last = p; }
        check(n == 0x44C && last == 0x10555 + 0x44B * 0x22 && ab.readWord(0xC982) == 0 && ab.readWord(0xC989) == 0x44C &&
                  c.r[0] == 5 && c.ar[0] == 6 && c.ar[1] == 7 && c.rc == 0xFFFFFFFFu,
              "0x6FEB: 1100 objetos de 0x22 palavras na lista livre [C983]; listas zeradas; R0/AR0/AR1 preservados");

        using namespace proc::proc_vars;
        namespace pcb = proc::pcb;
        ab.writeWord(kActiveHeadPtr, kActiveHead);
        ab.writeWord(kFreeHeadPtr, kFreeHead);
        ab.writeWord(kPoolPtr, 0xEBE7);
        proc::Scheduler sched;
        proc::registerRoutines(d, sched);
        game::registerAttract(d);
        d.call(0xA8F5, c, ab);
        ab.writeWord(0xB188, 0x3889);
        ab.writeWord(0xC8CF, 0);
        c.ar[2] = 0x3859; c.r[2] = 0x500;
        d.call(0xA844, c, ab);
        const uint32_t ctl = c.ar[0];
        auto pend1 = [&](uint32_t a) { uint32_t k = 0; for (const auto& e : pend.calls) k += e.addr == a; return k; };
        sched.run(c, ab, d);
        uint32_t scene = ab.readWord(kActiveHead);
        check(pend1(0x9F2F) == 5 && pend1(0x9DF6) == 1 && ab.readWord(scene + pcb::kPc) == 0x3889 &&
                  ab.readWord(scene + pcb::kId) == 0x560 && ab.readWord(scene + pcb::kAr6) == 0 &&
                  ab.readWord(ctl + pcb::kPc) == 0x3872 && ab.readWord(ctl + pcb::kAr6) == 0xD2,
              "0x3859: carrega os blocos, cria a cena 0x3889 (ID 0x560, AR6 = cena 0) e espera 211 quadros");
        // 211 quadros sem entrada: cena 0 -> AR5 = 1 (ímpar) -> nova cena 1
        for (int i = 0; i < 211; ++i) sched.run(c, ab, d);
        check(ab.readWord(ctl + pcb::kAr5) == 1 && ab.readWord(ctl + pcb::kAr6) == 0xD2 && pend1(0x1E1C) == 0,
              "0x3872: após 211 quadros avança para a cena 1");
        // [E5FF] muda: avança já, mata as cenas 0x56x
        ab.writeWord(0xE5FF, 3);
        pend.calls.clear();
        sched.run(c, ab, d);
        check(ab.readWord(ctl + pcb::kAr5) == 2 && ab.readWord(0xE5FE) == 3 && pend1(0x1E1C) == 0,
              "0x3872: [E5FF] mudou -> cena 2 imediatamente");
        // Por tempo: 2 -> 3 (ímpar, continua); 3 -> 4 (par) -> 0x1E1C
        for (int i = 0; i < 211; ++i) sched.run(c, ab, d);
        check(ab.readWord(ctl + pcb::kAr5) == 3 && pend1(0x1E1C) == 0, "0x3872: cena 2 -> 3 (ímpar) continua");
        for (int i = 0; i < 211; ++i) sched.run(c, ab, d);
        check(pend1(0x1E1C) == 1, "0x3872: cena 3 -> 4 (par) -> 0x1E1C (próxima tela do ciclo)");
    }
}

/// Buscas por tipo na lista visível (0x36B3-0x3737, 0x9EDB)
void testObjectQueries() {
    {
        MemoryBus qb;
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        scene::registerObjectQuery(d);
        Tms32031State c;
        qb.writeWord(0x0040, 0xC982);
        const uint32_t o[4] = {0x20000, 0x20040, 0x20080, 0x200C0};
        const uint32_t ty[4] = {0x0105, 0x0207, 0x1005, 0x0030};
        qb.writeWord(0xC982, o[0]);
        for (int i = 0; i < 4; ++i) {
            qb.writeWord(o[i], i < 3 ? o[i + 1] : 0);
            qb.writeWord(o[i] + 0xF, ty[i]);
            qb.writeWord(o[i] + 2, doubleToTms(10.0));
        }
        auto cy = [&]() { return (c.st & Tms32031State::kStCarry) != 0; };
        c.r[0] = 0x77; c.ar[5] = 0x88; c.r[1] = 5;
        d.call(0x36B3, c, qb);
        const bool f1 = !cy() && c.ar[0] == o[0];
        d.call(0x36C7, c, qb);
        const bool f2 = !cy() && c.ar[0] == o[2];
        d.call(0x36C7, c, qb);
        check(f1 && f2 && cy() && c.ar[0] == 0 && c.r[0] == 0x77 && c.ar[5] == 0x88,
              "0x36B3/0x36C7: tipo & 0xFF == R1 (acha o 1º e o seguinte; fim: carry e AR0 = 0)");
        c.r[1] = 9;
        d.call(0x36B3, c, qb);
        check(cy() && c.ar[0] == o[3], "0x36B3 sem resultado: carry e AR0 = último nó");
        c.r[1] = 0x1000;
        d.call(0x36DB, c, qb);
        const bool m1 = !cy() && c.ar[0] == o[2];
        d.call(0x36EF, c, qb);
        check(m1 && cy() && c.ar[0] == 0, "0x36DB/0x36EF: tipo & R1 != 0");
        // 0x3702: lista {7, (5<<8)|5... faixa 0x2F..0x30, 0}
        qb.writeWord(0x30000, 7);
        qb.writeWord(0x30001, (0x2Fu << 8) | 0x30u);
        qb.writeWord(0x30002, 0);
        c.ar[3] = 0x30000; c.r[2] = 0x4000;
        d.call(0x3702, c, qb);
        check(qb.readWord(o[1] + 0xF) == 0x4207 && qb.readWord(o[3] + 0xF) == 0x4030 && qb.readWord(o[0] + 0xF) == 0x0105 &&
                  c.ar[3] == 0x30003,
              "0x3702: tipo isolado e faixa (de << 8 | até) recebem tipo |= R2");
        c.r[1] = 0x0100; c.r[2] = doubleToTms(2.5); c.r[3] = 0x99;
        d.call(0x371E, c, qb);
        check(tmsToDouble(qb.readWord(o[0] + 2)) == 12.5 && tmsToDouble(qb.readWord(o[1] + 2)) == 10.0 && c.r[3] == 0x99,
              "0x371E: +2 += R2 nos objetos com tipo & R1");
        c.r[1] = 0x0200;
        d.call(0x372E, c, qb);
        check(pend.calls.size() == 1 && pend.calls[0].addr == 0x715F, "0x372E: CALL $715F para cada objeto com tipo & R1");
        qb.writeWord(0x9E7F, 0x31000);
        for (uint32_t i = 0; i < 0x82; ++i) qb.writeWord(0x31000 + i, 0x500 + i);
        c.ar[2] = 0x503;
        d.call(0x9EDB, c, qb);
        const bool t1 = cy() && c.r[0] == 0x300;
        c.ar[2] = 0x500 + 0x81;
        d.call(0x9EDB, c, qb);
        const bool t2 = cy() && c.r[0] == 0x8100;
        c.ar[2] = 0x500 + 0x82;
        d.call(0x9EDB, c, qb);
        check(t1 && t2 && !cy() && c.r[0] == 0xFFFFFFFFu, "0x9EDB: índice << 8 e carry; 0x82 entradas; senão R0 = -1");
    }
}

/// Objetos de cena (0x7025-0x70C9, 0x715F, 0x9ECD)
void testSceneObjects() {
    {
        MemoryBus ob;
        ob.writeWord(0x6FE9, 0xC983);
        ob.writeWord(0x6FEA, 0x10555);
        ob.writeWord(0x0040, 0xC982);
        ob.writeWord(0x0041, 0xC984);
        ob.writeWord(0x0042, 0xC985);
        ob.writeWord(0x7253, 80000);
        ob.writeWord(0x9E7E, 0xEA4C);
        ob.writeWord(0xEA4C + 0x123, 0x00450000);
        for (uint32_t i = 0; i < 3; ++i) ob.writeWord(0x809800 + i, doubleToTms(0.0));
        for (uint32_t i = 0; i < 3; ++i) ob.writeWord(0x80980F + i, doubleToTms(i == 2 ? 1.0 : 0.0));
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        scene::registerRoutines(d);
        uint64_t hangs = 0;
        scene::registerSceneObject(d, hangs);
        Tms32031State c;
        d.call(0x6FEB, c, ob);
        // modelo em 0x30001: [-1] = flags 0x3123, [0] = raio 77
        ob.writeWord(0x30000, 0x3123);
        ob.writeWord(0x30001, 77);
        c.ar[2] = 0x30001; c.r[0] = 0x11; c.r[1] = 0x22;
        d.call(0x700A, c, ob);
        const uint32_t a = c.ar[0];
        check(a == 0x10555 && !(c.st & Tms32031State::kStCarry) && ob.readWord(0xC989) == 0x44B &&
                  ob.readWord(a + 0x0D) == 0x30001 && ob.readWord(a + 0x1D) == 77 && ob.readWord(a + 0x0E) == 0x440 &&
                  ob.readWord(a + 0x10) == 0x4500 && tmsToDouble(ob.readWord(a + 4)) == 1.0 &&
                  tmsToDouble(ob.readWord(a + 5)) == 0.0 && c.r[0] == 0x11 && c.r[1] == 0x22 && hangs == 0,
              "0x700A: aloca do pool, matriz identidade, modelo/raio, flags 0x40|0x400 e textura (0x9ECD)");
        // 3 objetos a z = 100, 300, 200 -> visível em ordem decrescente; z = 90000 -> distante
        auto mk = [&](double z) {
            d.call(0x7025, c, ob);
            ob.writeWord(c.ar[0] + 3, doubleToTms(z));
            c.ar[2] = c.ar[0];
            d.call(0x7093, c, ob);
            return c.ar[2];
        };
        const uint32_t b1 = mk(100), b2 = mk(300), b3 = mk(200), bf = mk(90000);
        auto lst = [&](uint32_t cell) {
            std::vector<uint32_t> v;
            for (uint32_t p = ob.readWord(cell); p != 0 && v.size() < 10; p = ob.readWord(p)) v.push_back(p);
            return v;
        };
        check(lst(0xC982) == std::vector<uint32_t>{b2, b3, b1} && lst(0xC984) == std::vector<uint32_t>{bf} &&
                  ob.readWord(b1 + 0x0E) == 0x1000 && ob.readWord(bf + 0x0E) == 0x2000 && ob.readWord(b3 + 0x1C) == 200,
              "0x7093: profundidade; visível em ordem decrescente (+0xE |= 0x1000); >= 80000 -> distante (0x2000)");
        c.ar[2] = b3;
        d.call(0x715F, c, ob);
        check(lst(0xC982) == std::vector<uint32_t>{b2, b1} && ob.readWord(b3 + 0x0E) == 0 && c.ar[2] == b3,
              "0x715F: retira da lista indicada por +0xE e zera os bits 0x3000");
        pend.calls.clear();
        d.call(0x715F, c, ob);
        check(pend.calls.size() == 1 && pend.calls[0].addr == 0xAB1A && pend.calls[0].r0 == 0x104,
              "0x715F com objeto fora da lista: erro 0x104 (0xAB1A)");
        ob.writeWord(b1 + 0x0F, 0x321);
        c.ar[2] = 0x321;
        d.call(0x70BB, c, ob);
        const bool fnd = (c.st & Tms32031State::kStCarry) && c.ar[0] == b1;
        c.ar[2] = 0x322;
        d.call(0x70BB, c, ob);
        check(fnd && !(c.st & Tms32031State::kStCarry), "0x70BB: procura +0xF == AR2 na lista visível (carry = achou)");
        // terceira lista ordenada
        c.ar[2] = b3;
        d.call(0x7070, c, ob);
        check(lst(0xC985) == std::vector<uint32_t>{b3} && ob.readWord(b3 + 0x0E) == 0x3000, "0x7070: terceira lista (+0xE |= 0x3000)");
        // 0x440C: 2 objetos de uma lista
        math::registerTrig(d);
        math::registerRoutines(d);
        ob.writeWord(0xB0D1, 0xE47D);
        for (uint32_t i = 0xC8BD; i <= 0xC8C3; ++i) ob.writeWord(i, 0);  // constantes do seno zeradas: o teste só confere cadeia, posições e tipos
        const uint32_t L = 0x31000;
        const uint32_t words[] = {0, 2, 0x30001, 10, 20, 30, doubleToTms(0.0), 0x5123, 0x30001, 1, 2, 70000, doubleToTms(0.0), 0x0456};
        for (uint32_t i = 0; i < sizeof(words) / 4; ++i) ob.writeWord(L + i, words[i]);
        c.ar[2] = L; c.r[4] = 0x44;
        d.call(0x440C, c, ob);
        const uint32_t s1 = ob.readWord(0xE47D), s2 = ob.readWord(s1 + 0x1F);
        check(s1 != 0 && s2 != 0 && ob.readWord(s2 + 0x1F) == 0 && c.r[0] == s1 && c.r[4] == 0x44 && c.ar[2] == L &&
                  tmsToDouble(ob.readWord(s1 + 3)) == 30.0 && ob.readWord(s1 + 0x0F) == 0x123 && ob.readWord(s2 + 0x0F) == 0x456 &&
                  ob.readWord(s1 + 0x1C) == 30 && ob.readWord(s2 + 0x1C) == 70000,
              "0x440C: cria os objetos da lista, cadeia +0x1F a partir de [E47D], tipo & 0xFFF, profundidade");
        // pool vazio
        ob.writeWord(0xC983, 0);
        pend.calls.clear();
        d.call(0x7025, c, ob);
        check((c.st & Tms32031State::kStCarry) && c.ar[0] == 0 && pend.calls.size() == 1 && pend.calls[0].r0 == 0x110,
              "0x7025 com pool vazio: erro 0x110 e carry");
    }
}

/// Matrizes 3×3 (0x947F/0x93F1/0x9494) e pipeline 3D (0x0064)
void testMatricesAndPipeline3d() {
    {
        MemoryBus mb;
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        math::registerMatrix(d);
        Tms32031State c;
        auto putM = [&](uint32_t a, std::initializer_list<double> v) {
            uint32_t i = 0;
            for (double x : v) mb.writeWord(a + i++, doubleToTms(x));
        };
        auto getM = [&](uint32_t a) {
            std::vector<double> v;
            for (uint32_t i = 0; i < 9; ++i) v.push_back(tmsToDouble(mb.readWord(a + i)));
            return v;
        };
        putM(0x20000, {1, 2, 0, 0, 1, 0, 0, 0, 2});
        putM(0x20010, {1, 0, 0, 3, 1, 0, 0, 0, 1});
        c.r[2] = 0x20000; c.ar[2] = 0x20010; c.r[3] = 0x20020;
        d.call(0x947F, c, mb);
        check(getM(0x20020) == std::vector<double>{7, 2, 0, 3, 1, 0, 0, 0, 2} && c.ar[0] == 0x20009 && c.ar[1] == 0x20029,
              "0x947F: [R3] = [R2] × [AR2] (linhas × colunas)");
        mb.writeWord(0x4A, 0x20030);
        c.r[2] = 0x20000; c.ar[2] = 0x20010; c.r[3] = 0x20010; c.ar[0] = 0x777;
        d.call(0x9494, c, mb);
        check(getM(0x20010) == std::vector<double>{7, 2, 0, 3, 1, 0, 0, 0, 2} && c.r[0] == 0x777 && c.ar[2] == 0x20010,
              "0x9494: [AR2] = [R2] × [AR2] via [0x4A]; R0 = AR0 (bug do original preservado)");

        // Cena sintética: câmera na origem, matriz identidade, T = 0,5 para todo Z.
        struct Fifo : IIoDevice {
            std::vector<uint32_t> words;
            int triggers = 0;
            bool ioRead(uint32_t a, uint32_t& v) override {
                if (a == 0x980082) { v = 0; return true; }
                if (a == 0x980083) { v = 0; ++triggers; return true; }
                return false;
            }
            void ioWrite(uint32_t a, uint32_t v) override { if (a == 0x600000) words.push_back(v); }
        } fifo;
        mb.attachDevice(&fifo);
        const uint32_t ptrs[][2] = {{0x40, 0xC982}, {0x42, 0xC985}, {0x43, 0xC986}, {0x44, 0xC987},
                                    {0x45, 0x809800}, {0x47, 0x809809}, {0x49, 0x809870}, {0x4A, 0x809874},
                                    {0x4B, 0x809818}, {0x4C, 0x809822}, {0x4D, 0x80982D}, {0x4E, 0x8099DB},
                                    {0x4F, 0x809D00}, {0x50, 0x809815}, {0x51, 0x809816}, {0x52, 0x30000},
                                    {0x55, 80000}, {0x5A, 0x809837}, {0x61, 0x809856}, {0x62, 0xEA4C}, {0x5F5, 0x809889}};
        for (const auto& p : ptrs) mb.writeWord(p[0], p[1]);
        mb.writeWord(0x53, doubleToTms(256.0));
        mb.writeWord(0x54, doubleToTms(200.0));
        for (uint32_t i = 0x30000 - 0x50; i <= 0x30000 + 0x1387; ++i) mb.writeWord(i, doubleToTms(0.5));
        putM(0x809809, {1, 0, 0, 0, 1, 0, 0, 0, 1});
        for (uint32_t i = 0; i < 3; ++i) mb.writeWord(0x809800 + i, doubleToTms(0.0));
        mb.writeWord(0x809807, doubleToTms(0.0));
        mb.writeWord(0xEA4C + 3, (7u << 16) | 1u);
        const uint32_t obj = 0x10600, model = 0x21000;
        mb.writeWord(0xC982, obj);
        mb.writeWord(obj, 0);
        mb.writeWord(obj + 0x0D, model);
        mb.writeWord(obj + 0x0E, 0x1000);
        putM(obj + 4, {1, 0, 0, 0, 1, 0, 0, 0, 1});
        auto vtx = [](int x, int y) { return (static_cast<uint32_t>(y) << 16) | (static_cast<uint32_t>(x) & 0xFFFFu); };
        const uint32_t mdl[] = {50, 3, vtx(-10, -10), 0, vtx(10, -10), 0, vtx(10, 10), 0, vtx(-10, 10), 0,
                                (3u << 16) | 0x0100u, 0x03020100u, 0x11223344u, 0x55667788u, 0x99u};
        for (uint32_t i = 0; i < sizeof(mdl) / 4; ++i) mb.writeWord(model + i, mdl[i]);
        render::Pipeline3dStats st;
        render::registerPipeline3d(d, st);
        auto place = [&](double z) {
            putM(obj + 1, {0.0, 0.0, z});
            fifo.words.clear();
            fifo.triggers = 0;
            pend.calls.clear();
        };
        place(3000.0);
        c.r[4] = 0x4444;
        d.call(0x0064, c, mb);
        const std::vector<uint32_t> want = {(3u << 16) | 0x0100u, 0x700, 251, 194, 261, 194, 261, 205, 251, 205,
                                            0x11223344u, 0x1122, 0x55667788u, 0x5566, 0x99};
        check(fifo.words == want && fifo.triggers == 1 && mb.readWord(obj + 0x1C) == 3000 &&
                  tmsToDouble(mb.readWord(0x809823)) == 3000.0 && c.r[4] == 0x4444 && st.polys == 1 &&
                  !pend.calls.empty() && pend.calls[0].addr == 0x938A && (mb.readWord(0xC8C7) & 0x80u) == 0 &&
                  mb.readWord(0x994000) == mb.readWord(0xC8C7),
              "0x0064: objeto longe (0x0453): projeção sx = X·T + 256, sy = Y·T·1,04 + 200, pacote de 15 palavras");
        place(1000.0);
        d.call(0x0064, c, mb);
        check(fifo.words == want && st.subdivided == 0, "0x029D: objeto perto e quad pequeno: mesmo pacote, sem subdivisão");
        mb.writeWord(model + 11, 0x03000102u);  // ordem invertida: face traseira
        place(3000.0);
        d.call(0x0064, c, mb);
        check(fifo.words.empty() && st.backfaces == 1, "0x0453: polígono de costas é descartado");
        mb.writeWord(model + 11, 0x03020100u);
        place(40.0);
        d.call(0x0064, c, mb);
        check(fifo.words.empty() && st.culled == 1, "0x00C7: z + raio < 100 descarta o objeto");
        place(-5000.0);
        mb.writeWord(obj + 0x0E, 0x1000 | 0x20);  // posição já relativa: mesmo resultado
        d.call(0x0064, c, mb);
        check(fifo.words.empty() && st.culled == 2, "0x00C7: objeto atrás da câmera é descartado");

        // 0x032F: quad de 4000 × 4000 → 4 filhos; 3 fora da tela; o de cima à esquerda é desenhado.
        const int32_t q[8] = {-1000, -1000, 3000, -1000, 3000, 3000, -1000, 3000};
        for (uint32_t i = 0; i < 8; ++i) mb.writeWord(0x809889 + i, static_cast<uint32_t>(q[i]));
        const uint32_t tex[] = {0x00C80000u, 0xC800C8C8u, 0x1234u};
        for (uint32_t i = 0; i < 3; ++i) mb.writeWord(0x22000 + i, tex[i]);
        uint32_t ar1 = 0x22000;
        fifo.words.clear();
        render::subdivideQuad(mb, 0x809889, ar1, 0x300, 0xABCD, st);
        const uint32_t m1000 = static_cast<uint32_t>(-1000);
        const std::vector<uint32_t> sub = {0xABCD, 0x300, m1000, m1000, 1000, m1000, 1000, 1000, m1000, 1000,
                                           0, 100, 0x6464, 0x6400, 0x1234};
        check(fifo.words == sub && ar1 == 0x22003, "0x032F: subdivisão em 4, descarte fora da tela, u/v interpolados");
    }
}

/// Listas e pools (0x8EF1, 0x8F01, 0x8F13, 0x8F1F, 0x8F29)
void testListsAndPools() {
    {
        MemoryBus pb;
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        scene::registerNodePools(d);
        Tms32031State c;
        const uint32_t freeHead = 0x500, list = 0x501, list2 = 0x502, base = 0x600;
        pb.writeWord(list, 0x1234);
        c.ar[2] = base; c.r[2] = freeHead; c.r[3] = list; c.rc = 2; c.rs = 0x10; c.r[0] = 0x77; c.ar[0] = 0x88;
        d.call(0x8EF1, c, pb);
        const bool init = pb.readWord(list) == 0 && pb.readWord(freeHead) == base && pb.readWord(base) == base + 0x10 &&
                          pb.readWord(base + 0x10) == base + 0x20 && pb.readWord(base + 0x20) == 0 &&
                          c.ar[2] == base + 0x30 && c.rc == 0xFFFFFFFFu && c.r[0] == 0x77 && c.ar[0] == 0x88;
        check(init, "0x8EF1: lista livre de RC + 1 nós de RS palavras; lista vazia");
        c.ar[2] = freeHead; c.r[2] = list;
        d.call(0x8F01, c, pb);
        const bool a1 = c.carry() && c.ar[0] == base && c.r[0] == base && pb.readWord(list) == base && pb.readWord(base) == 0;
        d.call(0x8F01, c, pb);
        const bool a2 = c.ar[0] == base + 0x10 && pb.readWord(list) == base + 0x10 && pb.readWord(base + 0x10) == base;
        check(a1 && a2 && pb.readWord(freeHead) == base + 0x20, "0x8F01: tira da lista livre e põe na frente da lista (carry = 1)");
        c.ar[2] = base; c.r[2] = list; c.r[3] = list2;
        d.call(0x8F29, c, pb);
        check(pb.readWord(list) == base + 0x10 && pb.readWord(base + 0x10) == 0 && pb.readWord(list2) == base,
              "0x8F29: move AR2 de uma lista para a frente de outra");
        c.ar[2] = freeHead;
        d.call(0x8F13, c, pb);
        const bool t1 = c.carry() && c.ar[0] == base + 0x20 && pb.readWord(freeHead) == 0;
        d.call(0x8F13, c, pb);
        const bool t2 = !c.carry() && c.ar[0] == base + 0x20;
        c.ar[2] = base + 0x20; c.r[2] = list2;
        d.call(0x8F1F, c, pb);
        check(t1 && t2 && pb.readWord(list2) == base + 0x20 && pb.readWord(base + 0x20) == base,
              "0x8F13/0x8F1F: aloca (vazia: carry = 0) e insere na frente");
    }
}

/// Lista visível: 0xAF75 (objetos 0x7xx seguem o nó de pista mais próximo)
void testVisibleListRegroup() {
    {
        MemoryBus mb;
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        scene::registerRoutines(d);
        Tms32031State c;
        auto wf = [&](uint32_t a, double v) { mb.writeWord(a, doubleToTms(v)); };
        const uint32_t A = 0x5000, B = 0x5040, C = 0x5080;
        mb.writeWord(0x40, 0xC982); mb.writeWord(0xC982, A);
        mb.writeWord(A, B); mb.writeWord(B, C); mb.writeWord(C, 0);
        mb.writeWord(A + 0xF, 0x300); mb.writeWord(B + 0xF, 0x701); mb.writeWord(C + 0xF, 0x300);
        mb.writeWord(0xE46F, A); mb.writeWord(A + 0x21, C); mb.writeWord(C + 0x21, 0);
        wf(A + 1, 0.0); wf(A + 3, 0.0); wf(C + 1, 100.0); wf(C + 3, 100.0); wf(B + 1, 90.0); wf(B + 3, 95.0);
        mb.writeWord(C + 0xE, 0x1000); mb.writeWord(A + 0xE, 0x2000); mb.writeWord(B + 0xE, 0x3008);
        mb.writeWord(B + 0x1E, 0); mb.writeWord(0xC8C5, 2);
        mb.writeWord(0xAF74, 0x211502F9); // distância inicial da busca (valor da ROM, ≈ 1,2·10^10)
        d.call(0xAF75, c, mb);
        check(mb.readWord(0xC982) == A && mb.readWord(A) == C && mb.readWord(C) == B && mb.readWord(B) == 0 &&
                  mb.readWord(B + 0x1E) == C && mb.readWord(B + 0xE) == 0x1008,
              "0xAF75: objeto 0x7xx vai para depois do nó mais próximo e herda os bits 0x3000");
    }
}

/// 0xAE27 (próximo por tipo) e 0x8EE9 (aleatório < N)
void testNextByTypeAndRandom() {
    {
        MemoryBus qb;
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        scene::registerObjectQuery(d);
        math::registerRoutines(d);
        // cadeia 0x100 → 0x200 → 0x300 → 0; tipos 5, 7, 5
        qb.writeWord(0x100, 0x200); qb.writeWord(0x10F, 5);
        qb.writeWord(0x200, 0x300); qb.writeWord(0x20F, 7);
        qb.writeWord(0x300, 0);     qb.writeWord(0x30F, 5);
        Tms32031State c;
        c.ar[0] = 0x100; c.r[1] = 5; c.r[0] = 0x55; c.ar[5] = 0x66;
        d.call(0xAE27, c, qb);
        const bool first = c.ar[0] == 0x300 && !c.carry() && c.r[0] == 0x55 && c.ar[5] == 0x66;
        d.call(0xAE27, c, qb);
        check(first && c.ar[0] == 0 && c.carry(), "0xAE27: próximo objeto com +0xF == R1 (carry = 0 achou)");
        int below = 0, zeros = 0;
        qb.writeWord(math::kRandomSeed, 0x12345);
        for (int i = 0; i < 1000; ++i) {
            c.ar[2] = 200;
            d.call(0x8EE9, c, qb);
            if (c.carry()) { ++below; if (c.r[0] >= 200) zeros = -1000; } else if (c.r[0] == 0) ++zeros;
        }
        check(below > 100 && below < 300 && below + zeros == 1000, "0x8EE9: R0 < AR2 com carry (≈ 20 % para 200/1000); senão R0 = 0");
    }
}

} // namespace

void runSceneTests() {
    testObjectList();
    testVisibilityLists();
    testObjectPoolAndAttract();
    testObjectQueries();
    testSceneObjects();
    testMatricesAndPipeline3d();
    testListsAndPools();
    testVisibleListRegroup();
    testNextByTypeAndRandom();
}
