#pragma once
/**
 * @file music_title.hpp
 * @brief Processo `[C6BA]` = `0x90B9` (ID 0x5C6): nome da música na tela, criado por `0x90F3`.
 *
 * @details A cada quadro: texto `[[90B0] + [E916]]` (tabela `0x90B1`, 8 músicas) em (210, 370) com `0x7651`
 * (RC = 1), objeto `[C6B9]` em (0xA0, 0x168, 0x900) com `0xAF48` e dorme 1 quadro (`0x90C7`). Fora do modo 4
 * (corrida, `[C8C5]` & 0xF) zera `[E914]` e morre; na corrida conta `[E914]` (0x28 de `0x90F3`) e morre em 0.
 */

#include "core/subroutines.hpp"

namespace rea::core::game {

/// @brief Registra `0x90B9` e o ponto de retomada `0x90C7`.
void registerMusicTitle(SubroutineDispatcher& d);

} // namespace rea::core::game
