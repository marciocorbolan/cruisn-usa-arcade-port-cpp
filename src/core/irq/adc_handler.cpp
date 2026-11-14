/**
 * @file adc_handler.cpp
 * @brief Tradução linha a linha da INT3 (ROM `0x4D6F`–`0x4DE5`).
 */
#include "core/irq/adc_handler.hpp"

#include <cmath>

#include "core/io/cabinet_io.hpp"
#include "core/vunit_hw.hpp"

namespace rea::core::irq {

using namespace adc_vars;

namespace {
constexpr uint32_t kSavedIeMask = 0x8099DF;  ///< Máscara IE extra (RAM interna).
constexpr uint32_t kControlShadow = 0xC8C7;  ///< Cópia do último valor escrito em `0x994000`: bits 0x40/0x20 selecionam a leitura do ADC; 0x80 = pipeline em execução.

/// Constantes imediatas exatas (float curto TMS de 16 bits).
constexpr double kNewWeight = 2703.0 / 8192.0;  ///< 0xE28F ≈ 0.329956
constexpr double kOldWeight = 2744.0 / 4096.0;  ///< 0xF2B8 ≈ 0.669922
constexpr double kWheelSlew = 65.0;             ///< 0x6020 / 0x6FE0 = ±65.0

/// `FIX`: float → inteiro com arredondamento para baixo (piso).
uint32_t fix(double v) { return static_cast<uint32_t>(static_cast<int32_t>(std::floor(v))); }
/// `FLOAT` de um inteiro com sinal de 32 bits.
double toFloat(uint32_t v) { return static_cast<double>(static_cast<int32_t>(v)); }
} // namespace

void AdcHandler::run(Tms32031State& cpu) {
    (void)subs_;
    // Entrada pelo hardware: GIE desligado.
    cpu.st &= ~Tms32031State::kStGie;
    // 04D6F  PUSH ST, IE, DP; IE = [0x8099DF] (só a INT2 do link); CALL $4C33 (GIE)
    // 04D76  PUSH R0, R0f, R6, R1, R1f, AR0; R6 = bus control; bus control = 0x1090
    const Tms32031State saved = cpu;
    cpu.ie = bus_.readWord(kSavedIeMask);
    cpu.st |= Tms32031State::kStGie;
    cpu.r[6] = bus_.readWord(vunit::kBusControl);
    bus_.writeWord(vunit::kBusControl, 0x1090);

    // 04D80  R0 = [C93D]; BNED $4DAA — slots atrasados:
    //        DP = 0; R1 = [C8C7] & ~0x40 (valor que habilita a LEITURA do ADC)
    cpu.dp = 0;
    cpu.r[0] = bus_.readWord(kState);
    cpu.r[1] = bus_.readWord(kControlShadow) & ~0x0040u;

    if (cpu.r[0] == 0) {
        // ---- Estado 0: volante -------------------------------------------
        // 04D86  0x994000 = R1; R1 = ADC >> 24; leitura falsa
        bus_.writeWord(io::kPortControl, cpu.r[1]);
        cpu.ar[0] = 0;
        cpu.r[1] = bus_.readWord(io::kPortAdc);
        cpu.r[0] = bus_.readWord(cpu.ar[0]);
        cpu.dp = 0;
        cpu.r[1] >>= 24;
        // 04D8E  Δ = novo − anterior, limitado a ±65; filtro IIR
        double r1 = toFloat(cpu.r[1]);
        double r0 = toFloat(bus_.readWord(kWheel));
        r1 -= r0;
        if (r1 > kWheelSlew) r1 = kWheelSlew;
        if (r1 < -kWheelSlew) r1 = -kWheelSlew;
        r1 += r0;
        r1 *= kNewWeight;
        r0 *= kOldWeight;
        r0 += r1;
        cpu.r[0] = fix(r0);
        bus_.writeWord(kWheel, cpu.r[0]);
        // 04D9B  0x994000 = [C8C7] & ~0x20 (habilita COMANDO); ADC ← 5 (canal 2)
        cpu.r[0] = bus_.readWord(kControlShadow) & ~0x0020u;
        bus_.writeWord(io::kPortControl, cpu.r[0]);
        cpu.ar[0] = 0;
        cpu.r[0] = 0x0005u << 24;
        bus_.writeWord(io::kPortAdc, cpu.r[0]);
        cpu.r[0] = bus_.readWord(cpu.ar[0]);
        // 04DA6  BUD $4DD9 — slots: DP = 0; R0 = 1; [C93D] = 1
        cpu.dp = 0;
        cpu.r[0] = 1;
        bus_.writeWord(kState, cpu.r[0]);
    } else {
        // 04DAA  CMPI 1,R0; BNED $4DCA — slots: DP = 0x99; 0x994000 = R1; DP = 0x99
        const bool state1 = (cpu.r[0] == 1);
        bus_.writeWord(io::kPortControl, cpu.r[1]);
        if (state1) {
            // ---- Estado 1: acelerador --------------------------------------
            cpu.ar[0] = 0;
            cpu.r[1] = bus_.readWord(io::kPortAdc);
            cpu.r[0] = bus_.readWord(cpu.ar[0]);
            cpu.dp = 0;
            cpu.r[1] >>= 24;
            const double r0 = toFloat(bus_.readWord(kGas)) * kOldWeight + toFloat(cpu.r[1]) * kNewWeight;
            cpu.r[0] = fix(r0);
            bus_.writeWord(kGas, cpu.r[0]);
            // 04DBB  habilita comando; ADC ← 6 (canal 3 = freio)
            cpu.r[0] = bus_.readWord(kControlShadow) & ~0x0020u;
            bus_.writeWord(io::kPortControl, cpu.r[0]);
            cpu.r[0] = 0x0006u << 24;
            cpu.ar[0] = 0;
            bus_.writeWord(io::kPortAdc, cpu.r[0]);
            cpu.r[0] = bus_.readWord(cpu.ar[0]);
            // 04DC6  BUD $4DD9 — slots: DP = 0; R0 = 2; [C93D] = 2
            cpu.dp = 0;
            cpu.r[0] = 2;
            bus_.writeWord(kState, cpu.r[0]);
        } else {
            // ---- Estado 2 (ou 3): freio — não inicia nova conversão --------
            cpu.ar[0] = 0;
            cpu.r[1] = bus_.readWord(io::kPortAdc);
            cpu.r[0] = bus_.readWord(cpu.ar[0]);
            cpu.dp = 0;
            cpu.r[1] >>= 24;
            const double r0 = toFloat(bus_.readWord(kBrake)) * 0.75 + toFloat(cpu.r[1]) * 0.25;
            cpu.r[0] = fix(r0);
            bus_.writeWord(kBrake, cpu.r[0]);
            // 04DD6  [C93D] = 3
            cpu.r[0] = 3;
            bus_.writeWord(kState, cpu.r[0]);
        }
    }

    // 04DD9  restaura bus control (R6) e registradores; ANDN 8,IF; POP IE, ST; RETIU
    bus_.writeWord(vunit::kBusControl, cpu.r[6]);
    const uint32_t if_now = cpu.if_ & ~Tms32031State::kIeInt3;
    cpu = saved;
    cpu.if_ = if_now;
    cpu.st |= Tms32031State::kStGie;
}

} // namespace rea::core::irq
