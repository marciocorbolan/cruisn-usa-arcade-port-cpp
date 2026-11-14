#pragma once
/**
 * @file main_loop.hpp
 * @brief Loop principal do jogo (ROM `0x4BAF`–`0x4C32`) como máquina de estados retomável.
 */

#include <cstdint>

#include "core/memory_bus.hpp"
#include "core/subroutines.hpp"
#include "core/tms32031_state.hpp"

namespace rea::core::mainloop {

/// Variáveis RAM usadas pelo loop principal (nomes provisórios).
namespace main_vars {
inline constexpr uint32_t kFrameDelta = 0xC92F;    ///< Quadros (VBLANKs) gastos na última iteração.
inline constexpr uint32_t kFrameCounter = 0xC930;  ///< Incrementado pela INT0; zerado a cada iteração.
inline constexpr uint32_t kFrameLimit = 0xC931;    ///< Mínimo de quadros por iteração (limitador).
inline constexpr uint32_t kVarC934 = 0xC934;  ///< Zerada no início do loop junto com `[C935]` (uso ainda não identificado).
inline constexpr uint32_t kVarC935 = 0xC935;  ///< Zerada no início do loop junto com `[C934]` (uso ainda não identificado).
inline constexpr uint32_t kIterationStamp = 0xC936;///< float: amostra do timer 0 após a troca de página.
inline constexpr uint32_t kFlipRequest = 0xC93F;   ///< != 0: troca de página pedida; a INT0 zera.
inline constexpr uint32_t kVarC940 = 0xC940;  ///< Flag zerada pelo Slave ao pedir a troca de página; as cenas (`0x84F5`, `game_flow`) a põem em 1.
inline constexpr uint32_t kVarC98B = 0xC98B;       ///< != 0: CALL $A6F8.
inline constexpr uint32_t kGameMode = 0xC8C5;      ///< bits 0–3 = modo; 0x40 e 0x100 = flags.
inline constexpr uint32_t kDipSwitches = 0xC8CC;   ///< bit 0 = link Slave.
inline constexpr uint32_t kLinkStateA = 0xC8DB;  ///< Byte com sinal; anterior em `[C8DC]`.
inline constexpr uint32_t kLinkStateB = 0xC8DC;  ///< Byte de estado do link anterior (`0x7A04` copia `[C8DB]` para cá antes de gravar o novo).
inline constexpr uint32_t kLinkSave = 0xC8E8;  ///< Caso da atração/partida salvo para a ressincronização do Slave (`0x9503`).
inline constexpr uint32_t kSoftWatchdog = 0x8099E0;///< Zerado a cada iteração (ver INT0).
inline constexpr uint32_t kVideoStatus = 0x980082; ///< bits 0/1: DMA/blitter ocupado.
inline constexpr uint32_t kInitialSpPtr = 0x0063;  ///< Palavra com o SP inicial.
/// Itens da CMOS lidos na entrada.
inline constexpr uint32_t kItem21 = 0x21;          ///< faixa 0–25000, padrão 4000.
inline constexpr uint32_t kItem27 = 0x27;          ///< faixa 1000–25000, padrão 5000.
} // namespace main_vars

/**
 * @brief Pontos de retomada = endereços originais onde o loop pode esperar.
 */
enum class Point : uint32_t {
    Init = 0x4BAF,          ///< Entrada (vinda do RESET).
    LoopTop = 0x4BC0,       ///< Topo do laço.
    WaitDma0 = 0x4BC1,      ///< Espera `[0x980082] & 1 == 0`.
    WaitDma1 = 0x4BC6,      ///< Espera `[0x980082] & 2 == 0`.
    WaitFrames = 0x4BCE,    ///< Espera `[C930] >= [C931]` (limitador de quadros).
    WaitFlip = 0x4BD4,      ///< Espera `[C93F] == 0` (troca de página feita pela INT0).
    Body = 0x4BD6,          ///< Corpo da iteração (lógica, som, watchdog…).
    SlaveDma0 = 0x4C1C,     ///< Ressincronização do Slave do link: DMA bit 0.
    SlaveDma1 = 0x4C21,     ///< … DMA bit 1.
    SlaveWaitFlip = 0x4C2A, ///< … espera a troca de página pedida em 0x4C27.
};

/**
 * @brief Loop principal traduzido.
 *
 * @details No hardware o loop nunca retorna: ele espera ocupado (busy-wait)
 * enquanto as interrupções avançam o estado. No port, step() executa até
 * encontrar uma espera não satisfeita e devolve o controle; o FrameRunner
 * então roda a INT0/INT3 do quadro seguinte e step() continua do mesmo ponto.
 *
 * **Ritmo:** a `0x7187` pede a troca de página (`[C93F] = 1`) e a INT0 a
 * executa no VBLANK seguinte. Com o corpo mais rápido que um quadro, o jogo
 * gasta `[C931] + 1` quadros por iteração:
 * - `[C931] = 0`: 57,9 iterações/s;
 * - `[C931] = 1`: metade disso.
 *
 * **Aproximações:**
 * - No máximo um corpo (`0x4BD6`) por chamada, uma guarda para quando a
 *   `0x7187` não estiver registrada.
 * - O corpo é instantâneo: no hardware, um corpo mais lento que um quadro
 *   perderia VBLANKs.
 * - As interrupções só são atendidas nos pontos de espera; no hardware
 *   entram entre quaisquer instruções.
 */
class MainLoop {
public:
    /**
     * @param bus Barramento.
     * @param subs Despacho de sub-rotinas.
     */
    MainLoop(MemoryBus& bus, ISubroutines& subs) : bus_(bus), subs_(subs) {}

    /// @brief Volta ao ponto de entrada `0x4BAF` (após um RESET).
    void reset() { pc_ = Point::Init; iterations_ = 0; }

    /**
     * @brief Avança o loop até bloquear numa espera ou completar uma iteração.
     * @param cpu Registradores do código principal.
     * @return Ponto onde parou (retomado na próxima chamada).
     */
    Point step(Tms32031State& cpu);

    /// @return Ponto atual.
    Point pc() const { return pc_; }
    /// @return Iterações completas do corpo (`0x4BD6`) desde reset().
    uint64_t iterations() const { return iterations_; }

private:
    /// `0x4BAF`–`0x4BBF`: preparação antes do laço.
    void init(Tms32031State& cpu);
    /// `0x4BC5`–`0x4BCD` (após as esperas de DMA).
    void afterDma(Tms32031State& cpu);
    /// `0x4BD6`–`0x4C1A`: corpo. @return próximo ponto.
    Point body(Tms32031State& cpu);
    /// `0x4C2C`–`0x4C32`: fim da ressincronização do Slave. @return false se um processo saltou para `0x4C1B`.
    bool afterSlaveFlip(Tms32031State& cpu);
    /// Port: consome `cpu.jump` (salto de um processo para `0x4C1B`). @return true se havia salto.
    bool takeJump(Tms32031State& cpu);
    /// @return true se `[0x980082] & mask != 0` (DMA ocupado).
    bool dmaBusy(Tms32031State& cpu, uint32_t mask);

    /// Lê a palavra no endereço @p a.
    uint32_t rd(uint32_t a) const { return bus_.readWord(a); }
    /// Grava a palavra @p v no endereço @p a.
    void wr(uint32_t a, uint32_t v) { bus_.writeWord(a, v); }
    /// Chama a sub-rotina traduzida em @p addr (o `CALL` original).
    void call(uint32_t addr, Tms32031State& cpu) { subs_.call(addr, cpu, bus_); }

    MemoryBus& bus_;  ///< Barramento de memória simulado.
    ISubroutines& subs_;  ///< Despachante das sub-rotinas traduzidas.
    Point pc_ = Point::Init;  ///< Ponto de retomada da máquina de estados do loop.
    uint64_t iterations_ = 0;  ///< Iterações completas do loop principal.
};

} // namespace rea::core::mainloop
