#pragma once
/**
 * @file power_on_test.hpp
 * @brief Autoteste de energização chamado pelo RESET: `0x62D5` (RAM rápida 0x400000, U56/U57/U59/U60,
 *        antes da cópia do programa) e `0x6381` (tela "CPU BOARD TEST": paleta U85/U87, VRAM
 *        U94/U95/U101/U102, checksums das EPROMs U10–U29, RAM de texturas U69–U76 e CMOS U31).
 *
 * @details Tabelas de 7 palavras (`[61C2]` = 0x61C3 até a palavra 0): bloco de parâmetros, retângulo
 * (x0, y0, x1, y1), rótulo e tipo (0 = teste de RAM, 1 = checksum de ROM). O bloco de RAM tem
 * início, palavras, máscara, passadas, bus control e passo; o de ROM tem início, palavras, máscara,
 * deslocamento e o ponteiro do checksum esperado.
 * - `0x64FE` (executada da cópia em `0x809D00` feita pela `0x6548`; `0x64FC` desvia por `[C770]`):
 *   grava a sequência pseudoaleatória (semente `[61C1]` = 0x5A5A5A5A; x ← x·2 | 2·(bit31 ⊕ bit30))
 *   e confere com a máscara; R0 = 0 passou, 1 falhou.
 * - `0x6552`: soma de (palavra & R2) << R3, 16 bits.
 * - `0x6403`: CMOS — grava e relê o byte alto do item 0xFC seis vezes (x ← MPYI(x·2|c, 0x794F)).
 * - Cores: 4 = passou, 3 = falhou; moldura com `0xABFC`, caixa com `0xA7E5`, rótulo com `0xA7F4`.
 * - Falha em `0x62D5`: pisca o código do rótulo no LED (`0x632F`) e trava em `0x631F`; nos testes da
 *   `0x644D` só pisca (`0x630D`) e segue. As pausas (`RPTS` de NOPs: ~0,32 s por meia piscada e
 *   ~1,4 s por dígito) passam como quadros quando registradas com `registerPowerOnPauses`.
 * - `0x065F` restaura a paleta (tabela `[C7DF]`) que o teste destruiu. A tela traz as molduras
 *   (`0xAC23`), títulos (`[C76A..C76C]`, cor 0xF) e uma caixa por teste (verde/vermelha).
 * - Os 20 checksums esperados (`0x656C`–`0x657F`) conferem com as EPROMs (verificado em Python).
 *
 * **Quirk preservado:** em `0x64FE` o contador AR4 não é recarregado entre passadas; com mais de uma
 * passada a 2ª grava só uma palavra (todas as tabelas usam 1 passada).
 */

#include "core/subroutines.hpp"

namespace rea::core {
class IBusyWait;
}

namespace rea::core::boot {

/// @brief Registra `0x62D5`–`0x6565`, `0x503C` e `0x065F`.
void registerPowerOnTest(SubroutineDispatcher& d);

/**
 * @brief Troca `0x62D5`, `0x630D` e `0x632F` por versões com as pausas do LED (`RPTS` de NOPs) passadas
 *        como tempo de quadros em @p wait; a falha da `0x62D5` passa a piscar para sempre (`BU $631F`).
 */
void registerPowerOnPauses(SubroutineDispatcher& d, IBusyWait& wait);

} // namespace rea::core::boot
