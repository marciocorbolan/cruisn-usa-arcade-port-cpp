#pragma once
/**
 * @file player_effects.hpp
 * @brief Efeitos criados pelo processo do carro do jogador: chamas do escapamento (`0xAD93`)
 *        e fumaça dos pneus (`0x86FC`).
 *
 * @details
 * **`0xAD93`** (tabelas `[C61E]`, `[C620]`, `[C621]`, `[C623]`): se ainda não há objeto 0x101 e
 * existe o carro do jogador (tipo 0x100) sem rodopio, cria dois objetos 0x101 nas posições do
 * escapamento do modelo `[E640]` (`[B141]` + 3·modelo, inteiros; o segundo espelhado em X e girado
 * π) e os anima por 8 quadros com os quadros da tabela `[B140]` (lista terminada em negativo, em
 * ciclo), presos à matriz da carroceria (`0xAE1F`). Depois (ou no rodopio) apaga todos os 0x101.
 *
 * **`0x86FC`** (`[C61F]`, `[C622]`, `[C624]`): um só por vez (`[E918]`). A cada 2 quadros (todo
 * quadro em rodopio) solta um par de nuvens 0x102 nas rodas traseiras (peças com `+0x12` = 1)
 * se houver freio ≥ 0,4 a mais de 20, ou derrapagem ≥ 0,25 — até 20 nuvens, em `*+AR7(0xE..)`.
 * Animação `[C6BD]` (ou `[C6BE]` com derrapagem > 0,5 / rodopio), paleta `0x9EDB([C6BF])` no
 * asfalto. Cada nuvem anda para trás no rumo do carro (`[C92F]`·velocidade·1,51 − 50) e segue a
 * altura do chão (`0x1F8B`); some no fim da animação. Sem nuvens vivas, o processo morre.
 */

#include "core/subroutines.hpp"

namespace rea::core::game {

/// @brief Registra `0xAD93`, `0xADC2`, `0xADD7`, `0xADD9`, `0xADE0`, `0x86FC`, `0x8708`,
///        `0x8743`, `0x874E`, `0x877E`, `0x8780` e `0x8788`.
void registerPlayerEffects(SubroutineDispatcher& d);

} // namespace rea::core::game
