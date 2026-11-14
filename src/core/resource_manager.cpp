/**
 * @file resource_manager.cpp
 * @brief Implementação do ResourceManager.
 */
#include "core/resource_manager.hpp"

#include "core/rom_identity.hpp"

#include <algorithm>
#include <system_error>

namespace rea::core {

namespace fs = std::filesystem;

ResourceManager::ResourceManager(fs::path project_root) : root_(std::move(project_root)) {}

std::optional<fs::path> ResourceManager::findRomDir(const std::string& probe_file) const {
    const fs::path extracted = root_ / "assets" / "rom_extracted";
    if (fs::exists(extracted / probe_file)) return extracted;

    std::error_code ec;
    const fs::path rom = root_ / "rom";
    if (fs::exists(rom / probe_file)) return rom;
    for (fs::recursive_directory_iterator it(rom, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec) && it->path().filename() == probe_file)
            return it->path().parent_path();
    }
    return std::nullopt;
}

std::vector<fs::path> ResourceManager::romZips() const {
    std::error_code ec;
    std::vector<fs::path> zips;
    for (fs::directory_iterator it(root_ / "rom", ec), end; !ec && it != end; it.increment(ec)) {
        std::string ext = it->path().extension().string();
        for (char& ch : ext) ch = static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
        if (ext == ".zip" && it->is_regular_file(ec)) zips.push_back(it->path());
    }
    std::sort(zips.begin(), zips.end());  // ordem estável entre sistemas
    return zips;
}

std::unique_ptr<RomSource> ResourceManager::openRom(const std::string& probe_file) const {
    if (auto dir = findRomDir(probe_file)) return std::make_unique<DirRomSource>(*dir);
    for (const fs::path& z : romZips()) {
        auto src = std::make_unique<ZipRomSource>(z);
        if (src->ok() && src->has(probe_file)) return src;
    }
    return nullptr;
}

std::array<std::string, 4> ResourceManager::RomVersion::programChips() const {
    // "cusa-l4.u10" → "cusa-l4.u" + 10..13 (a grafia do u10 vale para os outros: "CUSA-L21.U10" → ".U11"...)
    const std::string stem = chip.size() > 2 ? chip.substr(0, chip.size() - 2) : chip;
    return {stem + "10", stem + "11", stem + "12", stem + "13"};
}

std::string ResourceManager::RomVersion::label() const { return versionLabel(chip); }

std::vector<ResourceManager::RomVersion> ResourceManager::findVersions() const {
    std::vector<RomVersion> out;
    auto add = [&](const std::vector<std::string>& names, const std::string& where) {
        for (const std::string& chip : programVersions(names)) {
            const RomVersion v{chip, where};
            if (std::none_of(out.begin(), out.end(), [&](const RomVersion& o) { return o.label() == v.label(); }))
                out.push_back(v);
        }
    };
    for (const fs::path& d : romDirs()) add(DirRomSource(d).list(), d.string());
    for (const fs::path& z : romZips()) {
        const ZipRomSource src(z);
        if (src.ok()) add(src.list(), z.string());
    }
    std::stable_sort(out.begin(), out.end(), [](const RomVersion& a, const RomVersion& b) {
        if (a.supported() != b.supported()) return a.supported();
        return a.label() > b.label();
    });
    return out;
}

ResourceManager::RomVersion ResourceManager::findOtherVersion() const {
    for (const RomVersion& v : findVersions())
        if (!v.supported()) return v;
    return {};
}

std::vector<fs::path> ResourceManager::romDirs() const {
    std::error_code ec;
    std::vector<fs::path> dirs;
    for (const fs::path& d : {extractedDir(), root_ / "rom"})
        if (fs::is_directory(d, ec)) dirs.push_back(d);
    for (fs::recursive_directory_iterator it(root_ / "rom", ec), end; !ec && it != end; it.increment(ec))
        if (it->is_directory(ec)) dirs.push_back(it->path());
    return dirs;
}

std::unique_ptr<RomSource> ResourceManager::openRomChain(const std::string& probe_file) const {
    auto first = openRom(probe_file);
    if (!first) return nullptr;
    const std::string first_name = first->describe();
    std::vector<std::unique_ptr<RomSource>> parts;
    parts.push_back(std::move(first));
    for (const fs::path& d : romDirs())
        if (d.string() != first_name) parts.push_back(std::make_unique<DirRomSource>(d));
    for (const fs::path& z : romZips()) {
        auto zip = std::make_unique<ZipRomSource>(z);
        if (zip->ok() && z.string() != first_name) parts.push_back(std::move(zip));
    }
    return std::make_unique<ChainRomSource>(std::move(parts));
}

fs::path ResourceManager::findRoot(const std::vector<fs::path>& candidates) {
    std::error_code ec;
    for (const fs::path& c : candidates)
        if (!c.empty() && fs::is_directory(c / "assets" / "lang", ec)) return c;
    for (const fs::path& c : candidates)
        if (!c.empty()) return c;
    return fs::path(".");
}

} // namespace rea::core
