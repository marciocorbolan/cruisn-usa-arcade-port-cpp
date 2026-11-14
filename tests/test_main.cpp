/**
 * @file test_main.cpp
 * @brief Executa a suíte de testes da camada agnóstica `rea_core`, área por área.
 */
#include "test_support.hpp"

using namespace rea::core;
using namespace rea::test;

int main() {
    runBasicsTests();
    runVideoTests();
    runSceneTests();
    runLinkTests();
    runProcessesTests();
    runIoTests();
    runAudioTests();
    runGameTests();

    // Testes com a ROM real (opcionais: só se a ROM de programa for encontrada)
    ResourceManager res(REA_SOURCE_DIR);
    if (auto dir = res.findRomDir("cusa-l41.u10")) {
        MemoryBus bus;
        check(bus.loadProgramRom(*dir, {"cusa-l41.u10", "cusa-l41.u11", "cusa-l41.u12", "cusa-l41.u13"}),
              "carrega ROM de programa");
        check(bus.vector(MemoryBus::Vector::Reset) == 0x4AD5u, "vetor RESET == 0x4AD5");
        check(bus.readPackedString(0x7456) == "INSERT COINS", "texto empacotado em 0x7456");

        runRomGameTests(dir);
        runRomSystemTests(dir, bus);
    } else {
        std::printf("[SKIP] ROM não extraída (tools/extract_rom.sh)\n");
    }

    runResourcesTests();
    runServiceTests();

    std::printf("%s (%d falha(s))\n", g_failures ? "FALHOU" : "SUCESSO", g_failures);
    return g_failures ? 1 : 0;
}
