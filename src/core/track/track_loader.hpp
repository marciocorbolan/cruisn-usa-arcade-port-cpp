#pragma once
/**
 * @file track_loader.hpp
 * @brief Carga da pista: carregadores por pista (`[[8C12] + [E481]]`, `0x8C14`–`0x8E48`),
 *        construção dos nós da pista (`0xAA30`–`0xAB18`) e listas de objetos da pista
 *        (`0x6754`, `0x6762`, `0x676B`, `0xAF55`, `0xAF58`, `0xAF66`, `0x3966`–`0x396E`).
 *
 * @details **Carregadores por pista** (14, indexados por `[E481]`): cada um é uma sequência fixa de:
 * - texturas LZW (`0xA3C2`, AR2 = `[C6xx]`);
 * - paletas (`0x9DF6`/`0x9DFE`) e `0x9F2F`;
 * - slots de paleta (`0x9EC2`, R0/R1);
 * - variáveis da pista `[E916]`, `[E47F]`, `[E8E3]`, `[E5F2]`/`[E5F3]` e o céu `[E3FD]`;
 * - processos extras (`0xA844`, ID 0x800);
 * - e, no fim, os trechos `0xAA30` + `0xAA66` (AR0 = primeiro, AR1 = último; `[E482]` = primeiro << 8).
 *
 * **Dados da pista** `[A103]` = 0xCD3923: registros de tamanho variável. Cada registro tem a
 * palavra de flags, 5 palavras fixas (`+4` = ângulo Y, `+5` = ponteiro da lista de pontos)
 * e opcionais: +1 se flags & 1, +4 se flags & 8 e +1 se flags & 0x1000.
 *
 * **Nós da pista** (`0xAA66` → `[AA2F]` = 0x1CB3C): para cada trecho de AR0 a AR1 (e mais 3
 * à frente), `0xAA9D` gira os pontos do trecho em Y (`0x939D`) e os translada pela origem
 * do registro (`+1..+3`; com flags & 8, soma antes o deslocamento `+7..+9`, uma palavra
 * antes sem flags & 1). Só os pontos com tipo (`& 0xF00`) = 0xB00 viram nós.
 *
 * Cada nó tem 4 palavras inteiras:
 * - x;
 * - y, com o bit 0 = flags bit 7;
 * - z;
 * - índice = (trecho << 8) | faixa. A faixa é invertida (0xFF − f) com flags & 8.
 *
 * Os nós de um trecho são ordenados pelo índice (lista `[E859]`, `0xAA48`) e copiados
 * (`0xAA54`). O fim da tabela tem 4 palavras −1. `0xAA66` guarda o ponto de parada em
 * `[E855]`/`[E856]` e `[E853]` = AR1 + 2.
 */

#include "core/subroutines.hpp"

namespace rea::core::track {

/// Variáveis da carga da pista.
namespace track_vars {
inline constexpr uint32_t kTrackData   = 0xA103; ///< `[A103]`: registros dos trechos.
inline constexpr uint32_t kNodes       = 0xAA2F; ///< `[AA2F]`: tabela de nós (4 palavras).
inline constexpr uint32_t kNextSeg     = 0xE856; ///< Próximo trecho a converter.
inline constexpr uint32_t kNextRec     = 0xE855; ///< Registro do próximo trecho.
inline constexpr uint32_t kLastSeg     = 0xE853; ///< Último trecho + 2.
inline constexpr uint32_t kSortHead    = 0xE859; ///< Lista ordenada dos nós de um trecho.
inline constexpr uint32_t kSortPool    = 0xE858; ///< Próxima entrada livre (5 palavras).
inline constexpr uint32_t kSortBase    = 0xC60B; ///< `[C60B]`: início do pool de ordenação.
inline constexpr uint32_t kCurSeg      = 0xE85C;  ///< Trecho sendo convertido em nós (`0xAA9D`).
inline constexpr uint32_t kCarList     = 0xE404; ///< Lista de carros (`0x6762`).
inline constexpr uint32_t kCarCount    = 0xE5EE;  ///< Número de carros na lista `[E404]`.
inline constexpr uint32_t kPropList    = 0xE8EC; ///< Lista de objetos 0x730 (`0xAF58`).
} // namespace track_vars

/// @brief Avança AR2 sobre um registro de trecho (flags em R1).
void skipRecord(Tms32031State& cpu, MemoryBus& bus);

/// @brief Registra as rotinas deste arquivo.
void registerTrackLoader(SubroutineDispatcher& d);

} // namespace rea::core::track
