/**
 * @file boot_screens.cpp
 * @brief Tradução de `0x4F32`–`0x4FA4`.
 */
#include "core/boot/boot_screens.hpp"

namespace rea::core::boot {

namespace {

constexpr uint32_t kWhite = 1;    ///< Índice de paleta do texto branco.
constexpr uint32_t kGray = 0x0B;  ///< Índice de paleta do texto cinza.

/// 04F42–04F64  título, créditos, "DOWNLOADING TEXTURES", versão
constexpr BootScreenLine kHeader[] = {
    {"BOOT_TITLE", 0xB095, 0x01, 0x28, kWhite},             // CRUISN USA (TM) HEAD 2 HEAD
    {"BOOT_COPYRIGHT", 0xB096, 0x01, 0x46, kWhite},         // (C) 1994 NINTENDO, DEVELOPED BY TV GAMES, INC.
    {"BOOT_MANUFACTURED", 0xB097, 0x01, 0x50, kWhite},      // MANUFACTURED AND SOLD BY MIDWAY ...
    {"BOOT_UNDER_LICENSE", 0xB098, 0x01, 0x5A, kWhite},     // UNDER LICENSE.
    {"BOOT_DOWNLOADING_TEXTURES", 0xB099, 0x28, 0x6E, kGray},
    {"BOOT_VERSION", 0xB09A, 0x28, 0x82, kWhite},           // VERSION  4.1
    {"BOOT_VERSION_CODE", 0xB09B, 0x104, 0x96, kWhite},     // I401
    {"BOOT_BUILD_DATE", 0xB09C, 0x28, 0x96, kWhite},        // MON FEB 13 16:53:40 1995
};
constexpr BootScreenLine kLoadingOs{"BOOT_LOADING_OS", 0xB09D, 0x01, 0xBE, kGray};  ///< "LOADING OS", desenhada no fim da `0x4F32`.
/// Linhas da `0x4F7D` (primeira etapa da carga).
constexpr BootScreenLine kStep1[] = {
    {"BOOT_INODES", 0xB09E, 0x01, 0xC8, kGray},
    {"BOOT_SATELLITE", 0xB09F, 0x01, 0xDC, kGray},
    {"BOOT_NETLINK", 0xB0A0, 0x01, 0xE6, kGray},
    {"BOOT_CONNECTING", 0xB0A1, 0x01, 0xF0, kGray},
};
/// Linhas da `0x4F8F` (segunda etapa).
constexpr BootScreenLine kStep2[] = {
    {"BOOT_CONNECTED", 0xB0A2, 0x01, 0x104, kGray},
    {"BOOT_FTP_GET", 0xB0A3, 0x01, 0x10E, kGray},
};
/// Linhas da `0x4F9B` (terceira etapa).
constexpr BootScreenLine kStep3[] = {
    {"BOOT_BYTES_RECEIVED", 0xB0A4, 0x01, 0x118, kGray},
    {"BOOT_TV30", 0xB0A5, 0x01, 0x122, kGray},
};

/// `LDI RC / LDI R2 / LDI R3 / LDI @(ptr),AR2 / CALL $A7F4`.
void drawLine(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs, std::vector<std::string>& shown,
              const BootScreenLine& l) {
    cpu.rc = l.color;
    cpu.r[2] = l.x;
    cpu.r[3] = l.y;
    cpu.ar[2] = bus.readWord(cpu.direct(l.rom_ptr));
    subs.call(0xA7F4, cpu, bus);
    shown.emplace_back(l.i18n_key);
}

/// Desenha todas as linhas de @p lines com `drawLine`.
template <std::size_t N>
void drawLines(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs, std::vector<std::string>& shown,
               const BootScreenLine (&lines)[N]) {
    for (const auto& l : lines) drawLine(cpu, bus, subs, shown, l);
}

/// `0x4F32`: paleta mínima, limpa as páginas, desenha o cabeçalho, a moldura e "LOADING OS".
void loadingScreen(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs, std::vector<std::string>& shown) {
    // 04F32  paleta mínima (DP = 0x9E)
    cpu.r[0] = 0;
    bus.writeWord(0x9E0000, cpu.r[0]);
    cpu.r[0] = 0xFFFFFFFFu;
    bus.writeWord(0x9E0001, cpu.r[0]);
    cpu.r[0] = 0x6000;
    bus.writeWord(0x9E0003, cpu.r[0]);
    cpu.r[0] = 0x294A;
    bus.writeWord(0x9E000B, cpu.r[0]);
    cpu.dp = 0;
    // 04F3C  limpa as páginas; exibe a 0; desenha na 0
    subs.call(0x8E79, cpu, bus);
    subs.call(0x8E4E, cpu, bus);
    cpu.r[0] = 0x0090u << 16;
    bus.writeWord(cpu.direct(0xCE13), cpu.r[0]);
    drawLines(cpu, bus, subs, shown, kHeader);
    // 04F65  papel no link: [0x992000] >> 16, bit 0 (leitura descartada de [0])
    cpu.ar[0] = 0;
    cpu.r[0] = bus.readWord(0x992000);
    cpu.ar[0] = bus.readWord(cpu.ar[0]);
    cpu.r[0] >>= 16;
    const bool slave = (cpu.r[0] & 0x0001u) != 0;
    cpu.ar[2] = bus.readWord(cpu.direct(slave ? 0x4F9Au : 0x4F99u));
    cpu.r[2] = 0x28;
    cpu.r[3] = 0xAA;
    subs.call(0xA7F4, cpu, bus);
    shown.emplace_back(slave ? "BOOT_LINK_SLAVE" : "BOOT_LINK_MASTER");
    // 04F71  moldura: AR2 = 0xEF, R2 = 0x6E, R3 = 0x12D, RC = 0x75, RS = 0xB
    cpu.ar[2] = 0xEF;
    cpu.r[2] = 0x6E;
    cpu.r[3] = 0x12D;
    cpu.rc = 0x75;
    cpu.rs = 0x0B;
    subs.call(0xABFC, cpu, bus);
    drawLine(cpu, bus, subs, shown, kLoadingOs);
}

} // namespace

void registerBootScreens(SubroutineDispatcher& d, std::vector<std::string>& shown) {
    d.add(0x4F32, [&shown](Tms32031State& c, MemoryBus& b, ISubroutines& s) { loadingScreen(c, b, s, shown); });
    d.add(0x4F7D, [&shown](Tms32031State& c, MemoryBus& b, ISubroutines& s) { drawLines(c, b, s, shown, kStep1); });
    d.add(0x4F8F, [&shown](Tms32031State& c, MemoryBus& b, ISubroutines& s) { drawLines(c, b, s, shown, kStep2); });
    d.add(0x4F9B, [&shown](Tms32031State& c, MemoryBus& b, ISubroutines& s) { drawLines(c, b, s, shown, kStep3); });
}

} // namespace rea::core::boot
