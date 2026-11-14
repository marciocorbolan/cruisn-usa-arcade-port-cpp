/**
 * @file cmos_file.cpp
 * @brief Leitura e gravação de `save/cmos.nv`.
 */
#include "core/cmos/cmos_file.hpp"

#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

#include "core/vunit_hw.hpp"

namespace rea::core::cmos {

bool loadCmosFile(MemoryBus& bus, const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    const std::vector<uint8_t> b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (b.size() != static_cast<std::size_t>(vunit::kCmosWords) * 4u) return false;
    std::vector<uint32_t> words(vunit::kCmosWords);
    for (std::size_t i = 0; i < words.size(); ++i) {
        const std::size_t o = i * 4u;
        words[i] = static_cast<uint32_t>(b[o]) | static_cast<uint32_t>(b[o + 1]) << 8 |
                   static_cast<uint32_t>(b[o + 2]) << 16 | static_cast<uint32_t>(b[o + 3]) << 24;
    }
    return bus.restoreCmos(words);
}

bool saveCmosFile(const MemoryBus& bus, const std::filesystem::path& file) {
    std::error_code ec;
    if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);
    std::filesystem::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        for (uint32_t w : bus.cmosWords()) {
            const char bytes[4] = {static_cast<char>(w & 0xFFu), static_cast<char>(w >> 8 & 0xFFu),
                                   static_cast<char>(w >> 16 & 0xFFu), static_cast<char>(w >> 24 & 0xFFu)};
            out.write(bytes, 4);
        }
        out.close();  // o close descarrega o buffer: um erro de escrita (disco cheio) só aparece aqui
        if (!out) {
            std::filesystem::remove(tmp, ec);  // não troca o arquivo bom por um truncado
            return false;
        }
    }
    std::filesystem::rename(tmp, file, ec);  // substitui o anterior de uma vez
    if (ec) {
        std::filesystem::remove(file, ec);   // Windows antigo: rename não sobrescreve
        ec.clear();
        std::filesystem::rename(tmp, file, ec);
    }
    return !ec;
}

} // namespace rea::core::cmos
