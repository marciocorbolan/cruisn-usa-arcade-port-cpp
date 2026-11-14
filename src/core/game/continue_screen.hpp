#pragma once
/**
 * @file continue_screen.hpp
 * @brief Tela de continue (`0x1E2C`, chamada local do pós-corrida e da escolha de pista).
 *
 * @details
 * - Guarda o modo `[C8C5]` em `[E850]`; sem o DIP do modem (`0x4693`) reinicia o modem (`0x4936`)
 *   e zera `[EB4F]`/`[EB52]`/`[EB53]`.
 * - Soma o tempo jogado (`[C93A]` × 100) no item 0x73 e grava a média item 0x73 / item 0x72 no 0x74.
 * - Relógio `[E604]` = 20, música 0xEA, modo (`& ~0xF`) | 0x207; textos `[1E29]` (y = 100) e
 *   `[1E2A]` (y = 150) e o processo do relógio `[B22D]` = `0x1EE0` (ID 0x34, y = 270).
 * - Por quadro: linhas de crédito (`0x75AF`); moeda nova (item 0xF7 mudou) recarrega o relógio; com
 *   jogo grátis ou créditos suficientes (`0xC10075`) o 1º texto vira `[1E2B]` e `[C8CD]` |= 1. O
 *   relógio desce pela máscara `[B22E]` de `[C937]` (só se não for a máscara inteira) e a cada 15
 *   quadros pelo bit 4; `[E851]` conta 0..24.
 * - START (`[E631]`) com créditos: segurando `[C937]` & 0x720000 liga o cross-country (`[E636]` = 1);
 *   zera o item 0xF7, desconta o preço do item 0x71, modo | 4, `[E860]` = 1, conta o item 0x72 e
 *   devolve AR6 = 1. Relógio zerado devolve AR6 = 0.
 * - Fim: restaura o modo, mata o ID 0x34, `0x7648`, `0x9108` e zera `[C93A]`.
 */

#include "core/subroutines.hpp"

namespace rea::core::game {

/// @brief Registra `0x1E2C`, as retomadas `0x1EAB`/`0x1EF1` e o processo `0x1EE0`.
void registerContinueScreen(SubroutineDispatcher& d);

} // namespace rea::core::game
