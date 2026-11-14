#pragma once
/**
 * @file rom_source.hpp
 * @brief Origem dos arquivos da ROM: uma pasta (já extraída) ou o `.zip` direto.
 *
 * @details Os carregadores (`MemoryBus`, `DcsBoard`) pedem os chips por nome (ex.: "cusa.u14") e não sabem
 * se vêm de uma pasta ou de dentro de um `.zip`. O `.zip` é lido sem bibliotecas externas: diretório
 * central, cabeçalhos locais e os métodos 0 (guardado) e 8 (deflate, RFC 1951), com conferência do CRC-32.
 */

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace rea::core {

/// Fonte de arquivos da ROM, por nome.
class RomSource {
public:
    virtual ~RomSource() = default;
    /// @brief Lê o arquivo @p name inteiro. @return false se ele não existir ou estiver corrompido.
    virtual bool read(const std::string& name, std::vector<uint8_t>& out) const = 0;
    /// @return true se o arquivo existe na fonte.
    virtual bool has(const std::string& name) const = 0;
    /// @return Descrição para o console (pasta ou arquivo .zip).
    virtual std::string describe() const = 0;
    /// @return Nomes dos arquivos da fonte (para reconhecer a versão da ROM).
    virtual std::vector<std::string> list() const = 0;
    /// @brief CRC-32 do conteúdo de @p name (um zip responde pelo diretório central, sem descompactar; a
    /// leitura confere o conteúdo com ele). @return false se não existir.
    virtual bool crc(const std::string& name, uint32_t& out) const;
};

/// Arquivos soltos numa pasta (ex.: `assets/rom_extracted/`).
class DirRomSource final : public RomSource {
public:
    /// @param dir Pasta com os arquivos da ROM.
    explicit DirRomSource(std::filesystem::path dir) : dir_(std::move(dir)) {}
    bool read(const std::string& name, std::vector<uint8_t>& out) const override;
    bool has(const std::string& name) const override;
    std::string describe() const override { return dir_.string(); }
    std::vector<std::string> list() const override;

private:
    std::filesystem::path dir_;  ///< Pasta com os arquivos da ROM.
};

/// Arquivos dentro de um `.zip` (busca pelo nome do arquivo, ignorando subpastas do zip).
class ZipRomSource final : public RomSource {
public:
    /// Lê o diretório central de @p zip. Verifique ok().
    explicit ZipRomSource(std::filesystem::path zip);
    /// @return true se o diretório central foi lido.
    bool ok() const { return ok_; }
    bool read(const std::string& name, std::vector<uint8_t>& out) const override;
    bool has(const std::string& name) const override { return entries_.count(name) != 0; }
    /// @brief CRC-32 de @p name segundo o diretório central (sem descompactar). @return false se não existir.
    bool crc(const std::string& name, uint32_t& out) const override {
        const auto it = entries_.find(name);
        if (it == entries_.end()) return false;
        out = it->second.crc;
        return true;
    }
    std::string describe() const override { return zip_.string(); }
    std::vector<std::string> list() const override {
        std::vector<std::string> names;
        for (const auto& e : entries_) names.push_back(e.first);
        return names;
    }

private:
    /// Entrada do diretório central do zip.
    struct Entry {
        uint16_t method;  ///< 0 = armazenado, 8 = deflate.
        uint32_t crc;           ///< CRC-32 do conteúdo descompactado.
        uint32_t compressed;    ///< Tamanho compactado.
        uint32_t size;          ///< Tamanho descompactado.
        uint32_t local_offset;  ///< Posição do cabeçalho local no arquivo.
    };
    std::filesystem::path zip_;  ///< Caminho do zip.
    std::map<std::string, Entry> entries_;  ///< Entradas por nome de arquivo.
    bool ok_ = false;  ///< true se o diretório central foi lido.
};

/// @brief Várias fontes em ordem: cada arquivo vem da primeira que o tiver.
/// @details Os sets do MAME de outras versões (ex.: `crusnusa40.zip`) trazem só os chips que mudam (o programa);
///          gráficos e som ficam no set da L4.1. Assim o programa de um zip e o resto de outro formam a ROM inteira.
class ChainRomSource final : public RomSource {
public:
    /// @param parts Fontes, da mais prioritária para a menos.
    explicit ChainRomSource(std::vector<std::unique_ptr<RomSource>> parts) : parts_(std::move(parts)) {}
    bool read(const std::string& name, std::vector<uint8_t>& out) const override {
        for (const auto& p : parts_)
            if (p->has(name)) return p->read(name, out);
        return false;
    }
    bool has(const std::string& name) const override {
        for (const auto& p : parts_)
            if (p->has(name)) return true;
        return false;
    }
    bool crc(const std::string& name, uint32_t& out) const override {
        for (const auto& p : parts_)
            if (p->has(name)) return p->crc(name, out);
        return false;
    }
    /// @return As fontes separadas por " + ".
    std::string describe() const override {
        std::string d;
        for (const auto& p : parts_) d += (d.empty() ? "" : " + ") + p->describe();
        return d;
    }
    std::vector<std::string> list() const override {
        std::vector<std::string> names;
        for (const auto& p : parts_)
            for (auto& n : p->list()) names.push_back(std::move(n));
        return names;
    }

private:
    std::vector<std::unique_ptr<RomSource>> parts_;  ///< Fontes em ordem de prioridade.
};

/**
 * @brief Descompacta um fluxo deflate bruto (RFC 1951).
 * @param expected Tamanho esperado da saída: reserva e teto (0 = desconhecido; teto pela razão máxima do deflate).
 * @param data Início do fluxo DEFLATE bruto (sem cabeçalho zlib).
 * @param size Tamanho de @p data em bytes.
 * @param out Recebe os bytes descompactados.
 * @return false se o fluxo for inválido.
 */
bool inflateRaw(const uint8_t* data, std::size_t size, std::vector<uint8_t>& out, std::size_t expected = 0);

/// @brief CRC-32 (polinômio 0xEDB88320), o mesmo do zip.
uint32_t crc32(const uint8_t* data, std::size_t size);

} // namespace rea::core
