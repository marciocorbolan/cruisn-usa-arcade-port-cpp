/**
 * @file localization_manager.cpp
 * @brief Implementação do LocalizationManager.
 */
#include "ui/localization_manager.hpp"

#include <fstream>

namespace rea::ui {

namespace {
/// @brief Remove espaços nas pontas.
std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r");
    return s.substr(b, e - b + 1);
}
} // namespace

LocalizationManager::LocalizationManager(std::filesystem::path lang_dir) : dir_(std::move(lang_dir)) {}

bool LocalizationManager::loadFile(const std::filesystem::path& path, Table& out) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    bool first = true;
    while (std::getline(f, line)) {
        // BOM UTF-8 (Bloco de Notas do Windows): sem tirar, a primeira chave do arquivo não era achada.
        if (first && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
        first = false;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        const auto eq = t.find('=');
        if (eq == std::string::npos) continue;
        out[trim(t.substr(0, eq))] = trim(t.substr(eq + 1));
    }
    return true;
}

bool LocalizationManager::setLanguage(const std::string& code) {
    fallback_.clear();
    active_.clear();
    loadFile(dir_ / "en.lang", fallback_);
    const bool ok = loadFile(dir_ / (code + ".lang"), active_);
    if (ok) lang_ = code;
    return ok;
}

std::string LocalizationManager::tr(const std::string& key) const {
    if (auto it = active_.find(key); it != active_.end()) return it->second;
    if (auto it = fallback_.find(key); it != fallback_.end()) return it->second;
    return "[" + key + "]";
}

} // namespace rea::ui
