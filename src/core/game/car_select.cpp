/**
 * @file car_select.cpp
 * @brief Tradução de `0x59EC`–`0x5DDD` (câmbio), `0x1942`–`0x195C`, `0x1A27`–`0x1C5A` (carro) e `0x1D52`.
 */
#include "core/game/car_select.hpp"

#include <algorithm>
#include <tuple>
#include <utility>

#include "core/game/process_ctx.hpp"
#include "core/video/palette_slots.hpp"

namespace rea::core::game {

namespace {

/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }
constexpr uint32_t kNone = 0xFFFFFFFFu;  ///< Marcador de "nenhum" (−1).

// Peças da tela do câmbio.
constexpr uint32_t kChain     = 0xEA20; ///< Cadeia `+0x1F` dos objetos da tela.
constexpr uint32_t kSmoothed  = 0xEA22; ///< Volante filtrado (float).
constexpr uint32_t kLever     = 0xEA23;  ///< Objeto da alavanca do câmbio.
constexpr uint32_t kWheelObj  = 0xEA24;  ///< Objeto do volante.
constexpr uint32_t kAnimA     = 0xEA25;  ///< Objeto animado A (`[C6DB]`).
constexpr uint32_t kBlendA    = 0xEA26;  ///< Objeto com a paleta misturada A.
constexpr uint32_t kBlendB    = 0xEA27;  ///< Objeto com a paleta misturada B.
constexpr uint32_t kSignL     = 0xEA28;  ///< Placa da esquerda (entra de −240).
constexpr uint32_t kSignR     = 0xEA29;  ///< Placa da direita (entra de +240).
constexpr uint32_t kAnimB     = 0xEA2A;  ///< Objeto animado B (`[C6DC]`).
constexpr uint32_t kGearbox   = 0xE633; ///< Câmbio escolhido (0 ou 1).
constexpr uint32_t kGearSound = 0xEA21;  ///< Último som do câmbio tocado.
constexpr uint32_t kCar       = 0xE632; ///< Carro escolhido (0..3).
constexpr uint32_t kCarShown  = 0xE84B; ///< Carro já virado de frente.
constexpr uint32_t kCarDone   = 0xE641; ///< 1: escolha do carro encerrada.
constexpr uint32_t kCamAngle  = 0xEA1F;  ///< Ângulo da câmera na tela de escolha.
constexpr uint32_t kTimer     = 0xE604;  ///< Contagem regressiva por segundo, só com `[C8C5] & 0x200`.
constexpr uint32_t kStartReq  = 0xE631;  ///< ≠ 0: o jogador apertou para pular
constexpr uint32_t kBufA      = 0x59EA; ///< `[59EA]` = 0xE91D: paleta de mistura A.
constexpr uint32_t kBufB      = 0x59EB; ///< `[59EB]` = 0xE99E: paleta de mistura B.

/// Paleta base das duas metades: `[[9E7B] + 0xDD]`.
uint32_t basePalette(ProcCtx& c) { return c.rd(c.rd(0x9E7B) + 0xDD); }

/// `LDI obj,AR2 / CALL $70BB` → AR0.
uint32_t findObj(ProcCtx& c, uint32_t type) {
    c.cpu.ar[2] = type;
    c.call(0x70BB);
    return c.cpu.ar[0];
}

/// `0x9EDB` com a paleta do buffer @p bufPtr → R0; depois `0x9F5C` (envia, R3 = cabeçalho).
void sendPalette(ProcCtx& c, uint32_t bufPtr) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[2] = c.rd(bufPtr);
    c.call(0x9EDB);
    cpu.r[3] = c.rd(cpu.ar[2]++);
    cpu.r[2] = cpu.r[0];
    c.call(0x9F5C);
}

// --- Paletas -------------------------------------------------------------------------------

/// `0x5CE1`: copia a paleta base para os dois buffers e os carrega (`0x9F2F`).
void copyBlendPalettes(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    for (uint32_t ptr : {kBufA, kBufB}) {
        cpu.ar[0] = c.rd(ptr);
        cpu.ar[3] = basePalette(c);
        cpu.r[0] = c.rd(cpu.ar[3]++);
        c.wr(cpu.ar[0]++, cpu.r[0]);
        cpu.ar[2] = ((cpu.r[0] & 0x0FFFu) >> 1) - 1;
        do {
            cpu.r[0] = c.rd(cpu.ar[3]++);
            c.wr(cpu.ar[0]++, cpu.r[0]);
        } while (s(--cpu.ar[2]) >= 0);
        cpu.ar[2] = c.rd(ptr);
        c.call(0x9F2F);
    }
}

/**
 * `0x5D00`: mistura a paleta base com `[C6DD]`/`[C6DE]` (64 cores): o buffer B recebe a mistura com
 * t = R0 e o A com 1 − t. Preserva R5–R7.
 *
 * O original limita R0 (t < 0,08 → 0; t > 0,92 → 1) depois de copiá-lo para R7 (`LDF R0,R7` em
 * `0x5D05`), e a mistura usa R7: o limite não tem efeito (R0 é sobrescrito em `0x5D0F`).
 */
void blendPalettes(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t r5 = cpu.r[5], r6 = cpu.r[6], r7 = cpu.r[7];
    double t = tmsToDouble(cpu.r[0]);  // R7: o t sem o limite de 0x5D06–0x5D09
    for (auto [buf, other] : {std::pair{kBufB, 0xC6DDu}, std::pair{kBufA, 0xC6DEu}}) {
        cpu.ar[0] = c.rd(buf);
        cpu.ar[3] = basePalette(c) + 1;
        cpu.ar[2] = c.rd(other);
        cpu.r[0] = c.rd(cpu.ar[2]++);
        c.wr(cpu.ar[0]++, (cpu.r[0] & ~0x0FFFu) + 0x40);
        for (cpu.ar[5] = 0x1F; s(cpu.ar[5]) >= 0; --cpu.ar[5]) {
            cpu.r[4] = c.rd(cpu.ar[3]++);
            cpu.r[0] = c.rd(cpu.ar[2]++);
            cpu.r[1] = (video::lerpColor15(cpu.r[4] >> 16, cpu.r[0] >> 16, t) << 16) |
                       video::lerpColor15(cpu.r[4] & 0xFFFFu, cpu.r[0] & 0xFFFFu, t);
            c.wr(cpu.ar[0]++, cpu.r[1]);
        }
        sendPalette(c, buf);
        t = 1.0 - t;
    }
    cpu.r[7] = r7;
    cpu.r[6] = r6;
    cpu.r[5] = r5;
}

// --- Cadeia de objetos da tela ------------------------------------------------------------

/// `0x5CDD`: põe AR0 no início da cadeia `[EA20]`.
void chainPush(ProcCtx& c) {
    c.cpu.r[0] = c.rd(kChain);
    c.wr(c.cpu.ar[0] + 0x1F, c.cpu.r[0]);
    c.wr(kChain, c.cpu.ar[0]);
}

/// `0x5BA8`: tira AR2 da cadeia. Preserva R0 e AR1.
void chainRemove(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t r0 = cpu.r[0], ar1 = cpu.ar[1];
    for (cpu.ar[1] = kChain - 0x1F;;) {
        cpu.r[0] = c.rd(cpu.ar[1] + 0x1F);
        if (cpu.r[0] == 0) break;
        if (cpu.r[0] == cpu.ar[2]) {
            c.wr(cpu.ar[1] + 0x1F, c.rd(cpu.ar[2] + 0x1F));
            break;
        }
        cpu.ar[1] = cpu.r[0];
    }
    cpu.ar[1] = ar1;
    cpu.r[0] = r0;
}

/// `0x5B9D`: libera todos os objetos da cadeia.
void chainFree(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    for (cpu.ar[0] = c.rd(kChain); cpu.ar[0] != 0; cpu.ar[0] = cpu.r[0]) {
        cpu.r[0] = c.rd(cpu.ar[0] + 0x1F);
        cpu.ar[2] = cpu.ar[0];
        c.call(0x7100);
    }
    c.wr(kChain, 0);
}

/// Lado do objeto: tipo alto 0x100 = esquerda.
bool leftSide(ProcCtx& c, uint32_t obj) { return (c.rd(obj + 0xF) & ~0xFFu) == 0x100; }

/// `0x5BEB`: organiza os objetos da cena do câmbio.
void arrangeGearScene(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[6] = kNone;
    c.wr(kChain, 0);
    auto relink = [&]() {  // 715F (tira da lista visível) + 7070 (lista própria)
        cpu.ar[2] = cpu.ar[0];
        c.call(0x715F);
        c.call(0x7070);
    };
    auto shift = [&](double away) {  // ±600 conforme o lado
        c.wf(cpu.ar[0] + 1, (leftSide(c, cpu.ar[0]) ? away : -away) + c.rf(cpu.ar[0] + 1));
    };
    auto sign = [&](double dx, double angle) {
        relink();
        c.wf(cpu.ar[0] + 1, c.rf(cpu.ar[0] + 1) + dx);
        c.wf(cpu.ar[0] + 2, c.rf(cpu.ar[0] + 2) + 400.0);
        cpu.r[2] = doubleToTms(angle);
        c.wr(cpu.ar[0] + 0x16, cpu.r[2]);
        cpu.ar[2] = cpu.ar[0] + 4;
        c.call(0x93C4);
        shift(600.0);
        chainPush(c);
    };
    auto animated = [&](uint32_t ptr, uint32_t procPtr) {
        c.wr(ptr, cpu.ar[0]);
        relink();
        shift(600.0);
        c.wf(cpu.ar[0] + 2, 320.0 + c.rf(cpu.ar[0] + 2));
        cpu.ar[4] = cpu.ar[0];
        cpu.ar[2] = c.rd(procPtr);
        cpu.r[2] = 0x5C1;
        c.call(0xA844);
    };
    for (cpu.ar[5] = c.rd(0xC982); cpu.ar[5] != 0;) {
        cpu.ar[0] = cpu.ar[5];
        cpu.ar[5] = c.rd(cpu.ar[0]);
        if (cpu.r[6] == kNone) cpu.r[6] = cpu.ar[0];
        c.wr(cpu.ar[0] + 0x1F, cpu.ar[5]);
        c.wr(cpu.ar[0] + 0xE, (c.rd(cpu.ar[0] + 0xE) & ~0x400u) | 0x21u);
        c.wr(cpu.ar[0] + 0x11, c.rd(cpu.ar[0] + 1));
        shift(-600.0);  // 05BFC  esquerda −600, direita +600
        c.wf(cpu.ar[0] + 3, 368.0);
        c.wf(cpu.ar[0] + 0x13, 368.0);
        switch (c.rd(cpu.ar[0] + 0xF) & 0xFFu) {
        case 0x18: relink(); chainPush(c); break;
        case 0x15:
            c.wr(kWheelObj, cpu.ar[0]);
            relink();
            shift(600.0);
            c.wf(cpu.ar[0] + 2, 320.0 + c.rf(cpu.ar[0] + 2));
            break;
        case 0x14: c.wr(kLever, cpu.ar[0]); relink(); chainPush(c); break;
        case 0x10: c.wr(kBlendA, cpu.ar[0]); chainPush(c); break;
        case 0x11: c.wr(kBlendB, cpu.ar[0]); chainPush(c); break;
        case 0x12: c.wr(kSignL, cpu.ar[0]); sign(-240.0, 9.0); break;
        case 0x13: c.wr(kSignR, cpu.ar[0]); sign(240.0, -9.0); break;
        case 0x16: animated(kAnimA, 0xC6DB); break;
        case 0x17: animated(kAnimB, 0xC6DC); break;
        default: chainPush(c); break;
        }
    }
    c.wr(0xEA2B, cpu.r[6]);
}

/// `0x5B0F`: desliza 15 para o centro tudo da cadeia, menos as placas.
void slideIn(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    for (cpu.ar[4] = kChain - 0x1F; (cpu.r[0] = c.rd(cpu.ar[4] + 0x1F)) != 0;) {
        cpu.ar[4] = cpu.r[0];
        if (cpu.ar[4] == c.rd(kSignL) || cpu.ar[4] == c.rd(kSignR)) continue;
        c.wf(cpu.ar[4] + 1, c.rf(cpu.ar[4] + 1) + (leftSide(c, cpu.ar[4]) ? 15.0 : -15.0));
    }
}

/// `0x5AC1`: desce 8 o volante e os dois animados.
void lowerParts(ProcCtx& c) {
    for (uint32_t ptr : {kWheelObj, kAnimA, kAnimB}) {
        c.cpu.ar[0] = c.rd(ptr);
        c.wf(c.cpu.ar[0] + 2, c.rf(c.cpu.ar[0] + 2) - 8.0);
    }
}

/// `0x5BBF`: R0 = posição do volante filtrado entre `[E89C]` e `[E89D]` (0..1).
void wheelFraction(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const double v = static_cast<double>(s(c.rd(0xC8C8))) * c.k(0x5BC1) + c.rf(kSmoothed) * c.k(0x5BC2);
    c.wf(kSmoothed, v);
    cpu.r[0] = doubleToTms(v - c.rf(0xE89C));
    cpu.r[1] = doubleToTms(c.rf(0xE89D) - c.rf(0xE89C));
    c.call(0xA509);
}

/// `0x5BB6`: gira o volante 3D (π/2 − π·t).
void turnWheel(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    c.call(0x5BBF);
    cpu.r[2] = doubleToTms(-(c.k(0x5BB7) * tmsToDouble(cpu.r[0]) - c.k(0x5BB9)));
    cpu.ar[2] = c.rd(kWheelObj) + 4;
    c.call(0x93C4);
}

/// `0x5B66`: alavanca do câmbio pelo volante (histerese em 0,45..0,55).
void moveLever(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    c.call(0x5BBF);
    double t = tmsToDouble(cpu.r[0]);
    cpu.r[1] = c.rd(kGearbox);
    cpu.ar[2] = c.rd(kLever);
    if (t < c.k(0x5B69)) t = 0.0;
    if (t > c.k(0x5B6B)) t = 1.0;
    bool right;
    if (t >= 0.5) right = !(t <= c.k(0x5B6F) && cpu.r[1] == 0);
    else right = t >= c.k(0x5B7C) && cpu.r[1] == 1;
    const double target = right ? 101.0 : -101.0;
    c.wf(cpu.ar[2] + 1, (target - c.rf(cpu.ar[2] + 1)) * c.k(0x5B76) + c.rf(cpu.ar[2] + 1));
    c.wr(kGearbox, right ? 1 : 0);
    // 05B88  centro do volante para o lado escolhido
    const double lo = c.rf(0xE89C), hi = c.rf(0xE89D), d = (hi - lo) * c.k(0x5B8B);
    c.wf(0xEA48, right ? hi - d : lo + d);
    cpu.r[0] = c.rd(kGearbox);
    if (cpu.r[0] != c.rd(kGearSound)) {
        c.wr(kGearSound, cpu.r[0]);
        c.sound(cpu.r[0] == 0 ? 0xBD : 0xC0);
    }
}

/// `0x5B29`: gira a cena pelo ângulo `[EA1F]` (esquerda com `[0x5A]`, direita com `[0x5B]` = −ângulo).
void rotateScene(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[2] = c.rd(kCamAngle);
    cpu.ar[2] = c.rd(0x5A);
    c.call(0x938A);
    cpu.r[2] = doubleToTms(-c.rf(kCamAngle));
    cpu.ar[2] = c.rd(0x5B);
    c.call(0x938A);
    for (cpu.ar[4] = kChain - 0x1F; (cpu.r[0] = c.rd(cpu.ar[4] + 0x1F)) != 0;) {
        cpu.ar[4] = cpu.r[0];
        c.call(0x9414);  // AR2 = vetor de rascunho
        const bool left = leftSide(c, cpu.ar[4]);
        const uint32_t pivotX = left ? 0x5B26 : 0x5B28, mat = left ? 0x5A : 0x5B;
        c.wf(cpu.ar[2], c.rf(cpu.ar[4] + 0x11) - c.rf(pivotX));
        c.wf(cpu.ar[2] + 2, c.rf(cpu.ar[4] + 0x13) - c.rf(0x5B27));
        cpu.r[2] = c.rd(mat);
        cpu.r[3] = cpu.ar[2];
        c.call(0x941E);
        c.wf(cpu.ar[4] + 1, c.rf(cpu.ar[2]) + c.rf(pivotX));
        c.wf(cpu.ar[4] + 3, c.rf(cpu.ar[2] + 2) + c.rf(0x5B27));
        cpu.r[2] = c.rd(mat);
        cpu.ar[2] = cpu.ar[4] + 4;
        c.call(0x93F1);
    }
}

// --- Processo do câmbio (chamada local [B1ED]) --------------------------------------------

void gearIntro(ProcCtx& c);
void gearChoose(ProcCtx& c);
void gearConfirm(ProcCtx& c);

/// Trecho traduzido da ROM em `0x59EC`.
void gearStart(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    c.call(0x8BE4);
    c.wr(0xC940, 1);
    c.wr(0xC941, 1);
    c.wr(0xC8C5, 8);
    c.call(0x5CE1);
    cpu.ar[2] = c.rd(0xC6D0);
    c.call(0x440C);
    c.call(0x5BEB);
    for (auto [obj, buf] : {std::pair{kBlendA, kBufB}, std::pair{kBlendB, kBufA}}) {
        cpu.ar[0] = c.rd(obj);
        cpu.ar[2] = c.rd(buf);
        c.call(0x9EDB);
        c.wr(cpu.ar[0] + 0x10, cpu.r[0]);
        c.orField(cpu.ar[0], 0xE, 0x400);
    }
    cpu.r[0] = doubleToTms(1.0);
    c.call(0x5D00);
    c.wr(0xC940, 0);
    c.wr(0xC941, 0);
    cpu.ar[2] = c.rd(0xC6D1);
    cpu.r[2] = 0x5C2;
    c.call(0xA844);
    cpu.ar[5] = 0x27;
    gearIntro(c);
}

/// `0x5A0D`: 40 quadros de entrada.
void gearIntro(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    c.call(0x5B0F);
    c.call(0x5AC1);
    c.call(0x5BB6);
    const uint32_t ar5 = cpu.ar[5];
    cpu.r[0] = doubleToTms(1.0);
    c.call(0x5D00);
    cpu.ar[5] = ar5;
    c.sleep(1, 0x5A16);
}

/// Trecho traduzido da ROM em `0x5A17`.
void gearReady(ProcCtx& c) {
    c.sound(0xC3);
    c.wr(0xC8C5, 0x2C8);
    c.wr(0xC940, 0);
    c.wr(0xC941, 0);
    c.wf(kSmoothed, static_cast<double>(s(c.rd(0xC8C8))));
    c.wr(kStartReq, 0);
    c.wr(kTimer, 10);
    c.call(0x1C5B);
    gearChoose(c);
}

/// `0x5A25`: um quadro da escolha do câmbio.
void gearChoose(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    if (c.rd(kStartReq) != 0) return gearConfirm(c);
    cpu.ar[2] = c.rd(kLever);
    cpu.r[0] = doubleToTms(c.rf(cpu.ar[2] + 1) + 101.0);
    cpu.r[1] = doubleToTms(204.0);
    c.call(0xA509);
    c.call(0x5D00);
    c.call(0x5BB6);
    c.call(0x5B66);
    cpu.ar[2] = c.rd(0xC6D2);
    cpu.r[2] = 0xF2;
    cpu.r[3] = 0x151;
    cpu.r[4] = 0x900;
    c.call(0xAF13);
    c.call(0x1C85);
    if (cpu.carry()) return gearConfirm(c);
    c.call(0x1D41);
    c.sleep(1, 0x5A39);
}

/// `0x5A3B`: câmbio escolhido; câmera atrás do percurso e entrada dos carros.
void gearConfirm(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    c.sound(0xD5);
    cpu.r[2] = c.rd(0xE402);
    cpu.ar[2] = c.rd(0x5A);
    c.call(0x939D);
    c.call(0x9414);
    c.wf(cpu.ar[2] + 1, -650.0);
    c.wf(cpu.ar[2] + 2, -28000.0);
    cpu.r[2] = c.rd(0x5A);
    cpu.r[3] = cpu.ar[2];
    c.call(0x941E);
    for (uint32_t i = 0; i < 3; ++i) c.wf(0x809800 + i, c.rf(cpu.ar[2] + i) + c.rf(0xE3FF + i));
    cpu.dp = 0;
    for (auto [ptr, id] : {std::pair{0xC6D3u, 0x5C1u}, std::pair{0xC6D4u, 0x5C2u}}) {
        cpu.ar[2] = c.rd(ptr);
        cpu.r[2] = id;
        c.call(0xA844);
    }
    c.call(0x5B62);
    cpu.r[2] = doubleToTms(-c.rf(0xE402));
    c.wr(0x809807, cpu.r[2]);
    cpu.ar[2] = c.rd(0x47);
    c.call(0x939D);
    cpu.ar[2] = c.rd(0xC6D5);
    c.call(0x44BB);
    c.sleep(3, 0x5A68);
}

/// Trecho traduzido da ROM em `0x5A6C`.
void gearCars(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[2] = c.rd(0xC6D6);
    cpu.r[2] = 0x5C2;
    c.call(0xA844);
    c.call(0x1A27);
    c.wr(0xE84E, 0);
    c.wr(0xE638, 4);
    c.wr(0xEA47, 2);
    c.wr(kCar, 1);
    cpu.ar[2] = c.rd(0xC6D7);
    cpu.r[2] = 0x5C1;
    c.call(0xA844);
    c.wf(kCamAngle, 0.0);
    c.sound(0xC6);
    c.lcall(0xC6D8, 0xC6D9);  // 0x5A89 → 0x5A85
}

/// `0x5A89`: cria a escolha do carro (`0x1942`) e gira a câmera em 31 quadros.
void cameraTurnStart(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[2] = c.rd(0xC6DA);
    cpu.r[2] = 0x500;
    c.call(0xA844);
    cpu.ar[2] = c.rd(0x5F);
    c.wf(cpu.ar[2], 0.0);
    c.wf(cpu.ar[2] + 1, 0.0);
    c.wf(cpu.ar[2] + 2, -8500.0);
    cpu.r[2] = c.rd(0x5A);
    cpu.r[3] = cpu.ar[2];
    c.call(0x941E);
    c.wf(cpu.ar[2], c.rf(cpu.ar[2]) + c.rf(0xE3FF));
    c.wf(cpu.ar[2] + 2, c.rf(cpu.ar[2] + 2) + c.rf(0xE401));
    cpu.ar[4] = 0x1E;
    c.jump(0x5A9C);
}

/// Trecho traduzido da ROM em `0x5A9C`.
void cameraTurnFrame(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    c.wf(kCamAngle, c.rf(kCamAngle) + c.k(0x5A9D));
    const uint32_t ar4 = cpu.ar[4];
    c.call(0x5B29);
    cpu.ar[4] = ar4;
    cpu.ar[0] = c.rd(0x45);
    cpu.ar[1] = c.rd(0x5F);
    for (uint32_t i : {0u, 2u}) {
        c.wf(cpu.ar[0] + i, (c.rf(cpu.ar[1] + i) - c.rf(cpu.ar[0] + i)) * c.k(0x5AA6) + c.rf(cpu.ar[0] + i));
    }
    c.sleep(1, 0x5AB0);
}

/// Trecho traduzido da ROM em `0x5AB1`.
void cameraTurnEnd(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    c.call(0x5B9D);
    cpu.r[0] = 0x5C2;
    cpu.r[1] = kNone;
    c.call(0xA8CF);
    for (uint32_t buf : {kBufB, kBufA}) {
        cpu.ar[2] = c.rd(buf);
        c.call(0x9EDB);
        cpu.ar[2] = cpu.r[0];
        c.call(0x9EEC);
    }
    c.lret();
}

// --- Processos das peças do câmbio ---------------------------------------------------------

/// `0x5BCA`: depois de 10 quadros, as placas giram e sobem/afastam em 30 quadros.
void signsFrame(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    for (auto [ptr, da, dx] : {std::tuple{kSignR, 1.0, -8.0}, std::tuple{kSignL, -1.0, 8.0}}) {
        cpu.ar[4] = c.rd(ptr);
        cpu.r[2] = doubleToTms(c.rf(cpu.ar[4] + 0x16) + da * c.k(0x5BCF));
        c.wr(cpu.ar[4] + 0x16, cpu.r[2]);
        cpu.ar[2] = cpu.ar[4] + 4;
        c.call(0x93C4);
        c.wf(cpu.ar[4] + 2, c.rf(cpu.ar[4] + 2) - c.k(0x5BD5));
        c.wf(cpu.ar[4] + 1, c.rf(cpu.ar[4] + 1) + dx);
    }
    c.sleep(1, 0x5BE9);
}

/// `0x5ACE`: auditoria do câmbio e a placa escolhida vem para a frente e sai girando.
void chosenSign(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const bool one = c.rd(kGearbox) == 1;
    c.audit(one ? 0x96 : 0x95);
    cpu.ar[4] = c.rd(one ? kSignL : kSignR);
    cpu.ar[2] = cpu.ar[4];
    c.call(0x5BA8);
    cpu.ar[5] = 0xF;
    c.jump(0x5ADA);
}

/// Trecho traduzido da ROM em `0x5ADA`.
void chosenSignShrink(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    c.wf(cpu.ar[4] + 1, c.rf(cpu.ar[4] + 1) * c.k(0x5ADB));
    c.wf(cpu.ar[4] + 2, c.rf(cpu.ar[4] + 2) * c.k(0x5ADE));
    c.sleep(1, 0x5AE2);
}

/// Trecho traduzido da ROM em `0x5AE5`.
void chosenSignSpin(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[2] = doubleToTms(c.rf(cpu.ar[4] + 0x14) - c.k(0x5AE6));
    c.wr(cpu.ar[4] + 0x14, cpu.r[2]);
    cpu.ar[2] = cpu.ar[4] + 4;
    c.call(0x9377);
    cpu.r[6] = doubleToTms(tmsToDouble(cpu.r[6]) * c.k(0x5AEB));
    cpu.r[7] = doubleToTms(tmsToDouble(cpu.r[7]) * c.k(0x5AEC));
    const double r6 = tmsToDouble(cpu.r[6]);
    c.wf(cpu.ar[4] + 1, c.rf(cpu.ar[4] + 1) + r6);
    c.wf(cpu.ar[4] + 2, c.rf(cpu.ar[4] + 2) - r6);
    c.wf(cpu.ar[4] + 3, c.rf(cpu.ar[4] + 3) - r6);
    c.sleep(1, 0x5AF8);
}

/// `0x5B03`: o objeto AR4 sai da cadeia e cai acelerando por 11 quadros.
void dropPart(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[2] = cpu.ar[4];
    c.call(0x5BA8);
    cpu.ar[5] = 0xA;
    cpu.r[6] = doubleToTms(2.0);
    c.jump(0x5B07);
}

/// Trecho traduzido da ROM em `0x5B07`.
void dropPartFrame(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    c.wf(cpu.ar[4] + 2, c.rf(cpu.ar[4] + 2) + tmsToDouble(cpu.r[6]));
    cpu.r[6] = doubleToTms(tmsToDouble(cpu.r[6]) * 2.0);
    c.sleep(1, 0x5B0D);
}

/// `0x5CA5`: os dois animados se abrem (±4, +3) por 11 quadros.
void openParts(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    c.wf(cpu.ar[4] + 1, c.rf(cpu.ar[4] + 1) + 4.0);
    c.wf(cpu.ar[4] + 2, c.rf(cpu.ar[4] + 2) + 3.0);
    c.wf(cpu.ar[6] + 1, c.rf(cpu.ar[6] + 1) - 4.0);
    c.wf(cpu.ar[6] + 2, c.rf(cpu.ar[6] + 2) + 3.0);
    c.sleep(1, 0x5CB6);
}

// --- Escolha do carro ----------------------------------------------------------------------

/// `0x1A27`: cria os 4 carros da lista `[18A4]` (x, y, z, modelo, ângulo, tipo) no espaço do percurso.
void spawnCars(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[2] = c.rd(0xE402);
    cpu.ar[2] = c.rd(0x5A);
    c.call(0x938A);
    cpu.ar[3] = c.rd(0xB210);
    cpu.ar[4] = c.rd(0x18A4);
    cpu.r[4] = 0;
    do {
        c.call(0x7025);
        cpu.ar[2] = c.rd(0x5D);
        for (uint32_t i = 0; i < 3; ++i) c.wf(cpu.ar[2] + i, static_cast<double>(s(c.rd(cpu.ar[4]++))));
        cpu.r[2] = c.rd(0x5A);
        cpu.r[3] = cpu.ar[2];
        c.call(0x941E);
        for (uint32_t i = 0; i < 3; ++i) c.wf(cpu.ar[0] + 1 + i, c.rf(cpu.ar[2] + i) + c.rf(0xE3FF + i));
        c.wr(cpu.ar[0] + 0xD, c.rd(cpu.ar[4]++));
        c.wr(cpu.ar[3]++, cpu.ar[0]);
        cpu.ar[2] = cpu.ar[0];
        c.call(0x7093);
        cpu.ar[1] = cpu.ar[7] + 0xC + cpu.r[4];
        c.wr(cpu.ar[1], cpu.ar[0]);
        ++cpu.r[4];
        cpu.r[2] = doubleToTms(c.rf(cpu.ar[4]++) + c.rf(0xE402));
        cpu.ar[2] = cpu.ar[0] + 4;
        c.wr(cpu.ar[0] + 0x15, cpu.r[2]);
        c.call(0x938A);
        c.wr(cpu.ar[0] + 0xF, c.rd(cpu.ar[4]++));
    } while (c.rd(cpu.ar[4]) != kNone);
}

/// `0x1B9D`: paleta do carro AR0 com brilho R0 (`0x9FB6`).
void carPalette(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t car = cpu.ar[0];
    cpu.ar[1] = c.rd(car + c.rd(0x1A88));
    cpu.ar[0] = c.rd(c.rd(car + c.rd(0x1A89)) + c.rd(0x9E7B));
    c.call(0x9FB6);
}

/// `0x1C34`: placa 0x601 + n com a paleta `[B21D]` (destaque desligado) e a base 0x401 + n visível.
void panelNormal(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t r0 = cpu.r[0], ar0 = cpu.ar[0], ar2 = cpu.ar[2];
    const uint32_t panel = findObj(c, ar2 + 0x601);
    cpu.ar[2] = c.rd(0xB21D);
    c.call(0x9EDB);
    c.wr(panel + 0x10, cpu.r[0]);
    c.orField(panel, 0xE, 0x400);
    const uint32_t car = findObj(c, ar2 + 0x401);
    c.wr(car + 0xE, c.rd(car + 0xE) & ~0x400u);
    cpu.ar[2] = ar2;
    cpu.ar[0] = ar0;
    cpu.r[0] = r0;
}

/// `0x1C4A`: placa 0x601 + n e base 0x401 + n com o bit 0x400 limpo.
void panelHighlight(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t r0 = cpu.r[0], ar0 = cpu.ar[0], ar2 = cpu.ar[2];
    for (uint32_t base : {0x601u, 0x401u}) {
        const uint32_t obj = findObj(c, ar2 + base);
        c.wr(obj + 0xE, c.rd(obj + 0xE) & ~0x400u);
    }
    cpu.ar[2] = ar2;
    cpu.ar[0] = ar0;
    cpu.r[0] = r0;
}

/// `0x1A9F`: prepara o carro AR3 com a paleta AR2 (`0x9F2F`); alturas em `*+AR7(IR0)` e `(IR0+1)`.
bool setupPanel(ProcCtx& c, uint32_t palPtr, uint32_t type, uint32_t ir0) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[2] = palPtr;
    c.wr(cpu.ar[2], 1);
    c.call(0x9F2F);
    cpu.r[6] = cpu.r[0];
    cpu.ar[2] = type;
    c.call(0x70BB);
    if (!cpu.carry()) {  // 01AA5  BHS $1AA5: trava se o painel não existir
        c.call(0x1AA5);
        return false;
    }
    c.orField(cpu.ar[0], 0xE, 0x400);
    c.wr(cpu.ar[0] + 0x10, cpu.r[6]);
    cpu.r[0] = c.rd(cpu.ar[0] + 2);
    c.setVar(ir0, cpu.r[0]);
    c.setVar(ir0 + 1, cpu.r[0]);
    c.wr(cpu.ar[0] + 0x1E, cpu.r[0]);
    return true;
}

/// `0x1B59`: o carro AR2 (0x481 + n) desliza para a altura `*+AR7(IR0)`, a base (0x401 + n)
/// acompanha, o carro que deixou de ser escolhido volta de frente e o escolhido gira.
void panelStep(ProcCtx& c, uint32_t type, uint32_t ir0, uint32_t ir1) {
    Tms32031State& cpu = c.cpu;
    const uint32_t car = findObj(c, type);
    c.wf(car + 2, (c.rf(cpu.ar[7] + ir0) - c.rf(car + 2)) * c.k(0x1B5D) + c.rf(car + 2));
    cpu.ar[1] = car;
    const uint32_t base = findObj(c, type - 0x80);
    c.wf(base + 2, c.rf(car + 2) - c.rf(cpu.ar[7] + ir0 + 1) + c.rf(cpu.ar[7] + ir0 + 9));
    cpu.ar[0] = car;  // POP AR0
    cpu.r[0] = c.rd(kCarShown);
    cpu.r[1] = c.rd(kCar);
    if (cpu.r[0] != cpu.r[1]) {
        if (cpu.r[0] != ir1) return;
        // 01B73  o carro que deixou de ser escolhido volta a ficar de frente para a câmera
        cpu.r[1] = c.rd(0xB21B);
        cpu.r[2] = doubleToTms(c.rf(car + 0x15) + c.rf(0xB21B));
        c.call(0x9308);
        const uint32_t angle = cpu.r[2];
        cpu.r[2] = doubleToTms(c.k(0x1B77) + c.rf(0xE402));
        c.call(0x9308);
        cpu.r[0] = cpu.r[2];
        cpu.r[2] = angle;
        c.call(0x94A7);
        const uint32_t kept = cpu.r[2];
        cpu.r[2] = cpu.r[0];
        c.call(0x9308);
        cpu.r[0] = cpu.r[2];
        cpu.r[2] = kept;
        double a = tmsToDouble(cpu.r[2]);
        const double d = tmsToDouble(cpu.r[0]);
        if (d < c.k(0x1B88)) {
            a += d;
            cpu.r[1] = c.rd(kCar);
            c.wr(kCarShown, cpu.r[1]);
        } else {
            a += d * c.k(0x1B8A);
        }
        cpu.r[2] = doubleToTms(a);
        c.wr(car + 0x15, cpu.r[2]);
        cpu.ar[2] = car + 4;
        c.call(0x938A);
    }
    if (cpu.r[1] != ir1) return;
    cpu.r[2] = doubleToTms(c.rf(car + 0x15) + c.k(0x1B97));
    c.wr(car + 0x15, cpu.r[2]);
    cpu.ar[2] = car + 4;
    c.call(0x938A);
}

/// `0x1AB0`: processo dos 4 carros (0x481..0x484) e suas bases (0x401..0x404).
void carPanelsStart(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    for (uint32_t i = 0; i < 4; ++i)
        if (!setupPanel(c, c.rd(0xB217 + i), 0x481 + i, 0xD + 2 * i)) return;
    for (uint32_t i = 0; i < 4; ++i) {
        const uint32_t car = findObj(c, 0x401 + i);
        cpu.r[0] = c.rd(car + 2);
        c.setVar(0x15 + 2 * i, cpu.r[0]);
        c.setVar(0x16 + 2 * i, cpu.r[0]);
    }
    for (uint32_t i = 0; i < 4; ++i) {
        cpu.ar[0] = i;
        cpu.r[0] = doubleToTms(0.5);
        c.call(0x1B9D);
    }
    c.call(0x1C27);
    c.wr(kCarShown, c.rd(kCar));
    c.setVar(0xC, 0);
    c.jump(0x1AE5);
}

/// `0x1AE5`: troca de carro e animação dos painéis.
void carPanelsFrame(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[2] = c.rd(kCar);
    cpu.r[0] = c.var(0xC);
    if (cpu.ar[2] != cpu.r[0]) {
        c.setVar(0xC, cpu.ar[2]);
        const uint32_t now = cpu.ar[2], old = cpu.r[0];
        if (c.rd(0xE63E) != 0) {
            c.sound(0xD2);
            cpu.ar[2] = now;
            cpu.r[0] = old;
        }
        // 01AF2  o anterior volta à altura original e escurece
        c.setVar(old * 2 + 0xD, c.var(old * 2 + 0xE));
        cpu.ar[2] = old;
        c.call(0x1C34);
        cpu.ar[0] = old;
        cpu.r[0] = doubleToTms(0.5);
        c.call(0x1B9D);
        // 01B00  o novo sobe 300 e acende
        c.wf(cpu.ar[7] + now * 2 + 0xD, c.rf(cpu.ar[7] + now * 2 + 0xE) - 300.0);
        cpu.ar[2] = now;
        c.call(0x1C4A);
        cpu.ar[0] = now;
        cpu.r[0] = doubleToTms(1.0);
        c.call(0x1B9D);
    }
    for (uint32_t i = 0; i < 4; ++i) panelStep(c, 0x481 + i, 0xD + 2 * i, i);  // 0x1B13
    c.sleep(1, 0x1B12);
}

/// `0x1A5C`: fichas técnicas do carro `[E632]` (com `[E63E]` ≠ 0).
void carSpecs(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    if (c.rd(0xE63E) == 0) return;
    const uint32_t ar3 = cpu.ar[3], ar4 = cpu.ar[4], ar5 = cpu.ar[5], ar6 = cpu.ar[6], ar7 = cpu.ar[7];
    cpu.ar[7] = c.rd(kCar) + c.rd(0x1A57);
    cpu.ar[5] = c.rd(0xB211);
    for (cpu.ar[3] = 4; s(cpu.ar[3]) >= 0; --cpu.ar[3]) {
        cpu.ar[2] = c.rd(cpu.ar[5]++);
        cpu.r[2] = c.rd(cpu.ar[7]);
        cpu.r[3] = doubleToTms(180.0 - static_cast<double>(s(cpu.ar[3])) * 10.0);
        cpu.rc = 1;
        c.call(0x7651);
        c.call(0x7712);
    }
    cpu.ar[5] = c.rd(c.rd(kCar) + c.rd(0x68EA));
    for (cpu.ar[3] = 5; s(cpu.ar[3]) >= 0; --cpu.ar[3]) {
        cpu.ar[2] = c.rd(cpu.ar[5]++);
        cpu.r[2] = cpu.ar[3] == 6 ? c.rd(cpu.ar[7]) : doubleToTms(c.rf(cpu.ar[7]) + 100.0);
        cpu.r[3] = doubleToTms(180.0 - static_cast<double>(s(cpu.ar[3])) * 10.0);
        cpu.rc = 1;
        c.call(0x7651);
        c.call(0x7712);
    }
    cpu.ar[7] = ar7;
    cpu.ar[6] = ar6;
    cpu.ar[5] = ar5;
    cpu.ar[4] = ar4;
    cpu.ar[3] = ar3;
}

/// `0x1942`: 12 s para escolher o carro; `[E641]` = 1 ao confirmar ou acabar o tempo.
void carChooseStart(ProcCtx& c) {
    c.wr(kCarDone, 0);
    c.wr(kStartReq, 0);
    c.wr(kTimer, 12);
    c.call(0x1C5B);
    c.jump(0x1948);
}

/// Trecho traduzido da ROM em `0x1948`.
void carChooseFrame(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    if (c.rd(kStartReq) == 0) {
        c.call(0x1C62);
        c.call(0x5BB6);
        c.call(0x1D52);
        c.call(0x1A5C);
        cpu.ar[2] = c.rd(0xB20F);
        cpu.r[2] = 0xF2;
        cpu.r[3] = 0x151;
        cpu.r[4] = 0x900;
        c.call(0xAF13);
        c.call(0x1C85);
        if (!cpu.carry()) {
            c.call(0x1D41);
            return c.sleep(1, 0x1958);
        }
    }
    c.wr(kCarDone, 1);
    c.die();
}

/// `0x1B29`: botões de vista 1/2 mostram o modelo alternativo do carro `[E632]` (menos o 3).
void altModelFrame(ProcCtx& c);

/// 01B4C (R0 = carro)
void altModelRestore(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t car = cpu.r[0];
    const uint32_t obj = findObj(c, car + 0x481);
    cpu.ar[2] = car * 6 + c.rd(0x18A4);
    c.wr(obj + 0xD, c.rd(cpu.ar[2] + 3));
    c.orField(obj, 0xE, 0x400);
}

void altModelFrame(ProcCtx& c) {  // 01B2D
    Tms32031State& cpu = c.cpu;
    if (c.rd(kCarDone) != 0) return c.die();
    if (((c.rd(0xC937) >> 16) & 0x30u) != 0) {
        cpu.r[0] = c.rd(0xE84A);
        if (cpu.r[0] != kNone) altModelRestore(c);
        cpu.ar[2] = c.rd(kCar);
        if (cpu.ar[2] != 3) {
            c.wr(0xE84A, cpu.ar[2]);
            const uint32_t obj = findObj(c, cpu.ar[2] + 0x481);
            c.wr(obj + 0xD, c.rd(c.rd(kCar) + c.rd(0x1B24)));
            c.wr(obj + 0xE, c.rd(obj + 0xE) & ~0x400u);
        }
    } else if (c.rd(0xE84A) != kNone) {
        cpu.r[0] = c.rd(0xE84A);
        altModelRestore(c);
        c.wr(0xE84A, kNone);
    }
    c.sleep(1, 0x1B2D);
}

} // namespace

void registerCarSelect(SubroutineDispatcher& d) {
    using C = ProcCtx;
    // rotinas
    addProc(d, 0x5CE1, copyBlendPalettes);
    addProc(d, 0x5D00, blendPalettes);
    addProc(d, 0x5CDD, chainPush);
    addProc(d, 0x5BA8, chainRemove);
    addProc(d, 0x5B9D, chainFree);
    addProc(d, 0x5BEB, arrangeGearScene);
    addProc(d, 0x5B0F, slideIn);
    addProc(d, 0x5AC1, lowerParts);
    addProc(d, 0x5BBF, wheelFraction);
    addProc(d, 0x5BB6, turnWheel);
    addProc(d, 0x5B66, moveLever);
    addProc(d, 0x5B29, rotateScene);
    addProc(d, 0x5B62, [](C& c) {
        c.cpu.ar[2] = c.rd(kLever);
        c.call(0x5BA8);
        c.call(0x7100);
    });
    addProc(d, 0x1A27, spawnCars);
    addProc(d, 0x1B9D, carPalette);
    addProc(d, 0x1C34, panelNormal);
    addProc(d, 0x1C4A, panelHighlight);
    addProc(d, 0x1C27, [](C& c) {
        const uint32_t ar2 = c.cpu.ar[2];
        c.cpu.ar[2] = c.rd(0xB21C);
        c.call(0x9F2F);
        for (uint32_t i = 0; i < 4; ++i) {
            c.cpu.ar[2] = i;
            c.call(0x1C34);
        }
        c.cpu.ar[2] = ar2;
    });
    addProc(d, 0x1A5C, carSpecs);
    addProc(d, 0x1A8A, [](C& c) {  // libera as paletas [B212]..[B216]
        for (uint32_t p = 0xB212; p <= 0xB216; ++p) {
            c.cpu.ar[2] = c.rd(p);
            c.call(0x9EDB);
            c.cpu.ar[2] = c.cpu.r[0];
            c.call(0x9EEC);
        }
    });
    addProc(d, 0x1D52, [](C& c) {
        c.cpu.ar[2] = static_cast<uint32_t>(std::clamp(s(c.rd(0xEA47)), 0, 3));
        c.wr(kCar, c.cpu.ar[2]);
    });
    // processo do câmbio
    addProc(d, 0x59EC, gearStart);
    addProc(d, 0x5A16, [](C& c) {
        if (s(--c.cpu.ar[5]) >= 0) return gearIntro(c);
        gearReady(c);
    });
    addProc(d, 0x5A39, [](C& c) {
        if (s(c.rd(kTimer)) > 0) return gearChoose(c);
        gearConfirm(c);
    });
    addProc(d, 0x5A68, [](C& c) {
        c.sound(0xCC);
        c.sleep(7, 0x5A6C);
    });
    addProc(d, 0x5A6C, gearCars);
    addProc(d, 0x5A85, [](C& c) { c.lret(); });
    addProc(d, 0x5A89, cameraTurnStart);
    addProc(d, 0x5A9C, cameraTurnFrame);
    addProc(d, 0x5AB0, [](C& c) {
        if (s(--c.cpu.ar[4]) >= 0) return cameraTurnFrame(c);
        cameraTurnEnd(c);
    });
    // processos das peças
    addProc(d, 0x5BCA, [](C& c) { c.sleep(0xA, 0x5BCC); });
    addProc(d, 0x5BCC, [](C& c) {
        c.cpu.ar[5] = 0x1D;
        signsFrame(c);
    });
    addProc(d, 0x5BE9, [](C& c) {
        if (s(--c.cpu.ar[5]) >= 0) return signsFrame(c);
        c.die();
    });
    addProc(d, 0x5ACE, chosenSign);
    addProc(d, 0x5ADA, chosenSignShrink);
    addProc(d, 0x5AE2, [](C& c) {
        if (s(--c.cpu.ar[5]) >= 0) return chosenSignShrink(c);
        c.cpu.r[7] = doubleToTms(1.0);
        c.cpu.r[6] = doubleToTms(1.0);
        chosenSignSpin(c);
    });
    addProc(d, 0x5AE5, chosenSignSpin);
    addProc(d, 0x5AF8, chosenSignSpin);
    addProc(d, 0x5AF9, [](C& c) { c.cpu.ar[4] = c.rd(kAnimB); dropPart(c); });
    addProc(d, 0x5AFB, [](C& c) { c.cpu.ar[4] = c.rd(kAnimA); dropPart(c); });
    addProc(d, 0x5AFD, [](C& c) { c.cpu.ar[4] = c.rd(kWheelObj); dropPart(c); });
    addProc(d, 0x5AFF, [](C& c) {
        c.cpu.ar[4] = c.rd(c.rd(kGearbox) != 1 ? kSignL : kSignR);
        dropPart(c);
    });
    addProc(d, 0x5B07, dropPartFrame);
    addProc(d, 0x5B0D, [](C& c) {
        if (s(--c.cpu.ar[5]) >= 0) return dropPartFrame(c);
        c.die();
    });
    addProc(d, 0x5CA5, [](C& c) {
        c.cpu.ar[6] = c.rd(kAnimB);
        c.cpu.ar[4] = c.rd(kAnimA);
        c.cpu.ar[5] = 0xA;
        openParts(c);
    });
    addProc(d, 0x5CB6, [](C& c) {
        if (s(--c.cpu.ar[5]) >= 0) return openParts(c);
        c.die();
    });
    // escolha do carro
    addProc(d, 0x1AB0, carPanelsStart);
    addProc(d, 0x1AE5, carPanelsFrame);
    addProc(d, 0x1B12, carPanelsFrame);
    addProc(d, 0x1942, carChooseStart);
    addProc(d, 0x1948, carChooseFrame);
    addProc(d, 0x1958, [](C& c) {
        if (s(c.rd(kTimer)) > 0) return carChooseFrame(c);
        c.wr(kCarDone, 1);
        c.die();
    });
    addProc(d, 0x1B29, [](C& c) {
        c.wr(0xE84A, kNone);
        c.sleep(1, 0x1B2D);
    });
    addProc(d, 0x1B2D, altModelFrame);
}

} // namespace rea::core::game
