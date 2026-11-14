#pragma once
/**
 * @file car_physics.hpp
 * @brief Física vertical do carro: 5 pontos de contato com o chão (`0x1FF6`), atitude
 *        pelo plano de 3 rodas (`0x20D4`) e distância lateral à pista (`0x67E3`).
 *
 * @details Dados do carro D = `[obj+0x1B]` (AR5 do laço). Pontos de contato em D + 6·i
 * (i = 0..4):
 * - `+0..+2` = posição (float);
 * - `+3` = altura do chão relativa ao ponto (`0x20B4`);
 * - `+4` = velocidade vertical;
 * - `+5` = objeto de pista sob o ponto (0 = nenhum).
 * Offsets locais dos pontos em D + 0x47 (3 floats cada).
 *
 * **`0x2185`**: caixa do modelo `[obj+0xD]` (máx/mín de x, y, z). Offsets dos pontos:
 * (0, máxY, 0), (máxX, máxY, máxZ), (mínX, máxY, máxZ), (mínX, máxY, mínZ), (máxX, máxY, mínZ).
 * Limites: `[D+0x3A]` = máxX − 25, `[D+0x3B]` = mínX + 25, `[D+0x3C/0x3D]` = máx/mínY,
 * `[D+0x3E/0x3F]` = máx/mínZ.
 *
 * **`0x2C39`** (AR4 = objeto, AR5 = D):
 * 1. `0x1FF6` com R3 = `[AR4+0x1B]`;
 * 2. `[AR4+0x15]` = rumo `[AR5+0x2C]`;
 * 3. matriz do objeto = matriz de rumo (`0x938A` em `[0x5A]`) × matriz do objeto (`0x9494`);
 * 4. `[AR5+0x46]` = distância lateral à pista (`0x67E3`).
 *
 * **`0x2041`**: pontos = M·offset + posição do objeto; zera `+3`/`+5`; procura o chão em
 * `[E403]` (`0x2074`) e, se algum ponto ficou sem chão, em `[E406]`. `[D+0x1E]` = 1 se algum
 * ponto tem chão.
 *
 * **`0x2074`**: para cada objeto da lista cujo raio somado ao do carro alcança o carro,
 * testa os 5 pontos (`0x20B4`). Ponto sem chão aceita qualquer acerto; com chão, só troca se
 * |altura| ≥ 2000 e a nova altura for melhor (regras de sinal da ROM).
 *
 * **`0x1FF6`**:
 * - `[D+0x1E]` = tipo (`[obj+0xF]` & 0xFF0) do chão do ponto 0.
 * - Para cada ponto: altura ≤ −9 → encosta (y += altura − 9, vel = 0); senão
 *   vel = min(altura, vel + 4·`[C92F]` (+16·`[C92F]` se o chão não é 0x330)), y += vel.
 * - `[D+0x20]` = ponto 0 no ar (altura ≥ 72), `[D+0x1F]` = pontos 1 e 2 no ar.
 * - `0x20D4` com os pontos 0, 1, 2.
 *
 * **`0x20D4`**: normal n = `0x9466` (normalizada); base `[E584]`: linha 0 = (n.y, −n.x, 0)
 * normalizada, linha 1 = n, linha 2 = n × linha 0 (normalizada); a transposta vai para a
 * matriz do objeto (`0x93FD`); y do objeto = y do ponto 0 − `[D+0x48]`.
 *
 * **`0x67E3`** (AR4 = carro, AR5 = D): R0 = distância com sinal do carro à reta nó
 * `[D+0x37]` → próximo (`+0x21`); carry = 1 se < 0 ou sem próximo. `0x67EC` usa o carro
 * `[E876]`.
 */

#include "core/subroutines.hpp"

namespace rea::core::game {

/// @brief Registra `0x2185`, `0x2C39`, `0x1FF6`, `0x2041`, `0x2074`, `0x20D4`, `0x67E3` e `0x67EC`.
void registerCarPhysics(SubroutineDispatcher& d);

} // namespace rea::core::game
