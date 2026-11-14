#pragma once
/**
 * @file hiscore_scene.hpp
 * @brief Cena da tabela de recordes do modo de atração (processo `0x3889`) e auxiliares.
 *
 * @details A cena de atração `[B188]` = `0x3889` é criada pelo controlador
 * `0x3859` com AR6 = índice da tabela (rota). Ela monta a tabela **em 3D**:
 * cada letra e cada dígito é um objeto de cena com o modelo do caractere.
 *
 * Sequência:
 * 1. `PCB[2D]` = índice; `PCB[1E]` = textura de `[B189]` (`0x9EDB`);
 *    `[C940]` = 0; câmera em (−4700, 650, −4200) com rotação nula.
 * 2. Cenário: `0x440C` com `[B18A]`; esconde os tipos de `[B16C]` (`0x38F3`).
 * 3. `0x3738`: 10 linhas lidas da CMOS (`0x9AD2`/`0x9B06`), cada uma com o
 *    tempo `mm:ss:cc` (`0x3788`) e as 3 iniciais (`0x37E6`). As linhas são
 *    afastadas em z (`0x38F9`) e o grupo `0x200` avança 120.
 * 4. Retira o objeto tipo 9 e escreve o título centralizado (`0x392E` → `0x3824`).
 * 5. Refaz as listas (`0x7298`) e cria o processo filho `[B18B]` = `0x38CD`
 *    (ID 0x561, destaque piscante do recorde novo).
 * 6. A cada quadro, aproxima as linhas 250 em z (`0x3913`, limite 1000) até a
 *    última chegar a 999; espera 60 quadros; move a câmera (−21 em y, +56 em z)
 *    por 40 quadros; espera 60; mata o filho e morre.
 *
 * O tempo é guardado em 1/55 s: `0x60BD` separa minutos (÷3300), segundos
 * (÷55) e centésimos (× 100/55), e o máximo é `[B180]` = 197999 (59:59.99).
 */

#include "core/subroutines.hpp"

namespace rea::core::game {

/// @brief `0x60BD`: R0 (1/55 s) → R2 = minutos (0–99), R1 = segundos (0–59), R0 = centésimos (0–99). Preserva R3.
void splitTime(Tms32031State& cpu, MemoryBus& bus);
/// @brief `0x37C2`: texto decimal em AR2 → dígitos, do menos significativo ao mais, no mesmo lugar, completando com 0.
void textToDigits(Tms32031State& cpu, MemoryBus& bus);
/// @brief `0x3813`: AR2 = caractere → objeto com o modelo do caractere (`[[B168] + i]`) via `0x700A`.
void allocCharObject(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);
/// @brief `0x3824`: texto empacotado em AR2 como objetos-letra centrados em x = R2 (y = R3, z = R4, tipo R6), espaçados 80.
void spawnText(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/// @brief Registra a cena `0x3889`, as retomadas e os auxiliares (`0x3738`–`0x3953`, `0x60BD`, `0x9AD2`, `0x9B06`).
void registerHiscoreScene(SubroutineDispatcher& d);

} // namespace rea::core::game
