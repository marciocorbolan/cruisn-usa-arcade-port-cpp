/**
 * @file cabinet_io.cpp
 * @brief Implementação das portas do gabinete e do ADC0844.
 */
#include "core/io/cabinet_io.hpp"

#include "core/vunit_hw.hpp"

namespace rea::core::io {

namespace {
/// Replica uma porta de 16 bits nas duas metades da palavra de 32 bits.
constexpr uint32_t both(uint16_t v) { return (static_cast<uint32_t>(v) << 16) | v; }
} // namespace

bool CabinetIo::ioRead(uint32_t addr, uint32_t& value) {
    switch (addr) {
        case kPortIn0: value = both(in0_); return true;
        case kPortIn1: value = both(in1_); return true;
        case kPortDsw: value = both(dsw_); return true;
        case kPortAdc:
            // MAME adc_r: leitura só com o bit 6 de 0x994000 em 0; senão, barramento em 1.
            value = (control_ & 0x40u) ? 0xFFFFFFFFu : static_cast<uint32_t>(adc_.read()) << 24;
            return true;
        case vunit::kIntcs: value = 4; return true;  // MAME intcs_r: sempre 4 (testado em 0x7F5E)
        case kPortSerial:
            // Latch de saídas/UART do controlador de movimento: só de escrita na placa principal (o MAME
            // não mapeia a leitura). Sem a placa de movimento ninguém responde: lê 0 (estado = "sem dado").
            value = 0;
            return true;
        default: return false;
    }
}

void CabinetIo::ioWrite(uint32_t addr, uint32_t value) {
    if (addr == kPortControl) {
        if ((value & 0x8u) && !(control_ & 0x8u)) ++watchdog_kicks_;  // MAME reset_line_w: borda de subida
        control_ = value;
    } else if (addr == kPortAdc && !(control_ & 0x20u)) {
        // MAME adc_w: comando só com o bit 5 em 0. A conversão termina e a
        // linha INTR do ADC0844 (ligada à INT3) é acionada.
        adc_.write(static_cast<uint8_t>(value >> 24));
        int3_pending_ = true;
    }
}

} // namespace rea::core::io
