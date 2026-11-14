/**
 * @file boot_sequence.cpp
 * @brief Tradução de `0x4FEF`–`0x501F` e encadeamento com o RESET.
 */
#include "core/boot/boot_sequence.hpp"

#include "core/boot/reset_routine.hpp"

namespace rea::core::boot {

void BootSequence::showScreen(Tms32031State& cpu, const BootMessage (&lines)[2]) {
    for (const auto& l : lines) messages_.emplace_back(l.i18n_key);
    // 04FEF  AR6 = 8 → DBU AR6 repete 9 vezes
    cpu.ar[6] = 0x0008;
    do {
        // 04FF0  CALL $8E79; CALL $8E4E; [CE13] = 0x90 << 16
        subs_.call(0x8E79, cpu, bus_);
        subs_.call(0x8E4E, cpu, bus_);
        cpu.r[0] = (0x0090u << 16) | 0x0000u;
        bus_.writeWord(0xCE13, cpu.r[0]);
        // 04FF6  AR5 = 100 → DBU AR5 repete 101 vezes
        cpu.ar[5] = 0x0064;
        do {
            // 04FF7  RC = 1; R2 = x; R3 = y; AR2 = ponteiro; CALL $A7F4 (texto)
            cpu.rc = 0x0001;
            for (const auto& l : lines) {
                cpu.r[2] = l.x;
                cpu.r[3] = l.y;
                cpu.ar[2] = bus_.readWord(l.rom_ptr);
                subs_.call(0xA7F4, cpu, bus_);
            }
        } while (static_cast<int32_t>(cpu.ar[5]--) > 0);  // 05000  DBU AR5
        subs_.call(0x5029, cpu, bus_);                     // 05001  watchdog
    } while (static_cast<int32_t>(cpu.ar[6]--) > 0);      // 05002  DBU AR6
}

uint32_t BootSequence::run(Tms32031State& cpu, bool hardware_reset) {
    exits_.clear();
    messages_.clear();
    if (hardware_reset) bus_.hardwareReset();
    ResetRoutine reset(bus_, subs_);
    return recover(cpu, reset.run(cpu));
}

uint32_t BootSequence::reenter(Tms32031State& cpu) {
    exits_.clear();
    messages_.clear();
    ResetRoutine reset(bus_, subs_);
    return recover(cpu, reset.runFrom4B28(cpu));
}

uint32_t BootSequence::recover(Tms32031State& cpu, uint32_t next) {
    ResetRoutine reset(bus_, subs_);
    exits_.push_back(next);
    for (int pass = 1; pass < kMaxPasses && next != ResetRoutine::kExitFallThrough; ++pass) {
        if (next == ResetRoutine::kExitLowerThan) {
            // 04FEF  "CMOS ERROR / FACTORY ADJUSTMENTS RESTORED"
            showScreen(cpu, kCmosErrorScreen);
            subs_.call(0x5020, cpu, bus_);   // 05003
            subs_.call(0x0CC0, cpu, bus_);   // 05004  restaura ajustes (pendente)
        } else {
            // 05006  "VERSION UPDATED / FACTORY SETTINGS RESTORED"
            showScreen(cpu, kVersionUpdatedScreen);
            subs_.call(0x5020, cpu, bus_);   // 0501A
            subs_.call(0x0DCE, cpu, bus_);   // 0501B  restaura tudo
            // 0501C  R2 = 0x28; AR2 = 0xFA; CALL $99F4 — grava a assinatura de versão
            cpu.r[2] = kVersionSignature;
            cpu.ar[2] = kVersionItem;
            subs_.call(0x99F4, cpu, bus_);
        }
        // 05005 / 0501F  BR $4B28
        next = reset.runFrom4B28(cpu);
        exits_.push_back(next);
    }
    return next;
}

} // namespace rea::core::boot
