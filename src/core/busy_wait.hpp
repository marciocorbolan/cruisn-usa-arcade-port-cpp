#pragma once
/**
 * @file busy_wait.hpp
 * @brief Passagem de tempo dentro de esperas ocupadas das rotinas traduzidas.
 *
 * @details No hardware, um laço de espera (`LDI porta / TSTB / Bcond`) gasta
 * ciclos e as interrupções entram entre as instruções. No port, as rotinas
 * são funções C++ síncronas e a INT0/INT3 só rodam entre quadros. Rotinas cujas
 * esperas dependem de interrupções (ex.: timeouts contados em quadros por
 * `[C930]`) chamam `spin()` com uma estimativa das instruções gastas.
 * A implementação decide quando "passou um quadro" e atende as interrupções.
 */

#include <cstdint>

#include "core/tms32031_state.hpp"

namespace rea::core {

/**
 * @brief Relógio das esperas ocupadas.
 */
class IBusyWait {
public:
    virtual ~IBusyWait() = default;
    /**
     * @brief Avança o tempo em @p instructions instruções de espera ocupada.
     * @param cpu Registradores do código interrompido (as interrupções respeitam GIE/IE).
     * @param instructions Estimativa de instruções gastas desde a última chamada.
     * @return false se a máquina deve parar (RESET pedido): a rotina retorna na hora.
     */
    virtual bool spin(Tms32031State& cpu, uint32_t instructions) = 0;
    /**
     * @brief Avisa que o código alimentou o watchdog de hardware por conta própria (bit 3 de
     *        `0x994000`, ex.: o autoteste `0x6381` no burn-in, com o GIE desligado).
     */
    virtual void feedWatchdog() {}
};

} // namespace rea::core
