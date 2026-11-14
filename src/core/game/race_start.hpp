#pragma once
/**
 * @file race_start.hpp
 * @brief Processo da largada (`0x1C9B`): contagem regressiva e liberação do grid.
 *
 * @details Estados (PC de retomada):
 * 1. **`0x1C9B`**: zera `[E860]`/`[E84C]`/`[E85F]`, guarda `[C8C5]` em R5 e liga os bits 0x110;
 *    com o ajuste 0x1A da CMOS cria `[B21E]` (e `[B21F]` fora do modo 2); dorme 5.
 * 2. **`0x1CB5`**: fora do modo 2 cria `[B220]`; `[E84D]` = 0; com link espera `[C8E1]` = 3
 *    (`0x7BF6`); toca o som 0x90.
 * 3. **`0x1CCC`** (2 vezes, tabela `[B221]` de 3 palavras: texto, som, função de link):
 *    espera 0x14 quadros (ou o passo do mestre `[E63B]` no link), mostra o texto em
 *    (256, 160) por 0x14 quadros (`0x7660`/`0x76BF`) e toca o som.
 * 4. **`0x1CF4`**: dorme 0xF; `[E84C]` = 1; dorme 5.
 * 5. **`0x1CFB`**: texto e som de `[B222]` ("GO"); `[C8C5]` = R5 | 0x200 sem 0x10; `[E860]` = idem;
 *    sem aviso da CMOS marca `[EB52]` = 0x11D pelas chaves `[991030]` & `[B223]`.
 * 6. **`0x1D23`**: `[E5EC]` = 0x5577 (libera o grid), som 0x93, `[E604]` = ajuste 0x1C · 5 + 0x3C,
 *    `0x9108` fora do modo 2 e morre.
 */

#include "core/subroutines.hpp"

namespace rea::core::game {

/// @brief Registra os estados `0x1C9B`, `0x1CB5`, `0x1CC4`, `0x1CCC`, `0x1CD1`, `0x1CDE`, `0x1CF7`
///        e `0x1CFB` do processo da largada.
void registerRaceStart(SubroutineDispatcher& d);

} // namespace rea::core::game
