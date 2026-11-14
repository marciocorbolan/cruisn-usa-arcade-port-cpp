/**
 * @file link_port.cpp
 * @brief Implementação da placa de link (MAME `comcs_r`/`comcs_w`).
 */
#include "core/io/link_port.hpp"

#include "core/vunit_hw.hpp"

namespace rea::core::io {

namespace {
constexpr uint8_t kComCoe = 0x20;  ///< Dirige as linhas de controle.
constexpr uint8_t kComDoe = 0x40;  ///< Dirige as linhas de dados.
} // namespace

uint16_t LinkPort::busOut() const {
    // MAME comm_bus_out
    uint16_t mask = 0;
    if (comm_flags_ & kComCoe) mask = static_cast<uint16_t>(mask | ((comm_data_ >> 4) & 0x0F00));
    if (comm_flags_ & kComDoe) mask = static_cast<uint16_t>(mask | 0x00FF);
    return static_cast<uint16_t>(comm_data_ & mask);
}

bool LinkPort::ioRead(uint32_t addr, uint32_t& value) {
    if (addr - vunit::kLinkPort >= vunit::kLinkPortWords) return false;
    if (addr != vunit::kLinkPort) {
        value = 0;
        return true;
    }
    // MAME comcs_r(0): barramento sem as linhas dirigidas por esta placa.
    uint16_t data = cable_ ? cable_->bus() : busOut();
    if (wire_) data = static_cast<uint16_t>(data | wire_->peerLines());
    if (comm_flags_ & kComCoe) data = static_cast<uint16_t>(data & ~((comm_data_ >> 4) & 0x0F00));
    if (comm_flags_ & kComDoe) data = static_cast<uint16_t>(data & ~0x00FF);
    value = static_cast<uint32_t>(data) << 16;
    return true;
}

void LinkPort::ioWrite(uint32_t addr, uint32_t value) {
    if (addr == vunit::kLinkPort) comm_data_ = static_cast<uint16_t>(value >> 16);
    else if (addr == vunit::kLinkPort + 1) comm_flags_ = static_cast<uint8_t>((value >> 24) & 0xE0u);
    else return;
    if (wire_) wire_->publish(busOut());
}

} // namespace rea::core::io
