#pragma once
/**
 * @file smoke_effects.hpp
 * @brief Nuvens de fumaça/poeira dos carros (`0x87D7`–`0x8973`): batida, raspão e arrancada.
 *
 * @details Cada efeito é um processo `0x8860` (ID 0x540, PC em `[C6C1]`/`[C6C2]`/`[C6C3]`, criado
 * com AR7 = `[E877]`) que controla até 6 objetos de fumaça (tipo 0x766, modelo de `[[C6C0]]`).
 * Só o carro focado gera fumaça, só fora do modo de atração (`[C8C5]` & 0xF ≠ 2), só com
 * `[D+0x37]` ≠ 0, e no máximo 2 efeitos ao mesmo tempo (`0x894D`).
 *
 * **PCB** (AR7): `+0xC..+0xE` deslocamento do ponto de origem em relação ao carro, `+0x10` carro,
 * `+0x11` dados do carro, `+0x12` número de nuvens, `+0x13..` objetos das nuvens.
 *
 * **Nuvem**: `+0xD` modelo (quadro da animação), `+0x12` altura sobre o chão, `+0x13` ponteiro
 * para a lista de quadros (começa em `[C6C0]` = 0xC110AD; termina num valor negativo).
 *
 * | Rotina | Efeito |
 * |---|---|
 * | `0x87D7` | arrancada (chamada em `0x30CC`): 6 nuvens espalhadas na largura do carro, na traseira |
 * | `0x88EB` | raspão do focado (chamada em `0x2FA7`): 2 nuvens no canto do lado do muro |
 * | `0x8928` | batida do focado (chamada em `0x2665`): 2 nuvens no ponto de contato (AR3) |
 * | `0x88B2` | cria 2 nuvens a ±60 do ponto de origem, altura 180 − (y + 20..−40 − 30); sem nenhuma → mata o processo |
 * | `0x894D` | (em `process_manager.cpp`) carry = já existem ≥ 2 efeitos |
 * | `0x8960` | AR2 (objeto) += R1 · M·`[AR2+0x11..0x13]` |
 * | `0x8820` | com < 4 nuvens: mais uma atrás da última (rumo do carro, z = −50 · `[C92F]` · R7) |
 * | `0x8860` | processo: avança cada nuvem pelo quadro seguinte da animação e pela velocidade do carro (1,5 · v − 100, no rumo), cola no chão; descarta a que acabou a animação, ficou a ≤ 1000 da câmera ou saiu do chão; nova nuvem a cada 2 quadros; morre sem nuvens |
 */

#include "core/subroutines.hpp"

namespace rea::core::game {

/// @brief Registra `0x87D7`, `0x8820`, `0x88B2`, `0x88EB`, `0x8928`, `0x8960` e o processo
///        `0x8860`/`0x889E`.
void registerSmokeEffects(SubroutineDispatcher& d);

} // namespace rea::core::game
