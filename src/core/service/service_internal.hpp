#pragma once
/**
 * @file service_internal.hpp
 * @brief Registro interno das partes do menu de serviço.
 */

#include <functional>
#include <utility>

#include "core/service/service_ctx.hpp"

namespace rea::core::service {

/// Corpo de uma rotina do menu.
using SvcFn = std::function<void(Ctx&)>;

/// @brief Registra @p fn em @p addr com um Ctx montado na chamada.
inline void addSvc(SubroutineDispatcher& d, IBusyWait& wait, uint32_t addr, SvcFn fn) {
    d.add(addr, [&wait, fn = std::move(fn)](Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
        Ctx c(cpu, bus, subs, wait);
        fn(c);
    });
}

/// @brief `0x0A4F` (@p resetCursor) / `0x0A52`: laço genérico de menu com AR5 = tabela, AR6 = itens,
///        AR7 = título. Volta quando o item `0x0AE0` é escolhido ou SERVICE é apertada (fora do principal).
void menuLoop(Ctx& c, bool resetCursor);
/// @brief `0x0AE9`: limpa as páginas, exibe a 0 e desenha nela (`[CE13]` = 0x900000).
void clearScreen(Ctx& c);
/// @brief `0x0602` (moldura clara por fora) e `0x0628` (escura por fora): fundo + 4 bordas.
void bevelBox(Ctx& c, uint32_t ar2, uint32_t r2, uint32_t r3, uint32_t rc, bool raised);

void registerMenuCore(SubroutineDispatcher& d, IBusyWait& wait);
void registerDiagnostics(SubroutineDispatcher& d, IBusyWait& wait);
void registerAdjustments(SubroutineDispatcher& d, IBusyWait& wait);
void registerAudits(SubroutineDispatcher& d, IBusyWait& wait);
void registerMotion(SubroutineDispatcher& d, IBusyWait& wait);
void registerCalibration(SubroutineDispatcher& d, IBusyWait& wait);

} // namespace rea::core::service
