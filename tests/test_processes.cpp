/**
 * @file test_processes.cpp
 * @brief Escalonador de processos cooperativos.
 */
#include "test_support.hpp"

using namespace rea::core;
using namespace rea::test;

namespace {

/// Processos cooperativos (0xA844-0xA93A, 0x4DE6, 0x69D6, 0x894D)
void testProcesses() {
    {
        using namespace proc::proc_vars;
        namespace pcb = proc::pcb;
        MemoryBus pb;
        pb.writeWord(kActiveHeadPtr, kActiveHead);
        pb.writeWord(kFreeHeadPtr, kFreeHead);
        pb.writeWord(kPoolPtr, 0xEBE7);
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        proc::Scheduler sched;
        proc::registerRoutines(d, sched);
        Tms32031State c;
        auto activeList = [&]() {
            std::vector<uint32_t> v;
            for (uint32_t p = pb.readWord(kActiveHead); p != 0 && v.size() < 0x100; p = pb.readWord(p)) v.push_back(p);
            return v;
        };
        auto freeCount = [&]() {
            uint32_t n = 0;
            for (uint32_t p = pb.readWord(kFreeHead); p != 0 && n < 0x100; p = pb.readWord(p)) ++n;
            return n;
        };

        pb.writeWord(kActiveHead, 0x1234);
        c.r[0] = 7; c.ar[0] = 8; c.ar[1] = 9;
        d.call(0xA8F5, c, pb);
        check(pb.readWord(kActiveHead) == 0 && freeCount() == 0x69 && pb.readWord(kFreeHead) == 0xEBE7 &&
                  pb.readWord(0xEBE7) == 0xEBE7 + 0x3E && c.r[0] == 7 && c.ar[0] == 8 && c.ar[1] == 9,
              "0xA8F5: lista ativa vazia; 0x69 PCBs de 0x3E palavras a partir de 0xEBE7; R0/AR0/AR1 preservados");

        // Cria A (PC 0x1000) e B (PC 0x3000, não traduzido): B fica à frente
        c.ar[2] = 0x1000; c.r[2] = 0x0501; c.r[4] = 0x44; c.ar[4] = 0x4444;
        d.call(0xA844, c, pb);
        const uint32_t pa = c.ar[0];
        c.ar[2] = 0x3000; c.r[2] = 0x0242; c.r[4] = 0x55;
        d.call(0xA844, c, pb);
        const uint32_t pbk = c.ar[0];
        check(activeList() == std::vector<uint32_t>{pbk, pa} && pa == 0xEBE7 && pb.readWord(pa + pcb::kPc) == 0x1000 &&
                  pb.readWord(pa + pcb::kId) == 0x0501 && pb.readWord(pa + pcb::kStackTop) == pa + 0x2F &&
                  pb.readWord(pa + pcb::kR4) == 0x44 && pb.readWord(pa + pcb::kAr4) == 0x4444 &&
                  !(c.st & Tms32031State::kStCarry) && freeCount() == 0x67,
              "0xA844: novo PCB no início da lista ativa com PC, ID, topo de pilha e registradores");

        // Processo traduzido de teste: 0x1000 conta, adormece 2 quadros; 0x1001 confere R4 e morre
        uint32_t ran = 0, resumedR4 = 0, resumedAr7 = 0;
        d.add(0x1000, [&](Tms32031State& cc, MemoryBus& b, ISubroutines& s) {
            ++ran;
            cc.r[4] = 0x99;
            cc.ret = 0x1001;
            cc.ar[2] = 2;
            s.call(0xA874, cc, b);
        });
        d.add(0x1001, [&](Tms32031State& cc, MemoryBus& b, ISubroutines& s) {
            resumedR4 = cc.r[4];
            resumedAr7 = cc.ar[7];
            s.call(0xA896, cc, b);
        });
        c.r[4] = 0;
        d.call(0xA872, c, pb);
        check(ran == 1 && pb.readWord(pa + pcb::kPc) == 0x1001 && pb.readWord(pa + pcb::kSleep) == 2 &&
                  pb.readWord(pa + pcb::kR4) == 0x99 && sched.untranslatedRuns().at(0x3000) == 1 &&
                  pb.readWord(pbk + pcb::kSleep) == 1 && pend.calls.size() == 1 && pend.calls[0].addr == 0x3000,
              "0xA872/0xA874: A adormece 2 quadros e retoma em 0x1001; B (não traduzido) fica para o próximo quadro");
        d.call(0xA872, c, pb);
        check(pb.readWord(pa + pcb::kSleep) == 1 && resumedAr7 == 0 && sched.untranslatedRuns().at(0x3000) == 2,
              "0xA872: sono decrementado (A ainda dorme); B roda de novo");
        d.call(0xA872, c, pb);
        check(resumedR4 == 0x99 && resumedAr7 == pa && activeList() == std::vector<uint32_t>{pbk} &&
                  pb.readWord(kFreeHead) == pa && freeCount() == 0x68 && sched.dispatched() == 5 &&
                  sched.untranslatedRuns().at(0x3000) == 3,
              "0xA896: A retoma com R4 restaurado e AR7 = PCB, morre e volta à lista livre");

        // Busca, contagem, existência
        c.ar[2] = 0x3002; c.r[2] = 0x0242;
        d.call(0xA844, c, pb);
        const uint32_t pc2 = c.ar[0];
        c.r[0] = 0x0240; c.r[1] = 0xFFF0;
        d.call(0xA912, c, pb);
        check(c.ar[0] == pc2 && c.r[0] == 0x0240, "0xA912: acha o primeiro com ID & R1 == R0 & R1");
        d.call(0xA90E, c, pb);
        check(c.ar[0] == pbk, "0xA90E: continua a busca a partir de AR0");
        d.call(0xA90E, c, pb);
        check(c.ar[0] == 0, "0xA90E: fim da lista -> AR0 = 0");
        c.r[2] = 0x0242;
        d.call(0x69D6, c, pb);
        check(c.r[0] == 2, "0x69D6: conta processos com ID == R2");
        pb.writeWord(kActiveHead + pcb::kId, 0x0242);  // [C983]: o pseudo-nó também é comparado
        d.call(0x69D6, c, pb);
        check(c.r[0] == 3, "0x69D6: compara também o pseudo-nó ([C983]), como no original");
        pb.writeWord(kActiveHead + pcb::kId, 0);
        pb.writeWord(pc2 + pcb::kId, 0x0142);
        pb.writeWord(pbk + pcb::kId, 0x0442);
        d.call(0x894D, c, pb);
        pb.writeWord(pc2 + pcb::kId, 0x0242);
        pb.writeWord(pbk + pcb::kId, 0x0242);
        check(c.r[2] == 2 && (c.st & Tms32031State::kStCarry), "0x894D: 2 processos com ID & 0x500 e ID & 0xF0 == 0x40 -> carry");
        c.ar[2] = 0x0500; c.r[2] = 0x0F00;
        d.call(0xA8E6, c, pb);
        check(!(c.st & Tms32031State::kStCarry) && c.ar[2] == 0x0500, "0xA8E6: nenhum ID & 0xF00 == 0x500 -> sem carry");
        c.ar[2] = 0x0200;
        d.call(0xA8E6, c, pb);
        check((c.st & Tms32031State::kStCarry) != 0, "0xA8E6: existe -> carry");

        // Mover depois de / criar depois do atual / matar outro / matar por máscara
        c.ar[7] = pc2;  // atual = cabeça; move para depois de pbk
        c.ar[2] = pbk;
        d.call(0xA91F, c, pb);
        check(activeList() == std::vector<uint32_t>{pbk, pc2} && c.ar[7] == pc2, "0xA91F: move o atual para depois de AR2");
        c.ar[7] = pbk;
        c.ar[2] = 0x5000; c.r[2] = 0x0777;
        d.call(0xA868, c, pb);
        const uint32_t pn = c.ar[0];
        check(activeList() == std::vector<uint32_t>{pbk, pn, pc2}, "0xA868: novo processo logo depois do atual");
        c.ar[2] = pn;
        d.call(0xA8AC, c, pb);
        check(activeList() == std::vector<uint32_t>{pbk, pc2} && pb.readWord(kFreeHead) == pn, "0xA8AC: encerra outro processo");
        c.ar[7] = pbk;
        c.r[0] = 0x0242; c.r[1] = 0xFFFF;
        d.call(0xA8CF, c, pb);
        check(activeList() == std::vector<uint32_t>{pbk}, "0xA8CF: encerra os que casam com a máscara, menos o atual");
        c.ar[0] = pbk; c.ar[1] = 0x6000;
        d.call(0xA908, c, pb);
        check(pb.readWord(pbk + pcb::kPc) == 0x6000 && pb.readWord(pbk + pcb::kSleep) == 1, "0xA908: redireciona (PC, sono 1)");

        // 0x4DE6: eventos -> processos 0x810
        pb.writeWord(kEventTablePtr, 0x4DFB);
        for (uint32_t i = 0; i < 4; ++i) pb.writeWord(0x4DFB + i, i == 1 ? 0 : 0x7000 + i);
        pb.writeWord(kEventBits, 0xF0000007);  // bits 0,1,2 (bit 1 tem entrada 0); 4 bits altos ignorados
        d.call(0x4DE6, c, pb);
        auto lst = activeList();
        check(pb.readWord(kEventBits) == 0 && lst.size() == 3 && pb.readWord(lst[0] + pcb::kPc) == 0x7002 &&
                  pb.readWord(lst[1] + pcb::kPc) == 0x7000 && pb.readWord(lst[0] + pcb::kId) == 0x810,
              "0x4DE6: um processo 0x810 por bit de [C8CB] com entrada != 0; zera [C8CB]");

        // Sem blocos livres: erro 0x210 e carry
        pend.calls.clear();
        while (freeCount() > 0) d.call(0xA844, c, pb);
        d.call(0xA844, c, pb);
        check((c.st & Tms32031State::kStCarry) && pend.calls.size() == 1 && pend.calls[0].addr == 0xAB1A &&
                  pend.calls[0].r0 == 0x210,
              "0xA844 sem PCB livre: registra o erro 0x210 (0xAB1A) e liga o carry");
    }
}

} // namespace

void runProcessesTests() {
    testProcesses();
}
