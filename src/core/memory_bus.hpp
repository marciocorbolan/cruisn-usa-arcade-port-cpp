#pragma once
/**
 * @file memory_bus.hpp
 * @brief Barramento de memória simulado do TMS320C31 (placa Midway V-Unit).
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/rom_source.hpp"

namespace rea::core {

/**
 * @brief Dispositivo de I/O mapeado em memória (portas, ADC, etc.).
 * @details Consultado pelo MemoryBus antes do banco genérico de latches.
 */
class IIoDevice {
public:
    virtual ~IIoDevice() = default;
    /**
     * @brief Atende uma leitura.
     * @param addr Endereço de palavra.
     * @param value Saída: valor lido.
     * @return true se o endereço pertence ao dispositivo.
     */
    virtual bool ioRead(uint32_t addr, uint32_t& value) = 0;
    /**
     * @brief Observa/atende uma escrita (o barramento também guarda o latch).
     * @param addr Endereço de palavra.
     * @param value Valor escrito.
     */
    virtual void ioWrite(uint32_t addr, uint32_t value) = 0;
};

/**
 * @brief Barramento de memória endereçado por PALAVRA de 32 bits.
 *
 * @details O TMS320C31 não possui endereçamento por byte: cada endereço do
 * barramento (24 bits) seleciona uma palavra inteira de 32 bits. Por isso
 * todas as funções aqui recebem um *endereço de palavra*, e não um offset em
 * bytes.
 *
 * O programa original está dividido em quatro EPROMs de 8 bits
 * (`cusa-l41.u10` a `u13`), uma por "pista de bytes" do barramento:
 *
 * | EPROM | Bits da palavra |
 * |-------|-----------------|
 * | u10   | D0  – D7        |
 * | u11   | D8  – D15       |
 * | u12   | D16 – D23       |
 * | u13   | D24 – D31       |
 *
 * A palavra é montada por deslocamentos explícitos (shift/OR), portanto o
 * resultado independe do endianness do hospedeiro (x86_64 e ARM little-endian,
 * ou qualquer big-endian).
 *
 * Regiões emuladas (ver `vunit_hw.hpp`): RAM, RAM rápida, periféricos e RAM
 * internos do TMS320C31, CMOS (com proteção de escrita), paleta e ROM. As
 * demais áreas de I/O (vídeo, controles, som) são guardadas num banco de
 * "latches" e repassadas a um observador opcional, para a plataforma reagir.
 *
 * @note Verificado: a palavra 0 da ROM é `0x00004AD5` (vetor de RESET).
 */
class MemoryBus {
public:
    /// Tamanho de cada EPROM de programa em bytes (= palavras resultantes).
    static constexpr std::size_t kProgramChipSize = 512 * 1024;
    /// Total de palavras de 32 bits do programa (4 × 512 KB / 4).
    static constexpr std::size_t kProgramWords = kProgramChipSize;
    /// Vetores de interrupção (palavras 0–4).
    enum class Vector : uint32_t { Reset = 0, Int0 = 1, Int1 = 2, Int2 = 3, Int3 = 4 };

    /**
     * @brief Observador de acessos a I/O não emulado internamente.
     * @param addr Endereço de palavra.
     * @param value Valor escrito.
     */
    using IoWriteHook = std::function<void(uint32_t addr, uint32_t value)>;

    MemoryBus();

    /**
     * @brief Carrega e intercala as quatro EPROMs de programa.
     * @param rom_dir Diretório com os arquivos já extraídos da ROM.
     * @param chip_names Nomes das EPROMs na ordem D0-D7, D8-D15, D16-D23, D24-D31.
     * @return true em caso de sucesso; false se algum arquivo faltar ou tiver tamanho inesperado.
     */
    bool loadProgramRom(const std::filesystem::path& rom_dir,
                        const std::array<std::string, 4>& chip_names);
    /// @brief Igual, lendo de uma pasta ou de dentro do `.zip` (RomSource).
    bool loadProgramRom(const RomSource& src, const std::array<std::string, 4>& chip_names);

    /**
     * @brief Carrega as 16 EPROMs gráficas (`cusa.u14`–`cusa.u29`) na janela de ROM.
     * @details Cada grupo de 4 EPROMs consecutivas forma um banco de 0x80000
     *          palavras, intercalado como o programa (u14 = D0-D7 … u17 = D24-D31).
     *          Os bancos ficam em 0xC80000, 0xD00000, 0xD80000 e 0xE00000
     *          (MAME `crusnusa`, região "maindata"). O jogo guarda ali, entre
     *          outros, os dados comprimidos em LZW descomprimidos por `0xA2F9`.
     * @param rom_dir Diretório com os arquivos já extraídos da ROM.
     * @param first_chip Número da primeira EPROM (14 → `cusa.u14`).
     * @return true se os 16 arquivos foram lidos com o tamanho esperado.
     */
    bool loadGraphicsRoms(const std::filesystem::path& rom_dir, unsigned first_chip = 14);
    /// @brief Igual, lendo de uma pasta ou de dentro do `.zip` (RomSource).
    bool loadGraphicsRoms(const RomSource& src, unsigned first_chip = 14);

    /**
     * @brief Lê um texel (byte) da RAM de texturas.
     * @param byte_offset Deslocamento em bytes a partir de 0xA00000 (palavra × 2).
     * @return Byte armazenado; 0 fora dos limites.
     */
    uint8_t textureByte(std::size_t byte_offset) const {
        return byte_offset < texture_.size() ? texture_[byte_offset] : uint8_t{0};
    }

    /**
     * @brief Instala uma ROM já montada (testes / ROMs sintéticas).
     * @param words Palavras de programa.
     */
    void setProgramRom(std::vector<uint32_t> words) { program_ = std::move(words); }

    /**
     * @brief Intercala quatro pistas de bytes já carregadas em memória.
     * @param lanes Vetores de bytes na ordem D0-D7 … D24-D31, todos do mesmo tamanho.
     * @return Palavras de 32 bits montadas independentemente do endianness do host.
     */
    static std::vector<uint32_t> interleave(const std::array<std::vector<uint8_t>, 4>& lanes);

    /**
     * @brief Reset de hardware da placa (antes da CPU executar o vetor de RESET).
     * @details Equivale ao `machine_reset` do MAME: as primeiras 0x20000
     *          palavras da ROM são copiadas para a RAM principal (de onde a CPU
     *          lê os vetores e executa), e o restante do estado é zerado.
     */
    void hardwareReset();

    /// @brief Conteúdo da CMOS (0x9C0000, `kCmosWords` palavras), para gravar e restaurar em `save/`.
    const std::vector<uint32_t>& cmosWords() const { return cmos_; }
    /// @brief Restaura a CMOS. @return false (sem alterar nada) se o tamanho não for `kCmosWords`.
    bool restoreCmos(const std::vector<uint32_t>& words) {
        if (words.size() != cmos_.size()) return false;
        cmos_ = words;
        return true;
    }

    /**
     * @brief Lê uma palavra do barramento.
     * @param addr Endereço de palavra de 24 bits (bits acima são ignorados).
     * @return Valor lido; 0 para áreas não mapeadas (barramento "aberto").
     */
    uint32_t readWord(uint32_t addr) const;

    /**
     * @brief Escreve uma palavra no barramento.
     * @param addr Endereço de palavra de 24 bits.
     * @param value Valor de 32 bits.
     * @note Escritas na ROM são ignoradas; escritas na CMOS só ocorrem com a
     *       proteção desligada (`kCmosProtect` com bits 0xC00).
     */
    void writeWord(uint32_t addr, uint32_t value);

    /**
     * @brief Lê uma palavra diretamente da ROM de programa.
     * @param rom_word Índice relativo ao início da ROM (0 = `0xC00000` no barramento).
     * @return Valor; 0 se fora dos limites.
     */
    uint32_t readRom(uint32_t rom_word) const;

    /**
     * @brief Lê o endereço de destino de um vetor de interrupção (na ROM).
     */
    uint32_t vector(Vector v) const { return readRom(static_cast<uint32_t>(v)); }

    /**
     * @brief Lê uma string ASCII empacotada da ROM.
     * @details O jogo armazena textos com 4 caracteres por palavra, primeiro
     *          caractere no byte menos significativo (ex.: "INSERT COINS" na
     *          palavra 0x7456 da ROM). A leitura termina no primeiro byte 0 ou
     *          em @p max_chars.
     * @param rom_word Palavra inicial relativa à ROM.
     * @param max_chars Limite de segurança.
     * @return Texto decodificado.
     */
    std::string readPackedString(uint32_t rom_word, std::size_t max_chars = 256) const;

    /// @brief Define o valor que uma porta de I/O devolve na leitura (entradas).
    void setIoInput(uint32_t addr, uint32_t value) { io_[addr & kAddrMask] = value; }
    /// @brief Observa escritas em I/O (vídeo, controles, som).
    void setIoWriteHook(IoWriteHook hook) { io_hook_ = std::move(hook); }
    /// @brief Conecta um dispositivo de I/O (não assume a posse do ponteiro).
    void attachDevice(IIoDevice* device) { devices_.push_back(device); }

    /// @brief Pixel da VRAM (índice de palavra a partir de 0x900000), para o compositor de vídeo.
    uint16_t videoWord(uint32_t index) const { return index < vram_.size() ? vram_[index] : 0u; }
    /// @brief Escrita direta de pixel na VRAM (renderizador de DMA); fora dos limites é ignorada.
    void videoWrite(uint32_t index, uint16_t value) { if (index < vram_.size()) vram_[index] = value; }
    /// @brief Entrada da paleta (xRRRRRGGGGGBBBBB nos 15 bits baixos).
    uint32_t paletteWord(uint32_t index) const { return index < palette_.size() ? palette_[index] : 0u; }

    /// @return true se a ROM de programa foi carregada.
    bool loaded() const { return !program_.empty(); }

private:
    static constexpr uint32_t kAddrMask = 0xFFFFFF;  ///< O barramento do C31 tem 24 bits de endereço.

    std::vector<uint32_t> program_;  ///< ROM de programa (palavras já montadas das 4 EPROMs).
    std::vector<uint32_t> graphics_;   ///< Bancos gráficos (0xC80000–0xE7FFFF); vazio se não carregados.
    std::vector<uint32_t> ram_;  ///< RAM principal (`0x000000`).
    std::vector<uint32_t> fast_ram_;  ///< RAM rápida (`0x400000`).
    std::vector<uint32_t> tms_periph_;  ///< Registradores internos do TMS320C31 (`0x808000`).
    std::vector<uint32_t> tms_ram_;  ///< RAM interna do TMS320C31 (`0x809800`).
    std::vector<uint32_t> cmos_;  ///< CMOS/NVRAM (`0x9C0000`).
    std::vector<uint32_t> palette_;  ///< RAM de paleta (`0x9E0000`).
    std::vector<uint16_t> vram_;       ///< VRAM: só 16 bits por palavra (MAME memory_share_creator<uint16_t>).
    std::vector<uint8_t> texture_;     ///< RAM de texturas: 2 bytes por palavra de barramento.
    std::unordered_map<uint32_t, uint32_t> io_;  ///< Demais portas de I/O: último valor escrito por endereço.
    bool cmos_protected_ = true;  ///< Estado da proteção de escrita da CMOS.
    IoWriteHook io_hook_;  ///< Chamado a cada escrita de I/O.
    std::vector<IIoDevice*> devices_;  ///< Dispositivos que respondem a leituras e escritas de I/O.
};

} // namespace rea::core
