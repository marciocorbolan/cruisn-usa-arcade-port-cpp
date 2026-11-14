#pragma once
/**
 * @file vunit_hw.hpp
 * @brief Constantes de hardware da placa Midway V-Unit (Cruis'n USA).
 *
 * @details Fontes: mapa de memória do driver `src/mame/williams/midvunit.cpp`
 * (MAME 0.289) e valores lidos diretamente da ROM de programa. Todos os
 * endereços são de PALAVRA de 32 bits (barramento de 24 bits do TMS320C31).
 */

#include <cstdint>

namespace rea::core::vunit {

// ---------------------------------------------------------------------------
// Mapa de memória (endereços de palavra)
// ---------------------------------------------------------------------------
inline constexpr uint32_t kRamBase        = 0x000000; ///< RAM principal (programa copiado da ROM + dados).
inline constexpr uint32_t kRamWords       = 0x020000;  ///< 128 K palavras.
inline constexpr uint32_t kFastRamBase    = 0x400000; ///< RAM rápida.
inline constexpr uint32_t kFastRamWords   = 0x020000;  ///< 128 K palavras.
inline constexpr uint32_t kTmsPeriphBase  = 0x808000; ///< Periféricos internos do TMS320C31 (timers, bus control).
inline constexpr uint32_t kTmsPeriphWords = 0x000080;  ///< Registradores internos.
inline constexpr uint32_t kTmsRamBase     = 0x809800; ///< RAM interna do TMS320C31 (blocos RAM0 + RAM1).
inline constexpr uint32_t kTmsRamWords    = 0x000800;  ///< 2 K palavras (RAM0 + RAM1).
inline constexpr uint32_t kCmosBase       = 0x9C0000; ///< CMOS/NVRAM (ajustes e auditorias).
inline constexpr uint32_t kCmosWords      = 0x002000;  ///< 8 K palavras.
inline constexpr uint32_t kVideoRamBase   = 0x900000; ///< VRAM (MAME videoram, 16 bits por palavra): 2 páginas de 512×512.
inline constexpr uint32_t kVideoRamWords  = 0x080000; ///< Palavras da VRAM (2 páginas de 512 × 512).
inline constexpr uint32_t kVideoPageWords = 0x040000; ///< Página 1 em 0x940000.
inline constexpr uint32_t kVideoStride    = 512;      ///< Pixels por linha na VRAM.
inline constexpr uint32_t kPaletteBase    = 0x9E0000; ///< RAM de paleta.
inline constexpr uint32_t kPaletteWords   = 0x008000;  ///< 32 K entradas de 16 bits.
inline constexpr uint32_t kTextureRamBase = 0xA00000; ///< RAM de texturas (MAME textureram_w): 2 bytes por palavra.
inline constexpr uint32_t kTextureRamWords = 0x200000; ///< 0xA00000–0xBFFFFF → 4 MB de texels de 8 bits.
inline constexpr uint32_t kRomBase        = 0xC00000; ///< Janela de ROM "maindata" (0xC00000–0xFFFFFF).
inline constexpr uint32_t kRomWindowWords = 0x400000; ///< 4M palavras; o que não tem EPROM lê 0 (MAME: região zerada).
/// Bancos de EPROMs gráficas (cada um: 4 × 512 KB intercalados como o programa).
/// MAME `crusnusa`: u14–u17 no byte 0x200000 da região → palavra 0x80000 → 0xC80000;
/// u18–u21 → 0xD00000; u22–u25 → 0xD80000; u26–u29 → 0xE00000.
inline constexpr uint32_t kGraphicsRomOffset = 0x080000; ///< Palavra (relativa a kRomBase) do 1º banco gráfico.
inline constexpr uint32_t kGraphicsBankWords = 0x080000; ///< Palavras por banco gráfico.
inline constexpr uint32_t kGraphicsBanks     = 4;  ///< Bancos gráficos (u14–u29).

// ---------------------------------------------------------------------------
// Registradores internos do TMS320C31
// ---------------------------------------------------------------------------
inline constexpr uint32_t kTimer0Control  = 0x808020; ///< Timer 0: Global Control.
inline constexpr uint32_t kTimer0Period   = 0x808028; ///< Timer 0: Period.
inline constexpr uint32_t kBusControl     = 0x808064; ///< Primary Bus Control (wait states).

// ---------------------------------------------------------------------------
// I/O mapeado em memória do V-Unit
// ---------------------------------------------------------------------------
inline constexpr uint32_t kVideoCtrlBase  = 0x980020; ///< 12 registradores de timing de vídeo (0x980020–0x98002B).
inline constexpr uint32_t kVideoCtrlCount = 12;  ///< Registradores de timing de vídeo.
inline constexpr uint32_t kPageControl    = 0x980040; ///< Seleção de página de vídeo (double buffer).
inline constexpr uint32_t kVideoMisc      = 0x980080; ///< Registrador de vídeo sem efeito no MAME (bit 3 manipulado pela ROM).
inline constexpr uint32_t kVideoStatus    = 0x980082; ///< Status do vídeo: bits 0/1 = DMA/blitter ocupado, bit 2 = fila de quads ocupada.
inline constexpr uint32_t kIntcs         = 0x990000; ///< Status de interrupções (MAME intcs_r: sempre 4).
inline constexpr uint32_t kLinkPort      = 0x997000; ///< Placa de link: +0 dados (bits 16–31), +1 flags (bits 29–31).
inline constexpr uint32_t kLinkPortWords = 9;        ///< MAME comcs: 0x997000–0x997008.
inline constexpr uint32_t kPort1050       = 0x991050; ///< Porta de entrada não documentada (lida no RESET).
inline constexpr uint32_t kAdc            = 0x993000; ///< Conversor A/D (volante/pedais); dado nos bits 24–31.
inline constexpr uint32_t kControl        = 0x994000; ///< Controle: bit7 LED, bit3 watchdog, bit1 reset do som DCS.
inline constexpr uint32_t kCmosProtect    = 0x995020; ///< Proteção de escrita da CMOS (libera só com bits 0xC00).
inline constexpr uint32_t kSoundLatch     = 0x9A0000; ///< Comando para a placa de som DCS.

// ---------------------------------------------------------------------------
// Vetores de interrupção (palavras 0–4 da ROM/RAM)
// ---------------------------------------------------------------------------
inline constexpr uint32_t kVecReset = 0x00004AD5; ///< RESET.
inline constexpr uint32_t kVecInt0  = 0x00004C34; ///< INT0 = VBLANK (scanline 399). Confirmado.
inline constexpr uint32_t kVecInt2  = 0x00008090; ///< INT2 = ajuste fino do vtotal (sincronismo; provável link).
inline constexpr uint32_t kVecInt3  = 0x00004D6F; ///< INT3 = fim de conversão do A/D (filtro do volante).

// ---------------------------------------------------------------------------
// Timing de vídeo
// ---------------------------------------------------------------------------
/// Cristal de vídeo (MAME: MIDVUNIT_VIDEO_CLOCK = 33.333333 MHz); pixel clock = metade.
inline constexpr double kVideoClockHz = 33333333.0;
/// htotal lido da tabela de timing da ROM (registrador 6).
inline constexpr uint32_t kHTotal = 666;
/// vtotal lido da tabela de timing da ROM (registrador 11).
inline constexpr uint32_t kVTotal = 432;
/// Linha da interrupção de vídeo (registrador 0 & 0x1FF) — última linha visível.
inline constexpr uint32_t kVblankScanline = 399;
/// Instruções por segundo do TMS320C31 (cristal de 50 MHz, 1 instrução a cada 2 ciclos).
inline constexpr double kCpuInstructionsPerSecond = 25000000.0;
/// Taxa de quadros original: 16.666.666,5 / (666 × 432) ≈ 57,928 Hz.
inline constexpr double kRefreshHz = (kVideoClockHz / 2.0) / (kHTotal * kVTotal);
/// Instruções do TMS320C31 por quadro (~431 600).
inline constexpr uint32_t kInstructionsPerFrame = static_cast<uint32_t>(kCpuInstructionsPerSecond / kRefreshHz);

} // namespace rea::core::vunit
