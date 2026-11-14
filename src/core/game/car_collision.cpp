/**
 * @file car_collision.cpp
 * @brief Tradução de `0x2633`–`0x28E6` e `0x5375`–`0x538B`.
 */
#include "core/game/car_collision.hpp"

#include <cmath>

#include "core/tms_float.hpp"

namespace rea::core::game {

namespace {

constexpr uint32_t kBoxBuffer = 0x004F; ///< `[0x4F]` = 0x809D00: cantos das duas caixas e planos.
constexpr uint32_t kPosTemp   = 0x004C; ///< `[0x4C]`: posição do objeto (−1..+1).
constexpr uint32_t kTriTable  = 0x28E7; ///< `[28E7]` = 0x28E8: 12 triângulos (3 ponteiros).
constexpr uint32_t kFaces     = 6;  ///< Faces de uma caixa de colisão.
constexpr uint32_t kCorners   = 8;  ///< Cantos de uma caixa de colisão.

/// Lê o float TMS em @p a e o converte para double.
double rf(MemoryBus& b, uint32_t a) { return tmsToDouble(b.readWord(a)); }
/// Converte @p v para float TMS e grava em @p a.
void wf(MemoryBus& b, uint32_t a, double v) { b.writeWord(a, doubleToTms(v)); }

constexpr uint32_t kFocusCar  = 0xE876;  ///< objeto do carro focado (jogador/câmera)
constexpr uint32_t kDirTemp   = 0x005D; ///< `[0x5D]`: vetor de rascunho (direção / braço do impacto).
constexpr uint32_t kElastTemp = 0x005A; ///< `[0x5A]`: coeficientes do choque elástico.
constexpr uint32_t kHalfPi    = 0x9302; ///< `[9302]` = π/2.
constexpr uint32_t kSpinKick  = 0xE58F; ///< impulso de giro do último impacto
constexpr uint32_t kImpact    = 0xE58D; ///< velocidade relativa do último impacto
constexpr uint32_t kAheadFlag = 0xE590; ///< 1 = o carro de AR0 está atrás (`0x5375`)
constexpr uint32_t kBumpGain  = 0xE58E; ///< ganho do empurrão no carro focado (`0x25E8`)

/// Contexto com R0–R7 em ponto flutuante e acesso aos registradores de endereço.
struct Ctx {
    Tms32031State& c;  ///< Registradores da CPU TMS320C31.
    MemoryBus& b;  ///< Barramento de memória simulado.
    ISubroutines& s;  ///< Despachante das sub-rotinas traduzidas.
    /// Lê o registrador R@p i como float TMS.
    double R(int i) const { return tmsToDouble(c.r[i]); }
    /// Grava @p v, convertido para float TMS, no registrador R@p i.
    void setR(int i, double v) const { c.r[i] = doubleToTms(v); }
    /// Float curto de 16 bits (imediato da instrução) na palavra @p pc da ROM.
    double K(uint32_t pc) const { return shortFloat(static_cast<uint16_t>(b.readWord(pc) & 0xFFFFu)); }
    /// Lê o float TMS em @p a e o converte para double.
    double f(uint32_t a) const { return rf(b, a); }
    /// Converte @p v para float TMS e grava em @p a.
    void wf(uint32_t a, double v) const { b.writeWord(a, doubleToTms(v)); }
    /// Lê a palavra no endereço @p a.
    uint32_t rd(uint32_t a) const { return b.readWord(a); }
    /// Chama a sub-rotina traduzida no endereço da ROM indicado (o `CALL` original).
    void call(uint32_t a) const { s.call(a, c, b); }
    /// @return O bit C (carry) de ST.
    bool carry() const { return c.carry(); }
    /// true se @p ar é o objeto do carro focado.
    bool isFocus(uint32_t ar) const { return ar == rd(kFocusCar); }
    /// `CALL 0x8EE9` com AR2 = @p n: carry = aleatório(1000) < n.
    bool chance(uint32_t n) const {
        c.ar[2] = n;
        call(0x8EE9);
        return carry();
    }
};

/// `LDI n,Rx` num registrador que guarda um float: os bits 31..0 do registrador estendido recebem o
/// inteiro e o expoente (39..32) fica. No formato de memória: expoente antigo + (n >> 8).
uint32_t ldiIntoFloat(uint32_t old, uint32_t n) { return (old & 0xFF000000u) | ((n >> 8) & 0x00FFFFFFu); }

/// 028AE  cantos da caixa do objeto AR0, com os limites `[D+3A..3F]` escalados por R3/R0 (x),
/// R4/R1 (y) e R5/R2 (z), em AR2 (local) e AR2+24 (mundo). AR2 += 48.
void scaledBoxCorners(Tms32031State& c, MemoryBus& b, ISubroutines&) {
    const uint32_t obj = c.ar[0], out = c.ar[2];
    const uint32_t d = b.readWord(obj + 0x1B);
    auto R = [&](int i) { return tmsToDouble(c.r[i]); };
    const double xs[2] = {R(0) * rf(b, d + 0x3B), R(3) * rf(b, d + 0x3A)};
    const double ys[2] = {R(1) * rf(b, d + 0x3D), R(4) * rf(b, d + 0x3C)};
    const double zs[2] = {R(2) * rf(b, d + 0x3F), R(5) * rf(b, d + 0x3E)};
    // 028BB  STF‖STF: canto k = (xs[k&1], ys[(k>>1)&1], zs[k>>2])
    for (uint32_t k = 0; k < kCorners; ++k) {
        wf(b, out + 3 * k, xs[k & 1]);
        wf(b, out + 3 * k + 1, ys[(k >> 1) & 1]);
        wf(b, out + 3 * k + 2, zs[k >> 2]);
    }
    // 028CB  posição em [0x4C]−1..+1
    const uint32_t pos = b.readWord(kPosTemp);
    b.writeWord(pos - 1, b.readWord(obj + 1));
    b.writeWord(pos, b.readWord(obj + 2));
    b.writeWord(pos + 1, b.readWord(obj + 3));
    // 028D3  mundo = M·v + posição
    const uint32_t m = obj + 4;
    for (uint32_t k = 0; k < kCorners; ++k) {
        const double vx = rf(b, out + 3 * k), vy = rf(b, out + 3 * k + 1), vz = rf(b, out + 3 * k + 2);
        for (uint32_t row = 0; row < 3; ++row) {
            const double v = vx * rf(b, m + 3 * row) + vy * rf(b, m + 3 * row + 1) +
                             vz * rf(b, m + 3 * row + 2) + rf(b, pos - 1 + row);
            wf(b, out + 24 + 3 * k + row, v);
        }
    }
    c.ar[2] = out + 48;
}

/// 028A8  `0x28AE` com todas as escalas = 1.
void boxCorners(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    for (int i = 0; i <= 5; ++i) c.r[i] = doubleToTms(1.0);
    scaledBoxCorners(c, b, s);
}

/// Índice do 1º dos 8 cantos em @p verts do lado de dentro das 6 faces em @p planes, ou −1.
int cornerInside(MemoryBus& b, uint32_t verts, uint32_t planes) {
    for (uint32_t k = 0; k < kCorners; ++k) {
        const double x = rf(b, verts + 3 * k), y = rf(b, verts + 3 * k + 1), z = rf(b, verts + 3 * k + 2);
        bool inside = true;
        for (uint32_t f = 0; f < kFaces && inside; ++f) {
            const uint32_t p = planes + 4 * f;
            const double dot = rf(b, p) * x + rf(b, p + 1) * y + rf(b, p + 2) * z;
            if (dot < rf(b, p + 3)) inside = false; // 02886 BLT: fora desta face
        }
        if (inside) return static_cast<int>(k);
    }
    return -1;
}

/// 02863  carry = 1 se as caixas de AR0 e AR1 se interpenetram.
void boxesCollide(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    const uint32_t r2 = c.r[2], r3 = c.r[3], r5 = c.r[5], ir0 = c.ir0, ar0 = c.ar[0], ar1 = c.ar[1];
    const uint32_t base = b.readWord(kBoxBuffer);
    c.ar[2] = base;
    boxCorners(c, b, s);
    c.ar[0] = ar1;
    boxCorners(c, b, s);
    const uint32_t planes = c.ar[2];
    // 0286E  12 planos: normal (0x9466) e d = n·a
    uint32_t tri = b.readWord(kTriTable);
    for (uint32_t i = 0; i < 2 * kFaces; ++i, tri += 3) {
        const uint32_t p = planes + 4 * i;
        c.ar[0] = p;
        c.ar[2] = tri;
        s.call(0x9466, c, b);
        const uint32_t a = b.readWord(tri);
        wf(b, p + 3, rf(b, a) * rf(b, p) + rf(b, a + 1) * rf(b, p + 1) + rf(b, a + 2) * rf(b, p + 2));
    }
    // 0287A  cantos de B × faces de A; 0288B  cantos de A × faces de B.
    // AR3 fica no y do canto em contato (a 0x2727 lê x/z em AR3 ∓ 1); AR4 = contador do DBU.
    bool hit = false;
    const uint32_t sets[2][2] = {{base + 72, planes}, {base + 24, planes + 4 * kFaces}};
    for (const auto& set : sets) {
        const int k = cornerInside(b, set[0], set[1]);
        if (k >= 0) {
            c.ar[3] = set[0] + 1 + 3 * static_cast<uint32_t>(k);
            c.ar[4] = static_cast<uint32_t>(7 - k);
            hit = true;
            break;
        }
        c.ar[3] = set[0] + 1 + 3 * kCorners;
        c.ar[4] = 0xFFFFFFFFu;
    }
    c.ar[0] = ar0;
    c.ar[1] = ar1;
    c.r[2] = r2; c.r[3] = r3; c.r[5] = r5; c.ir0 = ir0;
    if (hit) c.st |= Tms32031State::kStCarry;
    else c.st &= ~Tms32031State::kStCarry;
}


/// 02633  [0x5D] = direção normalizada (A − B) no plano xz; R0 = |velocidade relativa| (xz).
void separation(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    c.ar[2] = x.rd(kDirTemp);
    x.wf(c.ar[2], x.f(c.ar[0] + 1) - x.f(c.ar[1] + 1));
    x.wf(c.ar[2] + 1, 0.0);
    x.wf(c.ar[2] + 2, x.f(c.ar[0] + 3) - x.f(c.ar[1] + 3));
    x.call(0x943B);
    const double dvx = x.f(c.ar[0] + 0x11) - x.f(c.ar[1] + 0x11);
    const double dvz = x.f(c.ar[0] + 0x13) - x.f(c.ar[1] + 0x13);
    x.setR(2, dvx * dvx + dvz * dvz);
    x.call(0xA5F1);
}

/// 02835  som da batida: volume 127..255 por R0 / `[C92F]`; tabela `[2858]` (capotado, 4) ou `[285D]`
///        (5 sons abaixo de 0xDC, senão 3). Focado: `0x3160`; outro carro: `0x3168`. Preserva R0, AR0–AR2.
void crashSound(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    const uint32_t r0 = c.r[0], ar0 = c.ar[0], ar1 = c.ar[1], ar2 = c.ar[2];
    x.setR(1, static_cast<double>(static_cast<int32_t>(x.rd(0xC92F))));
    x.call(0xA509);
    double v = x.R(0) * x.K(0x283C) * x.K(0x283D);
    if (v > x.K(0x283E)) v = x.K(0x283F);
    const int32_t vol = static_cast<int32_t>(std::floor(v * x.K(0x2840) + x.K(0x2841)));
    c.r[1] = static_cast<uint32_t>(vol);
    if (x.f(c.ar[1] + 8) < 0.0) {
        c.ar[2] = x.rd(0x2858);
        c.r[0] = 4;
    } else {
        c.ar[2] = x.rd(0x285D);
        c.r[0] = vol < 0xDC ? 5 : 3;
    }
    if (x.isFocus(c.ar[0])) {
        x.call(0x3160);
    } else {
        const uint32_t ar4 = c.ar[4];
        c.ar[4] = c.ar[0];
        x.call(0x3168);
        c.ar[4] = ar4;
    }
    c.ar[2] = ar2; c.ar[1] = ar1; c.ar[0] = ar0; c.r[0] = r0;
}

/// 05375  quem está à frente: AR2/AR3 = objeto/dados de um carro, AR4/AR5 = do outro.
///        Devolve o sinal da comparação que o original deixa nas flags (< 0 ⇒ `LT`).
int compareAhead(Tms32031State& c, MemoryBus& b) {
    const int32_t mine = static_cast<int32_t>(b.readWord(b.readWord(c.ar[3] + 0x37) + 0x1E));
    c.r[0] = static_cast<uint32_t>(mine);
    c.ar[0] = b.readWord(c.ar[5] + 0x37);
    const int32_t other = static_cast<int32_t>(b.readWord(c.ar[0] + 0x1E));
    if (mine != other) return mine < other ? -1 : 1;
    for (int i = 0; i < 2; ++i) {
        const uint32_t next = b.readWord(c.ar[0] + 0x21);
        if (next != 0) c.ar[0] = next;
    }
    const double ox = rf(b, c.ar[0] + 1) - rf(b, c.ar[4] + 1), oz = rf(b, c.ar[0] + 3) - rf(b, c.ar[4] + 3);
    const double mx = rf(b, c.ar[0] + 1) - rf(b, c.ar[2] + 1), mz = rf(b, c.ar[0] + 3) - rf(b, c.ar[2] + 3);
    const double d4 = ox * ox + oz * oz, d2 = mx * mx + mz * mz;
    c.r[0] = doubleToTms(d4);
    c.r[1] = doubleToTms(d2);
    c.r[2] = doubleToTms(mx * mx);
    return d4 < d2 ? -1 : (d4 > d2 ? 1 : 0);
}

/// 0278F  `[E590]` = 1 se `0x5375` (AR0/AR4 × AR1/AR5) dá "menor". Preserva R0, R3, AR0, AR2–AR4.
void aheadFlag(Tms32031State& c, MemoryBus& b, ISubroutines&) {
    const uint32_t r0 = c.r[0], r3 = c.r[3], ar0 = c.ar[0], ar2 = c.ar[2], ar3 = c.ar[3], ar4 = c.ar[4];
    c.ar[2] = c.ar[0];
    c.ar[3] = c.ar[4];
    c.ar[4] = c.ar[1];
    b.writeWord(kAheadFlag, compareAhead(c, b) < 0 ? 1u : 0u);
    c.ar[0] = ar0; c.ar[4] = ar4; c.ar[3] = ar3; c.ar[2] = ar2; c.r[3] = r3; c.r[0] = r0;
}

/// 0281D  com rodopio ativo (`[AR5+0x31]` ≠ 0): R2 = (R2 + `[AR5+0x2E]`·0,5)·0,67. R3 é sobrescrito
///        (`LDI` do contador: com 0, R3 vira 2^expoente anterior, como no registrador estendido).
void blendSpin(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    const uint32_t n = x.rd(c.ar[5] + 0x31);
    c.r[3] = ldiIntoFloat(c.r[3], n);
    if (n == 0) return;
    x.setR(3, x.f(c.ar[5] + 0x2E) * x.K(0x2820));
    x.setR(2, (x.R(2) + x.R(3)) * x.K(0x2822));
}

/// 02824  carry = 1 se o rumo-alvo `[AR5+0x2D]` desvia ≥ 1,75 da pista e o rumo `[AR5+0x2C]` ≤ 1,4.
///        Herda de `0x3037` AR0 = nó seguinte e AR2 = nó (não preservados no original).
void offTrackTurn(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    x.call(0x3037);
    const double track = x.R(0);
    c.r[1] = x.rd(c.ar[5] + 0x2D);
    x.setR(2, track - x.R(1));
    x.call(0x9308);
    x.setR(3, std::fabs(x.R(2)));
    bool on = false;
    if (!(x.R(3) < x.K(0x2829))) {
        x.setR(2, x.f(c.ar[5] + 0x2C) - track);
        x.call(0x9308);
        x.setR(3, std::fabs(x.R(2)));
        on = !(x.R(3) > x.K(0x282F));
    }
    if (on) c.st |= Tms32031State::kStCarry;
    else c.st &= ~Tms32031State::kStCarry;
}

/// 02727  giro do carro AR1 (dados AR5) pelo impacto no ponto [AR3−1], [AR3+1]; R0/R1 = velocidade
///        relativa (outro − este); AR0 = o outro carro.
void spinFromImpact(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    b.writeWord(kAheadFlag, 0);
    const double r0in = x.R(0), r1in = x.R(1);
    x.setR(2, r0in * r0in + r1in * r1in);
    x.call(0xA5F1);
    const uint32_t speedBits = c.r[0];
    b.writeWord(c.ar[5] + 0x33, x.R(0) > x.K(0x2730) ? 1u : 0u);
    // 02737  braço r = contato − centro; ponto r + 0,5·v
    c.ar[2] = x.rd(kDirTemp);
    const uint32_t t = c.ar[2];
    x.wf(t, x.f(c.ar[3] - 1) - x.f(c.ar[1] + 1));
    x.wf(t + 1, x.f(c.ar[3] + 1) - x.f(c.ar[1] + 3));
    x.wf(t + 2, r0in * x.K(0x273E) + x.f(t));
    x.wf(t + 3, r1in * x.K(0x2741) + x.f(t + 1));
    const double cross = x.f(t + 3) * x.f(t) - x.f(t + 2) * x.f(t + 1);
    x.setR(2, x.f(t) * x.f(t) + x.f(t + 1) * x.f(t + 1));
    x.call(0xA5F1);
    const double arm = x.R(0);
    x.call(0xA585);
    x.wf(t, x.R(0) * x.f(t));
    x.wf(t + 1, x.R(0) * x.f(t + 1));
    x.setR(2, x.f(t + 2) * x.f(t + 2) + x.f(t + 3) * x.f(t + 3));
    x.call(0xA5F1);
    x.call(0xA585);
    double r1 = x.R(0) * x.f(t + 3) * x.f(t + 1) + x.R(0) * x.f(t + 2) * x.f(t);
    r1 = (1.0 - std::fabs(r1)) * arm * x.K(0x2763);
    double r0 = (cross < 0.0 ? x.K(0x2765) : x.K(0x2766)) * r1;
    double mass = x.f(c.ar[5] + 0x30);
    if (mass >= x.K(0x2769)) mass = x.K(0x276A);
    x.setR(0, r0);
    x.setR(1, mass);
    x.call(0xA509);
    b.writeWord(kSpinKick, c.r[0]);
    c.r[3] = speedBits;
    b.writeWord(kImpact, c.r[3]);

    enum class Next { Spin, Hard, Push, Soft };
    Next next = Next::Push;
    if (x.rd(c.ar[5] + 0x31) == 1) {
        next = Next::Soft;
    } else if (x.isFocus(c.ar[1])) {
        // 02774  carro focado: rodopio pela batida e pela direção
        aheadFlag(c, b, s);
        const double sp = x.R(3);
        auto viaFlag = [&]() { return x.rd(kAheadFlag) != 0 ? Next::Push : Next::Soft; };
        if (!(sp > x.K(0x2775))) {
            offTrackTurn(c, b, s);
            next = x.carry() ? Next::Spin : Next::Push;
        } else if (!(sp > x.K(0x277A))) {
            bool toChance = x.chance(0x1F4);
            if (!toChance) {
                offTrackTurn(c, b, s);
                if (x.carry()) next = Next::Spin;
                else toChance = true;
            }
            if (toChance) next = x.chance(x.rd(0xE871) != 0 ? 0x96 : 0x64) ? viaFlag() : Next::Push;
        } else {
            if (x.chance(x.rd(0xE871) != 0 ? 0x1F4 : 0xFA)) {
                next = viaFlag();
            } else {
                offTrackTurn(c, b, s);
                next = x.carry() ? Next::Spin : Next::Push;
            }
        }
    } else {
        // 027B6  outro carro: rodopio sorteado pela força
        const double sp = x.R(3);
        next = Next::Hard;
        if (std::fabs(x.f(kSpinKick)) < x.K(0x27B7) || sp < x.K(0x27B9)) next = Next::Push;
        else if (!(sp > x.K(0x27BB))) {
            const int32_t n = (static_cast<int32_t>(std::floor(sp)) - 0x1E) * 6 + 0xA0;
            if (!x.chance(static_cast<uint32_t>(n))) next = Next::Push;
        }
    }

    uint32_t count = 0;
    double angle = 0.0;
    const double kick = x.f(kSpinKick);
    switch (next) {
    case Next::Spin: { // 027A6
        double v = x.f(c.ar[5] + 0x26);
        if (v < x.K(0x27A7)) v = x.K(0x27A8);
        x.wf(c.ar[5] + 0x26, v);
        x.setR(4, v * x.K(0x27AA)); // R4 fica sobrescrito para quem chamou
        int32_t n = static_cast<int32_t>(std::floor(x.R(4)));
        if (n > 0x3C) n = 0x3C;
        b.writeWord(c.ar[5] + 0x31, static_cast<uint32_t>(n));
        x.setR(1, static_cast<double>(n));
        c.r[0] = c.r[2];
        x.call(0xA509);
        x.wf(c.ar[5] + 0x2E, x.R(0) * x.K(0x27B2));
        x.wf(c.ar[5] + 0x32, 0.0);
        break;
    }
    case Next::Hard: { // 027C3
        double r2 = std::fabs(kick);
        if (r2 > x.K(0x27C4)) r2 = x.K(0x27C5);
        r2 *= x.K(0x27C6) * x.K(0x27C7);
        x.setR(0, x.K(0x27C8));
        x.call(0x8EC2);
        r2 += x.R(0) + x.K(0x27CA);
        if (kick < 0.0) r2 = -r2;
        x.setR(0, x.K(0x27CF));
        x.call(0x8EC2);
        angle = x.K(0x27D1) + x.R(0);
        if (!(x.f(c.ar[5] + 0x30) < x.K(0x27D4))) {
            r2 *= x.K(0x27D6);
            angle = x.K(0x27D7);
        }
        x.setR(2, r2);
        blendSpin(c, b, s);
        b.writeWord(c.ar[5] + 0x2E, c.r[2]);
        count = 1;
        b.writeWord(c.ar[5] + 0x31, count);
        x.wf(c.ar[5] + 0x32, angle);
        break;
    }
    case Next::Soft: { // 027DC
        double r2 = std::fabs(kick);
        if (r2 > x.K(0x27DD)) r2 = x.K(0x27DE);
        r2 = r2 * x.K(0x27DF) * x.K(0x27E0) + x.K(0x27E1);
        if (kick < 0.0) r2 = -r2;
        x.setR(2, r2);
        blendSpin(c, b, s);
        r2 = x.R(2);
        const double lim = x.isFocus(c.ar[1]) ? x.K(0x27E7) : x.K(0x27E8);
        if (!(std::fabs(r2) > lim)) r2 = (r2 < 0.0 ? -1.0 : 1.0) * lim;
        x.wf(c.ar[5] + 0x2E, r2);
        angle = x.rd(c.ar[5] + 0x31) == 1 ? x.f(c.ar[5] + 0x32) : x.K(0x27F4);
        b.writeWord(c.ar[5] + 0x31, 1);
        x.wf(c.ar[5] + 0x32, angle);
        break;
    }
    case Next::Push: { // 027F7  empurrão sem rodopio
        double r2 = kick;
        if (r2 > x.K(0x27F8)) r2 = x.K(0x27F9);
        if (r2 < x.K(0x27FA)) r2 = x.K(0x27FB);
        x.setR(2, r2);
        blendSpin(c, b, s);
        double r3 = x.R(3);
        if (r3 > x.K(0x27FD)) r3 = x.K(0x27FE);
        r2 = x.R(2) * (r3 * x.K(0x27FF));
        double r0v = x.f(kImpact) * x.K(0x2802);
        if (r0v > x.K(0x2803)) r0v = x.K(0x2804);
        x.setR(0, r0v);
        x.call(0x8EC2);
        r0v = x.R(0);
        if (x.isFocus(c.ar[1])) r0v *= x.K(0x2808);
        if (r0v < x.K(0x2809)) r0v = x.K(0x280A);
        int32_t n = static_cast<int32_t>(std::floor(r0v));
        if (x.isFocus(c.ar[0])) n += 5;
        if (x.rd(kAheadFlag) != 0) {
            if (n > 0xE) n = 0xE;
            n = static_cast<int32_t>(static_cast<uint32_t>(n) >> 1);
        }
        x.wf(c.ar[5] + 0x2E, r2);
        b.writeWord(c.ar[5] + 0x31, static_cast<uint32_t>(n));
        x.wf(c.ar[5] + 0x32, 0.0);
        break;
    }
    }
    // 02818  capotado: inverte o giro
    if (x.f(c.ar[1] + 8) < 0.0) x.wf(c.ar[5] + 0x2E, -x.f(c.ar[5] + 0x2E));
}

/// 02645  resposta à colisão entre AR0 (A) e AR1 (B); AR3 = canto em contato (da `0x2863`).
void collisionResponse(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    c.ar[4] = x.rd(c.ar[0] + 0x1B);
    c.ar[5] = x.rd(c.ar[1] + 0x1B);
    const uint32_t A = c.ar[0], B = c.ar[1];
    if (x.rd(B + 0xF) == 0x270) {
        const double dx = x.f(A + 1) - x.f(B + 1), dy = x.f(A + 2) - x.f(B + 2), dz = x.f(A + 3) - x.f(B + 3);
        x.setR(2, dx * dx + dy * dy + dz * dz);
        x.call(0xA5F1);
        if (x.R(0) > x.f(B + 0x1D) * x.K(0x2656)) return;
    }
    const uint32_t typeA = x.rd(A + 0xF) & 0xFF0u;
    if (typeA == 0x260 && x.rd(B + 0xF) == typeA) return;
    if (std::fabs(x.f(A + 2) - x.f(B + 2)) > 750.0) return;
    x.call(0x8928);
    separation(c, b, s);
    crashSound(c, b, s);
    // 02668  separa A ao longo da direção
    const uint32_t dir = c.ar[2];
    const double half = x.R(0) * x.K(0x2668);
    const double px = half * x.f(dir), pz = half * x.f(dir + 2);
    x.setR(0, px);
    x.setR(1, pz);
    x.wf(A + 1, x.f(A + 1) + px);
    x.wf(A + 3, x.f(A + 3) + pz);
    if ((x.rd(B + 0xF) & 0xFF0u) == 0x260) return x.call(0x9BEF);
    x.wf(B + 1, x.f(B + 1) - px);
    x.wf(B + 3, x.f(B + 3) - pz);
    // 0267C  velocidades (R4, R5) de A e (R6, R7) de B pelo rumo + π/2
    const double halfPi = x.f(kHalfPi);
    auto vel = [&](uint32_t d, double& vx, double& vz) {
        x.setR(2, x.f(d + 0x2D) + halfPi);
        const double sp = x.f(d + 0x26);
        x.call(0x92E2);
        vz = x.R(0) * sp;
        x.call(0x92DF);
        vx = x.R(0) * sp;
    };
    c.ar[4] = x.rd(A + 0x1B);
    double r4, r5, r6, r7;
    vel(c.ar[4], r4, r5);
    c.ar[5] = x.rd(B + 0x1B);
    vel(c.ar[5], r6, r7);
    if (x.isFocus(c.ar[0])) {
        const double dvx = r6 - r4, dvz = r7 - r5;
        const double rel2 = dvx * dvx + dvz * dvz;
        if (!(rel2 < 20000.0 * x.K(0x2694))) {
            x.setR(0, rel2);
            x.setR(1, 70.0);
            x.call(0xA509);
            const uint32_t n = static_cast<uint32_t>(static_cast<int32_t>(std::floor(x.R(0))));
            if (x.chance(n) && x.f(c.ar[4] + 0x26) > 160.0) {
                c.ar[2] = dir;
                return x.call(0x23EF);
            }
            c.ar[2] = dir;
        }
    }
    // 026A2  choque elástico com massas [D+0x30] (a do focado × [E87E]³)
    const uint32_t ar3 = c.ar[3];
    c.ar[3] = x.rd(kElastTemp);
    const uint32_t e = c.ar[3];
    const uint32_t dA = c.ar[4], dB = c.ar[5];
    double mA = x.f(dA + 0x30);
    if (x.isFocus(c.ar[0])) {
        const double g = x.f(0xE87E);
        mA = mA * g * g * g;
    }
    const double mB = x.f(dB + 0x30);
    x.setR(0, mB + mA);
    x.call(0xA585);
    const double inv = x.R(0);
    x.wf(e + 4, (mA * r4 + mB * r6) * inv);
    x.wf(e + 5, (mA * r5 + mB * r7) * inv);
    x.wf(e + 3, x.f(dA + 0x30) + x.f(dB + 0x30));
    auto div = [&](double num, double den) {
        x.setR(0, num);
        x.setR(1, den);
        x.call(0xA509);
        return x.R(0);
    };
    x.wf(e, div(x.f(dA + 0x30) - x.f(dB + 0x30), x.f(e + 3)));
    x.wf(e + 1, div(x.f(dB + 0x30) * 2.0, x.f(e + 3)));
    x.wf(e + 2, div(x.f(dA + 0x30) * 2.0, x.f(e + 3)));
    auto heading = [&](double vx, double vz, uint32_t d) {
        x.setR(2, vx);
        x.setR(3, vz);
        x.call(0x931A);
        const double h = x.R(0) - halfPi;
        x.setR(2, vx * vx + vz * vz);
        x.call(0xA5F1);
        b.writeWord(d + 0x26, c.r[0]);
        return h;
    };
    // A: 1/4 do choque elástico + 3/4 do centro de massa + 10·direção
    double ax = (r4 * x.f(e) + r6 * x.f(e + 1)) * x.K(0x26D4) + x.f(e + 4) * x.K(0x26D2) + 10.0 * x.f(dir);
    double az = (r5 * x.f(e) + r7 * x.f(e + 1)) * x.K(0x26D5) + x.f(e + 5) * x.K(0x26D3) + 10.0 * x.f(dir + 2);
    const double hA = heading(ax, az, dA);
    // B: idem com −direção; o focado usa o ganho [E58E]
    x.wf(e, -x.f(e));
    double bx = (r6 * x.f(e) + r4 * x.f(e + 2)) * x.K(0x26F4) + x.f(e + 4) * x.K(0x26F2);
    double bz = (r7 * x.f(e) + r5 * x.f(e + 2)) * x.K(0x26F5) + x.f(e + 5) * x.K(0x26F3);
    if (x.isFocus(c.ar[0])) {
        bx *= x.f(kBumpGain);
        bz *= x.f(kBumpGain);
    }
    bx += -10.0 * x.f(dir);
    bz += -10.0 * x.f(dir + 2);
    const double hB = heading(bx, bz, dB);
    x.setR(0, x.K(0x270D));
    x.call(0x8ED1);
    x.setR(2, hB + x.R(0));
    x.call(0x9308);
    b.writeWord(dB + 0x2D, c.r[2]);
    x.setR(2, hA);
    x.call(0x9308);
    b.writeWord(dA + 0x2D, c.r[2]);
    c.ar[3] = ar3;
    c.r[4] = doubleToTms(r4); c.r[5] = doubleToTms(r5); c.r[6] = doubleToTms(r6); c.r[7] = doubleToTms(r7);
    // 02716  giro de B pela velocidade relativa A − B; depois de A com B − A (registradores trocados)
    x.setR(0, r4 - r6);
    x.setR(1, r5 - r7);
    spinFromImpact(c, b, s);
    const uint32_t savedAr1 = c.ar[1], savedAr0 = c.ar[0];
    const uint32_t ar1 = c.ar[5];
    c.ar[5] = c.ar[4];
    c.ar[4] = ar1;
    c.ar[1] = c.ar[0];
    c.ar[0] = savedAr1;
    x.setR(0, x.R(6) - x.R(4)); // 02721  R4 pode ter sido sobrescrito pelo rodopio (027A6)
    x.setR(1, x.R(7) - x.R(5));
    spinFromImpact(c, b, s);
    c.ar[0] = savedAr0;
    c.ar[1] = savedAr1;
}

} // namespace

void registerCarCollision(SubroutineDispatcher& d) {
    d.add(0x2863, boxesCollide);
    d.add(0x28A8, boxCorners);
    d.add(0x28AE, scaledBoxCorners);
    d.add(0x2633, separation);
    d.add(0x2645, collisionResponse);
    d.add(0x2727, spinFromImpact);
    d.add(0x278F, aheadFlag);
    d.add(0x281D, blendSpin);
    d.add(0x2824, offTrackTurn);
    d.add(0x2835, crashSound);
    d.add(0x5375, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { compareAhead(c, b); });
}

} // namespace rea::core::game
