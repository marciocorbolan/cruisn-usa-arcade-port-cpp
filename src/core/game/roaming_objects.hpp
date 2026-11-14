#pragma once
/**
 * @file roaming_objects.hpp
 * @brief Objetos do cenário que andam sozinhos: o grupo de tipos 0x470–0x476 (líder 0x261 e
 *        seguidores 0x260, que podem ser derrubados pelo jogador) e os objetos 0x498 que deslizam.
 *
 * @details Tratadores da tabela `[4500]` (AR4 = objeto):
 * | Endereço | Tipo | Ação |
 * |---|---|---|
 * | `0x9B43` | 0x470 | sem link (`[C8E3]` = 0) → `0xC101A4`: `[E8E6]` = 0, cria o líder `0x9B4E` (ID 0x261, `+C` = 1), alvo `[E8E7]`/`[E8E9]` = posição + rotY(`+15`)·(0, 0, `[9B42]`), velocidade `[E8EA]` = 240 |
 * | `0x9B46` | 0x471–0x476 | sem link → `0xC101DA`: cria o seguidor `0x9B49` (ID 0x260, `+C` = 0) |
 * | `0x4440` | 0x498 | som 0xB4 e processo `[B0D2]` = `0x4451` (ID 0x500) |
 *
 * **Processo `0x9B4E`** (`0x9B49` antes se põe logo depois do líder: `0xA912` + `0xA91F`):
 * dados de carro (`0x2945`), tipo 0x260, entra na lista `[E404]` (`0x6762`), guarda o trecho
 * `+1F` + 0x500 em `*+AR7(E)`. A cada quadro: some quando o trecho sai da janela; quando
 * `[E8E6]` ≠ 0 anda `[E8EA]`·quadros no rumo `+15` e procura o objeto 0x300 mais perto (`0x7EE9`).
 * O líder ajusta `[E8EA]` (170..280) para chegar ao alvo junto com o jogador e toca os sons
 * 0x114/0x117/0x11A; ao passar de 90000 da meta liga `[E8EB]` (todos somem).
 *
 * **Batida (`0x9BEF` → `0xC101FB`, chamada pela colisão `0x2645`)**: abaixo de 150 só marca
 * `+33` = 1. Acima, cada objeto 0x260 encadeado por `+20` ganha o processo `[9BF0]` = `0x9BF1`
 * (voo com gravidade, quique abaixo de 35, rolagem amortecida, e de pé (`0x9CC5`) dirige como carro
 * com `0x2C18`). O carro que bateu perde metade da velocidade, gira π e roda 12 vezes.
 * `0x9BEE` → `0xC101EB`: baixa o volume do som 0x114 de 5 em 5 por quadro e o desliga.
 */

#include "core/subroutines.hpp"

namespace rea::core::game {

/// @brief Registra `0x9B43`, `0x9B46`, `0x9B49`, `0x9B4E`, `0x9BEE`, `0x9BEF`, `0x9BF1`, `0x9CDF`,
///        `0x7EE9`, `0x9032`, `0x676B`, `0x4440`, `0x4451`, `0x4473`, `0x447F`, `0x448E`, `0x4495`
///        e os pontos de retomada.
void registerRoamingObjects(SubroutineDispatcher& d);

} // namespace rea::core::game
