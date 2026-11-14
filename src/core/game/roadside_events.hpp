#pragma once
/**
 * @file roadside_events.hpp
 * @brief Eventos de beira de estrada das pistas 9 e 11 (`0x69A8`–`0x6C8B`).
 *
 * @details Os carregadores `0x8DAD` (pista 9: `[C69E..C6A0]`) e `0x8DF6` (pista 11: `[C6A9..C6AB]`)
 * criam três processos (ID 0x800) cada. Todos esperam o início da corrida (`[C8C5]` & 0x200) e só agem
 * com o item 0x23 da CMOS ligado:
 * | Processo | Pista | Ação |
 * |---|---|---|
 * | `0x69E1` | 11 | carrega a paleta `[C62D]` (slot `0x9ECD(0xB4)`); a cada 600 quadros, com chance 500 (`0x8EE9`) e sem outro ID 0x751 vivo (`0x69D6`, em process_manager), cria `[C62E]` = `0x69FF` |
 * | `0x6A1D` | 9 | igual, sem paleta: `[C631]` = `0x6A34` (ID 0x752) |
 * | `0x6B14` | 9, 11 | depois do focado existir, a cada 600 quadros (chance 500) cria `[C637]` = `0x6B2D` (ID 0x500) com R4/R5/R6 de uma das 4 linhas de `[C636]` |
 * | `0x6BD7` | 9, 11 | a cada 150 quadros (chance 300, `[E871]` = 0): som 0x24 duas vezes e, 10 quadros depois, `[C639]` = `0x6BF5` com a tabela `[6BD6]` |
 *
 * - `0x69FF`/`0x6A34`: objeto `[C62F]`/`[[C630]]` (`0x6A42`) 20 trechos à frente do jogador, ±500 de
 *   dispersão, empurrado 750 para o lado (`0x69A8`) e assentado no chão. `0x69FF` toca o som 0 quando o
 *   jogador chega a 20 quadros dele; `0x6A34` anima o modelo pela tabela `[C630]` a cada 3 quadros.
 * - `0x6B2D`: objeto que anda pela linha de pontos `[AA2F]` (4 palavras por ponto) R5 pontos por quadro,
 *   rumo + R6, som 0x72; perto do jogador (≤ 5000, até 2000 de lado, chance 800) solta até 7 `0x6BF5`
 *   (tabela `[6B2C]`). Some fora da janela −40..+120 pontos do jogador.
 * - `0x6BF5`: objeto de tela 0x103 que desce até z = 368, anima pela tabela R5 e sai acelerando.
 * - Batidas (`0x6959` em scenery_collision): `0x6A66` estilhaça o objeto 0x750 em pedaços (`[C62B]`
 *   + 5·(tipo − 1), +2) lançados por `0x6AD9` (processo `[2333]`, ID 0x290), cria `[C633]` = `0x6AAC`
 *   (ID 0x500, animação `[C634]`) e, no modo `[E871]`, gira o carro; `0x6AD3` só lança o objeto.
 *   O carro perde metade da velocidade. Ambos salvam e restauram todos os registradores (`0x9042`/`0x905C`).
 */

#include "core/subroutines.hpp"

namespace rea::core::game {

/// @brief Registra `0x69A8`, `0x69C1`, `0x69E1`, `0x69FF`, `0x6A1D`, `0x6A34`, `0x6A40`,
///        `0x6A42`, `0x6A66`, `0x6AA6`, `0x6AAC`, `0x6AD3`, `0x6AD9`, `0x6B14`, `0x6B2D`, `0x6BD7`,
///        `0x6BF5`, `0x6C59`, `0x6C76`, `0x6C85` e os pontos de retomada.
void registerRoadsideEvents(SubroutineDispatcher& d);

} // namespace rea::core::game
