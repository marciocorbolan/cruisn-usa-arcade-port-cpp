#pragma once
/**
 * @file dcs_board.hpp
 * @brief Placa de som DCS (ADSP-2105 a 10 MHz, variante `DCS_AUDIO_2K`/REV_DCS1 do MAME).
 *
 * @details A placa roda o programa traduzido (`DcsProgram`) numa corrotina própria. O tempo do DSP avança
 * só nas esperas do programa (`burn()`): no laço de autoteste, nas pausas dos bipes e na espera do ponteiro
 * de saída I7 no laço principal. O cálculo entre duas esperas conta como instantâneo; isso não muda o som,
 * porque o programa sempre espera a metade livre do buffer antes de escrever nela.
 *
 * **Memória** (vista pelo DSP):
 *
 * | Endereço | Conteúdo |
 * |---|---|
 * | PM `0x0000–0x03FF` | PM interna (página de boot) |
 * | PM `0x0800–0x0FFF` (espelhos até `0x1FFF`) | RAM externa de 2K × 24 bits |
 * | DM `0x0000–0x07FF` (espelho `0x1800`) | a mesma RAM externa: bits 8–23 (a escrita preserva o byte baixo) |
 * | DM `0x2000–0x2FFF` | janela de 4K da ROM no banco atual: `0xFF00 \| byte` |
 * | DM `0x3000` | seleção de banco (11 bits) |
 * | DM `0x3400` | leitura: trinco de entrada (reconhece a IRQ2); escrita: trinco de saída (ninguém lê) |
 * | DM `0x3800–0x39FF` | DM interna |
 * | DM `0x3FE0–0x3FFF` | registradores de controle (`0x3FEF` autobuffer, `0x3FF1` SCLKDIV, `0x3FFF` sistema) |
 *
 * **ROMs**: `cusa.u2`…`u9` (512 KB cada). Banco b: ROM `u(2 + (b >> 8))`, deslocamento (b & 0xFF) · 0x1000 se
 * (b & 0xFF) < 0x80; acima disso, 0xFF.
 *
 * **Trinco de comandos**: a CPU principal escreve um byte em `0x9A0000`; a IRQ2 fica ativa até o DSP ler
 * `DM(0x3400)`. O port guarda os bytes numa fila (no original a CPU principal espera o DSP ler cada byte),
 * e cada leitura retira um. O bit 1 de `0x994000` em 0 segura o DSP em reset; ao soltar, o DSP faz o boot
 * do banco 0 (autoteste).
 *
 * **Saída**: SPORT1 em autobuffer, uma palavra de 16 bits a cada 2 · (SCLKDIV + 1) · 16 ciclos (320 com
 * SCLKDIV = 9: 31 250 Hz). A cada volta do buffer circular a interrupção do SPORT1 (vetor `0x0010`) fica
 * pendente.
 */

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

#include "core/rom_source.hpp"
#include "core/audio/dcs/adsp_core.hpp"
#include "core/cpu_coroutine.hpp"
#include "core/memory_bus.hpp"

namespace rea::core::audio::dcs {

class DcsProgram;

/// Endereços do lado da CPU principal.
inline constexpr uint32_t kHostDataPort = 0x9A0000;     ///< byte para o trinco de entrada
inline constexpr uint32_t kHostControlPort = 0x994000;  ///< bit 1 = DCS fora de reset

/// Registradores de controle do ADSP-2105 em DM.
namespace ctrl {
/**
 * @name Registradores mapeados em memória de dados (manual do ADSP-2100, apêndice E)
 * Endereços em DM dos registradores de controle usados pelo programa DCS.
 */
///@{
inline constexpr uint32_t kAutobuffer = 0x3FEF; ///< Registrador de controle do ADSP-2105 mapeado em DM.
inline constexpr uint32_t kSclkdiv1 = 0x3FF1;
inline constexpr uint32_t kSport1Ctrl = 0x3FF2;
inline constexpr uint32_t kWaitStates = 0x3FFE;
inline constexpr uint32_t kSysControl = 0x3FFF; ///< System Control Register (boot, wait states da PM).
///@}
} // namespace ctrl

/// Desvio não local: novo boot a partir de um banco (SYSCONTROL bit 9 ou reset).
struct Reboot {
    uint16_t bank;  ///< Banco da página de boot.
};

/**
 * @brief Placa de som DCS.
 */
class DcsBoard final : public IIoDevice, private IAdspMemory {
public:
    static constexpr uint32_t kClockHz = 10000000;  ///< Clock do ADSP-2105 (10 MHz).
    static constexpr uint32_t kRomBytes = 0x80000;  ///< Tamanho de cada EPROM de som (512 KB).
    static constexpr int kRomCount = 8;  ///< EPROMs de som (u2–u9).

    DcsBoard();
    ~DcsBoard() override;
    DcsBoard(const DcsBoard&) = delete;
    DcsBoard& operator=(const DcsBoard&) = delete;

    /**
     * @brief Carrega `cusa.u2`…`cusa.u9`.
     * @return true se as 8 ROMs foram lidas com 512 KB cada.
     */
    bool loadRoms(const std::filesystem::path& dir);
    /// @brief Igual, lendo de uma pasta ou de dentro do `.zip` (RomSource).
    bool loadRoms(const RomSource& src);
    /// @brief Instala as ROMs já montadas (8 × 512 KB; testes).
    void setRoms(std::vector<uint8_t> roms) { rom_ = std::move(roms); }
    /// @return true se as 8 ROMs de som foram carregadas.
    bool loaded() const { return rom_.size() == static_cast<std::size_t>(kRomBytes) * kRomCount; }

    // --- lado da CPU principal -----------------------------------------------------
    bool ioRead(uint32_t, uint32_t&) override { return false; }
    void ioWrite(uint32_t addr, uint32_t value) override;
    /// @brief Escreve um byte no trinco de entrada (IRQ2).
    void writeData(uint8_t b) {
        latch_.push_back(b);
        ++bytes_received_;
        // REA_DCS_TRACE=1: registra cada byte recebido (depuração).
        if (std::getenv("REA_DCS_TRACE")) std::fprintf(stderr, "[DCS] byte %02X partidas=%u quadros=%llu rodando=%d\n", b, boots_, (unsigned long long)frames_decoded_, (int)running_);
    }
    /// @brief Segura (true) ou solta (false) o reset do DSP.
    void setReset(bool asserted);

    /**
     * @brief Avança o DSP pelo tempo de @p samples amostras de saída e acrescenta-as a @p out.
     * @details Sem ROM, em reset ou sem o SPORT1 ligado, as amostras são 0.
     */
    void run(std::size_t samples, std::vector<int16_t>& out);

    /// Taxa de saída atual em Hz (31 250 com SCLKDIV = 9).
    double sampleRate() const { return static_cast<double>(kClockHz) / cyclesPerSample(); }

    // --- diagnóstico / testes ---------------------------------------------------------
    /// @return Boots feitos desde a criação.
    uint32_t boots() const { return boots_; }
    /// @return Banco do último boot.
    uint16_t lastBootBank() const { return last_boot_bank_; }
    /// @return Quadros comprimidos decodificados.
    uint64_t framesDecoded() const { return frames_decoded_; }
    /// Lê a DM do DSP (testes).
    uint16_t peekDm(uint32_t a) { return dmRead(a & 0x3FFF); }
    /// @return Bytes ainda no trinco de entrada.
    std::size_t latchPending() const { return latch_.size(); }
    /// @return Bytes recebidos da CPU principal.
    uint64_t bytesReceived() const { return bytes_received_; }

private:
    friend class DcsProgram;

    /// @name IAdspMemory: mapa de memória do DSP (tabela no início do arquivo)
    /// @{
    uint16_t dmRead(uint32_t addr) override;  ///< Lê DM pelo mapa da placa.
    void dmWrite(uint32_t addr, uint16_t value) override;  ///< Grava DM pelo mapa da placa.
    uint32_t pmRead(uint32_t addr) override;  ///< Lê PM pelo mapa da placa.
    void pmWrite(uint32_t addr, uint32_t value) override;  ///< Grava PM pelo mapa da placa.
    /// @}

    // --- usado pelo programa traduzido ----------------------------------------------------
    /// @brief Gasta @p cycles ciclos (amostras saem, interrupções são atendidas). Lança Reboot no reset.
    void burn(uint64_t cycles);
    /// @brief Espera até a próxima amostra sair (I7 muda) — passo das esperas por I7.
    void burnToNextSample() { burn(cycles_to_sample_); }
    /// @brief Carrega a página de boot do banco @p bank na PM interna; devolve o número de palavras.
    uint32_t loadBootPage(uint16_t bank);
    /// @brief Byte @p offset do banco atual (como lido em `DM(0x2000 + offset)`, sem o 0xFF00).
    uint8_t romByte(uint16_t bank, uint32_t offset) const;
    /// @return Banco atual das ROMs.
    uint16_t bank() const { return bank_; }
    /// @brief `TX1 = v`: primeira palavra do SPORT1 (o autobuffer busca as seguintes).
    void writeTx1(uint16_t v) {
        tx1_ = v;
        tx1_loaded_ = true;
    }
    /// @brief `IFC`: limpa as interrupções de borda pendentes (bits 0–5).
    void clearInterrupts(uint16_t ifc) {
        if (ifc & 0x04) sport_irq_ = false;
    }

    /// Ciclos por amostra: 2 · (SCLKDIV + 1) · 16 bits.
    uint32_t cyclesPerSample() const { return 2u * (static_cast<uint32_t>(ctrl_[ctrl::kSclkdiv1 - 0x3FE0] & 0x3FFF) + 1u) * 16u; }
    /// Envia uma amostra pelo SPORT1 (autobuffer) e marca a interrupção de TX.
    void emitSample();
    /// Atende IRQ2 (byte no trinco) e SPORT1 TX, se habilitadas.
    void serviceInterrupts();
    /// Corpo da corrotina do DSP: boot e execução do programa traduzido, recomeçando a cada Reboot.
    void body(CpuCoroutine& co);

    AdspCore cpu_;  ///< Registradores do ADSP-2105.
    std::unique_ptr<DcsProgram> program_;  ///< Programa traduzido.
    std::unique_ptr<CpuCoroutine> co_;  ///< Corrotina que executa o programa.
    std::vector<uint8_t> rom_;  ///< ROMs de som (8 × 512 KB).
    std::array<uint32_t, 0x400> iram_pm_{};  ///< PM interna
    std::array<uint32_t, 0x800> ext_ram_{};  ///< RAM externa (24 bits)
    std::array<uint16_t, 0x200> iram_dm_{};  ///< DM interna 0x3800–0x39FF
    std::array<uint16_t, 0x20> ctrl_{};      ///< 0x3FE0–0x3FFF
    uint16_t bank_ = 0;  ///< Banco atual das ROMs.
    std::deque<uint8_t> latch_;  ///< Trinco de entrada (bytes da CPU principal).
    uint8_t last_latch_ = 0;  ///< Último byte lido do trinco.
    uint16_t tx1_ = 0;  ///< Primeira palavra do SPORT1.
    bool tx1_loaded_ = false;  ///< TX1 carregado.
    bool sport_irq_ = false;  ///< Interrupção do SPORT1 pendente.

    bool running_ = false;      ///< o programa do DSP está executando (não parado em reset)
    bool reset_ = false;  ///< Placa em reset.
    bool reset_edge_ = false;   ///< reset pedido desde a última volta da corrotina
    uint64_t budget_ = 0;       ///< ciclos que ainda podem passar nesta chamada de run()
    uint32_t cycles_to_sample_ = 320;  ///< Ciclos até a próxima amostra.
    std::vector<int16_t>* out_ = nullptr;  ///< Destino das amostras da chamada atual.
    uint32_t boots_ = 0;  ///< Boots feitos.
    uint16_t last_boot_bank_ = 0;  ///< Banco do último boot.
    uint64_t frames_decoded_ = 0;  ///< Quadros comprimidos decodificados.
    uint64_t bytes_received_ = 0;  ///< Bytes recebidos da CPU principal.
};

} // namespace rea::core::audio::dcs
