#pragma once
/**
 * @file localization_manager.hpp
 * @brief Sistema de textos por chave. Só há `en`, o idioma original do arcade.
 */

#include <filesystem>
#include <string>
#include <unordered_map>

namespace rea::ui {

/**
 * @brief Carrega tabelas de textos por idioma e resolve chaves.
 *
 * @details Arquivos `assets/lang/<código>.lang`, formato `CHAVE=texto`
 * (UTF-8, `#` inicia comentário). O idioma original do arcade é **inglês
 * (en)** e é o único carregado. Se uma chave faltar no idioma ativo, cai para `en`
 * e, por fim, para a própria chave entre colchetes (fácil de achar na tela).
 */
class LocalizationManager {
public:
    /**
     * @brief Constrói o gerenciador.
     * @param lang_dir Diretório com os arquivos `.lang`.
     */
    explicit LocalizationManager(std::filesystem::path lang_dir);

    /**
     * @brief Seleciona o idioma ativo (carrega também o fallback `en`).
     * @param code Código do idioma ("en", "pt-BR").
     * @return true se o arquivo do idioma foi carregado.
     */
    bool setLanguage(const std::string& code);

    /**
     * @brief Traduz uma chave.
     * @param key Chave (ex.: "INSERT_COINS").
     * @return Texto no idioma ativo, fallback em inglês ou "[key]".
     */
    std::string tr(const std::string& key) const;

    /// @return Código do idioma ativo.
    const std::string& language() const { return lang_; }

private:
    /// Tabela chave → texto de um arquivo `.lang`.
    using Table = std::unordered_map<std::string, std::string>;
    /// Lê as linhas `CHAVE=texto` de @p path para @p out. @return false se o arquivo não abrir.
    static bool loadFile(const std::filesystem::path& path, Table& out);

    std::filesystem::path dir_;  ///< Pasta com os arquivos `.lang`.
    std::string lang_ = "en";  ///< Código do idioma ativo.
    Table active_;  ///< Textos do idioma ativo.
    Table fallback_;  ///< Textos de `en.lang`, usados quando a chave falta no idioma ativo.
};

} // namespace rea::ui
