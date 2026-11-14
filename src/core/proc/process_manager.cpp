/**
 * @file process_manager.cpp
 * @brief Tradução dos processos cooperativos (`0xA844`–`0xA93A`), `0x4DE6`, `0x95CB`, `0x69D6`, `0x894D`.
 */
#include "core/proc/process_manager.hpp"

namespace rea::core::proc {

using namespace proc_vars;

namespace {

/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }

/// Atalhos de acesso à memória usados pelas rotinas de processos.
struct Ctx {
    Tms32031State& cpu;  ///< Registradores da CPU TMS320C31.
    MemoryBus& bus;  ///< Barramento de memória simulado.
    /// Lê com endereçamento direto (página DP, deslocamento @p off).
    uint32_t rd(uint32_t off) { return bus.readWord(cpu.direct(off)); }
    /// Grava @p v com endereçamento direto (página DP, deslocamento @p off).
    void wr(uint32_t off, uint32_t v) { bus.writeWord(cpu.direct(off), v); }
    /// Lê a palavra no endereço absoluto @p addr.
    uint32_t at(uint32_t addr) { return bus.readWord(addr); }
    /// Grava @p v no endereço absoluto @p addr.
    void put(uint32_t addr, uint32_t v) { bus.writeWord(addr, v); }
};

/// Padrão `PUSH ST / PUSH R0 / LDI code,R0 / CALL $AB1A / POP R0 / POP ST`.
void logError(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs, uint32_t code) {
    const uint32_t st = cpu.st, r0 = cpu.r[0];
    cpu.r[0] = code;
    subs.call(kErrorLogger, cpu, bus);
    cpu.r[0] = r0;
    cpu.st = st;
}

/// Liga ou desliga o bit C (carry) de ST, que a rotina original usa como retorno de sucesso ou falha.
void setCarry(Tms32031State& cpu, bool on) {
    if (on) cpu.st |= Tms32031State::kStCarry;
    else cpu.st &= ~Tms32031State::kStCarry;
}

} // namespace

void initLists(Tms32031State& cpu, MemoryBus& bus) {
    Ctx c{cpu, bus};
    // 0A8F5  PUSH R0, AR0, AR1; [C980] = 0
    const uint32_t r0 = cpu.r[0], ar0 = cpu.ar[0], ar1 = cpu.ar[1];
    cpu.ar[0] = c.rd(kActiveHeadPtr);
    c.put(cpu.ar[0], 0);
    // 0A8FB  RPTB (0x69 vezes): *AR0 = AR1; AR0 = AR1; AR1 += 0x3E
    cpu.ar[0] = c.rd(kFreeHeadPtr);
    cpu.ar[1] = c.rd(kPoolPtr);
    cpu.rc = kPoolBlocks - 1;
    for (;;) {
        c.put(cpu.ar[0], cpu.ar[1]);
        cpu.ar[0] = cpu.ar[1];
        cpu.ar[1] += kBlockWords;
        if (s(--cpu.rc) < 0) break;
    }
    // 0A902  último bloco: próximo = 0
    c.put(cpu.ar[0], 0);
    cpu.ar[1] = ar1;
    cpu.ar[0] = ar0;
    cpu.r[0] = r0;
}

void createProcess(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    Ctx c{cpu, bus};
    // 0A844  PUSH R0; R0 = [C981]
    const uint32_t r0 = cpu.r[0];
    cpu.r[0] = c.rd(kFreeHead);
    if (cpu.r[0] == 0) {
        // 0A849  sem bloco livre: erro 0x210, carry ligado
        logError(cpu, bus, subs, kErrNoFreeBlock);
        setCarry(cpu, true);
        cpu.r[0] = r0;
        return;
    }
    // 0A851  tira da lista livre e põe no início da ativa
    cpu.ar[0] = cpu.r[0];
    cpu.r[0] = c.at(cpu.ar[0] + pcb::kNext);
    c.wr(kFreeHead, cpu.r[0]);
    cpu.r[0] = c.rd(kActiveHead);
    c.put(cpu.ar[0] + pcb::kNext, cpu.r[0]);
    c.wr(kActiveHead, cpu.ar[0]);
    // 0A857  sono 0, PC, ID, topo da pilha local, registradores
    cpu.r[0] = 0;
    c.put(cpu.ar[0] + pcb::kSleep, cpu.r[0]);
    c.put(cpu.ar[0] + pcb::kPc, cpu.ar[2]);
    c.put(cpu.ar[0] + pcb::kId, cpu.r[2]);
    cpu.r[0] = cpu.ar[0] + pcb::kStackOffset;
    c.put(cpu.ar[0] + pcb::kStackTop, cpu.r[0]);
    c.put(cpu.ar[0] + pcb::kAr4, cpu.ar[4]);
    c.put(cpu.ar[0] + pcb::kAr5, cpu.ar[5]);
    c.put(cpu.ar[0] + pcb::kAr6, cpu.ar[6]);
    c.put(cpu.ar[0] + pcb::kR4, cpu.r[4]);
    c.put(cpu.ar[0] + pcb::kR5, cpu.r[5]);
    c.put(cpu.ar[0] + pcb::kR6, cpu.r[6]);  // STF: o port guarda R6/R7 como float TMS cru
    c.put(cpu.ar[0] + pcb::kR7, cpu.r[7]);
    // 0A865  carry limpo; POP R0
    setCarry(cpu, false);
    cpu.r[0] = r0;
}

void createAfterCurrent(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    Ctx c{cpu, bus};
    // 0A868  CALL $A844; RETSLO
    createProcess(cpu, bus, subs);
    if (cpu.st & Tms32031State::kStCarry) return;
    // 0A86A  o novo (cabeça) sai do início e entra depois de AR7
    const uint32_t r0 = cpu.r[0];
    cpu.r[0] = c.at(cpu.ar[0]);
    c.wr(kActiveHead, cpu.r[0]);
    cpu.r[0] = c.at(cpu.ar[7]);
    c.put(cpu.ar[0], cpu.r[0]);
    c.put(cpu.ar[7], cpu.ar[0]);
    cpu.r[0] = r0;
}

void killProcess(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    Ctx c{cpu, bus};
    // 0A8AC  PUSH R1, AR1
    const uint32_t r1 = cpu.r[1], ar1 = cpu.ar[1];
    if (cpu.ar[7] == cpu.ar[2]) {
        // 0A8B1  encerrar a si mesmo: erro 0x223 e BEQ $A896 (R1/AR1 ficam na pilha)
        logError(cpu, bus, subs, kErrKillSelf);
        subs.call(0xA896, cpu, bus);
        return;
    }
    // 0A8B8  procura o anterior de AR2
    cpu.r[1] = c.rd(kActiveHeadPtr);
    for (;;) {
        cpu.ar[1] = cpu.r[1];
        cpu.r[1] = c.at(cpu.ar[1]);
        if (cpu.r[1] == 0) {
            logError(cpu, bus, subs, kErrKillNotFound);
            break;
        }
        if (cpu.r[1] != cpu.ar[2]) continue;
        // 0A8C6  desencadeia e põe no início da lista livre
        cpu.r[1] = c.at(cpu.ar[2]);
        c.put(cpu.ar[1], cpu.r[1]);
        cpu.ar[1] = c.rd(kFreeHeadPtr);
        cpu.r[1] = c.at(cpu.ar[1]);
        c.put(cpu.ar[2], cpu.r[1]);
        c.put(cpu.ar[1], cpu.ar[2]);
        break;
    }
    // 0A8CC  POP AR1, R1
    cpu.ar[1] = ar1;
    cpu.r[1] = r1;
}

void killMatching(Tms32031State& cpu, MemoryBus& bus) {
    Ctx c{cpu, bus};
    // 0A8CF  PUSH AR1, AR2; R0 &= R1; AR2 = pseudo-nó
    const uint32_t ar1 = cpu.ar[1], ar2 = cpu.ar[2];
    cpu.r[0] &= cpu.r[1];
    cpu.ar[2] = c.rd(kActiveHeadPtr);
    // 0A8D3  AR1 = anterior; R2 = próximo
    cpu.ar[1] = cpu.ar[2];
    for (;;) {
        cpu.r[2] = c.at(cpu.ar[1]);  // 0A8D4
        if (cpu.r[2] == 0) break;
        cpu.ar[2] = cpu.r[2];
        cpu.r[2] = c.at(cpu.ar[2] + pcb::kId) & cpu.r[1];
        if (cpu.r[2] != cpu.r[0] || cpu.ar[2] == cpu.ar[7]) {
            cpu.ar[1] = cpu.ar[2];  // 0A8D3
            continue;
        }
        // 0A8DD  desencadeia e libera; AR1 continua o anterior (BR $A8D4)
        cpu.r[2] = c.at(cpu.ar[2]);
        c.put(cpu.ar[1], cpu.r[2]);
        cpu.r[2] = c.rd(kFreeHead);
        c.put(cpu.ar[2], cpu.r[2]);
        c.wr(kFreeHead, cpu.ar[2]);
    }
    cpu.ar[2] = ar2;
    cpu.ar[1] = ar1;
}

void existsMatching(Tms32031State& cpu, MemoryBus& bus) {
    Ctx c{cpu, bus};
    // 0A8E6  carry limpo; RC = AR2 & R2
    setCarry(cpu, false);
    const uint32_t ar2 = cpu.ar[2];
    cpu.rc = cpu.ar[2] & cpu.r[2];
    cpu.ar[2] = c.rd(kActiveHeadPtr);
    for (;;) {
        cpu.r[0] = c.at(cpu.ar[2]);
        if (cpu.r[0] == 0) break;
        cpu.ar[2] = cpu.r[0];
        cpu.rs = c.at(cpu.ar[2] + pcb::kId) & cpu.r[2];
        if (cpu.rs == cpu.rc) {
            setCarry(cpu, true);
            break;
        }
    }
    cpu.ar[2] = ar2;
}

void redirect(Tms32031State& cpu, MemoryBus& bus) {
    // 0A908  +2 = AR1; +4 = 1
    bus.writeWord(cpu.ar[0] + pcb::kPc, cpu.ar[1]);
    cpu.r[0] = 1;
    bus.writeWord(cpu.ar[0] + pcb::kSleep, cpu.r[0]);
}

void findProcess(Tms32031State& cpu, MemoryBus& bus, bool fromHead) {
    Ctx c{cpu, bus};
    // 0A912 / 0A90E  PUSH R2; R0 &= R1; (0A915: AR0 = pseudo-nó)
    const uint32_t r2 = cpu.r[2];
    cpu.r[0] &= cpu.r[1];
    if (fromHead) cpu.ar[0] = c.rd(kActiveHeadPtr);
    for (;;) {
        // 0A918  R2 = próximo; BNED (slots: AR0 = R2; R2 = ID & R1)
        cpu.r[2] = c.at(cpu.ar[0]);
        const bool more = cpu.r[2] != 0;
        cpu.ar[0] = cpu.r[2];
        cpu.r[2] = c.at(cpu.ar[0] + pcb::kId) & cpu.r[1];
        if (!more) break;           // AR0 = 0
        if (cpu.r[0] == cpu.r[2]) break;  // 0A916/0A917  achou
    }
    cpu.r[2] = r2;
}

void moveAfter(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    Ctx c{cpu, bus};
    // 0A91F  PUSH R1, AR1, AR2, AR7
    const uint32_t r1 = cpu.r[1], ar1 = cpu.ar[1], ar2 = cpu.ar[2], ar7 = cpu.ar[7];
    cpu.r[1] = c.rd(kActiveHeadPtr);
    for (;;) {
        cpu.ar[1] = cpu.r[1];
        cpu.r[1] = c.at(cpu.ar[1]);
        if (cpu.r[1] == 0) {
            logError(cpu, bus, subs, kErrMoveNotFound);
            break;
        }
        if (cpu.ar[7] != cpu.r[1]) continue;
        // 0A931  desencadeia AR7 e reinsere depois de AR2
        cpu.r[1] = c.at(cpu.ar[7]);
        c.put(cpu.ar[1], cpu.r[1]);
        cpu.r[1] = c.at(cpu.ar[2]);
        c.put(cpu.ar[7], cpu.r[1]);
        c.put(cpu.ar[2], cpu.ar[7]);
        break;
    }
    cpu.ar[7] = ar7;
    cpu.ar[2] = ar2;
    cpu.ar[1] = ar1;
    cpu.r[1] = r1;
}

void countById(Tms32031State& cpu, MemoryBus& bus) {
    Ctx c{cpu, bus};
    // 069D6  R0 = 0; R1 = pseudo-nó
    cpu.r[0] = 0;
    cpu.r[1] = c.rd(kActiveHeadPtr);
    if (cpu.r[1] == 0) return;
    do {
        cpu.ar[0] = cpu.r[1];
        cpu.r[1] = c.at(cpu.ar[0] + pcb::kId);
        if (cpu.r[1] == cpu.r[2]) ++cpu.r[0];
        cpu.r[1] = c.at(cpu.ar[0]);
    } while (cpu.r[1] != 0);
}

void countClass40(Tms32031State& cpu, MemoryBus& bus) {
    Ctx c{cpu, bus};
    // 0894D  R2 = 0; R0 = pseudo-nó
    cpu.r[2] = 0;
    cpu.r[0] = c.rd(kActiveHeadPtr);
    if (cpu.r[0] != 0) {
        do {
            cpu.ar[0] = cpu.r[0];
            cpu.r[0] = c.at(cpu.ar[0] + pcb::kId);
            if (cpu.r[0] & 0x0500u) {
                cpu.r[0] &= 0x00F0u;
                if (cpu.r[0] == 0x40) ++cpu.r[2];
            }
            cpu.r[0] = c.at(cpu.ar[0]);
        } while (cpu.r[0] != 0);
        // 0895A  R2 ≥ 2 → carry
        if (s(cpu.r[2]) >= 2) {
            setCarry(cpu, true);
            return;
        }
    }
    setCarry(cpu, false);
}

void launchEventProcesses(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    Ctx c{cpu, bus};
    // 04DE6  R3 = [C8CB] sem os 4 bits altos; 0 → RETS
    cpu.r[3] = (c.rd(kEventBits) << 4) >> 4;
    if (cpu.r[3] == 0) return;
    cpu.r[1] = 0;
    c.wr(kEventBits, cpu.r[1]);
    cpu.r[1] = 0xFFFFFFFFu;
    do {
        // 04DED  próximo bit ligado (LSH −1 põe o bit que sai no carry)
        bool carry;
        do {
            cpu.r[1] += 1;
            carry = cpu.r[3] & 1u;
            cpu.r[3] >>= 1;
        } while (!carry);
        // 04DF0  AR2 = tabela[bit]; ≠ 0 → cria processo 0x810
        cpu.ar[0] = c.rd(kEventTablePtr) + cpu.r[1];
        cpu.ar[2] = c.at(cpu.ar[0]);
        if (cpu.ar[2] != 0) {
            cpu.r[2] = kEventProcId;
            subs.call(0xA844, cpu, bus);
        }
    } while (cpu.r[3] != 0);  // 04DF7
}

void resetSubsystems(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    // 095CB  CALL $A8F5, $6FEB, $7648, $6754, $8FF0, $9019, $AF55; [C93E] = 0
    for (uint32_t r : {0xA8F5u, 0x6FEBu, 0x7648u, 0x6754u, 0x8FF0u, 0x9019u, 0xAF55u}) subs.call(r, cpu, bus);
    cpu.r[0] = 0;
    bus.writeWord(cpu.direct(0xC93E), cpu.r[0]);
}

// --- Escalonador --------------------------------------------------------------

void Scheduler::run(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    Ctx c{cpu, bus};
    // 0A872  AR7 = pseudo-nó
    cpu.ar[7] = c.rd(kActiveHeadPtr);
    for (uint32_t n = 0; n < kMaxDispatchPerRun;) {
        // 0A881  R0 = próximo; BEQD $A895 (slots: AR7 = R0; R0 = sono − 1)
        cpu.r[0] = c.at(cpu.ar[7]);
        const bool end = cpu.r[0] == 0;
        cpu.ar[7] = cpu.r[0];
        cpu.r[0] = c.at(cpu.ar[7] + pcb::kSleep) - 1;
        if (end) return;  // 0A895  RETS
        // 0A886  BGTD $A882 (slots: grava o sono; R0 = próximo)
        c.put(cpu.ar[7] + pcb::kSleep, cpu.r[0]);
        const bool still = s(cpu.r[0]) > 0;
        cpu.r[0] = c.at(cpu.ar[7]);
        if (still) continue;
        // 0A88A  [C97F] = SP; [C97E] = AR7; restaura os registradores; BUD PC
        c.wr(kSchedulerSp, cpu.sp);
        c.wr(kCurrent, cpu.ar[7]);
        cpu.ar[6] = c.at(cpu.ar[7] + pcb::kAr6);
        cpu.ar[5] = c.at(cpu.ar[7] + pcb::kAr5);
        cpu.ar[4] = c.at(cpu.ar[7] + pcb::kAr4);
        cpu.r[7] = c.at(cpu.ar[7] + pcb::kR7);
        cpu.r[0] = c.at(cpu.ar[7] + pcb::kPc);
        cpu.r[6] = c.at(cpu.ar[7] + pcb::kR6);
        cpu.r[5] = c.at(cpu.ar[7] + pcb::kR5);
        cpu.r[4] = c.at(cpu.ar[7] + pcb::kR4);
        const uint32_t pc = cpu.r[0];
        outcome_ = Outcome::Returned;
        ++dispatched_;
        ++n;
        subs.call(pc, cpu, bus);
        if (cpu.jump != 0) return;  // salto não-local (ex.: 0x1E17 → 0x4C1B): a pilha do escalonador foi descartada
        if (outcome_ == Outcome::Returned) {
            // Port: processo não traduzido. Retoma no mesmo PC no próximo quadro.
            ++untranslated_[pc];
            cpu.sp = c.rd(kSchedulerSp);
            cpu.ar[7] = c.rd(kCurrent);
            c.put(cpu.ar[7] + pcb::kSleep, 1);
        }
        // Slept/Died: AR7 já aponta de onde o percurso continua (0A881).
    }
}

void Scheduler::sleep(Tms32031State& cpu, MemoryBus& bus) {
    Ctx c{cpu, bus};
    // 0A874  POP R0 (retorno do CALL) → PC de retomada no PCB atual (AR7)
    cpu.r[0] = cpu.ret;
    c.put(cpu.ar[7] + pcb::kPc, cpu.r[0]);
    // 0A876  DP = 0; SP = [C97F]; AR7 = [C97E]; salva os registradores; sono = AR2
    cpu.dp = 0;
    cpu.sp = c.rd(kSchedulerSp);
    cpu.ar[7] = c.rd(kCurrent);
    c.put(cpu.ar[7] + pcb::kR4, cpu.r[4]);
    c.put(cpu.ar[7] + pcb::kR5, cpu.r[5]);
    c.put(cpu.ar[7] + pcb::kR6, cpu.r[6]);
    c.put(cpu.ar[7] + pcb::kR7, cpu.r[7]);
    c.put(cpu.ar[7] + pcb::kSleep, cpu.ar[2]);
    c.put(cpu.ar[7] + pcb::kAr4, cpu.ar[4]);
    c.put(cpu.ar[7] + pcb::kAr5, cpu.ar[5]);
    c.put(cpu.ar[7] + pcb::kAr6, cpu.ar[6]);
    outcome_ = Outcome::Slept;
}

void Scheduler::die(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    Ctx c{cpu, bus};
    // 0A896  R0 = próximo; o PCB vai para o início da lista livre
    cpu.r[0] = c.at(cpu.ar[7]);
    cpu.ar[1] = c.rd(kFreeHead);
    c.put(cpu.ar[7], cpu.ar[1]);
    c.wr(kFreeHead, cpu.ar[7]);
    // 0A89A  procura o anterior
    cpu.r[1] = c.rd(kActiveHeadPtr);
    for (;;) {
        cpu.ar[1] = cpu.r[1];
        cpu.r[1] = c.at(cpu.ar[1]);
        if (cpu.r[1] == 0) {
            // 0A89F  erro 0x220; BEQ $A881 com AR7 = o bloco liberado (como no original)
            logError(cpu, bus, subs, kErrDieNotFound);
            break;
        }
        if (cpu.ar[7] != cpu.r[1]) continue;
        // 0A8A8  anterior → próximo; AR7 = anterior
        c.put(cpu.ar[1], cpu.r[0]);
        cpu.ar[7] = cpu.ar[1];
        break;
    }
    outcome_ = Outcome::Died;
}

void registerRoutines(SubroutineDispatcher& d, Scheduler& sched) {
    using S = ISubroutines;
    d.add(0xA8F5, [](Tms32031State& c, MemoryBus& b, S&) { initLists(c, b); });
    d.add(0xA844, createProcess);
    d.add(0xA868, createAfterCurrent);
    d.add(0xA8AC, killProcess);
    d.add(0xA8CF, [](Tms32031State& c, MemoryBus& b, S&) { killMatching(c, b); });
    d.add(0xA8E6, [](Tms32031State& c, MemoryBus& b, S&) { existsMatching(c, b); });
    d.add(0xA908, [](Tms32031State& c, MemoryBus& b, S&) { redirect(c, b); });
    d.add(0xA90E, [](Tms32031State& c, MemoryBus& b, S&) { findProcess(c, b, false); });
    d.add(0xA912, [](Tms32031State& c, MemoryBus& b, S&) { findProcess(c, b, true); });
    d.add(0xA91F, moveAfter);
    d.add(0x69D6, [](Tms32031State& c, MemoryBus& b, S&) { countById(c, b); });
    d.add(0x894D, [](Tms32031State& c, MemoryBus& b, S&) { countClass40(c, b); });
    d.add(0x4DE6, launchEventProcesses);
    d.add(0x95CB, resetSubsystems);
    d.add(0xA872, [&sched](Tms32031State& c, MemoryBus& b, S& subs) { sched.run(c, b, subs); });
    d.add(0xA874, [&sched](Tms32031State& c, MemoryBus& b, S&) { sched.sleep(c, b); });
    d.add(0xA896, [&sched](Tms32031State& c, MemoryBus& b, S& subs) { sched.die(c, b, subs); });
}

} // namespace rea::core::proc
