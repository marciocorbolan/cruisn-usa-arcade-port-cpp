#pragma once
/**
 * @file post_race.hpp
 * @brief Fluxo pós-corrida: processo `[B0D7]` = `0x3A46` (chegada ou fim de tempo) e o processo de
 *        resultados `[B0E0]` = `0x3A64`.
 *
 * @details
 * **`0x3A46`** (ID 0x16): guarda `[C938]` em `*+AR7(C)` e o tempo em `[E53C]`, para os sons
 * (`0x9166`, `0x92AD`), mata os processos de corrida (`0x3F42`), zera as listas de processos
 * (`0xA8F5`) e cria `0x3A64` (0x500), `0x2976` (câmera do modo 5, 0x110) e `0x903B` (0x500); reassenta
 * os objetos 0x767/0x768 (`0x3F50`) e reentra no laço principal (`SP = [0x63]`, `BR 4C1B`; no port,
 * `cpu.jump = 0x4C1B`). Como o PCB de `0x3A64` reaproveita o do `0x3A46`, o `*+AR7(C)` sobrevive.
 *
 * **`0x3A64`**: `[E4BC]` = tempo decorrido `[E85F]` (quirk: o `[E4BC]` = 0 do fim de tempo é
 * sobrescrito em seguida), saída da HUD (`[B0E3]` = `0x9CF5`), espera o fim da cortina `[809A79]`,
 * música 0xDB. Na chegada: bandeira `[B0E4]` (0x5C3), sons 0x8A/0x8D e, em 1º, `[B0E5]` e a chamada
 * local `[B0E6]` = `0x864D`. Depois os placares `[B0E8..B0EC]` (0x5C4/0x5C7), recorde (`0x32E7` →
 * `[B0ED]`/`[B0EE]`), ~2 s (`[E631]` encurta), cortina `[B0EF]`, paletas, a sequência de bandeiras do
 * cross-country (`0x3D85`, se `[E636]` = 1) e a decisão:
 * - fora do cross-country → `0x180C` (fim de jogo / atração);
 * - item 0x80 ≠ 0 → `0x6CE3`;
 * - vitória em 1º (item 0x1A) → próxima etapa direta; senão continue (`[B0F9]` = `0x1E2C`; AR6 ≠ 1 →
 *   desistência `0x1D59`);
 * - etapa seguinte: `0x1722`, `0x1617`, reinício dos subsistemas, `[E604]` = item 0x1C·5 + 60, modo 4
 *   com 0x44, `[B0FF]`, rotina da pista `[[3965]+[E481]]`, `0x1859` e morre;
 * - 14 etapas vencidas (`[E481]` = 0xE) → `[E636]` = 2, final (`[B101]`, `[B103]`, `[B105]`) e
 *   reinício do jogo com `[C8E8]` = −2 (`0x1E17`).
 *
 * **Quirk preservado:** `0x3D85` empilha R3 e RC e desempilha RC duas vezes — sai com RC = R3 antigo e
 * R3 sem restaurar.
 */

#include "core/subroutines.hpp"

namespace rea::core::game {

/// @brief Registra `0x3A46`, `0x3A64` (e retomadas), `0x3F42`, `0x3F50`, `0x3F64`, `0x6C52`,
///        `0x32E7`, `0x3D85` e o processo `0x3DB1`.
void registerPostRace(SubroutineDispatcher& d);

} // namespace rea::core::game
