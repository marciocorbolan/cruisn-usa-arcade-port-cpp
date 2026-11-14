#pragma once
/**
 * @file audio_manager.hpp
 * @brief Gerente de áudio: placa de som DCS (programa traduzido, hardware simulado) → saída da plataforma (`IAudio`) e gravação WAV opcional.
 *
 * @details No arcade, a CPU principal só envia comandos de 16 bits à placa DCS (trinco `0x9A0000`, ver
 * `sound_commands.hpp`); a placa (ADSP-2105, programa traduzido em `dcs/`) decodifica as falas e trilhas
 * comprimidas das ROMs `cusa.u2`–`u9` e gera PCM mono de 16 bits a 31 250 Hz. O AudioManager:
 * - acha as ROMs de som pelo `ResourceManager` e liga a placa ao barramento;
 * - a cada quadro do arcade (`frame()`), roda a placa pelo número de amostras daquele quadro (31 250 / taxa de
 *   quadros, com o resto acumulado) e entrega as amostras à saída;
 * - opcionalmente grava tudo e escreve um WAV (little-endian explícito, independente da CPU do PC);
 * - aplica os sons externos de `assets/audio/` (`SoundOverrides`): a porta da placa no barramento passa pelo
 *   AudioManager, que monta as palavras de comando (byte alto, byte baixo). Um comando com WAV externo não chega à
 *   placa e toca o arquivo; as palavras de parâmetro (depois de `0x55AA`–`0x55AF` e `0x55CC`) sempre seguem para a
 *   placa, `0x0000` para as vozes externas e o volume de `0x55AA` vale para elas.
 */

#include <cstdint>
#include <string>
#include <vector>

#include "core/audio/dcs/dcs_board.hpp"
#include "core/audio/sound_overrides.hpp"
#include "core/interfaces.hpp"
#include "core/memory_bus.hpp"
#include "core/resource_manager.hpp"

namespace rea::core::audio {

/// @brief Placa de som DCS ligada ao barramento: comandos da CPU, sons externos e saída PCM.
class AudioManager {
public:
    /// Taxa da saída da placa DCS (SPORT1 do ADSP-2105: 10 MHz / 320).
    static constexpr uint32_t kSampleRate = 31250;

    /**
     * @brief Carrega as ROMs de som e liga a placa ao barramento.
     * @return false se as ROMs `cusa.u2`–`u9` não forem encontradas (o jogo roda sem som).
     */
    bool load(const ResourceManager& res, MemoryBus& bus);
    /// @return true se as ROMs de som foram carregadas.
    bool loaded() const { return board_.loaded(); }

    /// @brief Carrega os sons externos (`cmd_XXXX[_loop].wav`) de @p dir. @return Comandos substituídos.
    int loadOverrides(const std::filesystem::path& dir) { return overrides_.loadDir(dir, kSampleRate); }
    /// @return Sons externos.
    SoundOverrides& overrides() { return overrides_; }
    /// @brief Comandos que tocaram um som externo em vez de ir para a placa.
    uint64_t overridden() const { return overridden_; }

    /// @brief Byte escrito pela CPU em `0x9A0000` (o mesmo caminho do barramento; usado também nos testes).
    void hostByte(uint8_t b);

    /// @brief Saída da plataforma (pode ser nula: só grava/descarta).
    void setOutput(IAudio* out) { out_ = out; }
    /// @brief Liga a gravação de todas as amostras (para `writeWav`).
    void setRecording(bool on) { recording_ = on; }

    /**
     * @brief Avança a placa um quadro do arcade e entrega as amostras.
     * @param refreshHz Taxa de quadros do arcade (~57,9 Hz).
     */
    void frame(double refreshHz);

    /**
     * @brief Escreve as amostras gravadas como WAV PCM mono 16 bits a 31 250 Hz.
     * @return false se o arquivo não pôde ser criado ou houve erro de escrita (ex.: disco cheio).
     */
    bool writeWav(const std::string& path) const;
    /// @return Amostras gravadas.
    const std::vector<int16_t>& recorded() const { return recorded_; }
    /// @return true se a gravação parou no limite do formato WAV (`kMaxRecordedSamples`).
    bool recordingTruncated() const { return recording_truncated_; }
    /// Máximo de amostras num WAV: o tamanho dos dados é um campo de 32 bits (~19 h a 31 250 Hz).
    static constexpr std::size_t kMaxRecordedSamples = (0xFFFFFFFFu - 36u) / 2u;

    /// @return Placa DCS.
    const dcs::DcsBoard& board() const { return board_; }

private:
    /// Porta da placa no barramento: passa pelo filtro de comandos.
    struct HostPort final : IIoDevice {
        AudioManager& am;  ///< Dono da porta.
        /// @param a Dono da porta.
        explicit HostPort(AudioManager& a) : am(a) {}
        bool ioRead(uint32_t, uint32_t&) override { return false; }
        void ioWrite(uint32_t addr, uint32_t value) override;
    };

    dcs::DcsBoard board_;  ///< Placa DCS.
    HostPort port_{*this};  ///< Porta da placa no barramento.
    SoundOverrides overrides_;  ///< Sons externos.
    uint8_t high_ = 0;  ///< Byte alto do comando em montagem.
    bool have_high_ = false;  ///< Byte alto recebido.
    bool expect_param_ = false;  ///< A próxima palavra é parâmetro (depois de `0x55AA`–`0x55AF`/`0x55CC`): vai sempre para a placa.
    bool param_is_volume_ = false;  ///< O parâmetro pendente é o volume geral (`0x55AA`).
    uint64_t overridden_ = 0;  ///< Comandos substituídos por som externo.
    IAudio* out_ = nullptr;  ///< Saída da plataforma.
    bool recording_ = false;  ///< Gravação ligada.
    double acc_ = 0.0;  ///< Fração de amostra acumulada entre quadros.
    std::vector<int16_t> pcm_;  ///< Amostras do quadro atual.
    std::vector<int16_t> recorded_;  ///< Amostras gravadas.
    bool recording_truncated_ = false;  ///< A gravação atingiu `kMaxRecordedSamples`.
};

} // namespace rea::core::audio
