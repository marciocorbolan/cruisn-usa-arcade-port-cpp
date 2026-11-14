#pragma once
/**
 * @file track_stream.hpp
 * @brief Fluxo de trechos da pista (`0x3F6F`–`0x4211`). Os trechos à frente da câmera
 *        viram objetos de cena e os que ficam para trás são liberados.
 *
 * @details **Janela de trechos** `[3F69]` = 0xE408: até 5 entradas de 5 palavras cada:
 * - marca (`trecho << 8 | 0xAA`);
 * - registro do trecho;
 * - lista de nós `[E473]`;
 * - flags `[E47A]`;
 * - número do trecho `[E474]`.
 *
 * `[E46C]` é o número de entradas e `[E46D]` o próximo registro.
 *
 * **`0x3F8D`** (início): zera as listas, posiciona no trecho `[E3FE]` (`0x3F6F`: pula os
 * registros e copia `+1..+4` em `[E3FF]..[E402]`), carrega 5 trechos (`0x405A`) e cria o
 * processo `[B0C2]` = `0x3FE3` (ID 0x5AA).
 *
 * **`0x3FE3`** (processo):
 * - fora dos modos 2 e 4, dorme 1 quadro;
 * - no modo 4, quando o carro de foco (`[[E878]+0x37]+0x1E`) passa do trecho `[E477]`,
 *   avança `[E476]` (e chama `0xACCA` com o byte 2 das flags);
 * - carrega o próximo trecho se ele está a ≤ `[3F6C]` (≈ 40000) da câmera (`[0x45]`);
 * - libera o 1º trecho da janela quando a câmera passa `[3F6D]` (modo 2: `[3F6E]`) do
 *   seu primeiro ponto (`0x41D1`) e desloca a janela;
 * - dorme 3 quadros.
 *
 * **`0x405A`** (trecho → objetos): gira cada ponto em Y pelo ângulo do registro, translada
 * (com deslocamento `+7..+9` se flags & 8) e aloca o objeto (`0x700A`, modelo da lista).
 * - Tipo `+0xF` = palavra & 0xFFF; o bit 0x2000 chama o tratador da tabela `[4500]` (`0x4212`).
 * - Os bits 16..21 (& 0x3B) vão para `+0xE`; 0x1000 → bit 26. `+0x1F` = marca do trecho.
 * - Por tipo:
 *   - 0x300 → lista `[E403]` (bit 28);
 *   - 0x900 → `[E406]` (bit 21);
 *   - 0x600 → `[E405]` (bit 31);
 *   - 0xB00 → nó da pista: índice `+0x1E` = trecho << 8 | faixa, tipo 0x300, e entra na
 *     lista ordenada `[E407]` (`0x4197`, próximo `+0x21`, anterior `+0x17`).
 * - Até 2 listas extras de pontos: `+6` (flags & 1) e a alternativa de flags & 0x1000.
 * - No fim, a lista `[E407]` do trecho é encadeada na lista global `[E46E]`/`[E470]`
 *   (`0x41B2`/`0x41BE`).
 * - O 4º trecho também ganha a placa `[B0C3]` (`0x7309`).
 */

#include "core/subroutines.hpp"

namespace rea::core::track {

/// @brief Registra `0x3F6F`, `0x3F8D`, `0x3FE3`, `0x405A`, `0x4197`, `0x41B2`, `0x41BE`,
///        `0x41D1`, `0x4204` e `0x4212`.
void registerTrackStream(SubroutineDispatcher& d);

} // namespace rea::core::track
