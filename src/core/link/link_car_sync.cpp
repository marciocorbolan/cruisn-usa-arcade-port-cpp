/**
 * @file link_car_sync.cpp
 * @brief Tradução de `0x7ACF`–`0x7B24`, `0x7B34`–`0x7BAF` (estado de carro pelo link) e `0x2587`–`0x25D5`
 *        (batida de carro pelo link, tipos 0x13 e 0x14)
 *        e `0x5255`–`0x52A7` (carro de tráfego passado ao outro gabinete, tipo 0xD).
 */
#include "core/link/link_car_sync.hpp"

#include "core/link/link_packet.hpp"
#include "core/tms_float.hpp"

namespace rea::core::link {

namespace {

constexpr uint32_t kScratchPtr = 0x7939;  ///< `[0x7939]` = 0x809A1B (rascunho da mensagem)
constexpr uint32_t kCarList = 0xE404;  ///< Lista de carros (`0x6762`).
constexpr uint32_t kLinkCar = 0xE592;  ///< Carro de tráfego que está com o outro gabinete (mensagem tipo 7).
constexpr uint32_t kSyncOff = 0xE593;  ///< != 0: ignora as mensagens de estado de carro (tipos 0xC, 0x12 e 0x15).
constexpr uint32_t kSlotTablePtr = 0x508E;  ///< `[508E]` = 0xE8A3: objeto de cada carro.
constexpr uint32_t kMatrixScale = 0x7B32;  ///< float ≈ 1/32000
constexpr uint32_t kAngleScale = 0x7B33;   ///< float ≈ 1/1000

/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }
/// `RND`: arredonda @p v para a precisão de 32 bits do float TMS.
inline double rnd(double v) { return tmsRound(v); }

/// Contexto das rotinas de estado de carro: CPU, barramento, sub-rotinas e leitura do pacote.
struct Ctx {
    Tms32031State& cpu;  ///< Registradores da CPU TMS320C31.
    MemoryBus& bus;  ///< Barramento de memória simulado.
    ISubroutines& subs;  ///< Despachante das sub-rotinas traduzidas.
    /// Lê a palavra no endereço @p a.
    uint32_t rd(uint32_t a) const { return bus.readWord(a); }
    /// Grava a palavra @p v no endereço @p a.
    void wr(uint32_t a, uint32_t v) { bus.writeWord(a, v); }
    /// Lê o float TMS em @p a e o converte para double.
    double rf(uint32_t a) const { return tmsToDouble(bus.readWord(a)); }
    /// Converte @p v para float TMS e grava em @p a.
    void wf(uint32_t a, double v) { bus.writeWord(a, doubleToTms(v)); }
    /// `STI R,*AR2++(1)`
    void put(uint32_t v) { bus.writeWord(cpu.ar[2]++, v); }
    /// `LSH3 *AR2++(1),R2,R` com R2 = −16
    uint32_t hi16() { return bus.readWord(cpu.ar[2]++) >> 16; }
    /// `LSH3 *AR2++(1),R3,R` com R3 = −8
    uint32_t hi8() { return bus.readWord(cpu.ar[2]++) >> 8; }
    /// Par (byte baixo, byte alto) → 16 bits com sinal (`ADDI / LSH 16 / ASH −16`).
    double s16() {
        uint32_t v = hi16();
        v += hi8();
        return static_cast<double>(static_cast<int32_t>(v << 16) >> 16);
    }
};

/// `0x7ADC`/`0x7AE0`…`0x7B24`: monta a mensagem @p type com a identificação @p id e anexa (`0x7998`).
void sendCar(Ctx& c, uint32_t type, bool idFromCarData) {
    Tms32031State& cpu = c.cpu;
    cpu.r[0] = type;
    cpu.ar[0] = cpu.ar[4];
    cpu.ar[2] = c.rd(kScratchPtr);
    c.put(cpu.r[0]);
    cpu.r[0] = idFromCarData ? c.rd(cpu.ar[5] + 0x59) : c.rd(cpu.ar[7] + 0x0E);
    c.put(cpu.r[0]);
    // 07AE1  posição: 3 inteiros de 32 bits, LSB primeiro
    cpu.ar[0] += 1;
    for (int k = 0; k < 3; ++k) {
        cpu.r[0] = tmsFix(c.rf(cpu.ar[0]++));
        for (int b = 0; b < 4; ++b) {
            c.put(cpu.r[0]);
            if (b < 3) cpu.r[0] >>= 8;
        }
    }
    // 07AEC  matriz · 32000 em 16 bits
    cpu.ar[0] = cpu.ar[4] + 4;
    cpu.r[1] = doubleToTms(32000.0);
    for (int k = 0; k < 9; ++k) {
        cpu.r[0] = tmsFix(c.rf(cpu.ar[0]++) * 32000.0);
        c.put(cpu.r[0]);
        cpu.r[0] >>= 8;
        c.put(cpu.r[0]);
    }
    // 07AF6  ângulos (+2C, +21, +2D) normalizados por 0x9308, · 1000
    cpu.ar[0] = cpu.ar[4];
    cpu.ar[1] = c.rd(cpu.ar[0] + 0x1B);
    for (uint32_t off : {0x2Cu, 0x21u, 0x2Du}) {
        cpu.r[2] = c.rd(cpu.ar[1] + off);
        c.subs.call(0x9308, cpu, c.bus);
        cpu.r[1] = doubleToTms(1000.0);
        cpu.r[2] = tmsFix(tmsToDouble(cpu.r[2]) * 1000.0);
        c.put(cpu.r[2]);
        cpu.r[2] >>= 8;
        c.put(cpu.r[2]);
    }
    // 07B10
    cpu.r[2] = tmsFix(c.rf(cpu.ar[1] + 0x26));
    c.put(cpu.r[2]);
    cpu.r[2] >>= 8;
    c.put(cpu.r[2]);
    cpu.r[2] = c.rd(cpu.ar[1] + 0x1F);
    c.put(cpu.r[2]);
    cpu.r[2] = c.rd(cpu.ar[1] + 0x20);
    c.put(cpu.r[2]);
    cpu.r[2] = tmsFix(c.rf(cpu.ar[1] + 0x2B) * 100.0);
    c.put(cpu.r[2]);
    cpu.r[2] = c.rd(cpu.ar[1] + 0x57);
    for (int b = 0; b < 3; ++b) {
        c.put(cpu.r[2]);
        if (b < 2) cpu.r[2] >>= 8;
    }
    // 07B22
    cpu.ar[2] = c.rd(kScratchPtr);
    cpu.rc = 0x2D;
    appendBlock(cpu, c.bus);
}

/// `0x7B34`: R1 = 1º byte; procura em `[E404]` (encadeado por `+20`) o carro com `[[+1B]+59]` = R1.
/// @return true (EQ) com AR0 = carro; false (NE, R0 = 1) se não achou.
bool findCar(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[2] = 0xFFFFFFF0u;
    cpu.r[3] = 0xFFFFFFF8u;
    cpu.r[1] = c.hi16();
    cpu.r[0] = c.rd(kCarList);
    while (cpu.r[0] != 0) {
        cpu.ar[0] = cpu.r[0];
        cpu.ar[1] = c.rd(cpu.ar[0] + 0x1B);
        if (c.rd(cpu.ar[1] + 0x59) == cpu.r[1]) return true;
        cpu.r[0] = c.rd(cpu.ar[0] + 0x20);
    }
    cpu.ar[0] = 0;
    cpu.r[0] = 1;
    return false;
}

/// `0x7B60`: aplica o payload (a partir de AR2) ao carro AR0.
void receiveCar(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[4] = cpu.ar[0];
    cpu.ar[0] += 1;
    for (int k = 0; k < 3; ++k) {
        uint32_t v = c.hi16();
        v |= c.hi16() << 8;
        v |= c.hi16() << 16;
        v |= c.hi16() << 24;
        cpu.r[0] = v;
        c.wf(cpu.ar[0]++, static_cast<double>(s(v)));
    }
    // 07B70  matriz
    cpu.ar[0] = cpu.ar[4] + 4;
    const double matrix = c.rf(kMatrixScale);
    for (int k = 0; k < 9; ++k) c.wf(cpu.ar[0]++, rnd(c.s16()) * matrix);
    // 07B7D  dados do carro
    cpu.ar[0] = cpu.ar[4];
    cpu.ar[1] = c.rd(cpu.ar[0] + 0x1B);
    c.wr(cpu.ar[1] + 0x5A, 1);
    const double angle = c.rf(kAngleScale);
    for (uint32_t off : {0x2Cu, 0x21u, 0x2Du}) c.wf(cpu.ar[1] + off, c.s16() * angle);
    c.wf(cpu.ar[1] + 0x26, c.s16());
    c.wr(cpu.ar[1] + 0x1F, c.hi16());
    c.wr(cpu.ar[1] + 0x20, c.hi16());
    cpu.r[1] = c.hi16();
    c.wf(cpu.ar[1] + 0x2B, static_cast<double>(s(cpu.r[1])) * shortFloat(0x923D));
    // 07BA8  24 bits: byte 0 + byte 1 · 256 + a 3ª palavra inteira
    cpu.r[0] = c.hi16();
    cpu.r[0] += c.hi8();
    cpu.r[0] += c.rd(cpu.ar[2]++);
    c.wr(cpu.ar[1] + 0x57, cpu.r[0]);
}

/// `0x7BAE`: pula o payload.
void skip(Ctx& c) { c.cpu.ar[2] += 0x2D; }

/// `0x2597`: tipo 0x14 — carro `*+AR5(59)`, R4 e as 6 palavras `*+AR7(C..11)` do processo (4 bytes cada).
void sendCrashState(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[2] = c.rd(kScratchPtr);
    c.put(0x14);
    c.put(c.rd(cpu.ar[5] + 0x59));
    c.put(cpu.r[4]);
    cpu.ar[0] = cpu.ar[7] + 0x0C;
    for (int k = 0; k < 6; ++k) {
        cpu.r[0] = c.rd(cpu.ar[0]++);
        for (int b = 0; b < 4; ++b) {
            c.put(cpu.r[0]);
            if (b < 3) cpu.r[0] >>= 8;
        }
    }
    cpu.rc = 0x1A;
    cpu.ar[2] = c.rd(kScratchPtr);
    appendBlock(cpu, c.bus);
}

/// `0x25AD`: tipo 0x14 recebido — copia as 6 palavras para o processo do carro e escolhe onde ele retoma.
void receiveCrashState(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    if (!findCar(c)) {
        cpu.ar[2] += 0x19;  // 025D1
        return;
    }
    cpu.r[4] = c.hi16();
    cpu.ar[1] = c.rd(cpu.ar[0] + 0x17) + 0x0C;
    cpu.r[5] = 8;
    for (int k = 0; k < 6; ++k) {
        cpu.r[0] = c.hi16();
        cpu.r[0] += c.hi8();
        cpu.r[0] += c.rd(cpu.ar[2]++);
        cpu.r[0] += c.rd(cpu.ar[2]++) << 8;
        c.wr(cpu.ar[1]++, cpu.r[0]);
    }
    // 025BC  processo do carro: retoma em 0x255E fora da janela [E46E]..[E470] (pelo +1E), senão conforme R4
    cpu.ar[5] = c.rd(cpu.ar[0] + 0x1B);
    cpu.ar[7] = c.rd(cpu.ar[0] + 0x17);
    c.wr(cpu.ar[5] + 0x58, 0);
    cpu.r[2] = c.rd(cpu.ar[5] + 0x57);
    cpu.r[5] = c.rd(0x2596);
    cpu.ar[0] = c.rd(0xE470);
    cpu.r[0] = c.rd(cpu.ar[0] + 0x1E);
    if (!(s(cpu.r[2]) > s(cpu.r[0]))) {
        cpu.ar[0] = c.rd(0xE46E);
        cpu.r[0] = c.rd(cpu.ar[0] + 0x1E);
        if (!(s(cpu.r[2]) < s(cpu.r[0]))) {
            cpu.r[5] = c.rd(0x25D3);
            if (cpu.r[4] == 0) cpu.r[5] = c.rd(0x25D4);
            if (cpu.r[4] == 1) cpu.r[5] = c.rd(0x25D5);
        }
    }
    c.wr(cpu.ar[7] + 2, cpu.r[5]);
}

/// `0x5278`: tipo 0xD recebido.
void receiveHandOff(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[0] = c.rd(cpu.ar[2]++);
    cpu.r[1] = c.rd(kSyncOff);
    if (cpu.r[1] != 0) return;
    cpu.r[0] >>= 16;
    if (s(cpu.r[0]) < 0 || s(cpu.r[0]) > 7) return;
    cpu.r[0] += c.rd(kSlotTablePtr);
    cpu.ar[4] = cpu.r[0];
    cpu.r[0] = c.rd(cpu.ar[4]);
    if (cpu.r[0] == 0) return;
    cpu.ar[4] = cpu.r[0];
    cpu.ar[7] = c.rd(cpu.ar[4] + 0x17);
    cpu.r[0] = doubleToTms(1.0);
    c.wr(cpu.ar[7] + 0x0F, cpu.r[0]);
    c.wr(cpu.ar[7] + 0x21, cpu.r[0]);
    c.wr(cpu.ar[7] + 0x22, cpu.r[0]);
    c.wr(cpu.ar[7] + 0x23, 0x4B);
    c.wr(cpu.ar[7] + 0x24, 0x1B);
    cpu.ar[5] = c.rd(cpu.ar[4] + 0x1B);
    c.wr(cpu.ar[7] + 0x25, 1);
    c.wr(cpu.ar[5] + 0x58, 0);
    cpu.r[1] = c.rd(cpu.ar[5] + 0x57);
    c.wr(cpu.ar[7] + 0x13, cpu.r[1]);
    cpu.ar[0] = c.rd(0xE470);
    cpu.r[0] = c.rd(cpu.ar[0] + 0x1E);
    if (!(s(cpu.r[0]) > s(cpu.r[1]))) {
        cpu.r[2] = c.rd(0x52A5);  // 0x5208
    } else {
        // 0529A
        cpu.ar[0] = c.rd(0xE46E);
        for (int k = 0; k < 3; ++k) cpu.ar[0] = c.rd(cpu.ar[0] + 0x21);
        cpu.r[0] = c.rd(cpu.ar[0] + 0x1E);
        cpu.r[2] = s(cpu.r[0]) < s(cpu.r[1]) ? c.rd(0x52A7) : c.rd(0x52A6);  // 0x52A8 / 0x5225
    }
    c.wr(cpu.ar[7] + 2, cpu.r[2]);
}

} // namespace

bool trafficHandOff(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs, bool behind) {
    Ctx c{cpu, bus, subs};
    // 05255 / 0525A
    subs.call(0x5268, cpu, bus);
    const int32_t laps = s(cpu.r[1]) - s(cpu.r[0]);
    if (behind ? laps <= 0 : laps >= 0) return false;
    // 0525F  dentro da faixa [E8AD]..[E8AE]?
    cpu.r[2] = c.rd(cpu.ar[7] + 0x13);
    if (s(cpu.r[2]) < s(c.rd(0xE8AD)) || s(cpu.r[2]) > s(c.rd(0xE8AE))) {
        cpu.r[0] = 1;
        return false;
    }
    cpu.r[0] = 0;
    // 0526F  tipo 0xD com *+AR7(E)
    cpu.r[0] = 0x0D;
    cpu.ar[2] = c.rd(kScratchPtr);
    c.wr(cpu.ar[2], cpu.r[0]);
    cpu.r[0] = c.rd(cpu.ar[7] + 0x0E);
    c.wr(cpu.ar[2] + 1, cpu.r[0]);
    cpu.rc = 1;
    appendBlock(cpu, bus);
    return true;  // 05276  POP R0 / BU $A718
}

void registerCarSync(SubroutineDispatcher& d) {
    using R = SubroutineDispatcher::Routine;
    auto with = [](void (*f)(Ctx&)) -> R {
        return [f](Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
            Ctx c{cpu, bus, subs};
            f(c);
        };
    };
    // Emissores
    d.add(0x7ACF, with([](Ctx& c) {
        c.wr(c.cpu.ar[5] + 0x57, c.rd(c.cpu.ar[7] + 0x13));
        sendCar(c, 0x12, true);
    }));
    d.add(0x7AD7, with([](Ctx& c) {
        c.wr(c.cpu.ar[5] + 0x57, c.rd(c.cpu.ar[7] + 0x13));
        sendCar(c, 0x0C, false);
    }));
    d.add(0x7ADB, with([](Ctx& c) { sendCar(c, 0x07, false); }));
    d.add(0x2587, with([](Ctx& c) {  // tipo 0x13: fim da batida do carro *+AR5(59)
        c.cpu.ar[2] = c.rd(kScratchPtr);
        c.wr(c.cpu.ar[2], 0x13);
        c.cpu.r[0] = c.rd(c.cpu.ar[5] + 0x59);
        c.wr(c.cpu.ar[2] + 1, c.cpu.r[0]);
        c.cpu.rc = 1;
        appendBlock(c.cpu, c.bus);
    }));
    d.add(0x2597, with(sendCrashState));
    d.add(0x258F, with([](Ctx& c) {  // tipo 0x13 recebido: o processo do carro retoma em [2595] = 0x2560
        if (!findCar(c)) return;
        c.cpu.ar[7] = c.rd(c.cpu.ar[0] + 0x17);
        c.cpu.r[2] = c.rd(0x2595);
        c.wr(c.cpu.ar[7] + 2, c.cpu.r[2]);
    }));
    d.add(0x25AD, with(receiveCrashState));
    d.add(0x5278, with(receiveHandOff));
    // Tratadores
    d.add(0x7B34, with([](Ctx& c) {
        if (findCar(c)) c.cpu.st |= Tms32031State::kStZero;
        else c.cpu.st &= ~Tms32031State::kStZero;
    }));
    d.add(0x7B43, with([](Ctx& c) {  // tipos 0x12 e 0x15
        if (c.rd(kSyncOff) != 0) return skip(c);
        if (!findCar(c)) {
            c.cpu.ar[2] -= 1;
            return skip(c);
        }
        receiveCar(c);
    }));
    d.add(0x7B49, with([](Ctx& c) {  // tipo 0xC: carro da tabela [0x508E]
        if (c.rd(kSyncOff) != 0) return skip(c);
        c.cpu.r[2] = 0xFFFFFFF0u;
        c.cpu.r[3] = 0xFFFFFFF8u;
        c.cpu.r[0] = c.rd(c.cpu.ar[2]) >> 16;
        if (s(c.cpu.r[0]) < 0 || s(c.cpu.r[0]) > 9) return skip(c);
        c.cpu.r[0] += c.rd(kSlotTablePtr);
        c.cpu.ar[0] = c.cpu.r[0];
        c.cpu.r[0] = c.rd(c.cpu.ar[0]);
        if (c.cpu.r[0] == 0) return skip(c);
        c.cpu.ar[2] += 1;
        c.cpu.ar[0] = c.cpu.r[0];
        receiveCar(c);
    }));
    d.add(0x7B58, with([](Ctx& c) {  // tipo 7: carro [E592]
        if (c.rd(kSyncOff) != 0) return skip(c);
        c.cpu.r[0] = c.rd(kLinkCar);
        if (c.cpu.r[0] == 0) return skip(c);
        c.cpu.ar[0] = c.cpu.r[0];
        c.cpu.r[2] = 0xFFFFFFF0u;
        c.cpu.r[3] = 0xFFFFFFF8u;
        c.cpu.r[1] = c.hi16();
        receiveCar(c);
    }));
}

} // namespace rea::core::link
