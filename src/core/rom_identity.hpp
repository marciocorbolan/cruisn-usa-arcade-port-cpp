#pragma once
/**
 * @file rom_identity.hpp
 * @brief Identificação da ROM: confere cada chip com a versão L4.1, a única que o port traduz.
 *
 * @details O port é a tradução, rotina por rotina e por endereço, do programa da versão **L4.1**
 * (`cusa-l41.u10`–`u13`); tabelas, textos, modelos, texturas e sons também são lidos da ROM em endereços fixos.
 * Outra versão do programa (L4.0, L2.1...) tem rotinas e dados em outros endereços, e um chip corrompido ou
 * modificado muda dados sem aviso. O leitor de `.zip` só confere o CRC de cada arquivo com o próprio zip; aqui
 * cada chip é comparado com o CRC-32 conhecido da L4.1 (os do conjunto em que o port foi traduzido e verificado:
 * ele passa no autoteste de checksums do próprio jogo, `0x6381`, e em toda a regressão).
 */

#include <cstdint>
#include <string>
#include <vector>

#include "core/rom_source.hpp"

namespace rea::core {

/// Chip da ROM com o CRC-32 conhecido da versão L4.1.
struct KnownChip {
    const char* name;  ///< Nome do arquivo (ex.: "cusa.u14").
    uint32_t crc;      ///< CRC-32 do conteúdo.
    const char* role;  ///< Função: "programa", "gráficos" ou "som".
};

/// @return Os 28 chips da L4.1 usados pelo port: programa (u10–u13), gráficos (u14–u29) e som (u2–u9).
const std::vector<KnownChip>& knownL41Chips();

/// Resultado da conferência de um chip.
struct ChipCheck {
    /// Situação do chip.
    enum class State {
        Ok,         ///< Presente, com o CRC da L4.1.
        Missing,    ///< Ausente na fonte.
        Different,  ///< Presente, mas com outro conteúdo (dump ruim, modificado ou de outra versão).
    };
    std::string name;  ///< Nome do chip.
    std::string role;  ///< Função do chip.
    State state = State::Missing;  ///< Situação.
    uint32_t crc = 0;  ///< CRC-32 encontrado (Different).
};

/// @brief Confere o chip @p k em @p src.
ChipCheck checkChip(const RomSource& src, const KnownChip& k);

/// @brief Confere todos os chips de knownL41Chips() em @p src.
/// @return Um item por chip, na ordem de knownL41Chips().
std::vector<ChipCheck> verifyRom(const RomSource& src);

/// @brief true se o @p zip tem todos os chips de knownL41Chips() com o CRC da L4.1 (pelo diretório central, rápido).
bool zipMatchesL41(const ZipRomSource& zip);

/// @brief Procura em @p names o programa de outra versão (`cusa-l*.u10` diferente de `cusa-l41.u10`).
/// @return O nome do arquivo encontrado, ou vazio.
std::string otherProgramVersion(const std::vector<std::string>& names);

/// @brief Todos os chips de programa D0–D7 de @p names (`cusa-l*.u10`, inclusive o da L4.1), na ordem dada.
std::vector<std::string> programVersions(const std::vector<std::string>& names);

/// @brief Versão escrita no nome do chip: "cusa-l41.u10" → "4.1", "cusa-l21.u10" → "2.1", "cusa-l4.u10" → "4.0".
std::string versionLabel(const std::string& chip);

} // namespace rea::core
