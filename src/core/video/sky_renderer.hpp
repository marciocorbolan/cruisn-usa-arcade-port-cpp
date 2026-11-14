#pragma once
/**
 * @file sky_renderer.hpp
 * @brief Céu da pista (`0x822B`) e a projeção dos pontos do céu (`0x839B`).
 *
 * @details Chamada pelo loop principal quando `[C8C5]` & 0x40 (e não & 0x400).
 * 1. No modo 4, chama `0x81D9`. Normaliza o yaw `[809807]` em [0, 2π) (`0x9313`) e o
 *    guarda em `[E3F8]`. Rolagem horizontal = FIX(yaw·`[8224]`), dobrada em (−0x300, 0x600)
 *    por ±0x600.
 * 2. `[5A]` = câmera `[0x47]` × rotação Y(−yaw) (`0x938A` + `0x9494`).
 * 3. `[E3FC]` = linha de recorte do chão: (`[E3F9]` + 25) − (`[E3FD]` + `[0x54]`), só no
 *    modo 4 (≥ 0).
 * 4. `0x839B` projeta 34 pontos da tabela `[831B]` (x + rolagem, y + `[E3FD]`, z) em
 *    `[0x4F]` = 0x809D00 (3 palavras cada):
 *    - x' = x + `[0x53]`;
 *    - y' = 1,04·y + `[0x54]`.
 *    O x é dobrado em `[8226]`.
 * 5. Envia 12 quads de céu (cabeçalho 0x100, textura `0x9ECD([8383+i])`, cantos 0,1,3,2,
 *    u/v −0x200, 0, 0xFF, −0x101, palavra final da tabela) e 3 quads de chão (cabeçalho
 *    0x1E, textura do slot 5, pontos a partir do 26º) direto na FIFO 0x600000, esperando
 *    `[980082]` & 4 e disparando com a leitura de `0x980083`.
 * 6. No modo 4 sem `[C8C5]` & 0x80, o quad de céu é recortado pela linha `[E3FA]`/`[E3F9]`
 *    (`0x94C9`, `0x94B4`). Com `[C8C5]` & 0x80, chama `0x83D1` (segunda camada).
 */

#include "core/subroutines.hpp"

namespace rea::core::video {

/// @brief Registra `0x822B`, `0x839B` e `0x83D1` (silhueta do horizonte, com `[C8C5]` & 0x80).
void registerSkyRenderer(SubroutineDispatcher& d);

} // namespace rea::core::video
