/**
 * @file link_car.cpp
 * @brief Tradução de `0xA66E`–`0xA717` (carro do outro gabinete e o objeto `[C98B]` acima dele).
 */
#include "core/game/link_car.hpp"

#include "core/game/process_ctx.hpp"

namespace rea::core::game {

namespace {

/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }

constexpr uint32_t kModels = 0x961B;  ///< `[961B]`: 11 palavras por modelo.
constexpr uint32_t kLinkCar = 0xE592;  ///< Carro de tráfego que está com o outro gabinete.
constexpr uint32_t kSlotTablePtr = 0x508E;  ///< `[508E]` = 0xE8A3: objeto de cada carro.
constexpr uint32_t kFirstNode = 0xE46E;  ///< Lista de trechos (`+0x1E` índice, `+0x21` próximo).
constexpr uint32_t kMarker = 0xC98B;  ///< != 0: CALL $A6F8.
constexpr uint32_t kScaleTablePtr = 0x0052;  ///< `[0x52]`: tabela de escala por distância.

/// `0xA6EA`: laço por quadro.
void linkCarLoop(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t r5 = cpu.r[5], ar6 = cpu.ar[6], ar4 = cpu.ar[4], ar5 = cpu.ar[5];
    cpu.r[3] = c.rd(cpu.ar[4] + 0x1B);
    c.call(0x2041);
    cpu.ar[5] = ar5;
    cpu.ar[4] = ar4;
    cpu.ar[6] = ar6;
    cpu.r[5] = r5;
    c.call(0x7BE0);
    c.sleep(1, 0xA6F7);
}

/// `0xA66E`: entrada (R0 = carro do outro gabinete, R4 = número do carro).
void linkCarEntry(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[1] = cpu.r[0];
    if (cpu.ar[1] == 4) cpu.r[0] = 0x0E;
    if (cpu.ar[1] == 5) cpu.r[0] = 0x11;
    if (cpu.ar[1] == 6) cpu.r[0] = 0x0F;
    if (cpu.ar[1] == 7) cpu.r[0] = 0x03;
    c.wr(cpu.ar[7] + 0x19, cpu.r[0]);
    cpu.ar[2] = cpu.r[0];
    cpu.ar[5] = cpu.r[0];
    cpu.ar[2] = c.rd(cpu.ar[2] * 0x0B + c.rd(kModels));
    c.call(0x700A);
    if (cpu.carry()) return c.die();  // BLO $A896
    cpu.ar[4] = cpu.ar[0];
    c.wr(kLinkCar, cpu.ar[4]);
    cpu.ar[1] = cpu.r[4] + c.rd(kSlotTablePtr);
    c.wr(cpu.ar[1], cpu.ar[4]);
    cpu.ar[2] = cpu.ar[5];
    c.call(0x8F3E);
    c.call(0xAE3A);
    c.wr(cpu.ar[5] + 0x58, 1);
    c.wf(cpu.ar[5] + 0x45, c.rf(cpu.ar[5] + 0x44) * 1.5);
    // 0A68C  tabela de carros (*+AR7(27))
    cpu.ar[0] = c.var(0x27);
    const double speed = c.rf(cpu.ar[0] + 2);
    c.wf(cpu.ar[7] + 0x1B, speed);
    c.wf(cpu.ar[7] + 0x20, speed);
    cpu.r[2] = speed > 600.0 ? 3 : 2;
    c.setVar(0x0C, cpu.r[2]);
    c.wf(cpu.ar[5] + 0x23, 1.0 * c.rf(cpu.ar[0] + 3));
    c.wf(cpu.ar[7] + 0x26, c.rf(cpu.ar[0] + 4));
    // 0A69A  no início da janela
    cpu.ar[2] = c.rd(kFirstNode);
    cpu.r[0] = c.rd(cpu.ar[2] + 0x1E) & ~0xFFu;
    cpu.ar[2] = c.rd(cpu.ar[2] + 0x21);
    cpu.ar[3] = c.rd(cpu.ar[0] + 1);
    c.call(0x54FD);
    c.wr(cpu.ar[4] + 0x15, cpu.r[2]);
    c.wr(cpu.ar[5] + 0x2C, cpu.r[2]);
    c.wr(cpu.ar[5] + 0x2D, cpu.r[2]);
    cpu.ar[2] = cpu.ar[4] + 4;
    c.call(0x938A);
    cpu.r[0] = 0x216;
    c.wr(cpu.ar[4] + 0x0F, cpu.r[0]);
    c.wr(cpu.ar[5] + 0x40, cpu.r[0]);
    c.wr(cpu.ar[7] + 3, cpu.r[0]);
    c.wf(cpu.ar[5] + 0x2B, 0.0);
    c.wf(cpu.ar[5] + 0x26, 0.0);
    c.wf(cpu.ar[5] + 0x22, c.k(0xA6AD));  // 0,439941
    c.wf(cpu.ar[7] + 0x0F, 1.0);
    c.wf(cpu.ar[7] + 0x21, 1.0);
    cpu.r[0] = doubleToTms(c.k(0xA6B2));  // 0,050003
    c.call(0x8EC2);
    c.wf(cpu.ar[7] + 0x22, tmsToDouble(cpu.r[0]) + c.k(0xA6B4));  // + 1,200195
    c.setVar(0x23, 0x78);
    c.setVar(0x24, 0x78);
    c.setVar(0x25, 0);
    cpu.ar[2] = cpu.ar[4];
    c.call(0x715F);
    c.call(0x7093);
    cpu.ar[5] = c.rd(cpu.ar[4] + 0x1B);
    cpu.r[3] = c.rd(cpu.ar[4] + 0x1B);
    c.call(0x1FF6);
    cpu.r[2] = c.rd(cpu.ar[5] + 0x2C);
    cpu.ar[2] = c.rd(0x005A);
    c.call(0x938A);
    cpu.r[2] = cpu.ar[4] + 4;
    cpu.r[3] = cpu.r[2];
    c.call(0x9494);
    // 0A6C8  objeto [C98B] acima do carro
    cpu.ar[2] = c.rd(0xB125);
    if (c.var(0x0E) != 8) cpu.ar[2] = c.rd(0xB126);
    c.call(0x700A);
    cpu.ar[2] = cpu.ar[0];
    cpu.ar[6] = cpu.ar[0];
    c.wr(kMarker, cpu.ar[0]);
    c.call(0x7093);
    cpu.r[1] = 1u << 27;
    cpu.r[0] = c.rd(cpu.ar[6] + 0x0E) | 0x482u | cpu.r[1];
    c.wr(cpu.ar[6] + 0x0E, cpu.r[0]);
    cpu.ar[2] = c.rd(0xB127);
    c.call(0x9EDB);
    c.wr(cpu.ar[6] + 0x10, cpu.r[0]);
    cpu.r[0] = c.var(0x0E);
    c.wr(cpu.ar[6] + 0x1B, cpu.r[0]);
    // 0A6DD  AR2 = entrada da tabela [A61F] pela pista (não usado adiante, como no original)
    if (cpu.ar[4] == c.rd(kLinkCar)) {
        cpu.r[0] = c.rd(0xE634);
        if (!(s(cpu.r[0]) > 12)) {
            cpu.r[0] *= 3;
            cpu.ar[2] = (c.rd(0xC8CC) & 1u) ? 0u : 0x27u;
            cpu.ar[2] += cpu.r[0] + c.rd(0xA61F);
        }
    }
    cpu.r[5] = 0;
    linkCarLoop(c);
}

/// `0xA6F8`: objeto `[C98B]` escalado pela distância e acima do carro `[E592]`.
void placeMarker(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    auto rf = [&](uint32_t a) { return tmsToDouble(bus.readWord(a)); };
    auto wf = [&](uint32_t a, double v) { bus.writeWord(a, doubleToTms(v)); };
    cpu.ar[6] = bus.readWord(kMarker);
    cpu.ar[4] = bus.readWord(kLinkCar);
    cpu.ir1 = static_cast<uint32_t>(s(bus.readWord(cpu.ar[4] + 0x1C)) >> 4);
    cpu.ar[2] = bus.readWord(kScaleTablePtr);
    cpu.r[0] = bus.readWord(cpu.ar[2] + cpu.ir1);
    subs.call(0xA585, cpu, bus);  // R0 = 1 / R0
    double scale = tmsToDouble(cpu.r[0]);
    if (scale < 16.0) scale = 16.0;
    cpu.r[0] = doubleToTms(scale);
    for (uint32_t off : {4u, 8u, 0xCu}) bus.writeWord(cpu.ar[6] + off, cpu.r[0]);
    bus.writeWord(cpu.ar[6] + 1, bus.readWord(cpu.ar[4] + 1));
    const double lift = 35.0 * rf(cpu.ar[6] + 4);
    wf(cpu.ar[6] + 2, rf(cpu.ar[4] + 2) - lift - 20.0);
    bus.writeWord(cpu.ar[6] + 3, bus.readWord(cpu.ar[4] + 3));
    // 0A70E  desencadeia e põe logo depois do carro na lista
    cpu.ar[2] = cpu.ar[6];
    subs.call(0x715F, cpu, bus);
    cpu.r[0] = bus.readWord(cpu.ar[4]);
    bus.writeWord(cpu.ar[6], cpu.r[0]);
    bus.writeWord(cpu.ar[4], cpu.ar[6]);
    cpu.r[0] = (bus.readWord(cpu.ar[4] + 0x0E) & 0x3000u) | bus.readWord(cpu.ar[6] + 0x0E);
    bus.writeWord(cpu.ar[6] + 0x0E, cpu.r[0]);
}

} // namespace

void registerLinkCar(SubroutineDispatcher& d) {
    addProc(d, 0xA66E, linkCarEntry);
    addProc(d, 0xA6EA, linkCarLoop);
    addProc(d, 0xA6F7, linkCarLoop);
    d.add(0xA6F8, placeMarker);
}

} // namespace rea::core::game
