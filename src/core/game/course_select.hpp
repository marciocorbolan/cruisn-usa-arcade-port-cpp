#pragma once
/**
 * @file course_select.hpp
 * @brief Tela de seleção de percurso (`0x6C8F`–`0x6FE8`), chamada local do fluxo da partida
 *        (`[B1EB]`), e as rotinas de pedal/volante que ela usa (`0x1C62`, `0x1C85`, `0x1D38`).
 *
 * @details **Montagem** (`0x6C8F`): espera o DMA, pede a troca de página (`[C93F]` = 1) e espera a
 * INT0 atendê-la (no port: dorme 1 quadro em `0x6C9E` até `[C93F]` = 0), reinicia as paletas
 * (`0x95F5`), descomprime as texturas do mapa (`0xA3C2`, esperando `[E5CA]` = 0), põe a câmera em
 * (0, 0, −368), carrega o mapa dos EUA (`0x440C` com `[C6E6]`), prepara os objetos (`0x6ECD`),
 * toca o som 0x1B9 e anima a entrada (`0x6EA2`: 21 quadros em que cada objeto desce até a posição
 * final, `0x6EB0`).
 *
 * **Escolha** (`0x6D48`, jogo com jogador): 12 s em `[E604]`. A cada quadro:
 * - o volante (`[C8C8]`) move o índice `[EA47]` (0..11; `0x1C62`, com histerese de 0,6 faixa);
 *   o percurso `[E634]` é `[EA47]`, menos 1 acima de 5 (o 5 ocupa duas faixas), com som 0xD5 ao
 *   mudar (`0x6E4F`);
 * - os botões de vista (`[C937]` bits 20–22 = 0x30/0x50/0x60) escolhem os percursos especiais
 *   0x401/0x406/0x409 (`0x6E0D`), trocando o modelo do marcador;
 * - o nome do percurso aparece sobre o marcador e o carrinho `*+AR7(C)` vai até ele (`0x6F93`);
 * - o título `[6C8C]` e o tempo (`0x1D38`); acelerar (`0x1C85`, borda de subida) confirma.
 *
 * **Fim** (`0x6D85`): som 0xD5, tira o carrinho da cadeia `+0x1F` e o libera; resolve o percurso
 * final (`[E636]` = 1 para o percurso 5, senão 2; especiais
 * → 0xB/1/0; normal → tabela `[6FC5]`, com auditorias), destaca o marcador (`0x6F2A`: processo
 * `0x6F42` aproxima-o e os demais se afastam, `0x6F6F`, por 31 quadros) e volta ao fluxo depois de
 * liberar as paletas (`0x8BE4`) e preparar a pista (`0x3F6F`).
 *
 * Processos de animação dos objetos do mapa: `0x5CC0` (5 quadros por quadro de animação),
 * `0x5CD0` (16 quadros iniciais e espera sorteada), `0x6F8C` (4 quadros).
 */

#include "core/subroutines.hpp"

namespace rea::core::game {

/// @brief Registra a seleção de percurso e as rotinas auxiliares.
void registerCourseSelect(SubroutineDispatcher& d);

} // namespace rea::core::game
