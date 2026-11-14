/**
 * @file test_basics.cpp
 * @brief Fundamentos: intercalação das EPROMs, float e LSH do TMS320C3x, loop de passo fixo, textos, aritmética inteira e entradas roteirizadas.
 */
#include "test_support.hpp"

using namespace rea::core;
using namespace rea::test;

namespace {

/// Intercalação independente de endianness
void testInterleave() {
    std::array<std::vector<uint8_t>, 4> lanes{{{0xD5}, {0x4A}, {0x00}, {0x00}}};
    check(MemoryBus::interleave(lanes)[0] == 0x00004AD5u, "interleave u10=D0-D7 ... u13=D24-D31");
}

/// Float TMS320C3x
void testTmsFloat() {
    check(tmsToDouble(0x80000000u) == 0.0, "TMS 0x80000000 == 0.0");
    check(tmsToDouble(0x00000000u) == 1.0, "TMS 0x00000000 == 1.0");
    check(tmsToDouble(0x00800000u) == -2.0, "TMS 0x00800000 == -2.0");
    check(tmsToDouble(0xFF800000u) == -1.0, "TMS 0xFF800000 == -1.0");
    check(tmsToDouble(0x01400000u) == 3.0, "TMS 0x01400000 == 3.0");
    bool roundtrip = true;
    for (double v : {1.0, -1.0, 0.5, -0.75, 3.14159, -1234.5, 1e-6, 65536.0})
        roundtrip &= std::fabs(tmsToDouble(doubleToTms(v)) - v) <= std::fabs(v) * 1e-6;
    check(roundtrip, "TMS float round-trip");
}

/// Loop de passo fixo
void testFixedStepLoop() {
    FakeClock clock;
    FakeInput input;
    GameLoop loop(clock, input, 60.0);
    uint64_t ticks = 0, vblanks = 0;
    loop.onTick([&](const ArcadeInputState&, uint64_t) { ++ticks; });
    loop.onVblank([&](uint64_t) { ++vblanks; });
    check(loop.run(120) == 120 && ticks == 120 && vblanks > 0, "GameLoop executa 120 quadros fixos");
    check(std::fabs(clock.t - 2.0) < 0.05, "120 quadros @60Hz ~ 2.0 s simulados");

    // Host lento: cada quadro custa 2 quadros de tempo real -> ~50 % da velocidade, avisado uma vez.
    FakeClock slow_clock;
    GameLoop slow(slow_clock, input, 60.0);
    slow.onTick([&](const ArcadeInputState&, uint64_t) { slow_clock.t += 2.0 / 60.0; });
    int warnings = 0;
    double speed = 0.0;
    slow.onSlow([&](double v) {
        ++warnings;
        speed = v;
    });
    slow.run(60 * 30);
    // Host rápido: nenhum aviso.
    FakeClock fast_clock;
    GameLoop fast(fast_clock, input, 60.0);
    int fast_warnings = 0;
    fast.onSlow([&](double) { ++fast_warnings; });
    fast.run(60 * 30);
    check(warnings == 1 && speed > 0.45 && speed < 0.55 && fast_warnings == 0,
          "GameLoop: host lento (~50 %) avisado uma vez; host em tempo real não avisa");
}

/// Textos por chave (LocalizationManager)
void testLocalization() {
    rea::ui::LocalizationManager i18n(std::string(REA_SOURCE_DIR) + "/assets/lang");
    check(i18n.setLanguage("en"), "carrega en (idioma original)");
    check(i18n.tr("INSERT_COINS") == "INSERT COINS", "en INSERT_COINS");
    check(i18n.tr("NAO_EXISTE") == "[NAO_EXISTE]", "chave ausente -> [chave]");
    check(!i18n.setLanguage("pt-BR") && i18n.tr("INSERT_COINS") == "INSERT COINS",
          "idioma sem arquivo .lang (pt-BR) -> cai para en");
    // Arquivo salvo com BOM UTF-8 (Bloco de Notas): a primeira chave continua valendo.
    const auto dir = tempPath("lang_bom");
    std::filesystem::create_directories(dir);
    { std::ofstream(dir / "xx.lang", std::ios::binary) << "\xEF\xBB\xBFINSERT_COINS = FICHAS\r\nSTART = INICIAR\r\n"; }
    rea::ui::LocalizationManager bom(dir);
    const bool ok = bom.setLanguage("xx") && bom.tr("INSERT_COINS") == "FICHAS" && bom.tr("START") == "INICIAR";
    std::filesystem::remove_all(dir);
    check(ok, "arquivo .lang com BOM UTF-8 e CRLF: primeira chave lida");
}

/// LSH do TMS320C3x
void testTmsLsh() {
    check(tmsLsh(0x80000001u, 0xFFFFFFE1u) == 1u && tmsLsh(0xFFFFFFFFu, 0xFFFFFFE0u) == 0u &&
              tmsLsh(1u, 32) == 0u && tmsLsh(1u, 0x7F) == 0u /* 7 bits: -1 */,
          "LSH: contagem de 7 bits com sinal; |n| >= 32 dá 0");
}

/// Aritmética inteira (0xA531 / 0xA5A9 / 0x8EB7) e MPYI
void testIntegerMath() {
    {
        auto u = [](int32_t v) { return static_cast<uint32_t>(v); };
        check(math::divI(u(7), u(2)) == 3 && math::divI(u(-7), u(2)) == u(-3) && math::divI(u(7), u(-2)) == u(-3) &&
                  math::divI(0x80000000u, 1) == 0x80000000u,
              "0xA531: divisão truncada com sinal (como DIV_I da TI)");
        check(math::modI(u(-7), u(2)) == u(-1) && math::modI(u(7), u(-2)) == 1 && math::modI(u(123), u(10)) == 3,
              "0xA5A9: resto com o sinal do dividendo");
        check(tmsMpyi(0x800000u, 2) == 0xFF000000u && tmsMpyi(0xFF000003u, 5) == 15,
              "MPYI: só os 24 bits baixos, com sinal");
        MemoryBus mb;
        Tms32031State c;
        c.r[1] = 0x55;
        mb.writeWord(math::kRandomSeed, 1);
        math::random(c, mb);
        const uint32_t r1 = c.r[0];
        mb.writeWord(math::kRandomSeed, 0x80000000u);
        math::random(c, mb);
        check(r1 == 2 * 0x794F && c.r[0] == 0x794F && mb.readWord(math::kRandomSeed) == 0x794F && c.r[1] == 0x55,
              "0x8EB7: x << 1 (| 1 se x ^ (x << 1) < 0), vezes 0x794F; R1 preservado");
    }
}

/// Entrada roteirizada (--press)
void testInputScript() {
    {
        const auto acts = io::ScriptedInput::parse("coin1@10-12,gas@20-30:128,start@5,bad,wheel@9-3");
        ArcadeInputState s0{}, s1{}, s2{};
        io::ScriptedInput::apply(acts, 11, s0);
        io::ScriptedInput::apply(acts, 25, s1);
        io::ScriptedInput::apply(acts, 5, s2);
        check(acts.size() == 3 && s0.coin1 && !s0.start && s1.gas == 128 && !s1.coin1 && s2.start,
              "ScriptedInput: ação@início-fim[:valor]; itens inválidos ignorados");
    }
    check(io::ScriptedInput::firstInvalid("coin1@10-12,gas@20-30:128,start@5").empty() &&
              io::ScriptedInput::firstInvalid("coin1@10-12,gaz@20-30:128") == "gaz@20-30:128" &&
              io::ScriptedInput::firstInvalid("start@5,bad") == "bad" &&
              io::ScriptedInput::firstInvalid("wheel@9-3") == "wheel@9-3" && io::ScriptedInput::firstInvalid("").empty(),
          "ScriptedInput::firstInvalid: aponta ação desconhecida, item sem @ e faixa invertida (o --press recusa)");
}

} // namespace

void runBasicsTests() {
    testInterleave();
    testTmsFloat();
    testFixedStepLoop();
    testLocalization();
    testTmsLsh();
    testIntegerMath();
    testInputScript();
}
