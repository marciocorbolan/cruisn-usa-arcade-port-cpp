/**
 * @file rom_identity.cpp
 * @brief Conferência dos chips da ROM com a versão L4.1.
 */
#include "core/rom_identity.hpp"

#include <cctype>

namespace rea::core {

const std::vector<KnownChip>& knownL41Chips() {
    static const std::vector<KnownChip> kChips = {
        {"cusa-l41.u10", 0xEB9372D1u, "programa"}, {"cusa-l41.u11", 0x76F3CD40u, "programa"},
        {"cusa-l41.u12", 0x9021A376u, "programa"}, {"cusa-l41.u13", 0x1687C932u, "programa"},
        {"cusa.u14", 0x6A4AE622u, "gráficos"},     {"cusa.u15", 0x1A0AD3B7u, "gráficos"},
        {"cusa.u16", 0x799D4DD6u, "gráficos"},     {"cusa.u17", 0x3D68B660u, "gráficos"},
        {"cusa.u18", 0x9E8193FBu, "gráficos"},     {"cusa.u19", 0x0BF60CDEu, "gráficos"},
        {"cusa.u20", 0xC07F68F0u, "gráficos"},     {"cusa.u21", 0xB0264AEDu, "gráficos"},
        {"cusa.u22", 0xAD137193u, "gráficos"},     {"cusa.u23", 0x842449B0u, "gráficos"},
        {"cusa.u24", 0x0B2275BEu, "gráficos"},     {"cusa.u25", 0x2B9FE68Fu, "gráficos"},
        {"cusa.u26", 0xAE56B871u, "gráficos"},     {"cusa.u27", 0x2D977A8Eu, "gráficos"},
        {"cusa.u28", 0xCFFA5FB1u, "gráficos"},     {"cusa.u29", 0xCBE52C60u, "gráficos"},
        {"cusa.u2", 0xB9338332u, "som"},           {"cusa.u3", 0xCD8325D6u, "som"},
        {"cusa.u4", 0xFAB457F3u, "som"},           {"cusa.u5", 0xBECC92F4u, "som"},
        {"cusa.u6", 0xA9F915D3u, "som"},           {"cusa.u7", 0x424F0BBCu, "som"},
        {"cusa.u8", 0x03C28199u, "som"},           {"cusa.u9", 0x24BA6371u, "som"},
    };
    return kChips;
}

ChipCheck checkChip(const RomSource& src, const KnownChip& k) {
    ChipCheck c;
    c.name = k.name;
    c.role = k.role;
    if (src.crc(k.name, c.crc)) c.state = c.crc == k.crc ? ChipCheck::State::Ok : ChipCheck::State::Different;
    return c;
}

std::vector<ChipCheck> verifyRom(const RomSource& src) {
    std::vector<ChipCheck> out;
    for (const KnownChip& k : knownL41Chips()) out.push_back(checkChip(src, k));
    return out;
}

bool zipMatchesL41(const ZipRomSource& zip) {
    for (const KnownChip& k : knownL41Chips()) {
        uint32_t crc = 0;
        if (!zip.crc(k.name, crc) || crc != k.crc) return false;
    }
    return true;
}

namespace {
/// @brief @p n em minúsculas.
std::string lower(std::string n) {
    for (char& ch : n) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return n;
}
/// @brief true se @p n é um chip de programa D0–D7: `cusa-l<versão>.u10` (o nome traz a versão; l41 = L4.1).
bool isProgramChip(const std::string& n) {
    const std::string low = lower(n);
    return low.size() > 10 && low.compare(0, 6, "cusa-l") == 0 && low.compare(low.size() - 4, 4, ".u10") == 0;
}
} // namespace

std::string otherProgramVersion(const std::vector<std::string>& names) {
    for (const std::string& n : names)
        if (isProgramChip(n) && lower(n) != "cusa-l41.u10") return n;
    return {};
}

std::vector<std::string> programVersions(const std::vector<std::string>& names) {
    std::vector<std::string> out;
    for (const std::string& n : names)
        if (isProgramChip(n)) out.push_back(n);
    return out;
}

std::string versionLabel(const std::string& chip) {
    const std::string low = lower(chip);
    std::string digits;
    for (std::size_t i = 6; i < low.size() && std::isdigit(static_cast<unsigned char>(low[i])); ++i) digits += low[i];
    if (digits.empty()) return low;
    return std::string(1, digits[0]) + "." + (digits.size() > 1 ? digits.substr(1) : "0");
}

} // namespace rea::core
