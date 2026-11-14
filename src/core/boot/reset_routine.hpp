#pragma once
/**
 * @file reset_routine.hpp
 * @brief Tradução da rotina de RESET do Cruis'n USA (ROM `0x4AD5`–`0x4BAE`).
 */

#include <cstdint>

#include "core/memory_bus.hpp"
#include "core/subroutines.hpp"
#include "core/tms32031_state.hpp"

namespace rea::core::boot {

/// Compatibilidade: as interfaces de despacho vivem em `core/subroutines.hpp`.
using core::ISubroutines;
using core::RecordingSubroutines;

/**
 * @brief Rotina de RESET (vetor 0 → `0x4AD5`).
 *
 * @details Sequência de inicialização do hardware e do estado do jogo:
 *  1. Configura pinos XF, wait states do barramento e cache;
 *  2. Carrega SP (= 0x809C00, RAM interna) e desliga interrupções;
 *  3. Protege a CMOS e zera a RAM interna do TMS320C31;
 *  4. Programa LEDs/watchdog/som e o timing de vídeo (tabela da ROM);
 *  5. Copia a RAM principal para a RAM rápida e recopia o programa da ROM;
 *  6. Inicializa variáveis globais, timer 0 e subsistemas;
 *  7. Habilita INT0 (VBLANK) e INT3 (A/D) e liga as interrupções;
 *  8. Decide, pelos ajustes da CMOS, para onde seguir.
 *
 * O fluxo de cada instrução foi preservado; o endereço original aparece no
 * comentário de cada bloco.
 */
class ResetRoutine {
public:
    /// Destinos possíveis ao fim do RESET.
    static constexpr uint32_t kExitCmosCheckFailed = 0x5006; ///< Ajuste 0xFA != 0x28.
    static constexpr uint32_t kExitLowerThan       = 0x4FEF; ///< `0x995C` devolveu carry (LO).
    static constexpr uint32_t kExitFallThrough     = 0x4BAF; ///< Segue para o código seguinte.

    /**
     * @brief Constrói a rotina.
     * @param bus Barramento (já com a ROM e `hardwareReset()` aplicado).
     * @param subs Despacho das sub-rotinas.
     */
    ResetRoutine(MemoryBus& bus, ISubroutines& subs) : bus_(bus), subs_(subs) {}

    /**
     * @brief Executa o RESET.
     * @param cpu Registradores (saída: estado ao final).
     * @return Endereço onde a execução original continua (`kExit*`).
     */
    uint32_t run(Tms32031State& cpu);

    /**
     * @brief Reentrada em `0x4B28`, usada após as telas de recuperação da CMOS
     *        (`0x5005`, `0x501F`) e por outros caminhos (`0x0677`, `0x6D1F`).
     * @details Grava 8 em `[0x8099E1]` e continua em `0x4B2B`: reconfigura o
     * barramento, recarrega SP, recopia o programa, zera variáveis/BSS e refaz
     * toda a inicialização, sem repetir a limpeza da RAM interna.
     * @param cpu Registradores.
     * @return Mesmo significado de run().
     */
    uint32_t runFrom4B28(Tms32031State& cpu);

private:
    /// Corpo comum a partir de `0x4B2B`.
    uint32_t runFrom4B2B(Tms32031State& cpu);

    /// `STI Rn,@($off)` — escrita direta com a página DP atual.
    void stDirect(const Tms32031State& cpu, uint32_t off, uint32_t value) { bus_.writeWord(cpu.direct(off), value); }
    /// `LDI @($off),Rn` — leitura direta com a página DP atual.
    uint32_t ldDirect(const Tms32031State& cpu, uint32_t off) const { return bus_.readWord(cpu.direct(off)); }
    /// `CALL addr`.
    void call(uint32_t addr, Tms32031State& cpu) { subs_.call(addr, cpu, bus_); }

    /**
     * @brief Idioma do jogo para desligar interrupções com segurança.
     * @details `PUSH IE / LDI 0,IE / NOP / NOP / AND ~GIE,ST / POP IE`: zera IE
     *          antes de limpar GIE para que nenhuma interrupção entre no meio.
     */
    static void disableInterrupts(Tms32031State& cpu) { cpu.st &= ~Tms32031State::kStGie; }

    /**
     * @brief `CALL $4C33` — a sub-rotina é um único `RETIU`.
     * @details Truque do jogo: `RETI` retorna e LIGA o GIE. Logo, chamá-la
     *          equivale a "habilitar interrupções".
     */
    static void enableInterrupts(Tms32031State& cpu) { cpu.st |= Tms32031State::kStGie; }

    MemoryBus& bus_;  ///< Barramento de memória simulado.
    ISubroutines& subs_;  ///< Despachante das sub-rotinas traduzidas.
};

} // namespace rea::core::boot
