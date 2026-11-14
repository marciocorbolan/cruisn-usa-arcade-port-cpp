/**
 * @file test_resources.cpp
 * @brief Recursos: leitura da ROM pelo .zip (RomSource).
 */
#include "test_support.hpp"

#include "core/rom_identity.hpp"

using namespace rea::core;
using namespace rea::test;

namespace {

/// ROM pelo .zip (RomSource): deflate próprio, CRC-32, pasta x zip
void testZipRomSource() {
    {
        const uint8_t fixed[] = {0x73, 0x2e, 0x2a, 0xcd, 0x2c, 0xce, 0x53, 0x70, 0x46, 0xa1, 0x42,
                                 0x83, 0x1d, 0x9d, 0x49, 0x14, 0x07, 0x00};
        std::string expect;
        for (int i = 0; i < 3; ++i) expect += "Cruisn Cruisn Cruisn USA";
        std::vector<uint8_t> out;
        const bool ok_fixed = inflateRaw(fixed, sizeof fixed, out) && std::string(out.begin(), out.end()) == expect;
        const uint8_t stored[] = {0x01, 0x03, 0x00, 0xFC, 0xFF, 'a', 'b', 'c'};
        const bool ok_stored = inflateRaw(stored, sizeof stored, out) && std::string(out.begin(), out.end()) == "abc";
        const uint8_t bad[] = {0x07};  // tipo de bloco 3 (reservado)
        const char* nine = "123456789";
        check(ok_fixed && ok_stored && !inflateRaw(bad, sizeof bad, out) &&
                  crc32(reinterpret_cast<const uint8_t*>(nine), 9) == 0xCBF43926u,
              "inflateRaw: blocos Huffman fixo e guardado, tipo inválido recusado; CRC-32 padrão");
        check(!inflateRaw(fixed, sizeof fixed, out, 10) && inflateRaw(fixed, sizeof fixed, out, expect.size()),
              "inflateRaw: a saída não passa do tamanho esperado (zip corrompido não expande sem fim)");

        {
            // Zip à mão: "ok.bin" (armazenado, "abc") e "big.bin" com tamanho compactado impossível.
            std::vector<uint8_t> z;
            auto u16 = [&](uint32_t v) { z.push_back(static_cast<uint8_t>(v)); z.push_back(static_cast<uint8_t>(v >> 8)); };
            auto u32 = [&](uint32_t v) { u16(v & 0xFFFF); u16(v >> 16); };
            auto str = [&](const char* t) { while (*t) z.push_back(static_cast<uint8_t>(*t++)); };
            const uint32_t crc = crc32(reinterpret_cast<const uint8_t*>("abc"), 3);
            u32(0x04034B50); u16(10); u16(0); u16(0); u16(0); u16(0); u32(crc); u32(3); u32(3); u16(6); u16(0);
            str("ok.bin"); str("abc");
            const auto cd = static_cast<uint32_t>(z.size());
            auto central = [&](const char* name, uint32_t comp, uint32_t size) {
                u32(0x02014B50); u16(20); u16(10); u16(0); u16(0); u16(0); u16(0); u32(crc); u32(comp); u32(size);
                u16(6); u16(0); u16(0); u16(0); u16(0); u32(0); u32(0); str(name);
            };
            central("ok.bin", 3, 3);
            central("big.bin", 0xFFFFFFF0u, 0xFFFFFFF0u);
            const auto cd_len = static_cast<uint32_t>(z.size()) - cd;
            u32(0x06054B50); u16(0); u16(0); u16(2); u16(2); u32(cd_len); u32(cd); u16(0);
            const auto path = tempPath("zip_test.zip");
            {
                std::ofstream f(path, std::ios::binary);
                f.write(reinterpret_cast<const char*>(z.data()), static_cast<std::streamsize>(z.size()));
            }
            ZipRomSource zip(path);
            std::vector<uint8_t> a;
            const bool good = zip.ok() && zip.read("ok.bin", a) && std::string(a.begin(), a.end()) == "abc";
            const bool skipped = !zip.has("big.bin") && !zip.read("big.bin", a);
            std::filesystem::remove(path);
            check(good && skipped, "ZipRomSource: entrada com tamanho além do arquivo é ignorada (sem alocação gigante)");
        }

        const ResourceManager rm(REA_SOURCE_DIR);
        std::vector<std::filesystem::path> zips;
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(rm.root() / "rom", ec))
            if (e.path().extension() == ".zip") zips.push_back(e.path());
        const auto dir = rm.findRomDir("cusa.u2");
        if (!zips.empty() && dir) {
            ZipRomSource zip(zips.front());
            DirRomSource folder(*dir);
            std::vector<uint8_t> a, b;
            const bool same = zip.ok() && zip.read("cusa.u2", a) && folder.read("cusa.u2", b) && a == b &&
                              zip.read("cusa-l41.u10", a) && folder.read("cusa-l41.u10", b) && a == b;
            check(same && !zip.has("nao_existe.bin") && !zip.read("nao_existe.bin", a),
                  "ZipRomSource: chips lidos do .zip (deflate dinâmico + CRC) iguais aos extraídos");
            MemoryBus zb;
            check(zb.loadProgramRom(zip, {"cusa-l41.u10", "cusa-l41.u11", "cusa-l41.u12", "cusa-l41.u13"}) &&
                      zb.vector(MemoryBus::Vector::Reset) == 0x4AD5,
                  "MemoryBus::loadProgramRom pelo .zip: vetor de RESET = 0x4AD5");
        } else {
            std::printf("[SKIP] rom/*.zip ou ROM extraída ausente\n");
        }
        const auto tmp = tempPath("root_test");
        std::filesystem::create_directories(tmp / "b" / "assets" / "lang");
        check(ResourceManager::findRoot({tmp / "a", tmp / "b", tmp}) == tmp / "b" &&
                  ResourceManager::findRoot({tmp / "a"}) == tmp / "a",
              "ResourceManager::findRoot: primeira candidata com assets/lang/; senão a primeira");
        std::filesystem::remove_all(tmp);
    }
}

/// Identificação da ROM: chips conferidos com a L4.1, outra versão reconhecida
void testRomIdentity() {
    // Fonte em memória: um chip de programa com outro conteúdo, o resto ausente.
    struct MemSource final : RomSource {
        std::map<std::string, std::vector<uint8_t>> files;
        bool read(const std::string& n, std::vector<uint8_t>& out) const override {
            const auto it = files.find(n);
            if (it == files.end()) return false;
            out = it->second;
            return true;
        }
        bool has(const std::string& n) const override { return files.count(n) != 0; }
        std::string describe() const override { return "memória"; }
        std::vector<std::string> list() const override {
            std::vector<std::string> v;
            for (const auto& f : files) v.push_back(f.first);
            return v;
        }
    } mem;
    mem.files["cusa-l41.u10"] = std::vector<uint8_t>(16, 0xAA);
    const auto checks = verifyRom(mem);
    const bool fake = checks.size() == 28 && checks[0].state == ChipCheck::State::Different &&
                      checks[1].state == ChipCheck::State::Missing && checks[0].role == "programa";
    check(fake && otherProgramVersion({"cusa.u14", "cusa-l4.u10"}) == "cusa-l4.u10" &&
              otherProgramVersion({"CUSA-L21.U10"}) == "CUSA-L21.U10" && otherProgramVersion({"cusa-l41.u10"}).empty() &&
              otherProgramVersion({"cusa.u10", "readme.html"}).empty(),
          "ROM: chip diferente/ausente detectado; outra versão do programa (cusa-l*.u10 ≠ l41) reconhecida");

    // Outra versão numa subpasta de rom/ de uma raiz qualquer.
    const auto root = tempPath("rom_identity");
    std::filesystem::create_directories(root / "rom" / "outra");
    { std::ofstream(root / "rom" / "outra" / "cusa-l21.u10") << "x"; }
    // Gráficos de outra fonte (como o set da L4.1 ao lado de um set que só traz o programa).
    std::filesystem::create_directories(root / "assets" / "rom_extracted");
    { std::ofstream(root / "assets" / "rom_extracted" / "cusa.u14") << "gfx"; }
    const ResourceManager rm(root);
    const auto other = rm.findOtherVersion();
    const auto chips = other.programChips();
    const auto chain = rm.openRomChain(chips[0]);
    std::vector<uint8_t> gfx;
    const bool chained = chain && chain->has("cusa-l21.u10") && chain->read("cusa.u14", gfx) && gfx.size() == 3 &&
                         !chain->has("cusa.u99") && !rm.openRomChain("nao-existe.u10");
    std::filesystem::remove_all(root);
    const ResourceManager empty(tempPath("rom_identity_vazia"));
    check(other.chip == "cusa-l21.u10" && other.where.find("outra") != std::string::npos &&
              chips[3] == "cusa-l21.u13" && empty.findOtherVersion().empty() && chained,
          "ResourceManager: acha o programa de outra versão (u10–u13) e o combina com os chips de outra fonte");

    // Várias versões (set "merged" do MAME num zip só e outra numa pasta): a 4.1 primeiro, depois da mais nova.
    {
        const auto mroot = tempPath("rom_versoes");
        std::filesystem::create_directories(mroot / "rom" / "velha");
        std::filesystem::create_directories(mroot / "assets" / "rom_extracted");
        for (const char* n : {"cusa-l21.u10", "cusa-l41.u10"}) std::ofstream(mroot / "assets" / "rom_extracted" / n) << "p";
        { std::ofstream(mroot / "rom" / "velha" / "cusa-l4.u10") << "p"; }
        { std::ofstream(mroot / "rom" / "velha" / "cusa-l21.u10") << "p"; }  // repetida: vale a primeira fonte
        const auto vs = ResourceManager(mroot).findVersions();
        std::filesystem::remove_all(mroot);
        check(vs.size() == 3 && vs[0].label() == "4.1" && vs[0].supported() && vs[1].label() == "4.0" &&
                  vs[2].label() == "2.1" && !vs[2].supported() &&
                  vs[2].where.find("rom_extracted") != std::string::npos &&
                  versionLabel("CUSA-L21.U10") == "2.1" && versionLabel("cusa-l4.u10") == "4.0",
              "ResourceManager::findVersions: lista as versões (4.1 primeiro), sem repetir, com o rótulo do nome");
    }

    // Com a ROM real: os 28 chips conferem (pasta extraída e, se houver, o .zip pelo diretório central).
    const ResourceManager real(REA_SOURCE_DIR);
    if (const auto rom = real.openRom("cusa-l41.u10")) {
        std::size_t ok = 0;
        for (const auto& c : verifyRom(*rom)) ok += c.state == ChipCheck::State::Ok;
        bool zips = true;
        for (const auto& z : real.romZips()) zips = zips && zipMatchesL41(ZipRomSource(z));
        check(ok == 28 && zips, "ROM real: 28/28 chips com o CRC da L4.1 (verifyRom e zipMatchesL41)");
        // O zip responde o CRC pelo diretório central; tem de ser o do conteúdo que ele entrega na leitura.
        for (const auto& z : real.romZips()) {
            const ZipRomSource zip(z);
            std::vector<uint8_t> data;
            uint32_t stored = 0;
            check(zip.ok() && zip.read("cusa-l41.u10", data) && zip.crc("cusa-l41.u10", stored) &&
                      stored == crc32(data.data(), data.size()) && data.size() == 512u * 1024u,
                  "ZipRomSource (só o diretório central lido na abertura): CRC e conteúdo de cusa-l41.u10");
        }
    } else {
        std::printf("[SKIP] ROM ausente: conferência da L4.1\n");
    }
}

} // namespace

void runResourcesTests() {
    testZipRomSource();
    testRomIdentity();
}
