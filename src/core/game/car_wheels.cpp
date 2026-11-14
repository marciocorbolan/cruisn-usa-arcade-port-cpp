/**
 * @file car_wheels.cpp
 * @brief Tradução de `0x8F68`–`0x8FEC` e `0xAEA8`–`0xAECA`.
 */
#include "core/game/car_wheels.hpp"

#include "core/tms_float.hpp"

namespace rea::core::game {

uint32_t addiFloatFields(uint32_t r0, uint32_t r1) {
    // No registrador de 40 bits o expoente fica em 39..32 e ADDI só altera 31..0 (sinal+mantissa).
    return (r0 & 0xFF000000u) | ((r0 + r1) & 0x00FFFFFFu);
}

namespace {

/// Constante curta da ROM em @p pc, como float.
double K(MemoryBus& b, uint32_t pc) { return shortFloat(static_cast<uint16_t>(b.readWord(pc) & 0xFFFFu)); }
/// Lê o float TMS em @p a e o converte para double.
double rf(MemoryBus& b, uint32_t a) { return tmsToDouble(b.readWord(a)); }
/// Converte @p v para float TMS e grava em @p a.
void wf(MemoryBus& b, uint32_t a, double v) { b.writeWord(a, doubleToTms(v)); }

/// `CMPF K(pc); LDFGT K(pc+1); CMPF K(pc+2); LDFLT K(pc+3)`.
double clampK(MemoryBus& b, double v, uint32_t pc) {
    if (v > K(b, pc)) v = K(b, pc + 1);
    if (v < K(b, pc + 2)) v = K(b, pc + 3);
    return v;
}

/// true se alguma roda do carro @p d está no ar.
bool airborne(MemoryBus& b, uint32_t d) { return (b.readWord(d + 0x1F) | b.readWord(d + 0x20)) != 0; }

/// 08FAE  carroceria: arfagem e rolagem. AR0 = peça; usa R6 (arfagem), R7 (velocidade anterior).
void body(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    const uint32_t d = c.ar[5], p = c.ar[7], obj = c.ar[4];
    c.ar[1] = c.ar[0] + 7;
    // 08FB0  R0 = nova velocidade − anterior (SUBF3 R7,R0,R0 = R7 − R0)
    const double oldSpeed = tmsToDouble(c.r[7]);
    c.r[7] = b.readWord(d + 0x26);
    double r6 = tmsToDouble(c.r[6]);
    r6 = (r6 + (tmsToDouble(c.r[7]) - oldSpeed) * K(b, 0x8FB3)) * K(b, 0x8FB5);
    c.r[6] = doubleToTms(r6);
    double r2 = -r6;
    if (airborne(b, d)) r2 = 0.0;
    if (r2 > 0.0) c.r[1] = doubleToTms(1.0);
    else if (r2 < 0.0) c.r[1] = doubleToTms(2.0);
    r2 *= tmsToDouble(c.r[1]);
    r2 += rf(b, d + 0x39) * K(b, 0x8FBF) * K(b, 0x8FC0);
    r2 = clampK(b, r2, 0x8FC2);
    wf(b, d + 0x42, r2);
    c.r[2] = doubleToTms(r2);
    c.ar[2] = b.readWord(0x5B);
    s.call(0x9377, c, b);
    c.ar[0] = c.ar[2];
    // 08FCA  rolagem pela variação do rumo
    const uint32_t r4 = b.readWord(p + 0xC);
    double r5 = rf(b, p + 0xD);
    const uint32_t heading = b.readWord(obj + 0x15);
    b.writeWord(p + 0xC, heading);
    double delta = tmsToDouble(heading) - tmsToDouble(r4);
    uint32_t r1 = doubleToTms(0.0);
    if (delta > K(b, 0x8FD0)) r1 = b.readWord(0x8FAD);
    if (delta < K(b, 0x8FD2)) r1 = b.readWord(0x9304);
    c.r[1] = r1;
    delta = tmsToDouble(addiFloatFields(doubleToTms(delta), r1)); // 08FD4 ADDI R1,R0
    delta *= tmsToDouble(c.r[7]) * K(b, 0x8FD6) * K(b, 0x8FD7);
    r5 = (r5 + delta) * K(b, 0x8FD9);
    wf(b, p + 0xD, r5);
    c.r[5] = doubleToTms(r5);
    c.r[4] = r4;
    r2 = -r5;
    if (airborne(b, d)) r2 = 0.0;
    r2 = clampK(b, r2, 0x8FDF);
    wf(b, d + 0x43, r2);
    r2 = clampK(b, r2 * K(b, 0x8FE4), 0x8FE5);
    c.r[2] = doubleToTms(r2);
    c.ar[2] = b.readWord(0x5A);
    s.call(0x93C4, c, b);
    s.call(0x9481, c, b);
}

/// Dorme @p frames quadros (`0xA874`) e retoma em `0x8F72`.
void sleep(Tms32031State& c, MemoryBus& b, ISubroutines& s, uint32_t frames) {
    c.ar[2] = frames;
    c.ret = 0x8F72;
    s.call(0xA874, c, b);
}

/// 08F72  laço por quadro.
void loop(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    const uint32_t d = c.ar[5], obj = c.ar[4], p = c.ar[7];
    const uint32_t c8c5 = b.readWord(0xC8C5);
    if ((c8c5 & 0xFu) != 3 && (c8c5 & 0x10u) == 0 && b.readWord(0xE5EC) == 0x33BB) {
        c.r[7] = b.readWord(d + 0x26);
        return sleep(c, b, s, 3);
    }
    if (static_cast<int32_t>(b.readWord(obj + 0x1C)) > 0x4E20) return sleep(c, b, s, 3);
    // 08F82  [5A] = rotY(esterço·1,5)
    c.r[2] = doubleToTms(rf(b, d + 0x21) * K(b, 0x8F83));
    c.ar[2] = b.readWord(0x5A);
    s.call(0x938A, c, b);
    c.ar[0] = c.ar[2];
    // 08F87  giro acumulado
    const double spin = rf(b, d + 0x26) * K(b, 0x8F88) + rf(b, p + 0xE);
    wf(b, p + 0xE, spin);
    c.r[2] = doubleToTms(spin);
    c.ar[2] = b.readWord(0x5B);
    s.call(0x9377, c, b);
    c.ar[1] = b.readWord(0x5C);
    c.ar[6] = c.ar[1];
    c.ar[3] = c.ar[2];
    s.call(0x9481, c, b);
    // 08F91  peças
    uint32_t part = b.readWord(obj + 0x1A);
    while (true) {
        c.ar[0] = part;
        const int32_t type = static_cast<int32_t>(b.readWord(part + 0x12));
        c.r[0] = static_cast<uint32_t>(type);
        if (type == 0) {
            body(c, b, s);
            return sleep(c, b, s, 1);
        }
        if (type > 0) {
            const uint32_t src = type == 1 ? c.ar[3] : c.ar[6];
            for (uint32_t i = 0; i < 9; ++i) b.writeWord(part + 7 + i, b.readWord(src + i));
            c.ar[2] = part + 7 + 9;
        }
        part = b.readWord(part);
        c.r[0] = part;
        if (part == 0) break;
    }
    sleep(c, b, s, 3);
}

/// 08F68  entrada do processo.
void entry(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    c.ar[5] = b.readWord(c.ar[4] + 0x1B);
    c.r[6] = doubleToTms(0.0);
    c.r[7] = b.readWord(c.ar[5] + 0x26);
    b.writeWord(c.ar[7] + 0xC, b.readWord(c.ar[4] + 0x15));
    c.r[5] = doubleToTms(0.0);
    b.writeWord(c.ar[7] + 0xD, c.r[5]);
    wf(b, c.ar[7] + 0xE, 0.0);
    loop(c, b, s);
}


/// 0AEAD  rodas simples (`0xAE6A`): todas as peças menos a última recebem rotX(giro). Dorme 3.
void simpleLoop(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    const uint32_t d = c.ar[5], obj = c.ar[4], p = c.ar[7];
    c.ar[2] = 3;
    if (b.readWord(0xE5EC) != 0x33BB && !(static_cast<int32_t>(b.readWord(obj + 0x1C)) > 0x4E20)) {
        const double spin = rf(b, d + 0x26) * K(b, 0xAEB5) + rf(b, p + 0xE);
        wf(b, p + 0xE, spin);
        c.r[2] = doubleToTms(spin);
        c.ar[2] = b.readWord(0x5B);
        s.call(0x9377, c, b);
        c.ar[3] = c.ar[2];
        for (uint32_t part = b.readWord(obj + 0x1A);;) {
            c.ar[0] = part;
            const uint32_t next = b.readWord(part);
            c.r[0] = next;
            if (next == 0) break;
            for (uint32_t i = 0; i < 9; ++i) b.writeWord(part + 7 + i, b.readWord(c.ar[3] + i));
            part = next;
        }
        c.ar[2] = 3;
    }
    c.ret = 0xAEAD;
    s.call(0xA874, c, b);
}

/// 0AEA8  entrada das rodas simples.
void simpleEntry(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    c.ar[5] = b.readWord(c.ar[4] + 0x1B);
    c.r[6] = doubleToTms(0.0);
    b.writeWord(c.ar[7] + 0xE, c.r[6]);
    c.r[7] = b.readWord(c.ar[5] + 0x26);
    simpleLoop(c, b, s);
}

} // namespace

void registerCarWheels(SubroutineDispatcher& d) {
    d.add(0x8F68, entry);
    d.add(0x8F72, loop);
    d.add(0x8FAE, body);
    d.add(0xAEA8, simpleEntry);
    d.add(0xAEAD, simpleLoop);
}

} // namespace rea::core::game
