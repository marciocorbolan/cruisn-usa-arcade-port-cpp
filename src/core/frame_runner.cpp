/**
 * @file frame_runner.cpp
 * @brief Implementação do FrameRunner.
 */
#include "core/frame_runner.hpp"

#include "core/vunit_hw.hpp"

namespace rea::core {

FrameRunner::Result FrameRunner::runFrame(Tms32031State& cpu) {
    adc_count_ = 0;
    // VBLANK: a linha INT0 é acionada na scanline 399.
    cpu.if_ |= Tms32031State::kIeInt0;
    if (accepts(cpu, Tms32031State::kIeInt0)) {
        if (vblank_.run(cpu) == irq::VblankHandler::Result::WatchdogReset) return Result::WatchdogReset;
    }
    // Conversões do ADC0844 encadeadas pela INT3.
    while (cabinet_.int3Pending() && adc_count_ < kMaxAdcPerFrame) {
        cpu.if_ |= Tms32031State::kIeInt3;
        if (!accepts(cpu, Tms32031State::kIeInt3)) break;
        cabinet_.ackInt3();
        adc_.run(cpu);
        ++adc_count_;
    }
    return Result::Ok;
}

bool FrameBusyWait::spin(Tms32031State& cpu, uint32_t instructions) {
    if (reset_) return false;
    acc_ += instructions;
    while (acc_ >= vunit::kInstructionsPerFrame) {
        acc_ -= vunit::kInstructionsPerFrame;
        ++frames_;
        if (hook_) hook_();
        if (!reset_ && runner_.runFrame(cpu) == FrameRunner::Result::WatchdogReset) reset_ = true;
        // Watchdog de hardware: zera a cada borda de subida do bit 3 de 0x994000 (INT0 → 0x5029).
        if (const uint64_t k = runner_.watchdogKicks(); k != last_kicks_) {
            last_kicks_ = k;
            starved_frames_ = 0;
        } else if (++starved_frames_ > hw_watchdog_frames_ && hw_watchdog_frames_ != 0) {
            reset_ = true;
        }
        if (reset_) return false;
    }
    return true;
}

} // namespace rea::core
