#pragma once
/**
 * @file traffic_cars.hpp
 * @brief Carros de tráfego: mesmo sentido (`0x9765`, ID 0x213) e contramão (`0xA3FF`, ID 0x214).
 *
 * @details Criados pelos geradores `0x6639` e `0x65ED` (`race_traffic.hpp`). Ambos montam o carro
 * como os carros da CPU (`0x700A`, `0x8F3E`, `0xAE3A`), sorteiam uma textura (`0x668C`) e a faixa
 * (`[AR7+0xC]` = 2..3) e nascem no penúltimo nó carregado (`[[E470]+0x17]+0x17`), deslocados pela
 * faixa (`[0x5D]` = 0x809855).
 *
 * **PCB** (AR7): `+0xC` faixa, `+0xD` esterço por quadro, `+0xF` acelerador, `+0x10`/`+0x13` nó-alvo
 * e índice, `+0x14` à frente do focado (`0x68F4`), `+0x15` modo ziguezague / troca de faixa,
 * `+0x17`/`+0x18` distância ao focado (atual/anterior), `+0x19` modelo, `+0x1B` deslocamento lateral,
 * `+0x20`/`+0x21` flags, `+0x22` sorteio do modelo, `+0x23..0x26` fase, passo, amplitude e centro do
 * ziguezague.
 *
 * **`0x9765`** (laço `0x97D3`): segue os nós `+0x17` (mesmo sentido do jogador); com `+0x15` = 1
 * ondula (seno de `+0x23`) enquanto o jogador estiver entre os 5 primeiros; buzina ao se aproximar
 * do focado; perto da trajetória do focado (`0x67B0`) e à frente dele, buzina ou freia; batido
 * (`[D+0x33]`) vira destroço (`0x98B9`).
 *
 * **`0xA3FF`** (laço `0xA451`): segue os nós `+0x21` (contramão), troca de faixa periodicamente com
 * `+0x15` = 1, buzina quando o focado vem rápido (≥ 127) e perto; freia diante da trajetória do
 * focado; sai da estrada (nenhum ponto sobre tipo 0x300 depois de ter estado nela) → destroço
 * (`0xA4EE`).
 *
 * Saída da janela de trechos ou fim de vida: `0x98A1` (remove da lista, libera os dados e o
 * processo das peças, `0x7100`, morre). Com link, carros espelhados ficam parados em `0xA718`.
 *
 * | Rotina | Efeito |
 * |---|---|
 * | `0x668C` | textura aleatória da lista `[modelo+9]` (`0x9EDB`) em `+0x10`, `+0xE` \|= 0x400 |
 * | `0x66BC` | R0 = distância xz de AR4 ao focado `[E876]` |
 * | `0x66DB`/`0x66E3`/`0x66EB`/`0x66F3` | `0x6739`/`0x6708`/`0x6740`/`0x670F` no nó `[AR7+0x10]`; R0 = distância de AR4 ao nó |
 * | `0x6708`/`0x670F` | rumo do nó AR2 ao `+0x17`/`+0x21`; `[0x5D]` = rotY(rumo)·(faixa, 0, 0), faixa da tabela `[6594]` (por `0x66A7`) |
 * | `0x6739`/`0x6740` | idem com o deslocamento `[AR7+0x1B]` |
 * | `0x67B0` | carry = focado a ≤ 400 da reta de movimento do carro (e a ≤ 15000) |
 * | `0x68F4` | carry = carro à frente do focado |
 * | `0x9891` | R0 = 1 se o índice `[AR7+0x13]` está dentro da janela `[E46E]`..`[E470]` |
 * | `0xA718` | carro parado (link): `[D+0x58]` = 1 e só atualiza os pontos de contato |
 */

#include "core/subroutines.hpp"

namespace rea::core::game {

/// @brief Registra os processos `0x9761`, `0x9765`, `0x9890`, `0x98E1`, `0xA3FF`, `0xA4ED`, `0xA507`,
///        `0xA718`, `0xA72C` e as rotinas `0x668C`, `0x66BC`, `0x66DB`, `0x66E3`, `0x66EB`, `0x66F3`,
///        `0x6708`, `0x670F`, `0x6739`, `0x6740`, `0x67B0`, `0x68F4`, `0x7B25`, `0x9891`, `0x9924`.
void registerTrafficCars(SubroutineDispatcher& d);

} // namespace rea::core::game
