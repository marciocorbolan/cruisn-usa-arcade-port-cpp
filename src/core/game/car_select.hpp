#pragma once
/**
 * @file car_select.hpp
 * @brief Seleção de câmbio e de carro (`0x59EC`–`0x5DDD`, `0x1942`–`0x1C5A`, `0x1D52`), chamada local
 *        `[B1ED]` do fluxo da partida.
 *
 * @details **Câmbio** (`0x59EC`): carrega a cena `[C6D0]` (`0x440C`) e organiza os objetos (`0x5BEB`):
 * a cadeia `+0x1F` começa em `[EA20]`; os tipos 0x10..0x18 (byte baixo) viram as peças da tela:
 * `[EA23]` alavanca (0x14), `[EA24]` volante (0x15), `[EA25]`/`[EA2A]` animados (0x16/0x17),
 * `[EA26]`/`[EA27]` com as paletas de mistura (0x10/0x11), `[EA28]`/`[EA29]` as duas placas (0x12/0x13).
 * Os objetos com tipo alto 0x100 ficam à esquerda (−600) e os outros à direita (+600). Em 40 quadros
 * eles deslizam para o centro (`0x5B0F`) e descem (`0x5AC1`), enquanto o processo `0x5BCA` gira as placas.
 *
 * Por 10 s o volante (média `[EA22]`, `0x5BBF`) move a alavanca entre −101 e +101 (`0x5B66`, com
 * histerese em torno de 0,5), escolhendo `[E633]` (0 ou 1; sons 0xBD/0xC0); a paleta das duas metades
 * mistura-se pela posição da alavanca (`0x5D00`) e o volante 3D gira (`0x5BB6`). Acelerar confirma.
 *
 * Depois: a câmera vai para trás do percurso (−650, −28000), a placa escolhida se afasta girando
 * (`0x5ACE`) e a outra cai (`0x5AFF`); entram os 4 carros (`0x44BB` com `[C6D5]`, `0x1A27`), a
 * câmera gira (`0x5A89`, 31 quadros, `0x5B29`) e a escolha do carro roda em paralelo.
 *
 * **Carro** (processos `0x1AB0` e `0x1942`): 4 carros (objetos 0x481..0x484, paletas
 * `[B217]`..`[B21A]`) sobre as bases 0x401..0x404, com as placas 0x601..0x604. O volante escolhe `[E632]` (0..3, `0x1D52`); o carro
 * escolhido sobe 300, acende (`0x9FB6` com 1,0; os outros 0,5) e gira; o anterior volta de frente
 * para a câmera. `0x1A5C` mostra as fichas técnicas `[68EF]`/`[[68EA] + carro]`. Acelerar ou o fim dos
 * 12 s marca `[E641]` = 1.
 */

#include "core/subroutines.hpp"

namespace rea::core::game {

/// @brief Registra a seleção de câmbio e de carro.
void registerCarSelect(SubroutineDispatcher& d);

} // namespace rea::core::game
