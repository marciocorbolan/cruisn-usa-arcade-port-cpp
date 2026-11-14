#pragma once
/**
 * @file dcs_program.hpp
 * @brief Programa do ADSP-2105 da placa DCS do Cruis'n USA, traduzido para C++.
 *
 * @details Três partes, todas lidas de `cusa.u2` (ver `tools/extract_dcs.py`):
 *
 * - **Autoteste** (página de boot do banco 0, `dcs_selftest.cpp`): espera ~0,25 s por um byte da CPU
 *   principal (que pula o teste); senão testa a RAM externa e soma as ROMs, toca um bipe (bom) ou N bipes
 *   (código de erro) pelo SPORT1 e faz o boot do banco 1.
 * - **Residente** (página de boot do banco 1, `dcs_resident.cpp`): carrega os overlays, recebe comandos pela
 *   IRQ2, interpreta os roteiros dos 4 canais e, a cada quadro de 240 amostras, decodifica os fluxos
 *   comprimidos (transformada de 128 pontos), mistura, aplica o janelamento e escreve na metade livre do
 *   buffer de saída.
 * - **Overlays** (`dcs_overlays.cpp`): inicialização (banco 0 + 0x800, chamado uma vez) e o sistema principal
 *   (banco 2, tabela de saltos em `0x0800`: `0x0803` início de fluxo, `0x0853` quadro de um canal, `0x0A7E`
 *   atualização por quadro).
 *
 * Cada função leva o endereço da rotina original no nome; os comentários trazem o endereço de cada trecho.
 *
 * **Variáveis do residente** (DM):
 *
 * | Endereço | Uso |
 * |---|---|
 * | `0x0340`/`0x0350`/`0x0360`/`0x0370` + 4k + canal | rampas de parâmetro: passos restantes, valor, incremento, alvo |
 * | `0x0380` + 2·canal | ponteiro do roteiro do canal (24 bits em 2 palavras; 0 = livre) |
 * | `0x0388` / `0x0390` + 2·canal | ponteiro atual / inicial do fluxo comprimido |
 * | `0x0398` + canal | repetições restantes do fluxo |
 * | `0x03A0` + canal | volume do canal (rampa 0) |
 * | `0x03A8` + canal | quadros desde o último passo do roteiro |
 * | `0x03AC` / `0x03B0` + canal | quadros restantes / total do fluxo |
 * | `0x03B4` + canal | canal dono (0xFFFF = nenhum) |
 * | `0x03B8` + 2·canal | tabela de bandas do fluxo (banco, deslocamento) |
 * | `0x03C0`… / `0x03F0` + canal | pilha de laços do roteiro / topo |
 * | `0x03FC` / `0x03FD` | fila de comandos (escrita / leitura), 16 entradas em `0x3910` |
 * | `0x03FE` | estado do receptor de bytes (IRQ2) |
 * | `0x03FF` | canal atual |
 * | `0x0400–0x05DF` | buffer de saída (480 amostras, I7 em autobuffer) |
 * | `0x05F0` / `0x05F1` | volume geral pedido / efetivo |
 * | `0x05F2` | alvo do par de volume (`0x55AA` + n) |
 * | `0x05F3`–`0x05FB` | fluxo PCM direto (`0x55CC`) |
 * | `0x05FC`–`0x05FF` | volume pedido por canal |
 * | `0x0600` | canais já processados no quadro |
 * | `0x0608` | quadros desde o último byte recebido (13 = reinicia o receptor) |
 * | `0x0609` | início da metade do buffer de saída a escrever |
 * | `0x060C` | número de sons da tabela |
 */

#include <cstdint>

#include "core/audio/dcs/adsp_core.hpp"

namespace rea::core::audio::dcs {

class DcsBoard;

/// @brief Programa do ADSP-2105 traduzido: autoteste, residente e overlays (ver o cabeçalho do arquivo).
class DcsProgram {
public:
    /// @param cpu Registradores do DSP. @param board Placa (memórias, trinco, SPORT1).
    DcsProgram(AdspCore& cpu, DcsBoard& board) : c(cpu), b(board) {}

    /// Vetores instalados: decide qual rotina atende cada interrupção.
    enum class Vectors { None, SelfTest, Resident };
    /// @return Vetores instalados.
    Vectors vectors() const { return vectors_; }
    /// Instala os vetores @p v.
    void setVectors(Vectors v) { vectors_ = v; }

    /// @brief Ponto de entrada da página de boot do banco 0 (`0x0000` → `0x0022`). Não retorna.
    void selfTest();
    /// @brief Ponto de entrada da página de boot do banco 1 (`0x0000` → `0x0008`). Não retorna.
    void resident();

    /// @brief IRQ2 (vetor `0x0004`): byte no trinco de entrada.
    void irq2();
    /// @brief SPORT1 TX (vetor `0x0010`): volta do autobuffer.
    void sportTx();

private:
    // --- autoteste (banco 0) ---------------------------------------------------------
    /// `0x0130`: avisa a CPU (trinco 0x79, depois o código) e toca os bipes do código @p code.
    void st0130Report(uint16_t code);
    /// Falha do autoteste: reporta @p code e para (`0x0153`).
    [[noreturn]] void stFail(uint16_t code);
    /// `0x00FC`: testa a RAM de dados externa. @return false em erro.
    bool st00FCTestDm();
    /// `0x0108`: testa a RAM de programa externa. @return false em erro.
    bool st0108TestPm();
    /// `0x011B`: lê uma palavra de 16 bits (byte alto primeiro) do banco 0x24.
    void st011BReadWord();
    /// `0x0154`: preenche o buffer de saída com a onda quadrada do bipe.
    void st0154BeepFill();
    /// `0x016E`: IRQ2 do autoteste (byte da CPU pula o teste).
    void st016EIrq2();
    void stNopLoop(uint32_t outer, uint32_t inner);

    // --- residente (banco 1) -------------------------------------------------------------
    /// `0x0021`: laço principal do residente.
    [[noreturn]] void r0021MainLoop();
    /// `0x0025`: executa os comandos da fila.
    void r0025Commands();
    /// `0x0158`: amostra do canal PCM direto.
    void r0158PcmSample();
    /// `0x01AC`: quadro do fluxo comprimido do canal atual.
    void r01ACChannelStream();
    /// `0x01E3`: interpreta o roteiro do canal atual.
    void r01E3ChannelScript();
    /// `0x02F9`: move o topo da pilha de laços do roteiro em @p delta.
    void loopStack(uint16_t delta);
    /// `0x0313`: põe AR na fila de comandos.
    void r0313Enqueue();
    /// `0x031B`: parâmetros de uma rampa do roteiro.
    void r031BRampParams();
    /// `0x0325`: zera as 4 rampas do canal MR0.
    void r0325ClearRamps();
    /// `0x0332`: ponteiro de 24 bits → banco e endereço na janela de ROM.
    void r0332PointerToBank();
    /// `0x033E`: lê um byte da ROM e avança.
    void r033EReadByte();
    /// `0x0351`: lê uma palavra (byte alto primeiro).
    void r0351ReadWord();
    /// `0x0358`: lê um ponteiro de 3 bytes.
    void r0358ReadPointer();
    /// `0x035C`: banco e endereço → ponteiro de 24 bits.
    void r035CBankToPointer();
    /// `0x0364`: inicia o roteiro PX:AR no canal MR0.
    void r0364StartScript();
    /// `0x0383`: IRQ2 do residente (recebe bytes da CPU).
    void r0383Irq2();
    /// `0x03F1`: carrega um overlay da ROM na PM.
    void r03F1LoadOverlay();

    // --- overlays ------------------------------------------------------------------------
    /// `0x0800`: inicialização (overlay do banco 0).
    void o0800Init();
    /// `0x089F`: copia uma tabela do banco 3 para a DM.
    void o089FCopyTable();
    /// `0x0803`: início de um fluxo comprimido.
    void o0803StreamStart();
    /// `0x0853`: decodifica um quadro do fluxo do canal.
    void o0853StreamFrame();
    /// `0x0A27`: lê AY0 bits do reservatório.
    void o0A27Bits();
    /// `0x0A49`: decodifica um símbolo de Huffman.
    void o0A49Huffman();
    /// `0x0A6E`: ponteiro de uma tabela do banco 3.
    void o0A6ETablePointer();
    /// `0x0A7E`: atualização por quadro das rampas.
    void o0A7EFrameUpdate();

    // --- atalhos -------------------------------------------------------------------------
    /// `DM(a)` direto.
    uint16_t rd(uint32_t a) { return c.dmAt(a); }
    /// `DM(a) = v` direto.
    void wr(uint32_t a, uint16_t v) { c.dmAtW(a, v); }
    /// Gasta @p cycles ciclos na placa (amostras e interrupções).
    void burn(uint64_t cycles);
    /// Espera I7 (autobuffer) entrar na metade baixa (@p wantLow) ou alta do buffer de saída.
    void waitI7(bool wantLow);

    AdspCore& c;  ///< Registradores do DSP.
    DcsBoard& b;  ///< Placa.
    Vectors vectors_ = Vectors::None;  ///< Vetores instalados.
};

} // namespace rea::core::audio::dcs
