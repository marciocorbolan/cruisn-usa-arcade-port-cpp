/**
 * @file memory_bus.cpp
 * @brief Implementação do barramento de memória do TMS320C31 / V-Unit.
 */
#include "core/memory_bus.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>

#include "core/vunit_hw.hpp"

#ifdef __EMSCRIPTEN__
#include "core/cpu_coroutine.hpp"
#endif

namespace rea::core {

namespace {

/**
 * @brief Testa se @p addr está em [base, base + words).
 */
constexpr bool inRange(uint32_t addr, uint32_t base, uint32_t words) {
    return addr >= base && addr - base < words;
}

} // namespace

MemoryBus::MemoryBus()
    : ram_(vunit::kRamWords),
      fast_ram_(vunit::kFastRamWords),
      tms_periph_(vunit::kTmsPeriphWords),
      tms_ram_(vunit::kTmsRamWords),
      cmos_(vunit::kCmosWords),
      palette_(vunit::kPaletteWords),
      vram_(vunit::kVideoRamWords),
      texture_(static_cast<std::size_t>(vunit::kTextureRamWords) * 2u) {}

std::vector<uint32_t> MemoryBus::interleave(const std::array<std::vector<uint8_t>, 4>& lanes) {
    const std::size_t n = lanes[0].size();
    std::vector<uint32_t> words(n);
    for (std::size_t i = 0; i < n; ++i) {
        // Montagem explícita: nenhuma dependência do endianness do hospedeiro.
        words[i] = static_cast<uint32_t>(lanes[0][i])
                 | static_cast<uint32_t>(lanes[1][i]) << 8
                 | static_cast<uint32_t>(lanes[2][i]) << 16
                 | static_cast<uint32_t>(lanes[3][i]) << 24;
    }
    return words;
}

bool MemoryBus::loadProgramRom(const std::filesystem::path& rom_dir,
                               const std::array<std::string, 4>& chip_names) {
    return loadProgramRom(DirRomSource(rom_dir), chip_names);
}

bool MemoryBus::loadProgramRom(const RomSource& src, const std::array<std::string, 4>& chip_names) {
    std::array<std::vector<uint8_t>, 4> lanes;
    for (std::size_t i = 0; i < 4; ++i) {
        if (!src.read(chip_names[i], lanes[i])) {
            std::cerr << "[MemoryBus] Falha ao ler: " << chip_names[i] << " (" << src.describe() << ")\n";
            return false;
        }
        if (lanes[i].size() != kProgramChipSize) {
            std::cerr << "[MemoryBus] Tamanho inesperado: " << chip_names[i]
                      << " (" << lanes[i].size() << " bytes)\n";
            return false;
        }
    }
    program_ = interleave(lanes);
    return true;
}

bool MemoryBus::loadGraphicsRoms(const std::filesystem::path& rom_dir, unsigned first_chip) {
    return loadGraphicsRoms(DirRomSource(rom_dir), first_chip);
}

bool MemoryBus::loadGraphicsRoms(const RomSource& src, unsigned first_chip) {
    std::vector<uint32_t> banks;
    banks.reserve(static_cast<std::size_t>(vunit::kGraphicsBanks) * vunit::kGraphicsBankWords);
    for (unsigned bank = 0; bank < vunit::kGraphicsBanks; ++bank) {
        std::array<std::vector<uint8_t>, 4> lanes;
        for (unsigned lane = 0; lane < 4; ++lane) {
            const std::string name = "cusa.u" + std::to_string(first_chip + 4 * bank + lane);
            if (!src.read(name, lanes[lane]) || lanes[lane].size() != kProgramChipSize) {
                std::cerr << "[MemoryBus] EPROM gráfica inválida: " << name << '\n';
                return false;
            }
        }
        const auto words = interleave(lanes);
        banks.insert(banks.end(), words.begin(), words.end());
    }
    graphics_ = std::move(banks);
    return true;
}

void MemoryBus::hardwareReset() {
    std::fill(ram_.begin(), ram_.end(), 0u);
    const std::size_t n = std::min<std::size_t>(ram_.size(), program_.size());
    std::copy_n(program_.begin(), n, ram_.begin());   // MAME: memcpy(ram, maindata, 0x20000*4)
    std::fill(fast_ram_.begin(), fast_ram_.end(), 0u);
    std::fill(tms_periph_.begin(), tms_periph_.end(), 0u);
    std::fill(tms_ram_.begin(), tms_ram_.end(), 0u);
    std::fill(palette_.begin(), palette_.end(), 0u);
    io_.clear();
    cmos_protected_ = true;
    // A CMOS é não-volátil: preservada.
}

uint32_t MemoryBus::readRom(uint32_t rom_word) const {
    return rom_word < program_.size() ? program_[rom_word] : 0u;
}

uint32_t MemoryBus::readWord(uint32_t addr) const {
#ifdef __EMSCRIPTEN__
    CpuCoroutine::checkStall();  // navegador: prazo da CPU (não há outra thread para vigiá-la)
#endif
    addr &= kAddrMask;
    if (inRange(addr, vunit::kRamBase, vunit::kRamWords)) return ram_[addr - vunit::kRamBase];
    if (inRange(addr, vunit::kFastRamBase, vunit::kFastRamWords)) return fast_ram_[addr - vunit::kFastRamBase];
    if (inRange(addr, vunit::kTmsPeriphBase, vunit::kTmsPeriphWords)) return tms_periph_[addr - vunit::kTmsPeriphBase];
    if (inRange(addr, vunit::kTmsRamBase, vunit::kTmsRamWords)) return tms_ram_[addr - vunit::kTmsRamBase];
    if (inRange(addr, vunit::kCmosBase, vunit::kCmosWords)) return cmos_[addr - vunit::kCmosBase];
    if (inRange(addr, vunit::kPaletteBase, vunit::kPaletteWords)) return palette_[addr - vunit::kPaletteBase];
    if (inRange(addr, vunit::kVideoRamBase, vunit::kVideoRamWords)) return vram_[addr - vunit::kVideoRamBase];
    if (inRange(addr, vunit::kTextureRamBase, vunit::kTextureRamWords)) {
        // MAME textureram_r: (byte[2n+1] << 8) | byte[2n].
        const std::size_t b = static_cast<std::size_t>(addr - vunit::kTextureRamBase) * 2u;
        return static_cast<uint32_t>(texture_[b]) | static_cast<uint32_t>(texture_[b + 1]) << 8;
    }
    if (addr >= vunit::kRomBase) {
        // Janela "maindata" 0xC00000–0xFFFFFF: programa, depois os bancos gráficos;
        // o restante da região lê 0 (sem espelhamento).
        const uint32_t off = addr - vunit::kRomBase;
        if (off < program_.size()) return program_[off];
        const uint32_t g = off - vunit::kGraphicsRomOffset;
        if (off >= vunit::kGraphicsRomOffset && g < graphics_.size()) return graphics_[g];
        return 0u;
    }
    for (IIoDevice* d : devices_) {
        uint32_t v = 0;
        if (d->ioRead(addr, v)) return v;
    }
    if (auto it = io_.find(addr); it != io_.end()) return it->second;
    return 0u;
}

void MemoryBus::writeWord(uint32_t addr, uint32_t value) {
    addr &= kAddrMask;
    if (inRange(addr, vunit::kRamBase, vunit::kRamWords)) { ram_[addr - vunit::kRamBase] = value; return; }
    if (inRange(addr, vunit::kFastRamBase, vunit::kFastRamWords)) { fast_ram_[addr - vunit::kFastRamBase] = value; return; }
    if (inRange(addr, vunit::kTmsPeriphBase, vunit::kTmsPeriphWords)) { tms_periph_[addr - vunit::kTmsPeriphBase] = value; return; }
    if (inRange(addr, vunit::kTmsRamBase, vunit::kTmsRamWords)) { tms_ram_[addr - vunit::kTmsRamBase] = value; return; }
    if (inRange(addr, vunit::kCmosBase, vunit::kCmosWords)) {
        if (!cmos_protected_) cmos_[addr - vunit::kCmosBase] = value;
        return;
    }
    if (inRange(addr, vunit::kPaletteBase, vunit::kPaletteWords)) { palette_[addr - vunit::kPaletteBase] = value; return; }
    if (inRange(addr, vunit::kVideoRamBase, vunit::kVideoRamWords)) {
        vram_[addr - vunit::kVideoRamBase] = static_cast<uint16_t>(value);  // MAME videoram_w: 16 bits
        return;
    }
    if (inRange(addr, vunit::kTextureRamBase, vunit::kTextureRamWords)) {
        // MAME textureram_w: só os 16 bits baixos são gravados, como dois texels.
        const std::size_t b = static_cast<std::size_t>(addr - vunit::kTextureRamBase) * 2u;
        texture_[b] = static_cast<uint8_t>(value & 0xFFu);
        texture_[b + 1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
        return;
    }
    if (addr >= vunit::kRomBase) return; // ROM: somente leitura.

    if (addr == vunit::kCmosProtect) cmos_protected_ = (value & 0xC00u) != 0xC00u; // MAME cmos_protect_w
    io_[addr] = value;
    for (IIoDevice* d : devices_) d->ioWrite(addr, value);
    if (io_hook_) io_hook_(addr, value);
}

std::string MemoryBus::readPackedString(uint32_t rom_word, std::size_t max_chars) const {
    std::string out;
    for (uint32_t w = rom_word; out.size() < max_chars; ++w) {
        const uint32_t word = readRom(w);
        for (int b = 0; b < 4; ++b) {
            const auto c = static_cast<char>((word >> (8 * b)) & 0xFFu);
            if (c == '\0' || out.size() >= max_chars) return out;
            out.push_back(c);
        }
    }
    return out;
}

} // namespace rea::core
