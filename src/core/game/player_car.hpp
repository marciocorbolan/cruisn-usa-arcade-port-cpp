#pragma once
/**
 * @file player_car.hpp
 * @brief Processo do carro do jogador (`0x29B8`/`0x29EE` → laço `0x2A5C`) e suas rotinas:
 *        pedais e volante, marcha, câmera (vistas 0/1/2), luz de freio, sons do motor e dos
 *        pneus, rodopio e reposição na pista.
 *
 * @details AR4 = objeto do carro (`[E876]`), AR5 = dados D (`[obj+0x1B]`, também em `[E878]`),
 * AR7 = PCB do processo. Globais usadas:
 * - `[E871]` vista (0 = cabine, 1 = atrás, 2 = longe); `[E86B]`/`[E86E]` distância/altura da câmera
 *   e `[E86C]`/`[E86F]` passo da transição até `[E86D]`/`[E870]` (`0x2EE6`);
 * - `[E872]` luz de freio acesa, `[E873]` rodopio em curso (`0x5567`), `[E87B]` quadros em 1º lugar;
 * - `[E87D]`/`[E87E]` fatores de potência/vácuo (só com link), `[E897]` último volume do motor;
 * - `[E86A]` quadros de tolerância fora da estrada (`0x2CD3`; 0 → reposição `0x2BF5`);
 * - `[E605]` velocímetro (velocidade × 0,49), `[C8EB]` fator de esterço da HUD;
 * - `[0x809807]` rumo da câmera, `[0x809806]` inclinação da câmera, `[0x45]` posição da câmera,
 *   `[0x47]` matriz da câmera.
 *
 * **Laço `0x2A5C`** (1 vez por quadro): abandono `[E635]` → `0x1D59`; tempo esgotado → `0x3C71`;
 * modo 5 → `0x2976`. Dirigindo: superfície `0x2CD3`, marcha `0x2E61` (troca para cima com
 * pedal > 0,7 dá o som 0x1C8 e o dobro do acelerador), acelerador `0x2E57`, freio `0x2E0C`,
 * velocidade `0x2D83`, derrapagem `0x2C9C`, volante `0x2E88` → esterço `0x2CEB`, bordas
 * `0x2F63`, rodopio `0x2F17`, matrizes e posição (`0x9081`, chão `0x1FF6`). Em rodopio
 * (`[E873]`), `0x5567` gira o carro por 60 unidades de tempo. Depois a câmera: cabine (matriz
 * transposta do carro com a inclinação do balanço `[D+0x42]`/`[D+0x43]` suavizada 80/20 com o
 * quadro anterior — `0x318C`/`0x3195`) ou perseguição (rumo pela velocidade, 20% por quadro,
 * desviando de laterais sem chão a ±170 — `0x2BB7`/`0x2BBD`). Por fim os sons (`0x2C4C`,
 * `0x3097`), a impressora/modem (`0x472A`), a vibração do volante nas faixas `0x304A`, a altura
 * `0x67EC` e, com link, a sincronia `0x7ADB` e o vácuo `0x2B61`.
 *
 * **Desvio de port:** as esperas ativas de `0x1D59` (DMA e troca de página) viram `sleep 1` na
 * mesma rotina, como em `0x6C9E`; `0x318C`/`0x3195` (cópia da matriz da câmera na pilha) viram
 * uma cópia local, porque o par só existe dentro do mesmo quadro.
 *
 * **Quirk preservado:** em `0x2B19` a ROM faz `LDIU $0080,R6` (em vez de DP) e lê
 * `LDF ($9807),R6` com DP = 0, isto é, a palavra 0x9807 da ROM de programa, não o rumo da câmera.
 */

#include "core/subroutines.hpp"

namespace rea::core::game {

/// @brief Registra `0x29B8`, `0x29EE`, `0x2A5C`, `0x2B60`, `0x2976`, `0x297D`, `0x2982`,
///        `0x2B61`, `0x2B93`, `0x2B95`, `0x2BAF`, `0x2BB7`, `0x2BBD`, `0x2BCF`, `0x2BE5`,
///        `0x2BF5`, `0x2C4C`, `0x2CD3`, `0x2D4A`, `0x2E0C`, `0x2E31`, `0x2E3B`, `0x2E57`,
///        `0x2E61`, `0x2E88`, `0x2EB4`, `0x2EB9`, `0x2EBE`, `0x2EE6`, `0x2F17`, `0x304A`,
///        `0x3097`, `0x3143`, `0x3148`, `0x25D6`, `0x5567`, `0x3C71`, `0x472A`, `0x1D59`
///        e `0x1D78`.
void registerPlayerCar(SubroutineDispatcher& d);

} // namespace rea::core::game
