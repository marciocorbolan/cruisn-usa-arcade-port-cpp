/**
 * @file texture_pack.cpp
 * @brief Leitura/escrita de BMP 8 bits para o pacote de texturas.
 */
#include "core/video/texture_pack.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

namespace rea::core::video {

namespace {

/// Lê um inteiro de 32 bits little-endian de @p b no deslocamento @p o.
uint32_t le32(const std::vector<uint8_t>& b, std::size_t o) {
    return static_cast<uint32_t>(b[o]) | static_cast<uint32_t>(b[o + 1]) << 8 | static_cast<uint32_t>(b[o + 2]) << 16 |
           static_cast<uint32_t>(b[o + 3]) << 24;
}
/// Lê um inteiro de 16 bits little-endian de @p b no deslocamento @p o.
uint16_t le16(const std::vector<uint8_t>& b, std::size_t o) {
    return static_cast<uint16_t>(b[o] | b[o + 1] << 8);
}

} // namespace

std::string TexturePack::pageName(int page) {
    char name[16];
    std::snprintf(name, sizeof name, "page_%02d.bmp", page);
    return name;
}

int TexturePack::loadDir(const std::filesystem::path& dir) {
    int n = 0;
    std::error_code ec;
    rejected_.clear();
    if (!std::filesystem::is_directory(dir, ec)) return 0;
    for (int p = 0; p < kPages; ++p) {
        const auto file = dir / pageName(p);
        if (!std::filesystem::exists(file, ec)) continue;
        if (loadPage(p, file)) ++n;
        else rejected_.push_back(pageName(p));  // ex.: salva em 24 bits por um editor de imagem
    }
    return n;
}

bool TexturePack::loadPage(int page, const std::filesystem::path& file) {
    if (page < 0 || page >= kPages) return false;
    std::ifstream in(file, std::ios::binary);
    const std::vector<uint8_t> b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (b.size() < 54 || b[0] != 'B' || b[1] != 'M') return false;
    const uint32_t data = le32(b, 10);
    const auto w = static_cast<int32_t>(le32(b, 18));
    const auto h = static_cast<int32_t>(le32(b, 22));
    if (le16(b, 28) != 8 || le32(b, 30) != 0) return false;  // 8 bits, BI_RGB
    if (w != kPageSide || (h != kPageSide && h != -kPageSide)) return false;
    const std::size_t stride = kPageSide;  // 256 já é múltiplo de 4
    if (data + stride * kPageSide > b.size()) return false;
    auto px = std::make_unique<std::vector<uint8_t>>(kPageBytes);
    for (int v = 0; v < kPageSide; ++v) {
        const int row = h > 0 ? kPageSide - 1 - v : v;  // positivo: de baixo para cima
        const std::size_t src = data + static_cast<std::size_t>(row) * stride;
        std::copy(b.begin() + static_cast<std::ptrdiff_t>(src), b.begin() + static_cast<std::ptrdiff_t>(src + kPageSide),
                  px->begin() + static_cast<std::ptrdiff_t>(v) * kPageSide);
    }
    if (!pages_[static_cast<std::size_t>(page)]) ++count_;
    pages_[static_cast<std::size_t>(page)] = std::move(px);
    return true;
}

bool TexturePack::writeBmp8(const std::filesystem::path& file, const uint8_t* pixels, const std::array<uint32_t, 256>& rgb) {
    std::ofstream out(file, std::ios::binary);
    if (!out) return false;
    auto u32 = [&](uint32_t v) {
        const char b[4] = {static_cast<char>(v), static_cast<char>(v >> 8), static_cast<char>(v >> 16), static_cast<char>(v >> 24)};
        out.write(b, 4);
    };
    auto u16 = [&](uint16_t v) {
        const char b[2] = {static_cast<char>(v), static_cast<char>(v >> 8)};
        out.write(b, 2);
    };
    const uint32_t data = 14 + 40 + 256 * 4;
    out.write("BM", 2);
    u32(data + static_cast<uint32_t>(kPageBytes));
    u32(0);
    u32(data);
    u32(40);
    u32(kPageSide);
    u32(kPageSide);  // positivo: de baixo para cima
    u16(1);
    u16(8);
    u32(0);  // BI_RGB
    u32(static_cast<uint32_t>(kPageBytes));
    u32(2835);
    u32(2835);
    u32(256);
    u32(0);
    for (uint32_t c : rgb) u32(c & 0xFFFFFFu);  // B, G, R, 0 em little-endian
    for (int row = kPageSide - 1; row >= 0; --row)
        out.write(reinterpret_cast<const char*>(pixels + static_cast<std::size_t>(row) * kPageSide), kPageSide);
    out.close();  // descarrega o buffer: um erro de escrita (disco cheio) só aparece aqui
    return static_cast<bool>(out);
}

int TexturePack::dumpRam(const MemoryBus& bus, const std::function<uint16_t(int)>& paletteBase,
                         const std::filesystem::path& dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    auto pal5 = [](uint32_t c) { return ((c & 0x1Fu) << 3) | ((c & 0x1Fu) >> 2); };
    std::vector<uint8_t> px(kPageBytes);
    int n = 0;
    for (int p = 0; p < kPages; ++p) {
        bool any = false;
        for (std::size_t i = 0; i < kPageBytes; ++i) {
            px[i] = bus.textureByte(static_cast<std::size_t>(p) * kPageBytes + i);
            any |= px[i] != 0;
        }
        if (!any) continue;
        std::array<uint32_t, 256> rgb{};
        const uint32_t base = paletteBase(p);
        for (uint32_t i = 0; i < 256; ++i) {
            const uint32_t c = bus.paletteWord((base + i) & 0x7FFFu);  // RGB555
            rgb[i] = pal5(c >> 10) << 16 | pal5(c >> 5) << 8 | pal5(c);
        }
        if (writeBmp8(dir / pageName(p), px.data(), rgb)) ++n;
    }
    return n;
}

} // namespace rea::core::video
