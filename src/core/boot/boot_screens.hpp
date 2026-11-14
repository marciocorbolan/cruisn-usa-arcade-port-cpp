#pragma once
/**
 * @file boot_screens.hpp
 * @brief Tela de carga do RESET (`0x4F32`, `0x4F7D`, `0x4F8F`, `0x4F9B`).
 *
 * @details O RESET mostra, durante a carga das texturas, uma tela de texto com
 * a fonte 8×7 da ROM (`0xA7F4`).
 *
 * A `0x4F32`:
 * 1. Monta uma paleta mínima: `[9E0000]` = 0, `[9E0001]` = branco,
 *    `[9E0003]` = 0x6000 e `[9E000B]` = 0x294A (cinza).
 * 2. Limpa as duas páginas (`0x8E79`) e exibe a página 0 (`0x8E4E`).
 * 3. Desenha nela (`[CE13]` = 0x900000) o título, os créditos, a versão,
 *    o papel no link (DIP Link Status) e uma moldura (`0xABFC`).
 *
 * As outras três acrescentam as linhas "de rede" fictícias à medida que a carga
 * avança.
 *
 * Os textos ficam na ROM e são desenhados como estão (a fonte não tem acentos).
 * As chaves de texto servem ao registro em console.
 */

#include <string>
#include <vector>

#include "core/subroutines.hpp"

namespace rea::core::boot {

/// Uma linha da tela de carga.
struct BootScreenLine {
    const char* i18n_key; ///< Chave em `assets/lang/*.lang`.
    uint32_t rom_ptr;     ///< Palavra com o ponteiro da string (ex.: 0xB095).
    uint32_t x;           ///< Coluna (R2).
    uint32_t y;           ///< Linha (R3).
    uint32_t color;       ///< RC (índice de paleta).
};

/**
 * @brief Registra `0x4F32`, `0x4F7D`, `0x4F8F` e `0x4F9B`.
 * @param shown Recebe as chaves I18N de cada linha desenhada, em ordem. Precisa
 * @param d Despachante onde as rotinas são registradas.
 *        viver tanto quanto @p d.
 */
void registerBootScreens(SubroutineDispatcher& d, std::vector<std::string>& shown);

} // namespace rea::core::boot
