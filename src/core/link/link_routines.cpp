/**
 * @file link_routines.cpp
 * @brief Tradução do protocolo de link entre gabinetes (`0x7F5D`–`0x81D8`).
 */
#include "core/link/link_routines.hpp"

#include "core/vunit_hw.hpp"

namespace rea::core::link {

using namespace link_vars;

namespace {
/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }

/**
 * @brief Decremento de `DBcond`: ARn − 1 (24 bits); continua se não ficou negativo.
 * @return true se o resultado de 24 bits é ≥ 0 (bit 23 em 0).
 */
inline bool decrementAr(uint32_t& ar) {
    const uint32_t res = (ar - 1u) & 0x00FFFFFFu;
    ar = (ar & 0xFF000000u) | res;
    return (res & 0x00800000u) == 0;
}

/// Atraso `RPTS n / NOP`: n + 1 NOPs + a própria RPTS e a escrita anterior.
constexpr uint32_t rpts(uint32_t n) { return n + 3; }

constexpr uint32_t kSlowPollTimeout = 0x2710; ///< 10000 leituras (Slave, 0x80E4).
constexpr uint32_t kPollTimeout = 0x012C;      ///< 300 leituras por borda.
} // namespace

void LinkRoutines::exchange(Tms32031State& cpu, MemoryBus& bus) {
    cpu_ = &cpu;
    bus_ = &bus;
    try {
        // 07F5D  AR7 = 0; R0 = [0x990000]; R1 = *AR7 (lê [0], sem uso)
        cpu.ar[7] = 0;
        cpu.dp = 0x99;
        cpu.r[0] = rd(vunit::kIntcs);
        cpu.r[1] = rd(cpu.ar[7]);
        cpu.dp = 0;
        // 07F62  (R0 & 0xF) != 4: RETS (no MAME intcs_r devolve sempre 4)
        cpu.r[0] &= 0x000Fu;
        if (cpu.r[0] != 4) return;
        // 07F65  DIP Linking desligada (bit 2 em 1): RETS
        cpu.r[0] = rd(kDipShadow);
        if (cpu.r[0] & 0x0004u) return;
        // 07F68  espera a cadeia de INT3 terminar ([C93D] == 3)
        for (;;) {
            cpu.r[1] = rd(kAdcState);
            if (cpu.r[1] == 3) break;
            spin(3);
        }
        // 07F6B  [E591] = 1; DIP bit 0: Slave → 0x80C1
        cpu.r[1] = 1;
        wr(kBusy, cpu.r[1]);
        if (cpu.r[0] & 0x0001u) slave();
        else master();
    } catch (const Abort&) {
        // RESET pedido pelo relógio: a rotina é abandonada, como num reset de hardware.
    }
    cpu_ = nullptr;
    bus_ = nullptr;
}

void LinkRoutines::disableInterrupts() {
    Tms32031State& cpu = *cpu_;
    const uint32_t ie = cpu.ie;
    cpu.ie = 0;
    cpu.st &= ~Tms32031State::kStGie;  // AND $DFFF,ST
    cpu.ie = ie;
}

void LinkRoutines::increment(uint32_t addr) {
    cpu_->r[0] = rd(addr) + 1;
    wr(addr, cpu_->r[0]);
}

bool LinkRoutines::pollTimedOut(uint32_t addr, bool wait_while_set) {
    Tms32031State& cpu = *cpu_;
    for (;;) {
        // TSTB src,R5 / DBEQ (espera subir) ou DBNE (espera descer) AR3,$-1
        const bool bit = (rd(addr) & cpu.r[5]) != 0;
        spin(2);
        const bool cond = wait_while_set ? bit : !bit;
        const bool counting = decrementAr(cpu.ar[3]);
        if (!(cond && counting)) break;
    }
    // TSTB -8000h,AR3: AR3 "negativo" = estourou
    return (cpu.ar[3] & 0xFFFF8000u) != 0;
}

// ---------------------------------------------------------------------------
// Master (0x7F6F)
// ---------------------------------------------------------------------------
void LinkRoutines::master() {
    Tms32031State& cpu = *cpu_;
    // 07F6F  AR5 = porta; [C8D0] != 0: RETS
    cpu.dp = 0;
    cpu.ar[5] = rd(kPortPtr);
    cpu.r[0] = rd(kSkip);
    if (cpu.r[0] != 0) return;
    // 07F74  R6 = bytes; AR2 = buffer; fora de 1…0x23F → erro (0x8084)
    cpu.r[6] = rd(kTxLength);
    cpu.ar[2] = rd(kTxBufferPtr);
    if (s(cpu.r[6]) <= 0 || s(cpu.r[6]) >= 0x240) {
        masterFail();
        return;
    }
    // 07F7A  duas palavras 0 depois dos dados (o envio lê 1 além do fim)
    cpu.ar[0] = cpu.ar[2] + cpu.r[6];
    cpu.r[2] = 0;
    wr(cpu.ar[0], cpu.r[2]);
    wr(cpu.ar[0] + 1, cpu.r[2]);
    // 07F7E  porta = 0xC000 (dirige 0x800/0x400 em 0)
    cpu.r[0] = 0xC0000000u;
    wr(cpu.ar[5], cpu.r[0]);
    // 07F81  timeout: 120 quadros, ou [C930] + 1 (máx. 4) após um timeout
    cpu.r[3] = 0x78;
    cpu.r[0] = rd(kTimedOut);
    if (cpu.r[0] != 0) {
        cpu.r[3] = 1 + rd(kFrameCounter);
        if (s(cpu.r[3]) > 4) cpu.r[3] = 4;
    }
    // 07F88  espera o Slave: 0x200 = desiste, 0x100 = pronto
    for (;;) {
        cpu.r[0] = rd(cpu.ar[5]) >> 16;
        if (cpu.r[0] & 0x0200u) {
            cpu.r[0] = 0;
            wr(kConnected, cpu.r[0]);
            wr(kTimedOut, cpu.r[0]);
            return;
        }
        if (cpu.r[0] & 0x0100u) break;
        if (s(cpu.r[3]) > s(rd(kFrameCounter))) {
            spin(9);
            continue;
        }
        wr(kTimedOut, 1);  // 08085
        masterFail();
        return;
    }
    // 07F95  conectado: [C8D2] = 0; [C8D1] = 1; strobe 0x400 alto; R3++
    cpu.r[0] = 0;
    wr(kTimedOut, cpu.r[0]);
    cpu.r[0] = 1;
    wr(kConnected, cpu.r[0]);
    cpu.r[0] = 0xC4000000u;
    wr(cpu.ar[5], cpu.r[0]);
    cpu.r[3] += 1;
    // 07F9D  espera 0x100 baixar (timeout em quadros)
    for (;;) {
        if (s(cpu.r[3]) <= s(rd(kFrameCounter))) {
            wr(kTimedOut, 1);  // 08085
            masterFail();
            return;
        }
        cpu.r[0] = rd(cpu.ar[5]) >> 16;
        if (!(cpu.r[0] & 0x0100u)) break;
        spin(7);
    }
    // 07FA3  strobe baixo; desliga o GIE; espera 0x200 subir (sem timeout)
    cpu.r[0] = 0xC0000000u;
    wr(cpu.ar[5], cpu.r[0]);
    disableInterrupts();
    for (;;) {
        cpu.r[0] = rd(cpu.ar[5]) >> 16;
        if (cpu.r[0] & 0x0200u) break;
        spin(4);
    }
    // 07FB0  liga COMCOE | COMDOE e envia
    cpu.r[0] = 0xC0000000u;
    wr(cpu.ar[5], cpu.r[0]);
    cpu.r[0] = 0x60000000u;
    wr(cpu.ar[5] + 1, cpu.r[0]);
    sendPacket(0xC400, 0xC000);
    // 07FFC  solta os dados (só COMCOE); espera 0x200 baixar (≤ 31 leituras)
    cpu.r[0] = 0x20000000u;
    wr(cpu.ar[5] + 1, cpu.r[0]);
    cpu.r[5] = 0x1E;
    for (;;) {
        cpu.r[5] -= 1;
        if (s(cpu.r[5]) < 0) {  // 08083
            masterFail();
            return;
        }
        cpu.r[0] = rd(cpu.ar[5]) >> 16;
        if (!(cpu.r[0] & 0x0200u)) break;
        spin(6);
    }
    // 08006  AR2 = buffer de recepção; strobe 0x400 alto = "sua vez"; R5 = strobe do Slave
    cpu.ar[2] = rd(kRxBufferPtr);
    cpu.r[0] = 0xC4000000u;
    wr(cpu.ar[5], cpu.r[0]);
    cpu.dp = 0x99;
    cpu.r[5] = 0x0200u << 16;
    if (!receivePacket(true)) {
        masterFail();
        return;
    }
    // 0806B  sucesso
    cpu.dp = 0x80;
    increment(kMasterOk);
    masterFinish();
}

void LinkRoutines::masterFinish() {
    Tms32031State& cpu = *cpu_;
    // 0806F  porta = 0xC000; só COMCOE; [E591] = 0; CALL $4C33 (RETIU → GIE)
    cpu.r[0] = 0xC0000000u;
    wr(cpu.ar[5], cpu.r[0]);
    cpu.r[0] = 0x20000000u;
    wr(cpu.ar[5] + 1, cpu.r[0]);
    cpu.dp = 0;
    cpu.r[0] = 0;
    wr(kBusy, cpu.r[0]);
    cpu.st |= Tms32031State::kStGie;
}

void LinkRoutines::masterFail() {
    // 08088  [0x809A16]++; [C8D4] = 0; BU $806F
    cpu_->dp = 0x80;
    increment(kMasterErrors);
    cpu_->dp = 0;
    cpu_->r[0] = 0;
    wr(kRxPairs, cpu_->r[0]);
    masterFinish();
}

// ---------------------------------------------------------------------------
// Envio / recepção (comuns aos dois lados)
// ---------------------------------------------------------------------------
void LinkRoutines::sendPacket(uint32_t hi, uint32_t lo) {
    Tms32031State& cpu = *cpu_;
    auto put = [&](uint32_t& reg, uint32_t strobe, uint32_t delay) {
        reg |= strobe;
        reg <<= 16;
        wr(cpu.ar[5], reg);
        spin(delay);
    };
    // 07FB6  n = (bytes + 1) / 2 pares; cabeçalho n_alto, n_baixo, ~n_alto, ~n_baixo
    cpu.r[6] = (cpu.r[6] + 1) >> 1;
    cpu.ar[6] = cpu.r[6];
    cpu.r[7] = ~cpu.r[6];
    cpu.r[5] = cpu.r[7];
    cpu.r[7] >>= 8;
    cpu.r[4] = cpu.r[6];
    cpu.r[6] >>= 8;
    cpu.r[7] &= 0xFF;
    cpu.r[6] &= 0xFF;
    cpu.r[5] &= 0xFF;
    cpu.r[4] &= 0xFF;
    put(cpu.r[6], hi, rpts(0x12));
    put(cpu.r[4], lo, rpts(0x12));
    put(cpu.r[7], hi, rpts(0x12));
    put(cpu.r[5], lo, rpts(0x41));
    // 07FD6  dados: AR6 = n − 1; R3 = checksum
    cpu.ar[6] -= 1;
    cpu.r[4] = 0xFF;
    cpu.r[3] = 0;
    cpu.r[6] = rd(cpu.ar[2]++) & cpu.r[4];
    cpu.r[3] += cpu.r[6];
    cpu.r[6] |= hi;
    for (;;) {
        // 07FDC  1º byte do par (strobe alto, já com OR)
        cpu.r[6] <<= 16;
        wr(cpu.ar[5], cpu.r[6]);
        spin(rpts(0x12));
        // 07FE0  2º byte (strobe baixo)
        cpu.r[6] = rd(cpu.ar[2]++) & cpu.r[4];
        cpu.r[3] += cpu.r[6];
        put(cpu.r[6], lo, rpts(0x12));
        // 07FE7  DBUD AR6; atrasadas: lê o próximo byte (strobe alto)
        const bool again = decrementAr(cpu.ar[6]);
        cpu.r[6] = rd(cpu.ar[2]++) & cpu.r[4];
        cpu.r[3] += cpu.r[6];
        cpu.r[6] |= hi;
        if (!again) break;
    }
    // 07FEB  desfaz o byte lido a mais; checksum de 16 bits, alto primeiro
    cpu.r[6] &= ~hi;
    cpu.r[3] -= cpu.r[6];
    cpu.r[3] &= 0xFFFF;
    cpu.r[6] = cpu.r[3] >> 8;
    put(cpu.r[6], hi, rpts(0x12));
    cpu.r[3] = (cpu.r[3] << 24) >> 24;
    put(cpu.r[3], lo, rpts(0x12));
}

bool LinkRoutines::receivePacket(bool master_side) {
    Tms32031State& cpu = *cpu_;
    const uint32_t port = vunit::kLinkPort;  // @($7000) com DP = 0x99
    // 0800D  cabeçalho: 4 bytes, um por borda do strobe do outro lado
    cpu.ar[3] = kPollTimeout;
    if (pollTimedOut(port, false)) return false;          // espera subir
    cpu.ar[3] = kPollTimeout;
    cpu.r[7] = rd(port);
    if (pollTimedOut(port, true)) return false;           // espera descer
    cpu.r[6] = rd(port) << 8;
    cpu.ar[3] = kPollTimeout;
    if (pollTimedOut(port, false)) return false;
    cpu.ar[3] = kPollTimeout;
    cpu.r[4] = ~rd(port);
    if (pollTimedOut(port, true)) return false;
    cpu.r[2] = ~rd(port) << 8;
    // 0802B  isola os bytes (bits 16–23)
    cpu.r[4] <<= 8;
    cpu.r[7] <<= 8;
    cpu.r[2] >>= 24;
    cpu.r[4] >>= 24;
    cpu.r[6] >>= 24;
    cpu.r[7] >>= 24;
    if (cpu.r[6] != cpu.r[2]) return false;   // n_baixo == ~(~n_baixo)
    if (cpu.r[7] != cpu.r[4]) return false;   // n_alto == ~(~n_alto)
    cpu.r[7] <<= 8;
    cpu.ar[6] = cpu.r[7] + cpu.r[6];
    if (master_side) {
        // 08037  0 < n < 0x140; AR6 = n − 1
        if (s(cpu.ar[6]) >= 0x140 || cpu.ar[6] == 0) return false;
        cpu.ar[6] -= 1;
    } else {
        // 08121  AR6 = n − 1; < 0x140 (n = 0 passa: AR6 = −1 recebe um par)
        cpu.ar[6] -= 1;
        if (s(cpu.ar[6]) >= 0x140) return false;
    }
    cpu.dp = 0;
    wr(kRxPairs, cpu.ar[6]);
    cpu.dp = 0x99;
    // 0803F  dados: cada byte guardado como (byte << 16); R3 = soma
    cpu.r[3] = 0;
    cpu.r[2] = 0x00FFu << 16;
    cpu.ar[3] = kPollTimeout;
    for (;;) {
        if (pollTimedOut(cpu.ar[5], false)) return false;
        cpu.ar[3] = kPollTimeout;
        cpu.r[6] = rd(cpu.ar[5]) & cpu.r[2];
        cpu.r[3] += cpu.r[6];
        wr(cpu.ar[2]++, cpu.r[6]);
        if (pollTimedOut(cpu.ar[5], true)) return false;
        cpu.r[6] = rd(cpu.ar[5]) & cpu.r[2];
        const bool again = decrementAr(cpu.ar[6]);   // DBUD AR6
        cpu.r[3] += cpu.r[6];
        wr(cpu.ar[2]++, cpu.r[6]);
        cpu.ar[3] = kPollTimeout;
        if (!again) break;
    }
    // 08057  checksum (alto, baixo)
    if (pollTimedOut(port, false)) return false;
    cpu.ar[3] = kPollTimeout;
    cpu.r[6] = rd(port);
    if (pollTimedOut(port, true)) return false;
    cpu.r[6] >>= 8;
    cpu.r[6] &= 0xFF00;
    cpu.r[7] = (rd(port) >> 16) & 0xFF;
    cpu.r[7] += cpu.r[6];
    cpu.r[3] >>= 16;
    return cpu.r[3] == cpu.r[7];
}

// ---------------------------------------------------------------------------
// Slave (0x80C1)
// ---------------------------------------------------------------------------
void LinkRoutines::slave() {
    Tms32031State& cpu = *cpu_;
    // 080C1  AR5 = porta; AR2 = buffer de recepção; R5 = strobe do Master (0x400)
    cpu.dp = 0;
    cpu.ar[5] = rd(kPortPtr);
    cpu.ar[2] = rd(kRxBufferPtr);
    cpu.r[5] = 0x0400u << 16;
    cpu.r[0] = rd(kSkip);
    if (cpu.r[0] != 0) return;
    // 080C9  strobe do Master já alto: desconectado; RETS
    if (rd(cpu.ar[5]) & cpu.r[5]) {
        cpu.r[0] = 0;
        wr(kTimedOut, cpu.r[0]);
        wr(kConnected, cpu.r[0]);
        return;
    }
    // 080CF  timeout em quadros (como no Master)
    cpu.r[3] = 0x78;
    cpu.r[0] = rd(kTimedOut);
    if (cpu.r[0] != 0) {
        cpu.r[3] = 1 + rd(kFrameCounter);
        if (s(cpu.r[3]) > 4) cpu.r[3] = 4;
    }
    // 080D6  [C8D1] = 1; porta = 0x3100 (0x100 = pronto); espera o strobe 0x400
    cpu.r[0] = 1;
    wr(kConnected, cpu.r[0]);
    cpu.r[0] = 0x31000000u;
    wr(cpu.ar[5], cpu.r[0]);
    for (;;) {
        if (s(cpu.r[3]) <= s(rd(kFrameCounter))) {
            wr(kTimedOut, 1);  // 081CE
            slaveFail();
            return;
        }
        if (rd(cpu.ar[5]) & cpu.r[5]) break;
        spin(6);
    }
    // 080DF  0x100 baixa; [C8D2] = 0; espera o strobe descer (10000 leituras)
    cpu.r[0] = 0x30000000u;
    wr(cpu.ar[5], cpu.r[0]);
    cpu.r[0] = 0;
    wr(kTimedOut, cpu.r[0]);
    cpu.ar[3] = kSlowPollTimeout;
    if (pollTimedOut(cpu.ar[5], true)) return;  // 080E9  RETS sem contador nem limpeza
    // 080EA  desliga o GIE; só COMCOE; porta = 0x3200 (0x200 alto: "pode enviar")
    disableInterrupts();
    cpu.r[0] = 0x20000000u;
    wr(cpu.ar[5] + 1, cpu.r[0]);
    cpu.r[0] = 0x32000000u;
    wr(cpu.ar[5], cpu.r[0]);
    cpu.dp = 0x99;
    if (!receivePacket(false)) {
        slaveFail();
        return;
    }
    // 08153  0x200 baixa; espera o Master subir 0x400 (≤ 50 leituras)
    cpu.dp = 0;
    cpu.r[0] = 0x30000000u;
    wr(cpu.ar[5], cpu.r[0]);
    cpu.r[5] = 0x32;
    for (;;) {
        cpu.r[5] -= 1;
        if (s(cpu.r[5]) < 0) {  // 081CC
            slaveFail();
            return;
        }
        cpu.r[0] = rd(cpu.ar[5]) >> 16;
        if (cpu.r[0] & 0x0400u) break;
        spin(6);
    }
    // 0815E  COMCOE | COMDOE; prepara o buffer de envio e envia
    cpu.r[0] = 0x60000000u;
    wr(cpu.ar[5] + 1, cpu.r[0]);
    cpu.dp = 0;
    cpu.r[6] = rd(kTxLength);
    cpu.ar[2] = rd(kTxBufferPtr);
    cpu.ar[0] = cpu.ar[2] + cpu.r[6];
    cpu.r[2] = 0;
    wr(cpu.ar[0], cpu.r[2]);
    wr(cpu.ar[0] + 1, cpu.r[2]);
    sendPacket(0x3200, 0x3000);
    // 081AE  espera o Master baixar 0x400 (≤ 50 leituras)
    cpu.r[5] = 0x32;
    for (;;) {
        cpu.r[5] -= 1;
        if (s(cpu.r[5]) < 0) {  // 081CD
            slaveFail();
            return;
        }
        cpu.r[0] = rd(cpu.ar[5]) >> 16;
        if (!(cpu.r[0] & 0x0400u)) break;
        spin(6);
    }
    // 081B5  sucesso
    cpu.dp = 0x80;
    increment(kSlaveOk);
    slaveFinish();
}

void LinkRoutines::slaveFinish() {
    Tms32031State& cpu = *cpu_;
    // 081B9  porta = 0x3000; só COMCOE; [E591] = 0; CALL $4C33 (RETIU → GIE)
    cpu.dp = 0;
    cpu.r[0] = 0x30000000u;
    wr(cpu.ar[5], cpu.r[0]);
    cpu.r[0] = 0x20000000u;
    wr(cpu.ar[5] + 1, cpu.r[0]);
    cpu.r[0] = 0;
    wr(kBusy, cpu.r[0]);
    cpu.st |= Tms32031State::kStGie;
}

void LinkRoutines::slaveFail() {
    // 081D1  [0x809A18]++; [C8D4] = 0; BU $81B9
    cpu_->dp = 0x80;
    increment(kSlaveErrors);
    cpu_->dp = 0;
    cpu_->r[0] = 0;
    wr(kRxPairs, cpu_->r[0]);
    slaveFinish();
}

void linkIdle(Tms32031State& cpu, MemoryBus& bus) {
    // 07F25  [E591] != 0 ou Slave → nada
    cpu.r[0] = bus.readWord(kBusy);
    if (cpu.r[0] != 0) return;
    cpu.r[0] = bus.readWord(kDipShadow);
    if (cpu.r[0] & 1u) return;
    // 07F2A  desliga o GIE (o original não volta a ligá-lo aqui) e o INT2
    cpu.st &= ~Tms32031State::kStGie;
    cpu.ie &= ~Tms32031State::kIeInt2;
    // 07F31  R1 = bit 0x400 da metade alta da porta; pulso 0xC800|R1 e depois 0xC000|R1 (16 bits altos)
    cpu.ar[5] = bus.readWord(kPortPtr);
    cpu.r[0] = (bus.readWord(cpu.ar[5]) >> 16) & 0x400u;
    cpu.r[1] = cpu.r[0];
    cpu.r[0] = (cpu.r[0] | 0xC800u) << 16;
    bus.writeWord(cpu.ar[5], cpu.r[0]);
    cpu.r[0] = (0xFFFFC000u | cpu.r[1]) << 16;
    bus.writeWord(cpu.ar[5], cpu.r[0]);
    cpu.if_ &= ~Tms32031State::kIeInt2;
}

void LinkRoutines::registerIn(SubroutineDispatcher& d) {
    d.add(0x7F5D, [this](Tms32031State& c, MemoryBus& b, ISubroutines&) { exchange(c, b); });
    d.add(0x7F25, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { linkIdle(c, b); });
}

} // namespace rea::core::link
