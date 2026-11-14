/**
 * @file music_title.cpp
 * @brief Tradução de `0x90B9`–`0x90D2` (nome da música na tela).
 */
#include "core/game/music_title.hpp"

#include "core/game/process_ctx.hpp"

namespace rea::core::game {

namespace {

constexpr uint32_t kMusic = 0xE916;  ///< Música atual.
constexpr uint32_t kMusicTimer = 0xE914;  ///< Quadros até a música mudar de passo (a `0x90F3` começa com 0x28).
constexpr uint32_t kMusicTitles = 0x90B0;  ///< `[90B0]` = 0x90B1: um texto por música.
constexpr uint32_t kTitleObject = 0xC6B9;  ///< Objeto do título da música.
constexpr uint32_t kGameMode = 0xC8C5;  ///< Modo do jogo: bits 0–3 = modo; 0x40 e 0x100 = flags.

void titleFrame(ProcCtx& c);

/// `0x90C7`: na corrida conta `[E914]`; fora dela (ou em 0) morre.
void afterSleep(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[0] = c.rd(kGameMode) & 0xFu;
    if (cpu.r[0] != 4) {
        // 090D0
        cpu.r[0] = 0;
        c.wr(kMusicTimer, cpu.r[0]);
        return c.die();
    }
    cpu.r[0] = c.rd(kMusicTimer) - 1;
    c.wr(kMusicTimer, cpu.r[0]);
    if (static_cast<int32_t>(cpu.r[0]) <= 0) return c.die();
    titleFrame(c);
}

/// `0x90B9`
void titleFrame(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[2] = c.rd(kMusic);
    cpu.ar[2] += c.rd(kMusicTitles);
    cpu.ar[2] = c.rd(cpu.ar[2]);
    cpu.r[2] = doubleToTms(210.0);  // FLOAT $00D2
    cpu.r[3] = doubleToTms(370.0);  // FLOAT $0172
    cpu.rc = 1;
    c.call(0x7651);
    cpu.ar[2] = c.rd(kTitleObject);
    cpu.r[2] = 0xA0;
    cpu.r[3] = 0x168;
    cpu.r[4] = 0x900;
    c.call(0xAF48);
    c.sleep(1, 0x90C7);
}

} // namespace

void registerMusicTitle(SubroutineDispatcher& d) {
    addProc(d, 0x90B9, titleFrame);
    addProc(d, 0x90C7, afterSleep);
}

} // namespace rea::core::game
