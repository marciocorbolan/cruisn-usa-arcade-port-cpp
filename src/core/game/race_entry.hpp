#pragma once
/**
 * @file race_entry.hpp
 * @brief Da garagem à pista: chamada local `[B1EF]` = `0x18A6` do fluxo da partida, a montagem da
 *        largada (`0x1859`) e a saída do carro do jogador (`0x196B` → processo do carro `0x29B8`).
 *
 * @details **`0x18A6`**: inicializa o percurso `[E634]` (rotina `[[8C12] + percurso]`, `0x3F8D`), modo
 * 0x2C3, som 0x14D, cria `0x1B29` (modelo alternativo pelos botões de vista) e mostra o título `[18A0]`.
 * Espera a escolha do carro (`[E641]`), então desloca a garagem inteira (cadeia `[E47D]`, câmera
 * `[0x45]` e os 4 objetos `[B207]`) para que o carro escolhido fique na largada (576 − `[[B202] + carro]`,
 * +1152 no gabinete da direita), tempo 5, `[E5ED]` = 0x100, auditoria do carro (0xE1 + carro ou
 * 0xE5 + variante) e `[E640]` = carro (+4 na variante). Sem atrato, faz a chamada local `0x1617`
 * (espera do link; sem link só reprograma o modo mestre). Depois `0x1859` e 5 processos (`[B20A]`..
 * `[B20E]`: as peças caem, a porta da garagem sobe — `0x195D` — e o carro sai — `0x196B`), espera 71
 * quadros, mata os processos 0x5C1, libera as paletas dos carros (`0x1A8A`) e volta ao fluxo.
 *
 * **`0x1859`**: zera o estado da corrida e cria os processos de posição/tráfego (`0x6639`, `0x65ED`,
 * `0x659F`), o pisca de paleta `0x1F2A`, o contador `0x9CE3` e os 9 carros da CPU (`0x50CB`, R4 = 0..7
 * e 9 — ou 8 no gabinete da direita).
 *
 * **`0x196B`**: a câmera acompanha o carro escolhido (`0x1841`, atrás e acima) enquanto ele vira para o
 * rumo do percurso (26 quadros); vira processo 0x110, inicializa o carro do jogador (`0x2987`), anda
 * até 700 do ponto `[[E46E] + 0x21]` e salta para o processo do carro `0x29B8` (`[E63F]` = 1, modo 0xC4).
 */

#include "core/subroutines.hpp"

namespace rea::core::game {

/// @brief Registra `0x18A6` e suas retomadas, `0x1617`/`0x167F` (sem link), `0x1841`, `0x1859`,
///        `0x195D`, `0x196B`, `0x2939`, `0x2987` e `0x9CE3`.
void registerRaceEntry(SubroutineDispatcher& d);

} // namespace rea::core::game
