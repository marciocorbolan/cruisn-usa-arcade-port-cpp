/**
 * @file game_flow.cpp
 * @brief Tradução de `0x168C`–`0x1840` (fluxo da partida) e das rotinas auxiliares `0x1C5B`,
 *        `0x317C`, `0x6DFB`, `0x6FD4`, `0x7F42`, `0x7F4D`, `0x9937`, `0x9EEC` e `0xB06F`.
 */
#include "core/game/game_flow.hpp"

#include <cmath>
#include <utility>

#include "core/proc/process_manager.hpp"
#include "core/tms_float.hpp"

namespace rea::core::game {

using namespace flow_vars;
namespace pcb = proc::pcb;

void localPush(Tms32031State& cpu, MemoryBus& bus, uint32_t value) {
    // LDI *+AR7(1),AR0 / STI value,*AR0++(1) / STI AR0,*+AR7(1)
    cpu.ar[0] = bus.readWord(cpu.ar[7] + pcb::kStackTop);
    bus.writeWord(cpu.ar[0]++, value);
    bus.writeWord(cpu.ar[7] + pcb::kStackTop, cpu.ar[0]);
}

uint32_t localPop(Tms32031State& cpu, MemoryBus& bus) {
    // LDI *+AR7(1),AR0 / LDI *--AR0(1),Rn / STI AR0,*+AR7(1)
    cpu.ar[0] = bus.readWord(cpu.ar[7] + pcb::kStackTop) - 1;
    bus.writeWord(cpu.ar[7] + pcb::kStackTop, cpu.ar[0]);
    return bus.readWord(cpu.ar[0]);
}

void processJump(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs, uint32_t pc) {
    bus.writeWord(cpu.ar[7] + pcb::kPc, pc);  // port: o escalonador retoma aqui se o alvo não estiver traduzido
    subs.call(pc, cpu, bus);
}

void localCall(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs, uint32_t targetPtr, uint32_t retPtr) {
    cpu.r[0] = bus.readWord(retPtr);
    localPush(cpu, bus, cpu.r[0]);
    cpu.r[0] = bus.readWord(targetPtr);
    processJump(cpu, bus, subs, cpu.r[0]);
}

void localReturn(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    cpu.r[0] = localPop(cpu, bus);
    processJump(cpu, bus, subs, cpu.r[0]);
}

namespace {

/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }

/// Contexto do fluxo de jogo: CPU, barramento, sub-rotinas e atalhos.
struct Ctx {
    Tms32031State& cpu;  ///< Registradores da CPU TMS320C31.
    MemoryBus& bus;  ///< Barramento de memória simulado.
    ISubroutines& subs;  ///< Despachante das sub-rotinas traduzidas.
    /// Lê a palavra no endereço @p a.
    uint32_t rd(uint32_t a) { return bus.readWord(a); }
    /// Grava a palavra @p v no endereço @p a.
    void wr(uint32_t a, uint32_t v) { bus.writeWord(a, v); }
    /// Chama a sub-rotina traduzida no endereço da ROM indicado (o `CALL` original).
    void call(uint32_t a) { subs.call(a, cpu, bus); }
    /// Modo do outro gabinete (`[C8D6]` & 0xF).
    uint32_t mode() { return rd(0xC8D6) & 0xFu; }
    /// Lê o item @p i da CMOS (`0x99E9`).
    uint32_t item(uint32_t i) {
        cpu.ar[2] = i;
        call(0x99E9);
        return cpu.r[0];
    }
    /// Incrementa o item de auditoria @p i (`0x99DA`).
    void audit(uint32_t i) {
        cpu.ar[2] = i;
        call(0x99DA);
    }
    /// `LDI n,AR2 / CALL $A874` com retomada em @p resume.
    void sleep(uint32_t n, uint32_t resume) {
        cpu.ar[2] = n;
        cpu.ret = resume;
        call(0xA874);
    }
    /// Desvia o processo atual para @p pc (`processJump`).
    void jump(uint32_t pc) { processJump(cpu, bus, subs, pc); }
    /// Chamada local do processo (`localCall`): salta para @p targetPtr e guarda @p retPtr para o retorno.
    void lcall(uint32_t targetPtr, uint32_t retPtr) { localCall(cpu, bus, subs, targetPtr, retPtr); }
    /// Retorno de uma chamada local do processo (`localReturn`).
    void lret() { localReturn(cpu, bus, subs); }
    /// Empilha @p v na pilha local do processo.
    void push(uint32_t v) { localPush(cpu, bus, v); }
    /// Desempilha da pilha local do processo.
    uint32_t pop() { return localPop(cpu, bus); }
    /// Grava o float @p v em @p a.
    void setF(uint32_t a, double v) { wr(a, doubleToTms(v)); }
    /// `[AR0 + off] |= bits` (texto centralizado etc.).
    void orField(uint32_t obj, uint32_t off, uint32_t bits) {
        cpu.r[0] = rd(obj + off) | bits;
        wr(obj + off, cpu.r[0]);
    }
    /// `LDI @(EA33),AR2 / CMPI 0,AR2 / CALLNE $7100`.
    void freeLinkWaitObj() {
        cpu.ar[2] = rd(kLinkWaitObj);
        if (cpu.ar[2] != 0) call(0x7100);
    }
};

// --- 0x168C–0x171F: tela de espera do link ----------------------------------------

/// `0x168C`: carry = 1 quando a espera do link acabou (sem link, falha, desistência ou modo 2).
bool linkWaitDone(Ctx& c) {
    if (c.rd(kLinkOn) == 0 || c.rd(0xC8E5) != 0 || c.rd(0xC8DD) != 0 || c.rd(0xC8E3) != 0) return true;
    c.cpu.r[0] = c.mode();
    return c.cpu.r[0] == 2;
}

void linkWaitEnd(Ctx& c);

/// `0x16E6`: mostra os dois textos `[B1E3]`/`[B1E4]` até a espera acabar.
void linkWaitLoop(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    c.wr(0xC8E4, 1);
    for (auto [ptr, y] : {std::pair{0xB1E3u, 100.0}, std::pair{0xB1E4u, 160.0}}) {
        cpu.ar[2] = c.rd(ptr);
        cpu.r[2] = doubleToTms(256.0);
        cpu.r[3] = doubleToTms(y);
        cpu.rc = 1;
        c.call(0x7660);
        c.call(0x76BF);
        c.orField(cpu.ar[0], 8, 0x100);
    }
    if (linkWaitDone(c)) return linkWaitEnd(c);
    c.sleep(1, 0x16FE);  // 016FE  BU $16E6
}

/// `0x16FF`: `[C8E4]` = 0; espera 2 quadros e desmonta a tela (`0x1703`).
void linkWaitEnd(Ctx& c) {
    c.wr(0xC8E4, 0);
    c.sleep(2, 0x1703);
}

/// `0x1703`: libera os objetos da tela (3 painéis ou o objeto `[EA33]`) e retorna.
void linkWaitTeardown(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[6] = c.pop();
    if (cpu.r[6] == 1) {
        c.freeLinkWaitObj();
    } else {
        for (int i = 0; i < 3; ++i) {
            cpu.ar[2] = c.pop();
            c.call(0x7100);
        }
        cpu.ar[2] = c.rd(0xB1E5);
        c.call(0x9EEC);
        cpu.ar[2] = c.rd(0xB1E6);
        c.call(0x9EEC);
    }
    c.lret();
}

/// `0x169C`: empilha R6 (1 = só o fundo `0x6FD4`) e espera 2 quadros.
void linkWaitStart(Ctx& c) {
    c.push(c.cpu.r[6]);
    c.sleep(2, 0x16A1);
}

/// `0x16A1`: monta a tela de espera (3 painéis com paletas, ou o fundo) e entra no laço.
void linkWaitBuild(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    if (linkWaitDone(c)) return linkWaitEnd(c);
    cpu.r[6] = c.pop();
    if (cpu.r[6] == 1) {
        c.call(0x6FD4);
        cpu.ar[2] = cpu.ar[0];
        c.call(0x7070);
    } else {
        cpu.ar[2] = c.rd(0xB1DB);
        c.call(0x9F2F);
        cpu.ar[2] = c.rd(0xB1DC);
        c.call(0x9F2F);
        struct Panel { uint32_t model; int32_t y; uint32_t palette; };
        static constexpr Panel kPanels[] = {{0xB1DD, -0x3C, 0xB1DE}, {0xB1DF, 0x3C, 0xB1E0}, {0xB1E1, 0, 0xB1E2}};
        for (const Panel& p : kPanels) {
            cpu.ar[2] = c.rd(p.model);
            cpu.r[2] = 0;
            cpu.r[3] = static_cast<uint32_t>(p.y);
            cpu.rc = 0x170;
            c.call(0x7309);
            c.orField(cpu.ar[0], 0xE, 0x4A3);
            cpu.ar[2] = cpu.ar[0];
            c.push(cpu.ar[2]);
            c.call(0x7070);
            cpu.ar[0] = cpu.ar[2];
            cpu.ar[2] = c.rd(p.palette);
            c.call(0x9EDB);
            c.wr(cpu.ar[0] + 0x10, cpu.r[0]);
        }
    }
    c.push(cpu.r[6]);  // 016E3
    linkWaitLoop(c);
}

// --- 0x1720–0x1774: sincronização com o link ---------------------------------------

/// Trecho traduzido da ROM em `0x176A`.
void linkSyncSolo(Ctx& c) {
    c.call(0x7A5F);
    c.sleep(1, 0x176D);
}

/// Trecho traduzido da ROM em `0x1764`.
void linkSyncFinish(Ctx& c) {
    c.wr(0xE63D, 0);
    c.lret();
}

/// `0x1752`: até 30 quadros esperando `[C8DE]` (1 = o outro gabinete entrou na partida).
void linkSyncWait(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    --cpu.ar[5];
    if (s(cpu.ar[5]) <= 0) return linkSyncFinish(c);
    c.sleep(1, 0x1757);
}

/// Trecho traduzido da ROM em `0x1757`.
void linkSyncAfterWait(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    if (c.rd(kLinkOn) == 0) return linkSyncSolo(c);
    cpu.r[0] = c.rd(0xC8DE);
    if (cpu.r[0] == 0) return linkSyncWait(c);
    if (cpu.r[0] == 1) {
        c.wr(0xC8E3, 1);
        c.audit(0xF8);
        cpu.r[0] = 0;
        c.wr(0xC8DD, 0);
        c.wr(0xE594, 0);
    }
    linkSyncFinish(c);
}

/// `0x173F`: decide se espera a resposta do outro gabinete.
void linkSyncDecide(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    const bool wait = c.rd(0xE53E) != 0 || c.rd(0xC8E5) != 0 || s(c.rd(0xC8E8)) == -7 || c.mode() == 2 || c.mode() == 3;
    if (!wait) return linkSyncSolo(c);
    c.wr(0xC8E4, 0);
    c.wr(0xC8DE, 0);
    c.call(0x7A61);
    cpu.ar[5] = 0x1E;
    linkSyncWait(c);
}

/// `0x1720` (R6 = 1) / `0x1722` (R6 = 0): sincroniza a partida com o link.
void linkSync(Ctx& c) {
    if (c.rd(kLinkOn) == 0) return linkSyncSolo(c);
    const uint32_t m = c.mode();
    if (c.rd(kPlaying) == 0 && c.rd(0xE53E) == 0 && (m == 5 || m == 6 || m == 7)) return linkSyncSolo(c);
    if (m == 5 || m == 6 || m == 7) return c.lcall(0xB1E7, 0xB1E8);  // tela de espera 0x169C → 0x173F
    linkSyncDecide(c);
}

// --- 0x1775–0x1840: o processo de fluxo --------------------------------------------

/// Trecho traduzido da ROM em `0x1775`.
void flowStart(Ctx& c) {
    c.wr(0xE636, 2);
    c.wr(kGameMode, 0x203);
    c.wr(0xE53E, 0);
    c.wr(kPlaying, 1);
    c.cpu.r[0] = 1;
    c.lcall(0xB1E9, 0xB1EA);  // 0x1722 → 0x1783
}

void flowWaitIdle(Ctx& c);

/// Trecho traduzido da ROM em `0x1783`.
void flowNewGame(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    c.call(0x9A77);
    cpu.r[2] = 0xFFFFFFFFu;
    cpu.ar[2] = 0x33;
    c.call(0x99EE);
    c.audit(0x6F);
    c.audit(0xD2);
    c.call(0x9937);
    c.wr(0xE481, 0);
    c.setF(0xE639, 1.0);
    // 01790  itens 0x8F e 0x90 devem ser iguais; senão erro 0x700 e corrige o 0x90
    cpu.r[1] = c.item(0x8F);
    cpu.r[0] = c.item(0x90);
    if (cpu.r[0] != cpu.r[1]) {
        const uint32_t r0 = cpu.r[0], st = cpu.st;
        cpu.r[0] = 0x700;
        c.call(0xAB1A);
        cpu.r[0] = r0;
        cpu.st = st;
        cpu.r[2] = cpu.r[1];
        cpu.ar[2] = 0x90;
        c.call(0x99F4);
    }
    c.audit(0x75);
    c.wr(0xE916, 1);
    flowWaitIdle(c);
}

/// `0x17A7`: espera `[0x809A79]` = 0 e começa a seleção.
void flowWaitIdle(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[0] = c.rd(0x809A79);
    cpu.dp = 0;
    if (cpu.r[0] != 0) return c.sleep(1, 0x17AD);  // 017AD  BU $17A7
    cpu.ar[2] = 0xF3;
    c.call(0x91E0);
    c.wr(0xC8CD, 1);
    for (uint32_t a : {0xE606u, 0xE635u, 0xE605u, 0xE3FEu, 0xE641u, kCourse}) c.wr(a, 0);
    cpu.r[0] = 0xFFFFFFFFu;
    c.wr(0xE84A, cpu.r[0]);
    for (uint32_t r : {0x317Cu, 0x1C5Bu, 0x6FEBu, 0x7648u}) c.call(r);
    c.lcall(0xB1EB, 0xB1EC);  // seleção do percurso 0x6C8F → 0x17C5
}

void flowAttract(Ctx& c);

/// Trecho traduzido da ROM em `0x17C5`.
void flowAfterCourse(Ctx& c) {
    if (c.rd(kPlaying) == 0) return flowAttract(c);
    for (uint32_t r : {0x1C5Bu, 0x6FEBu, 0x7648u}) c.call(r);
    c.wr(kSelectStep, 0);
    c.lcall(0xB1ED, 0xB1EE);  // 0x59EC → 0x17D2
}

/// Trecho traduzido da ROM em `0x17D2`.
void flowAfterCar(Ctx& c) {
    c.wr(kSelectStep, 1);
    c.lcall(0xB1EF, 0xB1F0);  // 0x18A6 → 0x17DA
}

void flowWaitReady(Ctx& c);

/// Trecho traduzido da ROM em `0x17DA`.
void flowReady(Ctx& c) {
    c.wr(0xC8CD, 4);
    c.sleep(1, 0x17DE);
}

void flowRaceSetup(Ctx& c);

/// Trecho traduzido da ROM em `0x17DE`.
void flowWaitReady(Ctx& c) {
    if (c.rd(kRaceReady) == 0) return c.sleep(1, 0x17DE);
    flowRaceSetup(c);
}

/// `0x17E0`: prepara a corrida.
void flowRaceSetup(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    c.wr(kGameMode, c.rd(kGameMode) | 0x40u);
    c.wr(kTimer, c.item(0x1C) * 5 + 0x3C);
    cpu.ar[2] = c.rd(0xB1F1);
    cpu.r[2] = 0x5C0;
    c.call(0xA868);
    c.call(0x6DFB);
    cpu.r[0] = 0x5C1;
    cpu.r[1] = 0xFFFFFFFFu;
    c.call(0xA8CF);
    c.setF(0xC93A, 0.0);
    c.audit(0x72);
    c.wr(0xC938, 0xAA);
    for (uint32_t a : {0xC931u, 0xC946u, 0xE5EBu}) c.wr(a, 1);
    for (uint32_t a : {0xE5F8u, 0xE480u, 0xE47Fu}) c.wr(a, 0);
    c.wr(0xE607, 9);
    c.wr(0xE868, c.item(0x1D));
    c.wr(0xC940, 1);
    if (c.rd(kPlaying) == 0) return c.jump(0x29EE);
    cpu.r[2] = 0;
    cpu.ar[2] = 0xDF;
    c.call(0x99F4);
    c.setF(0xC8EB, 1.0);
    c.call(0xA896);
}

/// `0x181F`: atrato — prepara o percurso `[E634]` e mostra a largada.
void flowAttract(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t course = c.rd(kCourse);
    c.call(c.rd(course + c.rd(0x8C12)));
    c.wr(kGameMode, 0x43);
    c.wr(0x809800, c.rd(0xE3FF));
    c.wr(0x809801, c.rd(0xE400));
    c.wr(0x809802, c.rd(0xE401));
    cpu.dp = 0;
    c.wr(0xE3FE, c.rd(c.rd(kCourse) + c.rd(0x18A5)));
    c.call(0x3F8D);
    c.call(0x1859);
    c.wr(kGameMode, 4);
    c.call(0xB06F);
    c.sleep(6, 0x1838);
}

/// Trecho traduzido da ROM em `0x1838`.
void flowAttractStart(Ctx& c) {
    c.wr(kGameMode, 0x144);
    c.call(c.rd(c.rd(kCourse) + c.rd(0x3965)));
    c.wr(0xC8CD, 4);
    flowRaceSetup(c);
}

/// `0x180C`: encerra a partida de atrato e volta a `0x17A7`.
void flowAttractEnd(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    c.call(0x7648);
    for (uint32_t id : {0x5C4u, 0x110u}) {
        cpu.r[0] = id;
        cpu.r[1] = 0xFFFFFFFFu;
        c.call(0xA8CF);
    }
    c.wr(kGameMode, c.rd(kGameMode) & ~0xC0u);
    for (uint32_t r : {0x6FEBu, 0x8FF0u, 0x9019u, 0xAF55u}) c.call(r);
    c.wr(kPlaying, 0);
    c.sleep(4, 0x181E);  // 0181E  BU $17A7
}

// --- Rotinas auxiliares ------------------------------------------------------------

/// `0x317C`: copia os itens 1..7 da CMOS (como float) para `[E89A]`..; `[E8A1]` = `[E89D]` − `[E89C]`.
void loadPedalSetup(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t ar3 = cpu.ar[3];
    cpu.ar[3] = c.rd(0x317B);
    for (cpu.ar[2] = 1; cpu.ar[2] <= 7; ++cpu.ar[2]) {
        const uint32_t ar2 = cpu.ar[2];
        c.call(0x99E9);
        cpu.ar[2] = ar2;
        cpu.r[0] = doubleToTms(static_cast<double>(s(cpu.r[0])));
        c.wr(cpu.ar[3]++, cpu.r[0]);
    }
    c.setF(0xE8A1, tmsToDouble(c.rd(0xE89D)) - tmsToDouble(c.rd(0xE89C)));
    cpu.r[0] = c.rd(0xE8A1);
    cpu.ar[3] = ar3;
}

/// `0x1C5B`: R5 = 1 se o acelerador (`[C93B]`) passar do repouso (`[E89A]`) + 20.
void gasPressed(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[5] = 0;
    cpu.r[0] = c.rd(0xC93B);
    cpu.r[1] = static_cast<uint32_t>(static_cast<int32_t>(std::floor(tmsToDouble(c.rd(kGasSetup))))) + 0x14;
    if (s(cpu.r[0]) >= s(cpu.r[1])) cpu.r[5] = 1;
}

/// `0x7F42` (mestre) / `0x7F4D` (escravo): programa o registrador do link `[0x997000]`.
void setLinkRole(Ctx& c, bool slave) {
    const bool right = (c.rd(kCabFlags) & 1u) != 0;
    c.wr(kLinkOn, slave ? 1 : 0);
    c.wr(kLinkMaster, slave ? 0 : 1);
    const int32_t v = slave ? (right ? 0x3000 : -0x4000) : (right ? 0x3200 : -0x3C00);
    c.wr(c.rd(0x7F17), static_cast<uint32_t>(v) << 16);
}

/// `0x9937`: item 0x21 − 1 (mínimo 0), gravado com checksum.
void decrementItem21(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[0] = c.item(0x21) - 1;
    if (s(cpu.r[0]) < 0) cpu.r[0] = 0;
    cpu.r[2] = cpu.r[0];
    cpu.ar[2] = 0x21;
    c.call(0x99EE);
}

/// `0x9EEC`: libera a entrada AR2 >> 8 das tabelas `[9E7F]` e `[9E7D]`.
void clearPaletteSlot(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t idx = cpu.ar[2] >> 8;
    cpu.ar[2] = idx;
    cpu.dp = 0;
    cpu.r[0] = 0;
    c.wr(c.rd(0x9E7F) + idx, 0);
    c.wr(c.rd(0x9E7D) + idx, 0);
}

} // namespace

void registerGameFlow(SubroutineDispatcher& d) {
    auto add = [&d](uint32_t a, void (*f)(Ctx&)) {
        d.add(a, [f](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
            Ctx x{c, b, s};
            f(x);
        });
    };
    d.add(0x168C, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        Ctx x{c, b, s};
        c.setFlag(Tms32031State::kStCarry, linkWaitDone(x));
    });
    add(0x169C, linkWaitStart);
    add(0x16A1, linkWaitBuild);
    add(0x16FE, linkWaitLoop);
    add(0x1703, linkWaitTeardown);
    add(0x1720, [](Ctx& c) { c.cpu.r[6] = 1; linkSync(c); });
    add(0x1722, [](Ctx& c) { c.cpu.r[6] = 0; linkSync(c); });
    add(0x173F, linkSyncDecide);
    add(0x1757, linkSyncAfterWait);
    add(0x176D, [](Ctx& c) {
        c.call(0x793A);
        c.wr(0xE63D, 1);
        c.cpu.r[0] = 1;
        c.call(0x7F42);
        c.lret();
    });
    add(0x1775, flowStart);
    add(0x1783, flowNewGame);
    add(0x17AD, flowWaitIdle);
    add(0x17C5, flowAfterCourse);
    add(0x17D2, flowAfterCar);
    add(0x17DA, flowReady);
    add(0x17DE, flowWaitReady);
    add(0x180C, flowAttractEnd);
    add(0x181E, flowWaitIdle);
    add(0x181F, flowAttract);
    add(0x1838, flowAttractStart);
    add(0x1C5B, gasPressed);
    add(0x317C, loadPedalSetup);
    add(0x6DFB, [](Ctx& c) {
        c.cpu.ar[2] = c.rd(0xC6F6);
        c.call(0x9DFE);
    });
    add(0x6FD4, [](Ctx& c) {
        c.cpu.ar[2] = c.rd(0xC700);
        c.cpu.r[2] = 0;
        c.cpu.r[3] = 0;
        c.cpu.rc = 0;
        c.call(0x7309);
        c.wr(kLinkWaitObj, c.cpu.ar[0]);
    });
    d.add(0x7F42, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        const uint32_t r0 = c.r[0], ar0 = c.ar[0];
        Ctx x{c, b, s};
        setLinkRole(x, false);
        c.r[0] = r0;
        c.ar[0] = ar0;
    });
    d.add(0x7F4D, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        const uint32_t r0 = c.r[0], ar0 = c.ar[0];
        Ctx x{c, b, s};
        setLinkRole(x, true);
        c.r[0] = r0;
        c.ar[0] = ar0;
    });
    add(0x9937, decrementItem21);
    d.add(0x9EEC, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        const uint32_t ar0 = c.ar[0];
        Ctx x{c, b, s};
        clearPaletteSlot(x);
        c.ar[0] = ar0;
    });
    add(0xB06F, [](Ctx& c) {
        c.wr(0xE5F8, 0);
        c.cpu.ar[2] = c.rd(0xB133);
        c.cpu.r[2] = 0x5C0;
        c.call(0xA868);
    });
}

} // namespace rea::core::game
