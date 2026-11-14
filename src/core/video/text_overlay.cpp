/**
 * @file text_overlay.cpp
 * @brief Tradução de `0x7720`–`0x77D0`, `0x7651`–`0x771F`, `0x7450` e `0x7594`.
 */
#include "core/video/text_overlay.hpp"

#include "core/tms_float.hpp"

namespace rea::core::video {

namespace {

using namespace text_vars;

/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }
/// Estende o sinal dos 16 bits baixos de @p v.
inline uint32_t lowSigned(uint32_t v) { return static_cast<uint32_t>(static_cast<int32_t>(v << 16) >> 16); }

/// Leitor de caracteres: RS = deslocamento (0, −8, −16, −24); `RS == −32` → próxima palavra.
struct CharReader {
    Tms32031State& cpu;  ///< Registradores da CPU TMS320C31.
    MemoryBus& bus;  ///< Barramento de memória simulado.
    /// Próximo caractere da string (8 bits); avança AR2 a cada 4 caracteres.
    uint32_t next() {
        if (cpu.rs == static_cast<uint32_t>(-0x20)) {
            cpu.rs = 0;
            cpu.ar[2] += 1;
        }
        const uint32_t c = tmsLsh(bus.readWord(cpu.ar[2]), cpu.rs) & 0xFFu;
        cpu.rs -= 8;
        return c;
    }
};

} // namespace

void textWidth(Tms32031State& cpu, MemoryBus& bus) {
    const uint32_t rs = cpu.rs, r1 = cpu.r[1], r2 = cpu.r[2], ar0 = cpu.ar[0], ar1 = cpu.ar[1], ar2 = cpu.ar[2];
    cpu.r[0] = 0;
    cpu.rs = 0;
    CharReader rd{cpu, bus};
    for (;;) {
        cpu.ar[0] = rd.next();
        if (cpu.ar[0] == 0) break;
        if (cpu.ar[0] != 0x20) {
            // 07734  R1 = g2 − g1 + (int16)g0 + (g0 >> 16)
            cpu.ar[0] -= 0x30;
            cpu.ar[1] = cpu.ar[0] * 4 + bus.readWord(cpu.ar[4] + 0xA);
            const uint32_t g0 = bus.readWord(cpu.ar[1]);
            cpu.r[1] = bus.readWord(cpu.ar[1] + 2) - bus.readWord(cpu.ar[1] + 1);
            cpu.r[1] += lowSigned(g0);
            cpu.r[1] += g0 >> 16;
        }
        cpu.r[0] += cpu.r[1];  // o espaço repete a última largura
    }
    cpu.ar[2] = ar2;
    cpu.ar[1] = ar1;
    cpu.ar[0] = ar0;
    cpu.r[2] = r2;
    cpu.r[1] = r1;
    cpu.rs = rs;
}

void drawTextOverlays(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    const uint32_t ar4 = cpu.ar[4], ar5 = cpu.ar[5], r4 = cpu.r[4], r7 = cpu.r[7];
    auto rd = [&](uint32_t a) { return bus.readWord(a); };
    auto wr = [&](uint32_t a, uint32_t v) { bus.writeWord(a, v); };
    cpu.ar[4] = rd(kListPtr);
    cpu.r[0] = rd(cpu.ar[4]);
    while (cpu.r[0] != 0) {
        // 07752  um texto
        cpu.ar[4] = cpu.r[0];
        const uint32_t n = cpu.ar[4];
        cpu.ar[2] = rd(n + 1);
        cpu.r[2] = tmsFix(tmsToDouble(rd(n + 3)));
        cpu.r[3] = tmsFix(tmsToDouble(rd(n + 4)));
        cpu.r[0] = rd(n + 8);
        if (cpu.r[0] & 0x100u) {
            subs.call(0x7720, cpu, bus);
            cpu.r[0] >>= 1;
            cpu.r[2] -= cpu.r[0];
        } else if (cpu.r[0] & 0x200u) {
            subs.call(0x7720, cpu, bus);
            cpu.r[2] -= cpu.r[0];
        }
        cpu.rs = 0;
        CharReader chars{cpu, bus};
        for (;;) {
            cpu.ar[0] = chars.next();
            if (cpu.ar[0] == 0) break;
            if (cpu.ar[0] == 0x2F) cpu.ar[0] = 0x40;  // '/' → '@'
            if (cpu.ar[0] == 0x20) {
                cpu.r[2] += cpu.r[7];  // 077A6: espaço = último avanço
                continue;
            }
            cpu.ar[0] -= 0x30;
            // 07771  modo e paleta
            cpu.r[0] = rd(n + 8);
            cpu.r[0] = (cpu.r[0] & 0x800u) ? ((cpu.r[0] & 0xFFu) | 0x0D00u) : 0x0900u;
            wr(kQuadMode, cpu.r[0]);
            cpu.r[1] = rd(n + 0xC);
            wr(0xEA35, cpu.r[1]);
            // 0777B  glyph
            cpu.ar[1] = cpu.ar[0] * 4 + rd(n + 0xA);
            const uint32_t g0 = rd(cpu.ar[1]);
            cpu.r[2] += lowSigned(g0);
            cpu.r[4] = rd(cpu.ar[1] + 3) << 8;
            const uint32_t h = rd(n + 9) << 8;
            cpu.r[0] = rd(cpu.ar[1] + 1) | cpu.r[4];
            wr(0xEA42, cpu.r[0]);
            wr(0xEA45, cpu.r[0] + h);
            cpu.r[0] = rd(cpu.ar[1] + 2) | cpu.r[4];
            wr(0xEA43, cpu.r[0]);
            cpu.r[1] = cpu.r[0] + h;
            wr(0xEA44, cpu.r[1]);
            wr(0xEA46, rd(n + 0xB));
            cpu.r[7] = rd(cpu.ar[1] + 2) - rd(cpu.ar[1] + 1);
            // 07796  vértices
            wr(0xEA36, cpu.r[2]);
            wr(0xEA3F, cpu.r[2]);
            wr(0xEA39, cpu.r[2] + cpu.r[7]);
            wr(0xEA3C, cpu.r[2] + cpu.r[7]);
            wr(0xEA37, cpu.r[3]);
            wr(0xEA3A, cpu.r[3]);
            wr(0xEA3D, cpu.r[3] + rd(n + 9));
            wr(0xEA40, cpu.r[3] + rd(n + 9));
            subs.call(0xAED6, cpu, bus);
            // 077A3  avanço = largura + espaço extra
            cpu.r[0] = g0 >> 16;
            cpu.r[7] += cpu.r[0];
            cpu.r[2] += cpu.r[7];
        }
        // 077A8  movimento
        if (rd(kFrozen) == 0) {
            wr(n + 3, doubleToTms(tmsToDouble(rd(n + 3)) + tmsToDouble(rd(n + 5))));
            wr(n + 4, doubleToTms(tmsToDouble(rd(n + 4)) + tmsToDouble(rd(n + 6))));
        }
        // 077B0  vida
        cpu.r[0] = rd(n + 7);
        cpu.r[1] = rd(n + 8);
        if (!(cpu.r[1] & 0x400u)) {
            cpu.r[0] -= 1;
            if (!(s(cpu.r[0]) > 0)) {
                // 077B6  tira da lista e devolve à lista livre
                cpu.r[7] = rd(n);
                cpu.ar[1] = rd(kListPtr);
                while (rd(cpu.ar[1]) != n) cpu.ar[1] = rd(cpu.ar[1]);
                wr(cpu.ar[1], rd(n));
                wr(kFreeCount, rd(kFreeCount) + 1);
                cpu.ar[1] = rd(kFreePtr);
                wr(n, rd(cpu.ar[1]));
                wr(cpu.ar[1], n);
                cpu.r[0] = cpu.r[7];
                continue;
            }
        }
        wr(n + 7, cpu.r[0]);
        cpu.r[0] = rd(n);
    }
    cpu.r[7] = r7;
    cpu.r[4] = r4;
    cpu.ar[5] = ar5;
    cpu.ar[4] = ar4;
}

namespace {
/// Estilos de texto (`0x7676`–`0x771F`): altura, página de textura, paleta e fonte.
struct TextStyle {
    uint32_t single;      ///< Rotina que estiliza só AR0.
    uint32_t pair;        ///< Rotina que estiliza AR0 e AR1.
    uint32_t height;      ///< `+9`.
    uint32_t pagePtr;     ///< `+0xB` = `[pagePtr]`.
    uint32_t palSingle;   ///< AR2 da busca de paleta (variante simples): `[palSingle]`, ou o próprio valor se `palImmediate`.
    uint32_t palPair;     ///< AR2 da busca de paleta (variante dupla).
    bool palImmediate;    ///< `0x76D8`/`0x76E2`: AR2 = 0x42 imediato.
    uint32_t palFn;       ///< `0x9ECD` (slot de textura) ou `0x9EDB` (índice de paleta).
    uint32_t fontPtr;     ///< `+0xA` = `[fontPtr]`.
};

/// Estilos de texto: um por par de rotinas (simples, dupla) de `0x7676` a `0x7712`.
constexpr TextStyle kStyles[] = {
    {0x7676, 0x7680, 0x0C, 0x7643, 0xC6C4, 0xC6C5, false, 0x9ECD, 0xB087},
    {0x768E, 0x7698, 0x16, 0x7643, 0xC6C6, 0xC6C7, false, 0x9ECD, 0xB086},
    {0x76A7, 0x76B1, 0x28, 0x76A6, 0xC6C8, 0xC6C9, false, 0x9ECD, 0xB088},
    {0x76BF, 0x76C9, 0x2A, 0x7646, 0xC6CA, 0xC6CB, false, 0x9ECD, 0xB089},
    {0x76D8, 0x76E2, 0x0C, 0x7647, 0x0042, 0x0042, true,  0x9ECD, 0x76D7},
    {0x76F0, 0x76FA, 0x11, 0x7644, 0xC6CC, 0xC6CD, false, 0x9ECD, 0x7645},
    {0x7708, 0x7712, 0x06, 0x7641, 0xC6CE, 0xC6CF, false, 0x9EDB, 0x7642},
};

/// Copia o estilo @p st para o nó AR0 (e AR1, se @p pair): altura, página, fonte e paleta.
void applyStyle(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs, const TextStyle& st, bool pair) {
    cpu.r[0] = st.height;
    bus.writeWord(cpu.ar[0] + 9, cpu.r[0]);
    if (pair) bus.writeWord(cpu.ar[1] + 9, cpu.r[0]);
    cpu.r[0] = bus.readWord(st.pagePtr);
    bus.writeWord(cpu.ar[0] + 0xB, cpu.r[0]);
    if (pair) bus.writeWord(cpu.ar[1] + 0xB, cpu.r[0]);
    const uint32_t pal = pair ? st.palPair : st.palSingle;
    cpu.ar[2] = st.palImmediate ? pal : bus.readWord(pal);
    subs.call(st.palFn, cpu, bus);
    bus.writeWord(cpu.ar[0] + 0xC, cpu.r[0]);
    if (pair) bus.writeWord(cpu.ar[1] + 0xC, cpu.r[0]);
    cpu.r[0] = bus.readWord(st.fontPtr);
    bus.writeWord(cpu.ar[0] + 0xA, cpu.r[0]);
    if (pair) bus.writeWord(cpu.ar[1] + 0xA, cpu.r[0]);
}

} // namespace

void createText(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    // 07660  PUSH AR2, R2; aloca (0x8F01) da lista livre para a lista; [E91B] −= 1
    const uint32_t ar2 = cpu.ar[2], r2 = cpu.r[2];
    cpu.ar[2] = bus.readWord(kFreePtr);
    cpu.r[2] = bus.readWord(kListPtr);
    subs.call(0x8F01, cpu, bus);
    bus.writeWord(kFreeCount, bus.readWord(kFreeCount) - 1);
    cpu.r[2] = r2;
    cpu.ar[2] = ar2;
    // 0766C  o original não testa o carry: com o pool vazio, escreve no AR0 anterior
    const uint32_t n = cpu.ar[0];
    bus.writeWord(n + 1, cpu.ar[2]);
    bus.writeWord(n + 3, cpu.r[2]);
    bus.writeWord(n + 4, cpu.r[3]);
    bus.writeWord(n + 7, cpu.rc);
    cpu.r[0] = 0;
    bus.writeWord(n + 8, cpu.r[0]);
    cpu.r[0] = doubleToTms(0.0);
    bus.writeWord(n + 5, cpu.r[0]);
    bus.writeWord(n + 6, cpu.r[0]);
    applyStyle(cpu, bus, subs, kStyles[5], false);  // BU 076F0
}

void createShadowedText(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    // 07651  frente (AR1) e sombra (AR0: modo 0x800, x + 2, y + 2)
    const uint32_t ar2 = cpu.ar[2];
    createText(cpu, bus, subs);
    cpu.ar[2] = ar2;
    cpu.ar[1] = cpu.ar[0];
    createText(cpu, bus, subs);
    cpu.r[0] = 0x800;
    bus.writeWord(cpu.ar[0] + 8, cpu.r[0]);
    cpu.r[0] = doubleToTms(tmsToDouble(cpu.r[2]) + 2.0);
    bus.writeWord(cpu.ar[0] + 3, cpu.r[0]);
    cpu.r[0] = doubleToTms(tmsToDouble(cpu.r[3]) + 2.0);
    bus.writeWord(cpu.ar[0] + 4, cpu.r[0]);
}

void showVolume(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    // 07594  [E56D]: quadros restantes da indicação de volume
    cpu.r[0] = bus.readWord(kVolumeTimer);
    if (cpu.r[0] == 0) return;
    cpu.r[0] -= 1;
    bus.writeWord(kVolumeTimer, cpu.r[0]);
    cpu.r[0] = bus.readWord(0xC8C5) & 0xFu;
    if (cpu.r[0] == 1) return;
    // 0759C  número = FIX(item 8 da CMOS × 0,117645) em [B122], na posição (180, 310)
    cpu.ar[2] = 8;
    subs.call(0x99E9, cpu, bus);
    cpu.r[2] = doubleToTms(static_cast<double>(s(cpu.r[0])) * shortFloat(0xC70F));
    cpu.r[2] = tmsFix(tmsToDouble(cpu.r[2]));
    cpu.ar[2] = bus.readWord(kVolumeDigits);
    subs.call(0xA762, cpu, bus);
    cpu.r[2] = doubleToTms(180.0);
    cpu.r[3] = doubleToTms(310.0);
    cpu.rc = 1;
    subs.call(0x7651, cpu, bus);
    subs.call(0x7450, cpu, bus);
    // 075A8  rótulo [B123] em (60, 310)
    cpu.ar[2] = bus.readWord(kVolumeLabel);
    cpu.r[2] = doubleToTms(60.0);
    cpu.r[3] = doubleToTms(310.0);
    cpu.rc = 1;
    subs.call(0x7651, cpu, bus);
    subs.call(0x7450, cpu, bus);
}

void registerTextOverlay(SubroutineDispatcher& d) {
    d.add(0x7720, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { textWidth(c, b); });
    d.add(0x774A, drawTextOverlays);
    d.add(0x7660, createText);
    d.add(0x765F, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        c.rc = 1;
        createText(c, b, s);
    });
    d.add(0x7651, createShadowedText);
    for (const TextStyle& st : kStyles) {
        d.add(st.single, [&st](Tms32031State& c, MemoryBus& b, ISubroutines& s) { applyStyle(c, b, s, st, false); });
        d.add(st.pair, [&st](Tms32031State& c, MemoryBus& b, ISubroutines& s) { applyStyle(c, b, s, st, true); });
    }
    // 07450  paleta [B120] (0x9EDB) em AR0 + 0xC e AR1 + 0xC
    d.add(0x7450, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        c.ar[2] = b.readWord(0xB120);
        s.call(0x9EDB, c, b);
        b.writeWord(c.ar[0] + 0xC, c.r[0]);
        b.writeWord(c.ar[1] + 0xC, c.r[0]);
    });
    d.add(0x7594, showVolume);
}

} // namespace rea::core::video
