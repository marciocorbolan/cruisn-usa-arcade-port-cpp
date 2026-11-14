#pragma once
/**
 * @file car_crash.hpp
 * @brief Batida forte do jogador em outro carro (`0x23EF`), capotagem do jogador (`0x5543`), o
 *        processo do carro arremessado (`0x2478`) e o chão sob a caixa do objeto (`0x1FB6`).
 *
 * @details
 * **`0x23EF`** (chamada pela resposta de colisão `0x2645` quando o focado bate a mais de 160 com
 * chance pela velocidade relativa; AR0 = jogador, AR1 = outro carro): salvo no link com o outro já
 * em rodopio, mata os processos de animação do outro (`+17` com o bit 23, `+16` com 0x800), dá a ele
 * rumo = rumo do jogador ± 0,1, velocidade = jogador · 0,75·(0,8..1,25) / massa, impulso vertical
 * −(0,3..0,6)·1,5·velocidade (até −65) e cria o processo `[2477]` = `0x2478` (ID 0x290). Depois o
 * jogador perde metade da velocidade (× `[E87E]`²) e o outro fica com pelo menos 1,25× isso. Sem
 * capotagem em curso (`[E873]` = 0), 40 % de chance de capotar o jogador (`0x5543`); senão rodopio
 * (meia-volta de π, ou na câmera ≠ 0 com 75 % de chance um giro de π em `+32`). Som `0x48`/`0x63`
 * para os modelos `[23ED]`/`[23EE]`, senão aleatório da tabela (`0x3159`).
 *
 * **`0x2478`**: fase 1 no ar (gravidade 2·dt, giros `*+AR7(C/D)`, quica com −0,5 e som da tabela
 * `[3085]` até a vertical < 35); fase 2 rolando no chão (atrito 0,98, giro 0,96, `dt` passos por
 * quadro): para de pé (`[+F]` → 0x2x, volta a ser carro de tráfego parado com `0x2C18`) ou de rodas
 * para cima (fica até ser empurrado). Some longe atrás (`+1C` < −6000) ou alto demais (vy > 300).
 *
 * **Quirks preservados:** o teste de troca de sinal do giro usa `XOR3` dos bits do float (o bit 31
 * é o do expoente, não o sinal); em `0x2545` o tipo 0x2x calculado em R0 é descartado (não é gravado).
 */

#include "core/subroutines.hpp"

namespace rea::core::game {

/// @brief Registra `0x1FB6`, `0x23EF`, `0x5543`, `0x894B`, `0x894C` e o processo `0x2478` (retomadas
///        `0x24CE`, `0x2542`, `0x254E`, `0x2586`).
void registerCarCrash(SubroutineDispatcher& d);

} // namespace rea::core::game
