#pragma once
/**
 * @file service_menu.hpp
 * @brief Menu de serviço do operador (`0x05F6`–`0x154C`).
 *
 * @details A chave de teste (F2) alterna `[C939]`; o loop principal chama `0x4E1B`, que desvia
 * para `0x0699`. O menu descarta a pilha (`0x0678`), passa ao modo 1 (sem watchdog de software)
 * e não volta mais ao loop principal: sai pela reentrada do RESET (`0x4B28`, item
 * "EXIT TO GAME OVER"). Ver docs/architecture.md, seção "Menu de serviço".
 *
 * | Menu (tabela) | Itens |
 * |---|---|
 * | principal `[C7E1]` = 0x10D8 | DIAGNOSTIC MENU, GAME AUDITS, ADJUSTMENTS MENU, UTILITIES, CALIBRATE CONTROLS, ADJUST VOLUME, EXIT TO GAME OVER |
 * | diagnóstico 0x10E7 | SWITCH TEST, DIP SWITCH TEST, CPU BOARD TEST, SOUND BOARD TEST, STEERING WHEEL TEST, MONITOR TEST, BURNIN TEST, MOTION MENU, LAMP TEST |
 * | utilidades 0x10FC | CLEAR CREDITS, CLEAR GAME AUDITS, RESET HIGH SCORES, DEFAULT ADJUSTMENTS, FULL FACTORY RESTORE |
 * | ajustes 0x110A | STANDARD/CUSTOM PRICING, FREE PLAY e 15 ajustes numéricos/sim-não |
 *
 * As rotinas precisam de um IBusyWait (as esperas rodam quadros); por isso o registro é feito por
 * um objeto criado depois do relógio de espera, como no link.
 */

#include "core/busy_wait.hpp"
#include "core/subroutines.hpp"

namespace rea::core::service {

/// @brief Registra as rotinas do menu de serviço com o relógio de espera injetado.
class ServiceMenu {
public:
    /// @param wait Espera ocupada usada pelas telas.
    explicit ServiceMenu(IBusyWait& wait) : wait_(wait) {}
    /// @brief Registra as rotinas do menu (e `0x4B28`, que lança NonLocalJump).
    void registerIn(SubroutineDispatcher& d);

private:
    IBusyWait& wait_;  ///< Espera ativa usada nos laços de polling do hardware.
};

} // namespace rea::core::service
