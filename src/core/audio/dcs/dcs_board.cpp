/**
 * @file dcs_board.cpp
 * @brief Memória, trinco, saída SPORT1 e relógio da placa DCS.
 */
#include "core/audio/dcs/dcs_board.hpp"

#include <algorithm>
#include <fstream>
#include <string>

#include "core/audio/dcs/dcs_program.hpp"

namespace rea::core::audio::dcs {

namespace {
/// Ciclos por amostra de saída: 10 MHz / 31 250 Hz (SCLKDIV = 9, palavras de 16 bits).
constexpr uint32_t kCyclesPerSample = 320;
} // namespace

DcsBoard::DcsBoard() : cpu_(*this), program_(std::make_unique<DcsProgram>(cpu_, *this)) {
    co_ = std::make_unique<CpuCoroutine>([this](CpuCoroutine& co) { body(co); });
}

DcsBoard::~DcsBoard() {
    co_.reset();  // encerra a corrotina antes do programa e da memória
}

bool DcsBoard::loadRoms(const std::filesystem::path& dir) { return loadRoms(DirRomSource(dir)); }

bool DcsBoard::loadRoms(const RomSource& src) {
    std::vector<uint8_t> all;
    all.reserve(static_cast<std::size_t>(kRomBytes) * kRomCount);
    for (int k = 0; k < kRomCount; ++k) {
        std::vector<uint8_t> chip;
        if (!src.read("cusa.u" + std::to_string(2 + k), chip) || chip.size() != kRomBytes) return false;
        all.insert(all.end(), chip.begin(), chip.end());
    }
    rom_ = std::move(all);
    return true;
}

void DcsBoard::ioWrite(uint32_t addr, uint32_t value) {
    if (addr == kHostDataPort) {
        writeData(static_cast<uint8_t>(value));  // midvunit: dcs data_w(data & 0xFF)
    } else if (addr == kHostControlPort) {
        setReset((value & 2u) == 0);
    }
}

void DcsBoard::setReset(bool asserted) {
    if (asserted && !reset_) {
        reset_ = true;
        latch_.clear();  // bytes escritos antes do reset não chegam ao novo boot
    } else if (!asserted && reset_) {
        reset_ = false;
        reset_edge_ = true;  // a corrotina refaz o boot do banco 0
    }
}

void DcsBoard::run(std::size_t samples, std::vector<int16_t>& out) {
    if (!loaded()) {
        out.insert(out.end(), samples, 0);
        return;
    }
    out_ = &out;
    budget_ = static_cast<uint64_t>(samples) * kCyclesPerSample;
    while (budget_ > 0 && co_->resume()) {
    }
    out_ = nullptr;
}

// --- corrotina ---------------------------------------------------------------------------

void DcsBoard::body(CpuCoroutine& co) {
    uint16_t boot_bank = 0;
    for (;;) {
        try {
            running_ = false;
            // Em reset (ou logo depois de soltar): DSP parado, saída em silêncio.
            while (reset_) {
                if (budget_ == 0) co.yield();
                else burn(budget_);
            }
            if (reset_edge_) {
                reset_edge_ = false;
                boot_bank = 0;
                bank_ = 0;
            }
            ++boots_;
            last_boot_bank_ = boot_bank;
            cpu_.reset();
            ctrl_.fill(0);
            tx1_loaded_ = false;
            sport_irq_ = false;
            program_->setVectors(DcsProgram::Vectors::None);
            loadBootPage(boot_bank);
            running_ = true;
            if (boot_bank == 0) program_->selfTest();
            else if (boot_bank == 1) program_->resident();
            for (;;) burn(kCyclesPerSample);  // página desconhecida: parado
        } catch (const Reboot& r) {
            boot_bank = r.bank;
        }
    }
}

void DcsBoard::burn(uint64_t cycles) {
    while (cycles > 0) {
        if (running_ && (reset_edge_ || reset_)) throw Reboot{0};
        if (budget_ == 0) {
            co_->yield();
            continue;
        }
        const uint64_t step = std::min<uint64_t>({cycles, budget_, cycles_to_sample_});
        cycles -= step;
        budget_ -= step;
        cycles_to_sample_ -= static_cast<uint32_t>(step);
        if (cycles_to_sample_ == 0) {
            emitSample();
            cycles_to_sample_ = kCyclesPerSample;
        }
        if (!reset_) serviceInterrupts();
    }
}

void DcsBoard::emitSample() {
    int16_t s = 0;
    const uint16_t sys = ctrl_[ctrl::kSysControl - 0x3FE0];
    const uint16_t ab = ctrl_[ctrl::kAutobuffer - 0x3FE0];
    if (!reset_ && (sys & 0x0800) && tx1_loaded_) {
        s = static_cast<int16_t>(tx1_);
        tx1_loaded_ = false;
        if (ab & 0x0002) {
            // Autobuffer TX: a próxima palavra vem de DM(I, M) (I = bits 9–11, M = bits 7–8 + bit 2 do I).
            const int ireg = (ab >> 9) & 7;
            const int mreg = ((ab >> 7) & 3) | (ireg & 4);
            const uint32_t before = cpu_.i[ireg];
            tx1_ = cpu_.dm(ireg, mreg);
            tx1_loaded_ = true;
            if (cpu_.i[ireg] <= before) sport_irq_ = true;  // deu a volta no buffer circular
        }
    }
    if (out_) out_->push_back(s);
}

void DcsBoard::serviceInterrupts() {
    // IRQ2 (nível): ativa enquanto houver byte no trinco. O tratador lê DM(0x3400), que retira o byte.
    for (std::size_t guard = latch_.size(); guard > 0 && !latch_.empty() && (cpu_.imask & 0x20); --guard)
        program_->irq2();
    if (sport_irq_ && (cpu_.imask & 0x04)) {
        sport_irq_ = false;
        program_->sportTx();
    }
}

// --- memória --------------------------------------------------------------------------------

uint8_t DcsBoard::romByte(uint16_t bank, uint32_t offset) const {
    const uint32_t chip = (bank >> 8) & 7u;
    const uint32_t page = bank & 0xFFu;
    if (page >= 0x80 || !loaded()) return 0xFF;
    return rom_[chip * kRomBytes + page * 0x1000u + (offset & 0xFFFu)];
}

uint32_t DcsBoard::loadBootPage(uint16_t bank) {
    const uint32_t words = 8u * (static_cast<uint32_t>(romByte(bank, 3)) + 1u);
    for (uint32_t w = 0; w < words && w < iram_pm_.size(); ++w)
        iram_pm_[w] = (static_cast<uint32_t>(romByte(bank, 4 * w)) << 16) |
                      (static_cast<uint32_t>(romByte(bank, 4 * w + 1)) << 8) | romByte(bank, 4 * w + 2);
    return words;
}

uint16_t DcsBoard::dmRead(uint32_t a) {
    a &= 0x3FFF;
    if (a < 0x2000) return static_cast<uint16_t>(ext_ram_[a & 0x7FF] >> 8);
    if (a < 0x3000) return static_cast<uint16_t>(0xFF00u | romByte(bank_, a - 0x2000));
    if (a >= 0x3400 && a < 0x3800) {
        // Trinco de entrada: a leitura reconhece a IRQ2 (auto-ack).
        if (!latch_.empty()) {
            last_latch_ = latch_.front();
            latch_.pop_front();
        }
        return last_latch_;
    }
    if (a >= 0x3800 && a < 0x3A00) return iram_dm_[a - 0x3800];
    if (a >= 0x3FE0) return ctrl_[a - 0x3FE0];
    return 0;
}

void DcsBoard::dmWrite(uint32_t a, uint16_t v) {
    a &= 0x3FFF;
    if (a < 0x2000) {
        uint32_t& w = ext_ram_[a & 0x7FF];
        w = (w & 0xFFu) | (static_cast<uint32_t>(v) << 8);
    } else if (a >= 0x3000 && a < 0x3400) {
        bank_ = v & 0x7FF;
    } else if (a >= 0x3800 && a < 0x3A00) {
        iram_dm_[a - 0x3800] = v;
    } else if (a >= 0x3FE0) {
        ctrl_[a - 0x3FE0] = v;
        if (a == ctrl::kSysControl && (v & 0x0200)) {
            // Novo boot a partir do banco atual (o MAME zera o registrador).
            ctrl_[a - 0x3FE0] = 0;
            throw Reboot{bank_};
        }
    }
    // 0x3400–0x37FF: trinco de saída (a CPU principal do Cruis'n USA não o lê).
}

uint32_t DcsBoard::pmRead(uint32_t a) {
    a &= 0x3FFF;
    if (a < 0x400) return iram_pm_[a];
    if (a >= 0x800 && a < 0x2000) return ext_ram_[a & 0x7FF];
    return 0;
}

void DcsBoard::pmWrite(uint32_t a, uint32_t v) {
    a &= 0x3FFF;
    v &= 0xFFFFFFu;
    if (a < 0x400) iram_pm_[a] = v;
    else if (a >= 0x800 && a < 0x2000) ext_ram_[a & 0x7FF] = v;
}

} // namespace rea::core::audio::dcs
