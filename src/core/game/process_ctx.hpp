#pragma once
/**
 * @file process_ctx.hpp
 * @brief Contexto comum das traduções de processos: acesso à memória, chamadas, sono, morte,
 *        variáveis do PCB (`*+AR7(n)`), chamadas locais e constantes float da ROM.
 */

#include <cstdint>

#include "core/game/game_flow.hpp"
#include "core/subroutines.hpp"
#include "core/tms_float.hpp"

namespace rea::core::game {

/// @brief Atalhos usados pelas rotinas de processo traduzidas.
struct ProcCtx {
    Tms32031State& cpu;  ///< Registradores da CPU TMS320C31.
    MemoryBus& bus;  ///< Barramento de memória simulado.
    ISubroutines& subs;  ///< Despachante das sub-rotinas traduzidas.

    /// Lê a palavra no endereço @p a.
    uint32_t rd(uint32_t a) const { return bus.readWord(a); }
    /// Grava a palavra @p v no endereço @p a.
    void wr(uint32_t a, uint32_t v) { bus.writeWord(a, v); }
    /// Lê o float TMS em @p a e o converte para double.
    double rf(uint32_t a) const { return tmsToDouble(bus.readWord(a)); }
    /// Converte @p v para float TMS e grava em @p a.
    void wf(uint32_t a, double v) { bus.writeWord(a, doubleToTms(v)); }
    /// Chama a sub-rotina traduzida no endereço da ROM indicado (o `CALL` original).
    void call(uint32_t a) { subs.call(a, cpu, bus); }

    /// Variável do processo `*+AR7(n)`.
    uint32_t var(uint32_t n) const { return bus.readWord(cpu.ar[7] + n); }
    /// Grava @p v na variável do processo `*+AR7(n)`.
    void setVar(uint32_t n, uint32_t v) { bus.writeWord(cpu.ar[7] + n, v); }

    /// Item da CMOS (`0x99E9`) → R0.
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
    /// `LDI id,AR2 / CALL $9188` (toca som).
    void sound(uint32_t id) {
        cpu.ar[2] = id;
        call(0x9188);
    }
    /// `LDI n,AR2 / CALL $A874`, retomando em @p resume. O chamador retorna em seguida.
    void sleep(uint32_t n, uint32_t resume) {
        cpu.ar[2] = n;
        cpu.ret = resume;
        call(0xA874);
    }
    /// Encerra o processo atual (`0xA896`).
    void die() { call(0xA896); }
    /// Desvia o processo atual para @p pc (`processJump`).
    void jump(uint32_t pc) { processJump(cpu, bus, subs, pc); }
    /// Chamada local do processo (`localCall`): salta para @p targetPtr e guarda @p retPtr para o retorno.
    void lcall(uint32_t targetPtr, uint32_t retPtr) { localCall(cpu, bus, subs, targetPtr, retPtr); }
    /// Retorno de uma chamada local do processo (`localReturn`).
    void lret() { localReturn(cpu, bus, subs); }

    /// Constante float curta embutida na instrução em @p pc (`K(pc)`).
    double k(uint32_t pc) const { return shortFloat(static_cast<uint16_t>(bus.readWord(pc) & 0xFFFFu)); }

    /// `[obj + off] |= bits`.
    void orField(uint32_t obj, uint32_t off, uint32_t bits) {
        cpu.r[0] = rd(obj + off) | bits;
        wr(obj + off, cpu.r[0]);
    }
    /// Texto centralizado: `LDI @(ptr),AR2 / FLOAT x,R2 / FLOAT y,R3 / LDI rc,RC / CALL fn` e flag 0x100.
    void text(uint32_t fn, uint32_t textAddr, double x, double y, uint32_t rc, bool pair) {
        cpu.ar[2] = textAddr;
        cpu.r[2] = doubleToTms(x);
        cpu.r[3] = doubleToTms(y);
        cpu.rc = rc;
        call(fn);
        orField(cpu.ar[0], 8, 0x100);
        if (pair) orField(cpu.ar[1], 8, 0x100);
    }
    /// Espera ativa por `[0x980082]` & @p bit = 0 (o DMA do port termina na hora; limite de segurança).
    void waitDma(uint32_t bit) {
        for (int i = 0; i < 1000000 && (rd(0x980082) & bit) != 0; ++i) {
        }
        cpu.dp = 0;
    }
};

/// @brief Registra @p f (que recebe um ProcCtx) no endereço @p a.
template <typename F>
void addProc(SubroutineDispatcher& d, uint32_t a, F f) {
    d.add(a, [f](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        ProcCtx x{c, b, s};
        f(x);
    });
}

} // namespace rea::core::game
