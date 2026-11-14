#pragma once
/**
 * @file special_car.hpp
 * @brief Carro especial 0x270 da pista 2 (`0x7C86`–`0x7EE8`) e o giro das rodas (`0x7E63` → `0xC10000`).
 *
 * @details O evento de pista `0xAD22` cria o processo `[C619]` = `0x7CFA` (ID 0x214):
 * - **Entrada `0x7CFA`**: vai para depois de `[E877]` (`0xA91F`), pede um objeto (`0x7025`, sem objeto →
 *   morre), modelo 0xC5F2C7, dados de carro (`0x2945`), nós das rodas (`0x7E3F`: tabela `[B11B]`, processo
 *   `[B11C]` = `0x7E63` com ID 0x230), tipo 0x270 (também o ID do processo) e `*+AR7(2C)` = 1..2 passadas.
 * - **`0x7D24`** (laço externo): `[E540]` ≠ 0 ou `*+AR7(2E)` ≠ 0 → sai (`0x7E29`). Cada passada
 *   (`0x7C86`) põe o carro 1000 abaixo no trecho `[[[E470]+17]+17]` (deslocamento `[E878]+46`) e o faz
 *   seguir os trechos `+17` (`0x66DB`) até ficar a 22000 do focado. Depois das passadas, uma vez,
 *   com 50% de chance (`*+AR7(2F)` = 0) vem a passada em sentido contrário (`0x7D39`, trechos `+21`,
 *   `0x66EB`, deslocamento −`[E878]+46`): acompanha o jogador, acelera ou freia conforme está à frente
 *   ou atrás (`*+AR7(29)`), e a cada 10..19 quadros, no modo de 1 jogador perto de 2300, conta
 *   `*+AR7(2B)` até `*+AR7(2D)` (20..39).
 * - **`0x7E08`**: sai de cena por 101 quadros (caindo, `R7` −3 até −95 por quadro, volta aleatória).
 * - **`0x7E29`**: devolve os dados, solta o objeto, cala o som 0x135, mata o processo das rodas e morre.
 * - **`0x7E64`**: dirige com volante `R2` e velocidade `R3` (`+11..+13` suavizados, inclinação `+1E`).
 * - **`0x7EC6`**: som 0x135 nos canais `[E8FF]`/`[E908]` com volume 128 + 127·(20000 − d)·`[B11D]`.
 */

#include "core/subroutines.hpp"

namespace rea::core::game {

/// @brief Registra `0x7CFA`, `0x7E3F`, `0x7E63`, `0x7E64`, `0x7EC6`, `0xC10000` e os pontos de retomada
///        (`0x7CF9`, `0x7E07`, `0x7E27`, `0xC1000E`).
void registerSpecialCar(SubroutineDispatcher& d);

} // namespace rea::core::game
