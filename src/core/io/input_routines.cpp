/**
 * @file input_routines.cpp
 * @brief Tradução linha a linha de `0x4CEF`, `0x4D28`, `0x4D3D` e `0x4D61`.
 */
#include "core/io/input_routines.hpp"

#include "core/io/cabinet_io.hpp"

namespace rea::core::io {

using namespace input_vars;

void sampleInputs(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    // 04CEF  AR0 = 0 (alvo das leituras falsas)
    cpu.ar[0] = 0;
    // 04CF0  R0 = @($992000); R2 = *AR0; [C8CC] = R0 >> 16 (LSH lógico)
    cpu.dp = 0x99;
    cpu.r[0] = bus.readWord(kPortDsw);
    cpu.r[2] = bus.readWord(cpu.ar[0]);
    cpu.dp = 0;
    cpu.r[0] >>= 16;
    bus.writeWord(kDipSwitches, cpu.r[0]);
    // 04CF6  R0 = IN1 << 16; R1 = (IN0 << 16) >> 16; R0 |= R1
    cpu.dp = 0x99;
    cpu.r[0] = bus.readWord(kPortIn1);
    cpu.r[2] = bus.readWord(cpu.ar[0]);
    cpu.r[0] <<= 16;
    cpu.r[1] = bus.readWord(kPortIn0);
    cpu.r[2] = bus.readWord(cpu.ar[0]);
    cpu.r[1] <<= 16;
    cpu.dp = 0;
    cpu.r[1] >>= 16;
    cpu.r[0] |= cpu.r[1];
    // 04D00  R1 = ~R0; AR0 = [0x4CEE] (= 0xC8C9); [C937] = R1
    cpu.r[1] = ~cpu.r[0];
    cpu.ar[0] = bus.readWord(kHistoryPtr);
    bus.writeWord(kPressed, cpu.r[1]);
    // 04D03  R2 = [AR0+1] & [AR0]; R2 &= R1; R2 |= [AR0+2]; [AR0+2] = R2  (debounce)
    cpu.r[2] = bus.readWord(cpu.ar[0] + 1) & bus.readWord(cpu.ar[0]);
    cpu.r[2] &= cpu.r[1];
    cpu.r[2] |= bus.readWord(cpu.ar[0] + 2);
    bus.writeWord(cpu.ar[0] + 2, cpu.r[2]);
    // 04D07  [AR0+1] = [AR0]; [AR0] = R0  (desloca o histórico)
    cpu.r[1] = bus.readWord(cpu.ar[0]);
    bus.writeWord(cpu.ar[0] + 1, cpu.r[1]);
    bus.writeWord(cpu.ar[0], cpu.r[0]);
    // 04D0A  PUSH DP; 0x994000 = [C8C7] & ~0x20 (habilita comando do ADC); POP DP
    cpu.r[0] = bus.readWord(kControlShadow) & ~0x0020u;
    bus.writeWord(kPortControl, cpu.r[0]);
    // 04D10  AR0 = 0; 0x993000 = 4 << 24 (ADC0844: canal 1 = volante); leitura falsa
    cpu.ar[0] = 0;
    cpu.r[0] = 0x0004u << 24;
    bus.writeWord(kPortAdc, cpu.r[0]);
    cpu.r[2] = bus.readWord(cpu.ar[0]);
    cpu.dp = 0;
    // 04D17  [C93D] = 0 (INT3 começa no estado "volante")
    cpu.r[0] = 0;
    bus.writeWord(kAdcState, cpu.r[0]);
    // 04D19  se [C8CB] & 0x10: CALL $4D61 (borda da chave de teste)
    cpu.r[0] = bus.readWord(kNewPresses);
    if (cpu.r[0] & input_bits::kTest) subs.call(0x4D61, cpu, bus);
    // 04D1C  modos 1 e 2 ignoram o volume
    cpu.r[0] = bus.readWord(kGameMode) & 0x000Fu;
    if (cpu.r[0] == 2 || cpu.r[0] == 1) return;
    // 04D22  R0 = [C937]; volume + / − por nível
    cpu.r[0] = bus.readWord(kPressed);
    if (cpu.r[0] & input_bits::kVolumeUp) subs.call(0x4D3D, cpu, bus);
    if (cpu.r[0] & input_bits::kVolumeDown) subs.call(0x4D28, cpu, bus);
}

void handleTestSwitch(Tms32031State& cpu, MemoryBus& bus) {
    const uint32_t r0 = cpu.r[0];  // 04D61  PUSH R0
    cpu.r[0] = bus.readWord(kGameMode) & 0x000Fu;
    if (cpu.r[0] != 1) {
        // 04D66  consome a borda; [C939] = ([C939] == 0) ? 1 : 0
        bus.writeWord(kNewPresses, bus.readWord(kNewPresses) & ~input_bits::kTest);
        cpu.r[0] = bus.readWord(kTestRequest) == 0 ? 1u : 0u;
        bus.writeWord(kTestRequest, cpu.r[0]);
    }
    cpu.r[0] = r0;  // 04D6D  POP R0
}

namespace {
/**
 * @brief Corpo comum de `0x4D28`/`0x4D3D` (cauda em `0x4D51`).
 * @param delta +1 (0x4D3D) ou −1 (0x4D28).
 * @param edge_bit Bit de borda consumido em [C8CB].
 * @param cpu Registradores do TMS320C31 (entrada e saída da rotina).
 * @param bus Barramento de memória simulado (RAM, ROM e I/O).
 * @param subs Despachante das sub-rotinas traduzidas (destino dos `CALL`).
 */
void changeVolume(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs, int32_t delta, uint32_t edge_bit) {
    // PUSH R0, R1, R2, R3, AR2
    const uint32_t r0 = cpu.r[0], r1 = cpu.r[1], r2 = cpu.r[2], r3 = cpu.r[3], ar2 = cpu.ar[2];
    cpu.dp = 0;
    bus.writeWord(kNewPresses, bus.readWord(kNewPresses) & ~edge_bit);
    // [E56D] = 0x3C — mostra o volume por 60 quadros.
    cpu.r[0] = 0x003C;
    bus.writeWord(kVolumeShown, cpu.r[0]);
    // R3 = item 0x20 (mínimo); R0 = item 8 (volume) ± 1; se R0 < R3: R0 = R3
    cpu.ar[2] = kCmosMinVolume;
    subs.call(0x99E9, cpu, bus);
    cpu.r[3] = cpu.r[0];
    cpu.ar[2] = kCmosVolume;
    subs.call(0x99E9, cpu, bus);
    cpu.r[0] += static_cast<uint32_t>(delta);
    if (static_cast<int32_t>(cpu.r[0]) < static_cast<int32_t>(cpu.r[3])) cpu.r[0] = cpu.r[3];
    // 04D51  limita a 0..255
    if (static_cast<int32_t>(cpu.r[0]) <= 0) cpu.r[0] = 0;
    if (static_cast<int32_t>(cpu.r[0]) > 0xFF) cpu.r[0] = 0xFF;
    // 04D55  R2 = R0; PUSH R0; AR2 = 8; CALL $99EE (grava + checksum); POP R1; CALL $910F (som)
    cpu.r[2] = cpu.r[0];
    const uint32_t volume = cpu.r[0];
    cpu.ar[2] = kCmosVolume;
    subs.call(0x99EE, cpu, bus);
    cpu.r[1] = volume;
    subs.call(0x910F, cpu, bus);
    // POP AR2, R3, R2, R1, R0
    cpu.ar[2] = ar2;
    cpu.r[3] = r3;
    cpu.r[2] = r2;
    cpu.r[1] = r1;
    cpu.r[0] = r0;
}
} // namespace

void volumeUp(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    changeVolume(cpu, bus, subs, +1, input_bits::kVolumeUp);
}

void volumeDown(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    changeVolume(cpu, bus, subs, -1, input_bits::kVolumeDown);
}

void registerRoutines(SubroutineDispatcher& d) {
    d.add(0x4CEF, sampleInputs);
    d.add(0x4D61, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { handleTestSwitch(c, b); });
    d.add(0x4D3D, volumeUp);
    d.add(0x4D28, volumeDown);
}

} // namespace rea::core::io
