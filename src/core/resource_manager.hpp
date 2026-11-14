#pragma once
/**
 * @file resource_manager.hpp
 * @brief Localização de assets e dos arquivos extraídos da ROM.
 */

#include <filesystem>
#include <memory>
#include <array>
#include <optional>
#include <string>
#include <vector>

#include "core/rom_source.hpp"

namespace rea::core {

/**
 * @brief Resolve caminhos de recursos de forma desacoplada da lógica.
 *
 * @details A lógica nunca abre arquivos por caminho fixo: pede ao
 * ResourceManager. Ordem de busca dos dados da ROM (`openRom`):
 *  1. `assets/rom_extracted/` (gerado por `tools/extract_rom.sh`, opcional);
 *  2. qualquer subpasta de `rom/` que contenha os arquivos já extraídos;
 *  3. qualquer `.zip` em `rom/` (lido direto, sem extrair).
 * O nome do .zip em `rom/` é irrelevante (ROM genérica).
 * Texturas/sons customizados (re-skin "Rota do Rio") ficam em `assets/` e
 * têm prioridade sobre os extraídos da ROM quando existirem.
 */
class ResourceManager {
public:
    /**
     * @brief Constrói o gerenciador.
     * @param project_root Raiz do workspace (contém `assets/`, `rom/`, `save/`).
     */
    explicit ResourceManager(std::filesystem::path project_root);

    /**
     * @brief Encontra o diretório que contém o arquivo @p probe_file da ROM.
     * @param probe_file Nome de um arquivo esperado (ex.: "cusa-l41.u10").
     * @return Diretório encontrado, ou vazio se a ROM não foi extraída.
     */
    std::optional<std::filesystem::path> findRomDir(const std::string& probe_file) const;

    /**
     * @brief Abre a fonte da ROM que contém @p probe_file: pasta extraída ou `.zip` em `rom/`.
     * @return nullptr se nenhuma fonte tiver o arquivo.
     */
    std::unique_ptr<RomSource> openRom(const std::string& probe_file) const;

    /// @return As pastas que podem ter os chips soltos: `assets/rom_extracted/`, `rom/` e as subpastas de `rom/`.
    std::vector<std::filesystem::path> romDirs() const;

    /// @return Os `.zip` de `rom/`, em ordem alfabética.
    std::vector<std::filesystem::path> romZips() const;

    /// @return `assets/rom_extracted/`: quando tem os arquivos, ela é usada antes de qualquer `.zip` de `rom/`.
    std::filesystem::path extractedDir() const { return root_ / "assets" / "rom_extracted"; }

    /// Programa de uma versão do jogo encontrado nas fontes da ROM.
    struct RomVersion {
        std::string chip;   ///< Nome do chip D0–D7 (ex.: "cusa-l21.u10"); vazio = nenhum.
        std::string where;  ///< Pasta ou `.zip` onde ele está.
        /// @return true se nada foi encontrado.
        bool empty() const { return chip.empty(); }
        /// @return "chip (onde)", para mensagens.
        std::string text() const { return chip + " (" + where + ")"; }
        /// @return A versão escrita no nome do chip (ex.: "2.1").
        std::string label() const;
        /// @return true se é a L4.1, a versão traduzida.
        bool supported() const { return label() == "4.1"; }
        /// @return Os 4 chips de programa dessa versão (u10–u13, com a mesma grafia do u10).
        std::array<std::string, 4> programChips() const;
    };

    /**
     * @brief Todas as versões do programa (`cusa-l*.u10`) nas fontes de openRom(): `assets/rom_extracted/`,
     *        `rom/` e subpastas, e os `.zip` de `rom/` (um set "merged" do MAME traz várias num zip só).
     * @return Uma por versão (a primeira fonte onde aparece), a L4.1 primeiro e as outras da mais nova à mais velha.
     */
    std::vector<RomVersion> findVersions() const;

    /// @brief A primeira versão que não é a L4.1 (findVersions()), ou vazia.
    RomVersion findOtherVersion() const;

    /**
     * @brief Fonte combinada: a que tem @p probe_file primeiro e, depois, todas as outras (pasta extraída, `rom/` e
     *        subpastas, cada `.zip`). Serve a uma versão cujo set só traz o programa (gráficos e som em outro zip).
     * @return nullptr se nenhuma fonte tiver @p probe_file.
     */
    std::unique_ptr<RomSource> openRomChain(const std::string& probe_file) const;

    /**
     * @brief Escolhe a raiz dos recursos: a primeira candidata que tenha `assets/lang/`.
     * @details Ordem usada pelo executável: `--root`, variável `REA_ROOT`, pasta do executável e até
     *          3 pastas acima dela, pasta atual e, por fim, a pasta do código-fonte (compilação local).
     * @return A candidata escolhida, ou a primeira não vazia se nenhuma tiver `assets/lang/`.
     */
    static std::filesystem::path findRoot(const std::vector<std::filesystem::path>& candidates);

    /// @return Caminho de um asset relativo a `assets/`.
    std::filesystem::path asset(const std::filesystem::path& rel) const { return root_ / "assets" / rel; }
    /// @return Caminho de um arquivo de save relativo a `save/`.
    std::filesystem::path save(const std::filesystem::path& rel) const { return root_ / "save" / rel; }
    /// @return Raiz do projeto.
    const std::filesystem::path& root() const { return root_; }

private:
    std::filesystem::path root_;  ///< Raiz do projeto (contém `assets/`, `rom/` e `save/`).
};

} // namespace rea::core
