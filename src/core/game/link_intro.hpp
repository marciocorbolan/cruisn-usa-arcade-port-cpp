#pragma once
/**
 * @file link_intro.hpp
 * @brief Tela "HEAD 2 HEAD" do link (`0x154D`–`0x1616`, caso −7 da `0x9503`) e caso −6 (`0xA1EA`).
 *
 * Verificada com dois executáveis: enquanto o Master escolhe a corrida, o Slave mostra as peças HEAD / 2 / HEAD
 * batendo, "JOIN IN NOW" (`[B1D7]`) e "INSERT COINS" (`[B1D6]`, sem créditos).
 *
 * @details O caso −7 vem da espera pelo outro gabinete (`0x9563`, `[C8E8]` = −7 quando o modo `[C8D6]&F` é
 * 5..7 ou `[C8DD]` ≠ 0). O processo `[C702]` = **`0x154D`** (AR6 = 1; `0x154F` com AR6 = 0):
 * - `[C931]` = 2, `[C8C5] &= 0xF`; cria três peças (`0x7309` com `[B1C9]`/`[B1CB]`/`[B1CD]`, flags 0x4A3,
 *   `0x7070`, paleta `0x9EDB([B1CA]/[B1CC]/[B1CE])`) em `*+AR7(C/D/E)`;
 * - 26 quadros de aproximação: `C+2` = −R6, `D+2` = R6 (R6 = 160 − 4/quadro), `E+3` = R7 (R7 → `[B1CF]` em 10 %);
 * - três batidas (`0x15F2`, chamada local): peça C (R6 = 120), E (300) e D (120); cada uma desce R6 em 2
 *   quadros, espera 5 (som 0x36 se o item 0x1E da CMOS = 1) e sobe R6 em 2;
 * - AR6 = 1: `[C931]` = 1 e laço com o texto `[B1D6]` enquanto `[E56C]` ≤ 0; AR6 = 0: 30 quadros e `0x1E1C`.
 *
 * A cada quadro `0x15E6` (com AR6 = 1) mostra o texto `[B1D7]`, e `0x15D4` testa o link: caído (sem link, modo
 * 2 ou `[C8DD]` = 0 fora dos modos 5..7) → `0x15D0`: `0x793A`, `[C8E8]` = −2 e `0x1E17`.
 *
 * O caso −6 (IN1 `~[991030] & 0x70` = 0x60: VIEW2 e VIEW3 apertados, VIEW1 solto, no reinício) cria `[C70D]` = **`0xA1EA`**:  chama os créditos finais (`0xA1F1`) como chamada
 * local que volta em `0xA1F0` (`BR $1E1C`).
 */

#include "core/subroutines.hpp"

namespace rea::core::game {

/// @brief Registra `0x154D`–`0x1613`, `0xA1EA` e `0xA1F0`.
void registerLinkIntro(SubroutineDispatcher& d);

} // namespace rea::core::game
