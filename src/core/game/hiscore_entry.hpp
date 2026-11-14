#pragma once
/**
 * @file hiscore_entry.hpp
 * @brief Entrada das iniciais do recorde da pista (`0x31A6`, chamada local do pós-corrida quando o
 *        item 0x1F está ligado) e a cena 3D do pódio que a acompanha.
 *
 * @details Fluxo de `0x31A6`:
 * - `0x32E7` decide se o tempo `[E4BC]` entra na tabela (carry); senão volta ao chamador na hora.
 * - Espera 4 quadros com `[E5CA]` = 0, música 0xF9, monta a cena `[B16E]`/`[B16F]` (câmera em
 *   (−24, −45, −1300)), as paletas `[B170]` (`*+AR7(1E)`) e `[B172]` (`*+AR7(2E)`, cursor) e localiza
 *   os objetos da cena por tipo (`0x355D`: 0x87 pivô, 0x88/0x86 faixas, 0x8B carro, 0x89 tampa).
 * - Iniciais já gravadas no item 0x33 (`0x32FB`) → só mostra (`0x331F`, 29 quadros). Senão o volante
 *   escolhe a letra (`0x1C62` → `[EA47]`, média de 2 quadros; 0x1A = apagar, 0x25 = fim), o pedal
 *   `[C93B]` confirma (com histerese em `[E5FD]`) e o relógio `[E604]` (20, desconta pela máscara
 *   `[B177]` de `[C937]`) força a escolha; o processo `0x32B2` mostra o letreiro `[B16A]`.
 * - Grava (`0x32DD` → `0x9B28`), refaz a tabela (`0x373C`), gira o pódio, sobe a tampa, sacode a
 *   câmera, gira o carro e o leva até a linha (`*+AR7(1B)`), afasta a câmera, acende os números
 *   (`0x36A0`) e pisca a linha nova (`0x38CD`); fecha com as portas (`0x5DE0`) e restaura o modo.
 * - `[E631]` ≠ 0 (o jogador apertou) pula cada etapa.
 *
 * Também traduz a tabela de recordes na CMOS: `0x9B18` (linha em que o tempo R0 entra na tabela
 * R1, ou −1), `0x9B28` (insere, empurrando as linhas de baixo) e `0x9A7F` (marca a linha nova).
 */

#include "core/subroutines.hpp"

namespace rea::core::game {

/// @brief Registra `0x31A6` e as retomadas, os auxiliares `0x32B2`–`0x3330`, `0x355D`–`0x36B2`,
///        `0x9A7F`, `0x9B18` e `0x9B28`.
void registerHiscoreEntry(SubroutineDispatcher& d);

} // namespace rea::core::game
