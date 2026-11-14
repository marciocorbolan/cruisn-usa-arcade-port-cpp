#pragma once
/**
 * @file service_ctx.hpp
 * @brief Contexto comum das rotinas do menu de serviço (`0x05F6`–`0x154C`).
 *
 * @details O menu de serviço não roda como processo nem volta ao loop principal: ele desenha
 * direto na página exibida e espera ocupado — por quadros (`0x4EC7`, contador `[C930]` da INT0),
 * pelas teclas já filtradas pela INT0 (`[C8C9]`, ativas em 0) ou por laços de contagem
 * (`RPTS`/`NOP`, contadores de repetição de tecla). O port conta essas esperas em ciclos e as
 * entrega a um IBusyWait, que roda os quadros (INT0/INT3) quando o tempo de um quadro passa.
 *
 * Se a espera for interrompida (RESET pedido ou fim da execução), as rotinas lançam Aborted.
 */

#include <cstdint>

#include "core/busy_wait.hpp"
#include "core/memory_bus.hpp"
#include "core/subroutines.hpp"
#include "core/tms32031_state.hpp"

namespace rea::core::service {

/// Endereços usados pelo menu.
namespace vars {
inline constexpr uint32_t kFrameCounter = 0xC930;  ///< incrementado pela INT0
inline constexpr uint32_t kKeys = 0xC8C9;          ///< entradas filtradas (IN0 nos bits 0–15, IN1 nos 16–31), ativas em 0
inline constexpr uint32_t kDips = 0xC8CC;          ///< DIPs filtradas
inline constexpr uint32_t kGameMode = 0xC8C5;  ///< Modo do jogo: bits 0–3 = modo; 0x40 e 0x100 = flags.
inline constexpr uint32_t kLampMask = 0xC8CD;      ///< lâmpadas (`0x4E4F`)
inline constexpr uint32_t kBackColor = 0xC938;     ///< cor de fundo da limpeza de página
inline constexpr uint32_t kScrollTop = 0xEBDE;     ///< primeiro item visível do menu
inline constexpr uint32_t kDrawPage = 0xCE13;  ///< Endereço da página de desenho do software.
inline constexpr uint32_t kPrinterFlag = 0xEBBB;   ///< 0 = auditorias pelo menu; 1 = pela sequência secreta
inline constexpr uint32_t kNumberBuf = 0x0723;     ///< `[0723]` = 0xEBBC, texto de números
} // namespace vars

/// Teclas em `[C8C9]` (bit em 0 = apertada).
namespace keys {
inline constexpr uint32_t kEnter = 0x0010;    ///< chave de teste
inline constexpr uint32_t kService = 0x0040;  ///< Botão de serviço.
inline constexpr uint32_t kDown = 0x0100;     ///< volume −
inline constexpr uint32_t kUp = 0x0200;       ///< volume +
} // namespace keys

/// Espera interrompida (RESET ou fim da execução): a rotina é abandonada.
struct Aborted {};

/**
 * @brief Estimativas de ciclos (1 ciclo = 1 instrução de um ciclo; 431 600 por quadro).
 * @details Desvios não atrasados custam 4 ciclos no TMS320C31.
 */
namespace cycles {
inline constexpr uint32_t kPoll = 4096;          ///< passo de espera em laços de leitura de `[C8C9]`
inline constexpr uint32_t kRepeatLoop = 9;       ///< `0x0AB0`–`0x0AB5` / `0x0FB1`–`0x0FB6`: SUBI, BLT, LDI, AND, CMPI, BNE
inline constexpr uint32_t kTextChar = 1000;     ///< por caractere da `0xA7F4`
inline constexpr uint32_t kLinePixel = 30;      ///< por pixel da `0xAC23`
inline constexpr uint32_t kRectPixel = 2;       ///< por pixel da `0xA7E5`
} // namespace cycles

/**
 * @brief Acesso à máquina para as rotinas do menu.
 */
class Ctx {
public:
    /// @param c Registradores. @param b Barramento. @param s Sub-rotinas traduzidas. @param wait Espera ocupada que roda os quadros.
    Ctx(Tms32031State& c, MemoryBus& b, ISubroutines& s, IBusyWait& wait) : cpu(c), bus(b), subs(s), wait_(wait) {}

    /// Lê a palavra no endereço @p a.
    uint32_t rd(uint32_t a) const { return bus.readWord(a); }
    /// Grava a palavra @p v no endereço @p a.
    void wr(uint32_t a, uint32_t v) { bus.writeWord(a, v); }
    /// @brief Chama a rotina @p addr. As de desenho gastam antes o tempo estimado (ver drawCycles()).
    void call(uint32_t addr) {
        if (const uint64_t n = drawCycles(addr)) burn(n);
        subs.call(addr, cpu, bus);
    }
    /**
     * @brief Ciclos estimados de uma rotina de desenho com os registradores atuais (0 = não é de desenho).
     * @details O menu desenha pela CPU, pixel a pixel, na página exibida; o tempo disso decide o que
     * um toque na tecla alcança (as telas testam o nível de `[C8C9]`). Aproximações:
     * - `0xA7F4` texto: 1000 ciclos por caractere (7 linhas × 8 bits, `0xA824` nos acesos);
     * - `0xAC23` linha: 30 por pixel; `0xABFC` moldura: 4 linhas; `0xA824` pixel: 25;
     * - `0xA7E5` retângulo: 2 por pixel; `0x8E79` duas páginas: 262 144; `0x8E7D` uma: 131 072;
     * - `0x8E98` preenchimento: 1 por palavra.
     */
    uint64_t drawCycles(uint32_t addr) const;
    /// Palavra 32 bits como inteiro com sinal.
    static int32_t s32(uint32_t v) { return static_cast<int32_t>(v); }

    /// @brief Gasta @p n ciclos em espera ocupada (as interrupções seguem). Lança Aborted.
    void burn(uint64_t n);
    /// @brief Ver IBusyWait::feedWatchdog().
    void feedWatchdog() { wait_.feedWatchdog(); }
    /// @brief `0x4EC7`: espera o próximo VBLANK (`[C930]` mudar). R0 = valor anterior.
    void waitVblank();
    /// @brief `0x05F6`: espera ENTER apertada (`[C8C9]` & 0x10 == 0). R0 = `[C8C9]`.
    void waitEnterDown();
    /// @brief `0x05FA`: espera ENTER solta. R0 = `[C8C9]`.
    void waitEnterUp();
    /// @brief Espera `([C8C9] & mask) == mask` (todas as teclas da máscara soltas). R0 = `[C8C9]` & mask.
    void waitReleased(uint32_t mask);

    /// @brief `0xA7F4` com R2 = x, R3 = y, AR2 = ponteiro do texto (RC = cor já carregada).
    void text(uint32_t x, uint32_t y, uint32_t str) {
        cpu.r[2] = x;
        cpu.r[3] = y;
        cpu.ar[2] = str;
        call(0xA7F4);
    }
    /// @brief `0xA7F4` com o ponteiro lido de @p ptrAddr e a cor @p color em RC.
    void textAt(uint32_t x, uint32_t y, uint32_t ptrAddr, uint32_t color) {
        cpu.rc = color;
        text(x, y, rd(ptrAddr));
    }
    /// @brief Carrega AR2, R2, R3, RC, RS e chama @p routine (`0xAC23` linha, `0xA7E5` retângulo cheio,
    ///        `0xABFC` moldura).
    void shape(uint32_t routine, uint32_t ar2, uint32_t r2, uint32_t r3, uint32_t rc, uint32_t rs) {
        cpu.ar[2] = ar2;
        cpu.r[2] = r2;
        cpu.r[3] = r3;
        cpu.rc = rc;
        cpu.rs = rs;
        call(routine);
    }
    /// @brief `0xA762`: R2 → texto decimal em `[0723]`; AR2 = `[0723]`.
    void number(uint32_t value) {
        cpu.r[2] = value;
        cpu.ar[2] = rd(vars::kNumberBuf);
        call(0xA762);
    }
    /// @brief `0x99E9`: R0 = item @p item da CMOS.
    uint32_t item(uint32_t item) {
        cpu.ar[2] = item;
        call(0x99E9);
        return cpu.r[0];
    }
    /// @brief `0x99EE`: grava R2 = @p value no item @p item da CMOS (com a soma de verificação).
    void setItem(uint32_t item, uint32_t value) {
        cpu.r[2] = value;
        cpu.ar[2] = item;
        call(0x99EE);
    }
    /// @brief `0x9211` (comando de som em AR2).
    void sound(uint32_t cmd) {
        cpu.ar[2] = cmd;
        call(0x9211);
    }
    /// @brief `0x91E0` (comando de som em AR2, outra fila).
    void sound2(uint32_t cmd) {
        cpu.ar[2] = cmd;
        call(0x91E0);
    }

    Tms32031State& cpu;  ///< Registradores da CPU TMS320C31.
    MemoryBus& bus;  ///< Barramento de memória simulado.
    ISubroutines& subs;  ///< Despachante das sub-rotinas traduzidas.

private:
    IBusyWait& wait_;  ///< Espera ativa usada nos laços de polling do hardware.
};

} // namespace rea::core::service
