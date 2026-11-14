#pragma once
/**
 * @file track_props.hpp
 * @brief Objetos animados do cenário: tratadores por tipo da tabela `[4500]` e seus processos.
 *
 * @details A tabela `[4500]` = 0x4501 lista pares (tipo, tratador), terminada em 0. O fluxo de trechos
 * (`track_stream.cpp`) chama o tratador com AR4 = objeto recém-carregado. Os tratadores de animação
 * criam um processo (`0xA844`, ID 0x850) que troca o modelo `[AR4+0xD]` pela lista de quadros AR6
 * (valor negativo = recomeça), esperando 1 ou 2 quadros sorteados entre cada troca. O PCB criado vai
 * para `[AR4+0x17]` e o bit 23 de `[AR4+0xE]` marca o objeto como animado.
 *
 * | Tipo | Tratador | Efeito |
 * |---|---|---|
 * | 0x40A, 0x465 | `0x43DE` | lista `[4341]`, fase aleatória (0..6), processo `0x4402`; limpa o bit 0x400 |
 * | 0x466 | `0x43CB` | lista `[434C]`, fase aleatória, processo `0x4402`; limpa o bit 0x400 |
 * | 0x460 | `0x4292` | só `0xAF58` (lista `[E8EC]`, tipo 0x730) |
 * | 0x461–0x463 | `0x428C` | `0xAF58` e tipo 0x731 |
 * | 0x467 | `0x42EE` | processo de som `0x4313` + animação `[42E2]` (`0x4308`) |
 * | 0x469 | `0x4225` | modelo sorteado de `[423D]`, processo andarilho `0x4242` (ID 0x16 → 0x800) |
 * | 0x481 / 0x482 | `0x4293` / `0x429C` | listas `[42A5]` / `[42AD]`, processo `0x4401` |
 * | 0x4A1 / 0x4A2 | `0x42D6` / `0x42C3` | listas `[42CF]` / `[42BB]`, processo `0x4308` |
 *
 * **Processos:** `0x4308` (1–2 quadros), `0x43C0` (2–3 quadros), `0x4401`/`0x4402` (1–2 quadros, a
 * `0x4402` começa na fase AR5); `0x4313` mantém o som 0xB7 com volume 7 · (75 − \|dist\|/1000, ×4 atrás
 * da câmera), parando-o e morrendo a mais de 20000 atrás (salvo `[E871]` = 0 com o focado rodando);
 * `0x4242` anda para sempre a passos aleatórios e às vezes dá meia-volta (16000 à frente, +π).
 *
 * **`0x4330`**: som AR2 com volume R2. Se AR2 for o som contínuo do canal 1 (`[E8FF]`) ou 2 (`[E908]`),
 * só ajusta o volume (`0x9114`); senão toca (`0x9188`) e ajusta se ele passar a ocupar um canal.
 */

#include "core/subroutines.hpp"

namespace rea::core::game {

/// @brief Registra os tratadores `0x4225`, `0x428C`, `0x4292`, `0x4293`, `0x429C`, `0x42C3`, `0x42D6`,
///        `0x42EE`, `0x43CB`, `0x43DE`, a rotina `0x4330` e os processos `0x4242`/`0x428B`,
///        `0x4308`/`0x4312`, `0x4313`/`0x4315`, `0x43C0`/`0x43CA`, `0x4401`/`0x4402`/`0x440B`.
void registerTrackProps(SubroutineDispatcher& d);

} // namespace rea::core::game
