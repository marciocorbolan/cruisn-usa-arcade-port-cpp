#pragma once
/**
 * @file cmos_file.hpp
 * @brief Persistência da CMOS (NVRAM) em `save/cmos.nv`.
 *
 * @details No gabinete a CMOS é alimentada por bateria: ajustes, auditorias, recordes e a calibração dos
 * controles (`0x0CC0`, itens 1–7) sobrevivem ao desligamento. O arquivo guarda as `vunit::kCmosWords`
 * palavras de 32 bits em little-endian (32 KiB), na ordem do barramento a partir de 0x9C0000.
 */

#include <filesystem>

#include "core/memory_bus.hpp"

namespace rea::core::cmos {

/// @brief Restaura a CMOS de @p file. @return false se o arquivo não existir ou tiver outro tamanho.
bool loadCmosFile(MemoryBus& bus, const std::filesystem::path& file);

/// @brief Grava a CMOS em @p file (por um arquivo temporário e renomeação). @return false se falhar.
bool saveCmosFile(const MemoryBus& bus, const std::filesystem::path& file);

} // namespace rea::core::cmos
