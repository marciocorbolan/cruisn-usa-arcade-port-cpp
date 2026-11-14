#pragma once
/**
 * @file race_hud.hpp
 * @brief HUD da corrida (`0x9CFF`, por quadro nos modos 4 e 5), radar (`0x60DF`), conta-giros
 *        (`0x9E22`), letreiro do nome da pista (`0x1F56`), logotipo (`0x86C5`) e a saída da HUD
 *        (`0x9CF5`).
 *
 * @details `[E60F]` é o deslocamento da HUD (150 = fora da tela; `0x9CE3` a traz em 25 quadros de −6
 * e `0x9CF5` a leva de volta). A cada quadro a `0x9CFF`:
 * - copia o modelo do conta-giros `[B1C4]` → `[B1C5]`, apaga as barras acima de rotação·0,458 e
 *   acende a marcha (`0x9E22`, paleta do slot 0x40);
 * - desenha o radar (`0x60DF`): caixa 0x1CC..0x1F4 × 0x64..0xC8, um ponto por carro da lista
 *   `[[0x57]]` girado pelo rumo da câmera (cor 3 = CPU 0x216, 0xC = tráfego) e o jogador no centro;
 *   com link, o adversário `[E592]` fica preso na borda com uma seta;
 * - aviso do modem `[B18C]` (item 0x34), molduras `[B190..B192]`, marcha `[B193]`, logotipos
 *   `[B19E..B1A1]`, tempo `[E604]` (contagem regressiva com bipes 0x13E/0x240), velocímetro `[E605]`
 *   (MPH, ou ×1,6665 = KM/H com o item 0x22), tempo decorrido `[E85F]` (mm'ss"cc, `0x6099`) e a posição
 *   `[E607]` (`[B1A5]`); acumula a distância `[E47E]` += velocímetro·`[E87E]`·dt e `[E608]` += dt.
 *
 * **Quirk preservado:** o aviso de "fora da estrada" (`[E86A]` < 10) testa `[C8C5] & ~0xF == 4`
 * (`ANDN` no lugar de `AND`), que nunca é verdadeiro — o trecho é código morto na ROM.
 */

#include "core/subroutines.hpp"

namespace rea::core::game {

/// @brief Registra `0x9CFF`, `0x9CF5`, `0x9E22`, `0x9E59`, `0x9E63`, `0xAF48`, `0x60DF`,
///        `0x6099`, `0x60BD`, `0x1F56` e `0x86C5` (com as retomadas dos processos).
void registerRaceHud(SubroutineDispatcher& d);

} // namespace rea::core::game
