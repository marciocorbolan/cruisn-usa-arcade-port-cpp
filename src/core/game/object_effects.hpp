#pragma once
/**
 * @file object_effects.hpp
 * @brief Processos de efeito de objetos da pista: nuvem que sobe (`0x677E`), partículas 0x520
 *        (`0x6822`) e detritos dos objetos 0x615 (`0x685D`).
 *
 * @details
 * **`0x677E`** (AR4 = objeto de origem): só perto da câmera (`+1C` ≤ `[B12D]`). Cria um objeto na
 * posição da origem com a animação `[67AF]` (lista terminada em −1), deriva fixa aleatória em x/z de
 * ±6 por quadro e sobe 10 por quadro; some ao fim da animação ou longe (`+1C` > `[B12E]`).
 *
 * **`0x6822`** (AR4 = origem): até 5 vivos (`[E5F6]`). Objeto 0x520 com a paleta `0x9EDB([B130])`,
 * flags 0x408; tabela `[685C]` em trios (quadro, espera, subida), com tremor aleatório ±6 em x/z a cada
 * passo; some ao fim ou longe (`[B131]`).
 *
 * **`0x685D`** (AR5 = carro): até 5 vivos (`[E5F7]`). Objeto `[B132]` 1500 acima do carro, espalhado
 * ±30 em x/z; cai com gravidade 25 (máx. 450) até a altura do carro e quica: no 1º toque ganha
 * velocidade horizontal aleatória ±45, depois perde metade a cada toque e inverte metade da vertical;
 * no 4º toque (ou vertical ≤ 3) fica parado até sair da vista (`+1C` < 0). Som 0x12F na criação e a
 * cada quique (fora do modo 2).
 *
 * **Quirks preservados:** em `0x6822` e `0x685D`, as saídas antecipadas depois do incremento do
 * contador (objeto longe, ou sem objeto livre) não o decrementam — o contador "vaza" e pode travar em 5.
 */

#include "core/subroutines.hpp"

namespace rea::core::game {

/// @brief Registra `0x677E`, `0x67AB`, `0x6822`, `0x6855`, `0x685D`, `0x68D9` e `0x68DE`.
void registerObjectEffects(SubroutineDispatcher& d);

} // namespace rea::core::game
