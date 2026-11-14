/**
 * @file main.cpp
 * @brief Ponto de entrada: monta ROM, textos, plataforma SDL2 e o loop de passo fixo.
 *
 * Uso: `rea_cruisn [opções]` — a lista completa das opções está no README (seção "Opções do executável").
 */
#include <memory>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <utility>
#include <vector>
#include <string>
#include <system_error>

#include "core/boot/boot_screens.hpp"
#include "core/boot/boot_sequence.hpp"
#include "core/boot/reset_routine.hpp"
#include "core/boot/power_on_test.hpp"
#include "core/boot/system_routines.hpp"
#include "core/cmos/cmos_file.hpp"
#include "core/cmos/cmos_routines.hpp"
#include "core/cpu_coroutine.hpp"
#include "core/frame_runner.hpp"
#include "core/non_local_jump.hpp"
#include "core/rom_identity.hpp"
#include "core/service/service_ctx.hpp"
#include "core/service/service_menu.hpp"
#include "core/io/cabinet_io.hpp"
#include "core/io/input_manager.hpp"
#include "core/io/input_script.hpp"
#include "core/io/input_routines.hpp"
#include "core/irq/adc_handler.hpp"
#include "core/irq/vblank_handler.hpp"
#include "core/gfx/lzw_loader.hpp"
#include "core/io/link_port.hpp"
#include "core/link/link_messages.hpp"
#include "core/link/link_packet.hpp"
#include "core/link/link_routines.hpp"
#include "core/main/main_loop.hpp"
#include "core/scene/object_list.hpp"
#include "core/main/profiler_routines.hpp"
#include "core/proc/process_manager.hpp"
#include "core/math/int_math.hpp"
#include "core/game/game_start.hpp"
#include "core/game/attract.hpp"
#include "core/video/palette_slots.hpp"
#include "core/game/hiscore_scene.hpp"
#include "core/math/trig.hpp"
#include "core/math/matrix.hpp"
#include "core/game/title_screen.hpp"
#include "core/game/credits_display.hpp"
#include "core/game/car_proximity.hpp"
#include "core/game/attract_demo.hpp"
#include "core/game/demo_camera.hpp"
#include "core/track/track_loader.hpp"
#include "core/track/track_stream.hpp"
#include "core/track/ground_height.hpp"
#include "core/video/sky_renderer.hpp"
#include "core/game/drone_car.hpp"
#include "core/game/car_physics.hpp"
#include "core/game/car_dynamics.hpp"
#include "core/game/drone_ai.hpp"
#include "core/game/race_start.hpp"
#include "core/game/car_wheels.hpp"
#include "core/game/race_traffic.hpp"
#include "core/game/car_collision.hpp"
#include "core/game/scenery_collision.hpp"
#include "core/game/smoke_effects.hpp"
#include "core/game/track_props.hpp"
#include "core/game/roaming_objects.hpp"
#include "core/game/music_title.hpp"
#include "core/game/link_car.hpp"
#include "core/game/link_intro.hpp"
#include "core/game/special_car.hpp"
#include "core/game/ending.hpp"
#include "core/game/roadside_events.hpp"
#include "core/game/credits.hpp"
#include "core/game/game_flow.hpp"
#include "core/game/course_select.hpp"
#include "core/game/car_select.hpp"
#include "core/game/race_entry.hpp"
#include "core/game/player_car.hpp"
#include "core/game/player_effects.hpp"
#include "core/game/race_hud.hpp"
#include "core/game/race_events.hpp"
#include "core/game/object_effects.hpp"
#include "core/game/car_crash.hpp"
#include "core/game/post_race.hpp"
#include "core/game/results_boards.hpp"
#include "core/game/screen_doors.hpp"
#include "core/game/hiscore_entry.hpp"
#include "core/game/continue_screen.hpp"
#include "core/game/coin_start.hpp"
#include "core/game/traffic_cars.hpp"
#include "core/io/serial_printer.hpp"
#include "core/video/palette_blink.hpp"
#include "core/math/float_math.hpp"
#include "core/io/cabinet_outputs.hpp"
#include "core/audio/sound_commands.hpp"
#include "core/video/text_overlay.hpp"
#include "core/render/pipeline3d.hpp"
#include "core/scene/object_query.hpp"
#include "core/scene/node_pools.hpp"
#include "core/scene/scene_object.hpp"
#include "core/video/display.hpp"
#include "core/video/dma_renderer.hpp"
#include "core/video/video_routines.hpp"
#include "core/tms_float.hpp"
#include "core/game_loop.hpp"
#include "core/memory_bus.hpp"
#include "core/resource_manager.hpp"
#include "core/audio/audio_manager.hpp"
#include "platform/link_shm.hpp"
#include "core/vunit_hw.hpp"
#include "platform/sdl/sdl_platform.hpp"
#include "ui/localization_manager.hpp"

#ifdef _WIN32
#include <windows.h>  // SetConsoleOutputCP, AllocConsole
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

// Navegador: as caixas de mensagem do SDL não existem lá; usam as janelas da própria página.
// NOLINTBEGIN
EM_JS(void, rea_web_alert, (const char* msg), { alert(UTF8ToString(msg)); });
EM_JS(int, rea_web_confirm, (const char* msg), { return confirm(UTF8ToString(msg)) ? 1 : 0; });
EM_JS(int, rea_web_prompt_int, (const char* msg, int def), {
    const r = prompt(UTF8ToString(msg), String(def));
    if (r === null) return -1;
    const n = parseInt(r, 10);
    return isNaN(n) ? -1 : n;
});
/// Grava save/ (IDBFS) no armazenamento do navegador; a página define Module.reaSyncSave.
EM_JS(void, rea_web_sync_save, (), { if (Module.reaSyncSave) Module.reaSyncSave(); });
/// Avisa a página que o jogo terminou (o onExit do Emscripten não dispara: áudio e temporizadores do SDL
/// mantêm o runtime vivo).
EM_JS(void, rea_web_exited, (int code), { if (Module.reaExit) Module.reaExit(code); });
// NOLINTEND
#endif

namespace {

/// @brief Caixa de mensagem simples (no navegador, `alert`). Sem vídeo, só falha (o texto já foi ao console).
void showMessage(Uint32 flags, const char* title, const std::string& msg) {
#ifdef __EMSCRIPTEN__
    (void)flags;
    (void)title;
    rea_web_alert(msg.c_str());
#else
    SDL_ShowSimpleMessageBox(flags, title, msg.c_str(), nullptr);
#endif
}

/// Taxa de quadros original do V-Unit (≈57,928 Hz, tabela de timing da ROM).
constexpr double kArcadeRefreshHz = rea::core::vunit::kRefreshHz;

/// Nomes das EPROMs de programa, na ordem D0-D7 … D24-D31.
const std::array<std::string, 4> kProgramChips = {
    "cusa-l41.u10", "cusa-l41.u11", "cusa-l41.u12", "cusa-l41.u13"};

/// Escrita forçada de --poke: @p value em @p addr nos quadros [from, to].
struct Poke {
    uint32_t addr;   ///< Endereço de palavra escrito.
    uint32_t value;  ///< Valor gravado.
    uint64_t from;   ///< Primeiro quadro da faixa.
    uint64_t to;     ///< Último quadro da faixa (inclusive).
};

/// @brief Número inteiro em @p t (base 0: aceita 0x...). @return false se vazio ou com sobra.
bool parseNumber(const std::string& t, uint64_t& out, int base) {
    if (t.empty()) return false;
    char* end = nullptr;
    out = std::strtoull(t.c_str(), &end, base);
    return end && *end == '\0';
}

/// @brief Lê --poke (`0xADDR=VAL[@INI[-FIM]],...`). @return O primeiro item inválido, ou vazio.
std::string parsePokes(const std::string& spec, std::vector<Poke>& out) {
    for (std::size_t pos = 0; pos < spec.size();) {
        const std::size_t end = std::min(spec.find(',', pos), spec.size());
        const std::string item = spec.substr(pos, end - pos);
        pos = end + 1;
        const std::size_t eq = item.find('='), at = item.find('@');
        uint64_t addr = 0, value = 0, from = 0, to = UINT64_MAX;
        bool ok = eq != std::string::npos && (at == std::string::npos || at > eq) &&
                  parseNumber(item.substr(0, eq), addr, 0) && parseNumber(item.substr(eq + 1, at - eq - 1), value, 0);
        if (ok && at != std::string::npos) {
            const std::size_t dash = item.find('-', at);
            ok = parseNumber(item.substr(at + 1, dash - at - 1), from, 10) &&
                 (dash == std::string::npos ? (to = from, true) : parseNumber(item.substr(dash + 1), to, 10));
        }
        if (!ok || addr > 0xFFFFFFu || value > 0xFFFFFFFFu || from > to) return item.empty() ? "(vazio)" : item;
        out.push_back({static_cast<uint32_t>(addr), static_cast<uint32_t>(value), from, to});
    }
    return {};
}

/// @brief Lê --shots (`QUADRO:arquivo.ppm,...`). @return O primeiro item inválido, ou vazio.
std::string parseShots(const std::string& spec, std::vector<std::pair<uint64_t, std::string>>& out) {
    for (std::size_t pos = 0; pos < spec.size();) {
        const std::size_t end = std::min(spec.find(',', pos), spec.size());
        const std::string item = spec.substr(pos, end - pos);
        pos = end + 1;
        const std::size_t c = item.find(':');
        uint64_t frame = 0;
        if (c == std::string::npos || c + 1 >= item.size() || !parseNumber(item.substr(0, c), frame, 10))
            return item.empty() ? "(vazio)" : item;
        out.emplace_back(frame, item.substr(c + 1));
    }
    return {};
}

/// @brief Grava @p rgba (página visível, RGBA) como PPM P6 em @p path. @return false se falhar.
bool writePpm(const std::string& path, const std::vector<uint32_t>& rgba) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", rea::core::video::kVisibleWidth, rea::core::video::kVisibleHeight);
    for (uint32_t px : rgba) {
        const unsigned char rgb[3] = {static_cast<unsigned char>(px >> 24), static_cast<unsigned char>(px >> 16),
                                      static_cast<unsigned char>(px >> 8)};
        std::fwrite(rgb, 1, 3, f);
    }
    const bool ok = !std::ferror(f);
    return std::fclose(f) == 0 && ok;
}

/// @brief Pede confirmação para rodar com o programa de outra versão. A opção padrão é sair.
/// @param found Chip de programa da outra versão e onde ele está (ex.: `cusa-l21.u10 (rom/crusnusa21.zip)`), mostrado na mensagem.
/// @param allow `--allow-other-version`: aceita sem perguntar (execuções sem janela).
/// @return true se o usuário aceitou.
bool confirmOtherVersion(const std::string& found, bool allow) {
    const std::string msg =
        "Foi encontrada a ROM de outra versão do jogo: " + found +
        "\n\nEste port é a tradução, endereço por endereço, da versão L4.1 (cusa-l41.u10 a u13). Com outra versão "
        "as rotinas e os dados ficam em outros endereços: o jogo pode mostrar telas erradas, travar ou se comportar "
        "de forma imprevisível. Use só para testes.";
    std::fprintf(stderr, "Aviso: %s\n", msg.c_str());
    if (allow) {
        std::fprintf(stderr, "--allow-other-version: continuando\n");
        return true;
    }
#ifdef __EMSCRIPTEN__
    return rea_web_confirm((msg + "\n\nOK = continuar mesmo assim; Cancelar = sair.").c_str()) != 0;
#endif
    const SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Sair"},
        {0, 1, "Continuar mesmo assim"},
    };
    const SDL_MessageBoxData box{SDL_MESSAGEBOX_WARNING, nullptr, "REA Cruis'n — ROM de outra versão", msg.c_str(),
                                 2, buttons, nullptr};
    int choice = 0;
    if (SDL_ShowMessageBox(&box, &choice) != 0) {
        // Sem janela (ex.: SDL_VIDEODRIVER=dummy) não há como perguntar.
        std::fprintf(stderr, "Sem janela para confirmar: rode com --allow-other-version para continuar mesmo "
                             "assim.\n");
        return false;
    }
    return choice == 1;
}

/// @brief Janela de escolha quando a ROM traz mais de uma versão do programa (ex.: set "merged" do MAME).
/// @return Índice em @p versions; −1 = sair; −2 = sem janela para perguntar.
int chooseVersion(const std::vector<rea::core::ResourceManager::RomVersion>& versions) {
    std::string msg = "Foram encontradas " + std::to_string(versions.size()) + " versões do programa do jogo:\n\n";
    for (const auto& v : versions)
        msg += "  • " + v.label() + " — " + v.chip + " em " + std::filesystem::path(v.where).filename().string() +
               (v.supported() ? " (suportada)\n" : " (só para testes)\n");
    msg += "\nEste port é a tradução, endereço por endereço, da versão 4.1. Com outra versão as rotinas e os dados "
           "ficam em outros endereços: o jogo pode mostrar telas erradas, travar ou se comportar de forma "
           "imprevisível.\n\nQual versão usar?";
#ifdef __EMSCRIPTEN__
    msg += "\n";
    for (std::size_t i = 0; i < versions.size(); ++i)
        msg += "  " + std::to_string(i + 1) + " = Versão " + versions[i].label() + (versions[i].supported() ? "" : " (teste)") + "\n";
    msg += "  Cancelar = sair\n\nDigite o número:";
    const int n = rea_web_prompt_int(msg.c_str(), 1);
    return n >= 1 && static_cast<std::size_t>(n) <= versions.size() ? n - 1 : -1;
#endif
    std::vector<std::string> labels;
    std::vector<SDL_MessageBoxButtonData> buttons;
    labels.reserve(versions.size());
    for (std::size_t i = 0; i < versions.size(); ++i) {
        labels.push_back("Versão " + versions[i].label() + (versions[i].supported() ? "" : " (teste)"));
        // Enter escolhe a 4.1 (a findVersions() a põe primeiro, se existir).
        const Uint32 flags = i == 0 && versions[i].supported() ? static_cast<Uint32>(SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT) : 0u;
        buttons.push_back({flags, static_cast<int>(i), labels.back().c_str()});
    }
    buttons.push_back({SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, -1, "Sair"});
#if SDL_VERSION_ATLEAST(2, 0, 12)
    const Uint32 box_flags = SDL_MESSAGEBOX_INFORMATION | SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT;
#else
    const Uint32 box_flags = SDL_MESSAGEBOX_INFORMATION;
#endif
    const SDL_MessageBoxData box{box_flags, nullptr, "REA Cruis'n — escolha a versão da ROM", msg.c_str(),
                                 static_cast<int>(buttons.size()), buttons.data(), nullptr};
    int choice = -1;
    if (SDL_ShowMessageBox(&box, &choice) != 0) return -2;
    return choice;
}

/// @brief Confere cada chip da ROM com a versão L4.1 e avisa (console e, se algo difere, caixa de mensagem).
/// @details Só avisa: um dump levemente diferente ainda pode ser jogável. Um chip ausente já tem aviso próprio
///          (sem texturas ou sem som); aqui ele aparece na contagem.
void romCheck(const rea::core::RomSource& rom, const rea::core::ResourceManager& res, bool other_version) {
    using rea::core::ChipCheck;
    const auto& known = rea::core::knownL41Chips();
    auto checks = rea::core::verifyRom(rom);
    // Com o programa de outra versão (já confirmado pelo usuário), só gráficos e som são conferidos.
    if (other_version)
        checks.erase(std::remove_if(checks.begin(), checks.end(),
                                    [](const ChipCheck& c) { return c.role == "programa"; }),
                     checks.end());
    std::size_t ok = 0;
    std::string different, missing;
    for (ChipCheck& c : checks) {
        const auto k = std::find_if(known.begin(), known.end(), [&](const auto& kc) { return c.name == kc.name; });
        // O som abre a própria fonte (openRom("cusa.u2")): os chips que faltam aqui podem vir de outra
        // (ex.: pasta extraída sem o som e o .zip com ele) e não são "ausentes".
        if (c.state == ChipCheck::State::Missing)
            if (const auto other = res.openRom(c.name)) c = rea::core::checkChip(*other, *k);
        if (c.state == ChipCheck::State::Ok) ++ok;
        char crc[16];
        std::snprintf(crc, sizeof crc, "%08X", c.crc);
        if (c.state == ChipCheck::State::Different) different += "  " + c.name + " (" + c.role + ", CRC " + crc + ")\n";
        if (c.state == ChipCheck::State::Missing) missing += " " + c.name;
    }
    std::printf("ROM: %zu/%zu chips %sconferem com a versão L4.1\n", ok, checks.size(),
                other_version ? "de gráficos e som " : "");
    if (!missing.empty()) std::printf("ROM: ausentes:%s\n", missing.c_str());
    if (different.empty() && missing.empty()) return;
    // Caixa de mensagem: quem abre com duplo clique não vê o console.
    std::string msg;
    if (!different.empty())
        msg += "Chips da ROM diferentes da versão L4.1 (dump ruim, modificado ou de outra versão):\n" + different;
    if (!missing.empty()) msg += "Chips ausentes (sem gráficos ou sem som):" + missing + "\n";
    msg += "O jogo vai rodar, mas pode se comportar diferente do original.";
    std::fprintf(stderr, "Aviso: %s\n", msg.c_str());
    showMessage(SDL_MESSAGEBOX_WARNING, "REA Cruis'n", msg);
}

/// Texto do `--help`: as opções do executável (a tabela completa está no README, seção 3.11).
constexpr const char* kUsage =
    "Uso: rea_cruisn [opções]\n"
    "  --frames N             roda N quadros e sai\n"
    "  --screenshot ARQ.ppm   salva a página exibida ao sair\n"
    "  --shots Q:ARQ,...      capturas extras por quadro\n"
    "  --pending              lista as rotinas não traduzidas que foram chamadas\n"
    "  --list-translated      lista os endereços traduzidos e sai\n"
    "  --press ROTEIRO        entradas roteirizadas (ação@início-fim[:valor],...)\n"
    "  --peek ENDS            mostra palavras da memória ao sair (0xC8C5,...)\n"
    "  --poke ESCRITAS        força valores na memória (0xEND=VALOR@início-fim,...)\n"
    "  --wav ARQ.wav          grava a saída da placa de som\n"
    "  --dump-textures DIR    exporta a RAM de texturas como DIR/page_NN.bmp\n"
    "  --link master|slave    liga dois executáveis pelo cabo de link\n"
    "  --link-file ARQ        arquivo compartilhado do cabo (padrão build/link.shm)\n"
    "  --cmos ARQ|none        CMOS usada (padrão save/cmos.nv)\n"
    "  --lang CÓDIGO          idioma dos textos de console (padrão en)\n"
    "  --root DIR             raiz dos recursos (assets/, rom/, save/)\n"
    "  --debug-overlay        painel de depuração sobre a tela\n"
    "  --fullscreen           abre em tela cheia (F11 ou Alt+Enter alternam)\n"
    "  --hw-watchdog N        RESET após N quadros sem alimentar o watchdog (0 = desligado)\n"
    "  --allow-other-version  roda com a ROM de outra versão sem perguntar (só para testes)\n"
    "  --versao V             usa a versão V do programa (ex.: 2.1) quando a ROM tiver mais de uma\n"
    "  --console              Windows: abre também a janela de texto com as mensagens (depuração)\n"
    "  --help, -h             mostra esta ajuda\n";

} // namespace

#if defined(__EMSCRIPTEN__) || defined(_WIN32)
/// @brief Corpo do programa; o main() no fim do arquivo o embrulha (navegador: avisa a página no fim;
///        Windows: abre a janela de texto de --console).
static int reaMain(int argc, char* argv[]) {
#else
int main(int argc, char* argv[]) {
#endif
    using namespace rea;
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);  // as mensagens são UTF-8; sem isto o console do Windows mostra os acentos errados
#endif

    std::string lang = "en";  // idioma original do arcade
    uint64_t max_frames = 0;
    std::string root;  // --root; vazio = procura (ResourceManager::findRoot)
    bool allow_other_version = false;  // --allow-other-version
    std::string version_arg;           // --versao: versão do programa (ex.: 2.1)
    bool list_pending = false;
    std::string screenshot;
    std::string press;
    std::string peek;  // endereços a mostrar no fim (depuração), separados por vírgula
    std::string poke;  // escritas forçadas por faixa de quadros (depuração): 0xADDR=VAL@INI-FIM,...
    std::string shots; // capturas extras: QUADRO:arquivo.ppm,...
    std::string dump_textures;  // diretório: exporta a RAM de texturas (page_NN.bmp) no fim
    std::string wav;   // grava a saída da placa DCS (WAV mono 16 bits, 31 250 Hz)
    std::string link_side;                    // --link master|slave: liga a DIP Linking e o cabo entre processos
    std::string link_file = "build/link.shm";  // --link-file: arquivo compartilhado do cabo
    std::string cmos_arg;                      // --cmos: arquivo da NVRAM ("none" = volátil)
    bool debug_overlay = false;                // --debug-overlay: barras de entrada sobre a tela (não é do arcade)
    uint32_t hw_watchdog = 0;                  // --hw-watchdog: prazo em quadros (0 = desligado, como no MAME)
    bool list_translated = false;
    bool fullscreen = false;                   // --fullscreen: abre em tela cheia (F11/Alt+Enter alternam)
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--lang" && i + 1 < argc) lang = argv[++i];
        else if (a == "--frames" && i + 1 < argc) max_frames = std::strtoull(argv[++i], nullptr, 10);
        else if (a == "--root" && i + 1 < argc) root = argv[++i];
        else if (a == "--pending") list_pending = true;
        else if (a == "--list-translated") list_translated = true;  // lista os endereços traduzidos e sai
        else if (a == "--screenshot" && i + 1 < argc) screenshot = argv[++i];
        else if (a == "--press" && i + 1 < argc) press = argv[++i];  // roteiro de entradas por quadro
        else if (a == "--peek" && i + 1 < argc) peek = argv[++i];    // ex.: 0xC8C5,0xE573
        else if (a == "--poke" && i + 1 < argc) poke = argv[++i];    // ex.: 0xE604=99@3000-20000
        else if (a == "--shots" && i + 1 < argc) shots = argv[++i];  // ex.: 9000:build/a.ppm,9500:build/b.ppm
        else if (a == "--wav" && i + 1 < argc) wav = argv[++i];      // ex.: build/som.wav
        else if (a == "--dump-textures" && i + 1 < argc) dump_textures = argv[++i];  // páginas da RAM de texturas em BMP
        else if (a == "--link" && i + 1 < argc) link_side = argv[++i];
        else if (a == "--link-file" && i + 1 < argc) link_file = argv[++i];
        else if (a == "--cmos" && i + 1 < argc) cmos_arg = argv[++i];
        else if (a == "--debug-overlay") debug_overlay = true;
        else if (a == "--fullscreen") fullscreen = true;
        else if (a == "--console") {}  // tratada no main() do Windows; nos outros sistemas as mensagens já vão ao terminal
        else if (a == "--allow-other-version") allow_other_version = true;
        else if (a == "--versao" && i + 1 < argc) version_arg = argv[++i];
        else if (a == "--hw-watchdog" && i + 1 < argc) hw_watchdog = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        else if (a == "--help" || a == "-h") {
            std::fputs(kUsage, stdout);
            return 0;
        } else {
            // Opção desconhecida ou sem o valor: antes era ignorada em silêncio (ex.: "--frame 100" rodava sem limite).
            std::fprintf(stderr, "Opção inválida ou sem valor: %s\n%s", a.c_str(), kUsage);
            return 2;
        }
    }
    if (const std::string bad = core::io::ScriptedInput::firstInvalid(press); !bad.empty()) {
        // Antes, uma ação com erro de digitação (ex.: "gaz@3200-9999:255") era ignorada em silêncio.
        std::fprintf(stderr,
                     "--press: item inválido: %s\n"
                     "  formato: ação@início-fim[:valor]; ações: coin1-coin4, start, service, "
                     "view1-view3, radio, test, volup, voldown, gas, brake, wheel, gear\n",
                     bad.c_str());
        return 2;
    }
    // --poke e --shots: antes um item com erro era ignorado em silêncio, como no --press.
    std::vector<Poke> pokes;
    if (const std::string bad = parsePokes(poke, pokes); !bad.empty()) {
        std::fprintf(stderr, "--poke: item inválido: %s\n  formato: 0xENDEREÇO=VALOR[@INÍCIO[-FIM]]\n",
                     bad.c_str());
        return 2;
    }
    std::vector<std::pair<uint64_t, std::string>> shot_list;
    if (const std::string bad = parseShots(shots, shot_list); !bad.empty()) {
        std::fprintf(stderr, "--shots: item inválido: %s\n  formato: QUADRO:arquivo.ppm\n", bad.c_str());
        return 2;
    }
    std::printf("Taxa de quadros: %.3f Hz\n", kArcadeRefreshHz);

    // Raiz dos recursos (assets/, rom/, save/): --root, REA_ROOT, pasta do executável (e até 3 acima),
    // pasta atual e, por último, a pasta do código-fonte (execução direto de build/bin).
    std::vector<std::filesystem::path> roots;
    if (!root.empty()) roots.emplace_back(root);
    if (const char* env = std::getenv("REA_ROOT"); env && *env) roots.emplace_back(env);
    if (char* base = SDL_GetBasePath()) {
        std::filesystem::path p = std::filesystem::path(base);
        SDL_free(base);
        if (!p.has_filename()) p = p.parent_path();  // tira a barra final
        for (int up = 0; up < 4 && !p.empty(); ++up, p = p.parent_path()) {
            roots.push_back(p);
            if (p == p.parent_path()) break;
        }
    }
    roots.push_back(std::filesystem::current_path());
    roots.emplace_back(REA_SOURCE_DIR);
    core::ResourceManager res(root.empty() ? core::ResourceManager::findRoot(roots) : std::filesystem::path(root));
    std::printf("Raiz: %s\n", res.root().string().c_str());
    const std::filesystem::path cmos_file =
        cmos_arg == "none" ? std::filesystem::path() : cmos_arg.empty() ? res.save(link_side == "slave" ? "cmos_slave.nv" : "cmos.nv") : std::filesystem::path(cmos_arg);
    ui::LocalizationManager i18n(res.asset("lang"));
    if (!i18n.setLanguage(lang)) std::cerr << "Idioma não encontrado: " << lang << '\n';

    // --- ROM de programa ---------------------------------------------------
    core::MemoryBus bus;
    core::Tms32031State cpu;
    core::CountingSubroutines pending;         // sub-rotinas ainda não traduzidas (contadas)
    core::SubroutineDispatcher subs(pending);  // traduzidas executam de verdade
    core::boot::registerRoutines(subs);
    core::cmos::registerRoutines(subs);
    core::io::registerRoutines(subs);
    core::prof::registerRoutines(subs);
    core::scene::registerRoutines(subs);       // lista de objetos: ordenação/reagrupamento (0x7187)
    core::link::registerPacketRoutines(subs);  // pacote de envio do link (0x7989/0x798F/0x7998/0x79AF)
    core::link::registerMessageRoutines(subs); // mensagens do link: leitura 0x794C, tratadores, emissores, 0x793A
    core::gfx::registerRoutines(subs);         // descompressor LZW de texturas (0xA2F9/0xA323/0xA3C2)
    core::proc::Scheduler scheduler;           // processos cooperativos (0xA872/0xA874/0xA896)
    core::proc::registerRoutines(subs, scheduler);
    core::game::registerRoutines(subs);        // (re)início do jogo 0x9503 e casos
    core::game::registerAttract(subs);         // modo de atração 0x3859
    core::game::registerHiscoreScene(subs);    // cena de recordes 3D 0x3889
    core::math::registerTrig(subs);            // seno/cosseno 0x92DF/0x92E2
    core::math::registerFloatMath(subs);       // 0xA509, 0xA5F1, 0x931A, 0x9308, 0x941E…
    core::math::registerMatrix(subs);          // matrizes 3×3 0x947F/0x93F1/0x9494/0x9343
    core::game::registerTitleScreen(subs);     // tela de título 0xA93B, 0x1DCF, 0x59E3
    core::game::registerCreditsDisplay(subs);   // textos de créditos 0x745E, preços 0x73A0, 0x77E7
    core::game::registerCarProximity(subs);    // simulação 0x21D0 e laços de proximidade
    core::game::registerAttractDemo(subs);     // demonstração do atrato 0x559E
    core::game::registerDemoCamera(subs);      // roteiro de câmera da demo 0x5624..0x591A
    core::track::registerTrackLoader(subs);    // carga da pista e nós [AA2F]
    core::track::registerTrackStream(subs);    // fluxo de trechos 0x3F8D/0x3FE3
    core::track::registerGroundHeight(subs);   // altura do chão 0x1F8B/0x20FF
    core::video::registerSkyRenderer(subs);    // céu da pista 0x822B
    core::game::registerDroneCar(subs);        // carros da CPU 0x50CB (criação)
    core::game::registerCarPhysics(subs);      // física dos carros 0x2C39/0x1FF6
    core::game::registerCarDynamics(subs);     // dinâmica de direção 0x2C18
    core::game::registerDroneAi(subs);         // IA dos carros 0x513A/0x5140
    core::game::registerRaceStart(subs);       // largada 0x1C9B (contagem, libera o grid)
    core::game::registerCarWheels(subs);       // rodas e carroceria 0x8F68
    core::game::registerRaceTraffic(subs);     // posição 0x659F e geradores 0x65ED/0x6639
    core::game::registerCarCollision(subs);    // caixas orientadas 0x2863
    core::game::registerSceneryCollision(subs); // carro × objetos do cenário 0x221F
    core::game::registerSmokeEffects(subs);     // fumaça de batida/raspão/arrancada 0x8928
    core::game::registerTrackProps(subs);       // objetos animados do cenário (tabela [4500])
    core::game::registerRoamingObjects(subs);   // grupo 0x260/0x261 (tipos 0x470–0x476) e objetos 0x498
    core::game::registerSpecialCar(subs);       // carro especial 0x270 da pista 2 (0x7CFA)
    core::game::registerMusicTitle(subs);       // nome da música na tela (0x90B9)
    core::game::registerLinkCar(subs);          // carro do outro gabinete (0xA66E) e objeto [C98B] (0xA6F8)
    core::game::registerLinkIntro(subs);        // abertura do link (0x154D, caso −7) e caso −6 (0xA1EA)
    core::game::registerEnding(subs);           // encerramento 0x8481 e painéis 0xB07B–0xB07F
    core::game::registerRoadsideEvents(subs);   // eventos das pistas 9 e 11 (0x69A8–0x6C8B)
    core::game::registerCredits(subs);          // créditos finais 0xA1F1
    core::game::registerCoinStart(subs);        // moedas 0x7314…0x7360 e START 0x1D92
    core::game::registerGameFlow(subs);         // fluxo da partida 0x1775 (chamadas locais na pilha do PCB)
    core::game::registerCourseSelect(subs);     // seleção de percurso 0x6C8F
    core::game::registerCarSelect(subs);        // seleção de câmbio e carro 0x59EC/0x1942/0x1AB0
    core::game::registerRaceEntry(subs);        // garagem → pista 0x18A6/0x1859/0x196B
    core::game::registerPlayerCar(subs);        // carro do jogador 0x29B8 (pedais, câmera, sons)
    core::game::registerPlayerEffects(subs);    // chamas 0xAD93 e fumaça dos pneus 0x86FC
    core::game::registerRaceHud(subs);          // HUD 0x9CFF, radar 0x60DF, letreiro 0x1F56, logotipo 0x86C5
    core::game::registerRaceEvents(subs);       // eventos de pista 0xACCA, chegada 0x3998, objetos 0x691B
    core::game::registerObjectEffects(subs);    // nuvem 0x677E, partículas 0x6822, detritos 0x685D
    core::game::registerCarCrash(subs);         // batida forte 0x23EF, carro arremessado 0x2478
    core::game::registerPostRace(subs);         // pós-corrida 0x3A46/0x3A64 (resultados, continue, etapa seguinte)
    core::game::registerResultsBoards(subs);    // placares da tela de resultados 0x3DCF..0x3F01
    core::game::registerScreenDoors(subs);      // portas de transição 0x5DE0/0x5EC0
    core::game::registerHiscoreEntry(subs);     // iniciais do recorde 0x31A6
    core::game::registerContinueScreen(subs);   // tela de continue 0x1E2C
    core::game::registerTrafficCars(subs);     // tráfego 0x9765 (mesmo sentido) e 0xA3FF (contramão)
    core::io::registerSerialPrinter(subs);     // impressora serial opcional 0x4834 (ajuste 0x34)
    core::video::registerPaletteBlink(subs);   // pisca de paleta 0x1F2A
    core::io::registerCabinetOutputs(subs);    // lâmpadas 0x4E4F, motor do volante 0xAFC2, sequência 0x4FAE
    core::audio::registerSoundCommands(subs);  // comandos de som 0x90E6..0x92C7
    core::video::registerTextOverlay(subs);    // textos sobrepostos 0x774A/0x7720
    core::render::Pipeline3dStats pipe_stats;  // pipeline 3D 0x0064
    core::render::registerPipeline3d(subs, pipe_stats);
    core::scene::registerNodePools(subs);      // listas e pools 0x8EF1..0x9035, 0x7648
    core::scene::registerObjectQuery(subs);    // buscas por tipo 0x36B3..0x3737 e 0x9EDB
    uint64_t object_hangs = 0;                 // travas de 0x701F (textura não encontrada)
    core::scene::registerSceneObject(subs, object_hangs);  // objetos de cena 0x7025..0x70C9, 0x715F
    core::math::registerRoutines(subs);        // divisão/resto (0xA531/0xA5A9) e aleatório (0x8EB7)
    core::video::registerRoutines(subs);       // páginas, preenchimentos, pixel, texto e números
    core::video::registerPaletteSlots(subs);   // slots de paleta 0x9DF6..0x9FB5 (fila servida pela INT0)
    std::vector<std::string> boot_lines;       // linhas da tela de carga do RESET (chaves I18N)
    core::boot::registerBootScreens(subs, boot_lines);
    core::mainloop::MainLoop main_loop(bus, subs);  // 0x4BAF
    core::boot::BootSequence boot(bus, subs);       // RESET + telas de recuperação
    // Executa a partida e imprime as telas de recuperação traduzidas.
    auto boot_game = [&]() {
        boot_lines.clear();
        const uint32_t pc = boot.run(cpu);
        for (const auto& key : boot_lines) std::printf("  %s\n", i18n.tr(key).c_str());
        for (const auto& key : boot.messagesShown()) std::printf("  [%s]\n", i18n.tr(key).c_str());
        main_loop.reset();
        return pc;
    };
    core::io::CabinetIo cabinet;               // portas IN0/IN1/DIP + ADC0844 (fonte da INT3)
    // DIPs: SW2:6 (bit 2) em 0 liga o Linking; SW2:8 (bit 0) em 1 = Slave.
    uint16_t dsw = core::io::kDefaultDsw;
    std::unique_ptr<platform::ShmLinkWire> link_wire;
#ifdef __EMSCRIPTEN__
    if (!link_side.empty()) {
        std::fprintf(stderr, "--link não existe na versão para navegador\n");
        return 2;
    }
#endif
    if (link_side == "master" || link_side == "slave") {
        dsw = static_cast<uint16_t>(dsw & ~0x0004u);
        if (link_side == "slave") dsw = static_cast<uint16_t>(dsw | 0x0001u);
        link_wire = std::make_unique<platform::ShmLinkWire>(link_file, link_side == "slave" ? 1 : 0);
        std::printf("Link: %s via %s (%s)\n", link_side.c_str(), link_file.c_str(),
                    link_wire->ok() ? "ok" : "falhou");
        if (!link_wire->ok()) link_wire.reset();
    }
    cabinet.setDsw(dsw);                       // DIPs já no RESET (a tela de carga lê Master/Slave)
    bus.attachDevice(&cabinet);
    core::video::TexturePack texture_pack;     // re-skin: assets/textures/page_NN.bmp (8 bits, 256 x 256)
    if (const int n = texture_pack.loadDir(res.asset("textures")); n > 0)
        std::printf("Texturas externas: %d página(s) de assets/textures\n", n);
    if (const auto& bad = texture_pack.rejected(); !bad.empty()) {
        std::string list;
        for (const auto& b : bad) list += " " + b;
        std::fprintf(stderr, "Aviso: texturas ignoradas:%s (precisam ser BMP 8 bits indexado, 256x256, sem "
                     "compressão)\n", list.c_str());
    }
    core::video::DmaRenderer dma(bus);         // fila de quads 0x600000 + disparo 0x980083
    dma.setTexturePack(&texture_pack);
    bus.attachDevice(&dma);
    core::io::LinkPort link_port;              // placa de link 0x997000 (sozinha: loopback do MAME)
    bus.attachDevice(&link_port);
    if (link_wire) link_port.connectWire(link_wire.get());
    core::audio::AudioManager sound;           // placa de som DCS (ADSP-2105): trinco 0x9A0000, reset em 0x994000
    if (sound.load(res, bus)) {
        std::printf("DCS: ROMs de som cusa.u2-u9 carregadas\n");
    } else {
        std::printf("Aviso: ROMs de som ausentes (cusa.u2-u9); sem áudio.\n");
    }
    if (const int n = sound.loadOverrides(res.asset("audio")); n > 0)
        std::printf("Sons externos: %d comando(s) de assets/audio\n", n);
    sound.setRecording(!wav.empty());          // --wav
    bool rom_ok = false;
    bool other_version = false;  // programa de outra versão, aceito pelo usuário
    bool other_declined = false;
    std::array<std::string, 4> program_chips = kProgramChips;
    std::unique_ptr<core::RomSource> rom;
    if (list_translated) {
        rom = res.openRom(program_chips[0]);
    } else {
        // Versões do programa nas fontes da ROM: com mais de uma, o usuário escolhe; só outra que não a 4.1,
        // avisa e pergunta. --versao escolhe sem janela.
        const auto versions = res.findVersions();
        std::optional<core::ResourceManager::RomVersion> chosen;
        if (!version_arg.empty()) {
            std::string names;
            for (const auto& v : versions) {
                names += " " + v.label();
                if (v.label() == version_arg || v.chip == version_arg) chosen = v;
            }
            if (!chosen) {
                std::fprintf(stderr, "--versao: versão %s não encontrada; disponíveis:%s\n", version_arg.c_str(),
                             names.empty() ? " nenhuma" : names.c_str());
                return 2;
            }
        } else if (versions.size() > 1) {
            const int pick = chooseVersion(versions);
            if (pick >= 0) {
                chosen = versions[static_cast<std::size_t>(pick)];
            } else if (pick == -2 && versions.front().supported()) {
                std::printf("ROM: há %zu versões do programa; sem janela para perguntar, usando a 4.1 (--versao "
                            "escolhe outra)\n", versions.size());
                chosen = versions.front();
            } else {
                if (pick == -2)
                    std::fprintf(stderr, "ROM: há %zu versões do programa e nenhuma é a 4.1; sem janela para "
                                         "perguntar, use --versao.\n", versions.size());
                other_declined = true;
            }
        } else if (versions.size() == 1) {
            if (versions.front().supported() || confirmOtherVersion(versions.front().text(), allow_other_version))
                chosen = versions.front();
            else
                other_declined = true;
        }
        if (chosen && chosen->supported()) {
            rom = res.openRom(program_chips[0]);
        } else if (chosen) {
            if (!version_arg.empty() || versions.size() > 1)
                std::fprintf(stderr, "Aviso: versão %s escolhida; este port é a tradução da 4.1 e pode mostrar telas "
                                     "erradas ou travar com outra versão.\n", chosen->label().c_str());
            program_chips = chosen->programChips();
            rom = res.openRomChain(program_chips[0]);  // gráficos e som podem estar em outro zip (o da 4.1)
            other_version = rom != nullptr;
        }
    }
    if (rom) {
        if (bus.loadProgramRom(*rom, program_chips)) {
            if (!bus.loadGraphicsRoms(*rom))
                std::printf("Aviso: EPROMs gráficas ausentes (cusa.u14-u29); texturas ficam vazias.\n");
            std::printf("ROM: %s\n", rom->describe().c_str());
            // assets/rom_extracted/ tem prioridade sobre o .zip: quem troca o .zip continuaria, sem perceber,
            // com os arquivos extraídos antes.
            // Só avisa se algum .zip de rom/ não for a L4.1 (ex.: trocado por outro): igual, não há o que avisar.
            if (rom->describe() == res.extractedDir().string()) {
                for (const auto& z : res.romZips()) {
                    const core::ZipRomSource zip(z);
                    if (zip.ok() && !core::zipMatchesL41(zip))
                        std::printf("Aviso: usando os arquivos extraídos de assets/rom_extracted/, que têm "
                                    "prioridade; o %s em rom/ é diferente e está sendo ignorado. Para usá-lo, apague "
                                    "ou renomeie assets/rom_extracted/.\n",
                                    z.filename().string().c_str());
                }
            }
            if (other_version)
                std::printf("ROM: programa de outra versão: %s .. %s (não conferido)\n", program_chips[0].c_str(), program_chips[3].c_str());
            romCheck(*rom, res, other_version);
            std::printf("RESET=0x%06X INT0=0x%06X INT1=0x%06X INT2=0x%06X INT3=0x%06X\n",
                        bus.vector(core::MemoryBus::Vector::Reset), bus.vector(core::MemoryBus::Vector::Int0),
                        bus.vector(core::MemoryBus::Vector::Int1), bus.vector(core::MemoryBus::Vector::Int2),
                        bus.vector(core::MemoryBus::Vector::Int3));
            std::printf("ROM[0x7456] = \"%s\"  ->  %s\n", bus.readPackedString(0x7456).c_str(),
                        i18n.tr("INSERT_COINS").c_str());

            // A CMOS (bateria no gabinete) volta de save/cmos.nv; o RESET roda no primeiro quadro.
            if (!cmos_file.empty()) {
                std::error_code ec;
                const bool exists = std::filesystem::exists(cmos_file, ec);
                if (core::cmos::loadCmosFile(bus, cmos_file)) {
                    std::printf("CMOS: %s (restaurada)\n", cmos_file.string().c_str());
                } else if (exists) {
                    // Arquivo com tamanho errado (ex.: NVRAM de outro emulador): antes era sobrescrito sem aviso.
                    // Fica guardado ao lado, e a CMOS começa nova (ajustes de fábrica e calibração).
                    std::filesystem::path bad = cmos_file;
                    bad += ".invalida";
                    std::filesystem::copy_file(cmos_file, bad, std::filesystem::copy_options::overwrite_existing, ec);
                    std::fprintf(stderr, "CMOS: %s inválida (tamanho errado); começa nova, cópia em %s\n", cmos_file.string().c_str(), ec ? "-" : bad.string().c_str());
                } else {
                    std::printf("CMOS: %s (nova)\n", cmos_file.string().c_str());
                }
            }
            rom_ok = true;
        }
    }
    // Sem ROM o jogo não tem o que rodar: antes abria uma janela preta e o aviso só saía no console (invisível
    // ao abrir com duplo clique). Agora explica numa caixa de mensagem e sai com erro. --list-translated não
    // precisa da ROM.
    if (!rom_ok && !list_translated) {
        if (other_declined) return EXIT_FAILURE;  // o usuário escolheu sair (ou não havia janela para perguntar)
#ifdef __EMSCRIPTEN__
        const std::string msg =
            "ROM do jogo não encontrada ou incompleta.\n"
            "Escolha na página o .zip da ROM (set MAME \"crusnusa\", versão L4.1).";
#else
        const std::string where = (res.root() / "rom").string();
        const std::string msg =
            "ROM do jogo não encontrada ou incompleta.\n"
            "Coloque o .zip da ROM (set MAME \"crusnusa\", versão L4.1) na pasta:\n" + where;
#endif
        std::cerr << msg << '\n';
        showMessage(SDL_MESSAGEBOX_ERROR, "REA Cruis'n", msg);
        return EXIT_FAILURE;
    }

    // --- Plataforma ---------------------------------------------------------
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) != 0) {
        // Áudio pode faltar (ex.: container sem PulseAudio): tenta sem ele.
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
            std::cerr << "SDL_Init: " << SDL_GetError() << '\n';
            return EXIT_FAILURE;
        }
    }
    int exit_code = EXIT_SUCCESS;
    {
        platform::sdl::SdlRenderer renderer("REA Cruis'n", 2);
        if (!renderer.ok()) {
            SDL_Quit();
            return EXIT_FAILURE;
        }
        const std::string title = "REA Cruis'n — " + i18n.tr("INSERT_COINS") +
                                  (other_version ? " — ROM de outra versão (teste)" : "");
        renderer.setTitle(title.c_str());
        if (fullscreen) renderer.toggleFullscreen();  // --fullscreen
        // Mapeamentos extras de controles (formato do SDL_GameControllerDB): volantes e controles que o SDL
        // não reconhece sozinho passam a ser "game controllers" com volante, pedais e botões.
        if (const auto db = res.asset("config") / "gamecontrollerdb.txt"; std::filesystem::exists(db)) {
            const int n = SDL_GameControllerAddMappingsFromFile(db.string().c_str());
            std::printf("Controles: %d mapeamento(s) de %s\n", n < 0 ? 0 : n,
                        db.string().c_str());
        }
        platform::sdl::SdlInput real_input;
        core::io::ScriptedInput input(real_input, press);  // --press: ações por faixa de quadros
        platform::sdl::SdlAudio audio;
        sound.setOutput(&audio);
        platform::sdl::SdlClock clock;

        // InputManager: estado da plataforma -> portas ativas em 0 + canais do ADC.
        core::io::InputManager inputs(input, cabinet, dsw);
        core::FrameRunner runner(bus, subs, cabinet);
        // Esperas ocupadas dentro de rotinas (ex.: timeouts do link em quadros) rodam quadros aqui.
        core::FrameBusyWait busy_wait(runner);
        busy_wait.setHardwareWatchdog(hw_watchdog);
        core::boot::registerPowerOnPauses(subs, busy_wait);  // pausas do código de falha no LED (0x632F)
        // As esperas do link também avançam o relógio do cabo entre processos.
        struct ClockedWait final : core::IBusyWait {
            core::IBusyWait& inner;
            platform::ShmLinkWire* wire;
            bool spinning = false;  ///< dentro de uma espera do link (o quadro pode terminar nela)
            ClockedWait(core::IBusyWait& i, platform::ShmLinkWire* w) : inner(i), wire(w) {}
            bool spin(core::Tms32031State& cpu, uint32_t n) override {
                if (wire) wire->advance(n);
                spinning = true;
                const bool ok = inner.spin(cpu, n);
                spinning = false;
                return ok;
            }
            void feedWatchdog() override { inner.feedWatchdog(); }
        } link_wait(busy_wait, link_wire.get());
        core::link::LinkRoutines link(link_wait);  // 0x7F5D: troca de pacotes (só com a DIP Linking ligada)
        link.registerIn(subs);
        core::service::ServiceMenu service(busy_wait);  // menu de serviço 0x0699 (esperas rodam quadros)
        service.registerIn(subs);
        if (list_translated) {
            const auto addrs = subs.addresses();
            std::printf("Endereços traduzidos: %zu\n", addrs.size());
            for (uint32_t a : addrs) std::printf("0x%04X\n", a);
            return EXIT_SUCCESS;  // os destrutores fecham a janela; SDL_Quit não é necessário na saída
        }
        std::vector<uint32_t> shot_buf;
        auto savePpm = [&](const std::string& path) {
            core::video::composeDisplay(bus, shot_buf);
            if (!writePpm(path, shot_buf))
                std::fprintf(stderr, "Captura: falha ao gravar %s\n", path.c_str());
        };
        core::ArcadeInputState last;
        core::GameLoop loop(clock, input, kArcadeRefreshHz);
        loop.onSlow([](double speed) {
            std::fprintf(stderr,
                         "Aviso: o jogo está rodando a %.0f%% da velocidade do arcade (câmera lenta, som falhando); "
                         "compile em Release ou RelWithDebInfo\n",
                         speed * 100.0);
        });
        // Código principal da CPU numa corrotina: um resume() por quadro. As esperas ocupadas
        // (link, menu de serviço) devolvem o controle a cada quadro pelo gancho do busy_wait.
        bool need_boot = true;  // o RESET roda dentro da corrotina: a calibração do boot (0x0CC0) precisa de quadros
        auto reboot = [&](const char* why) {
            std::printf("%s\n", why);
            busy_wait.clearReset();
            need_boot = true;
        };
        core::CpuCoroutine cpu_co([&](core::CpuCoroutine& co) {
            busy_wait.setFrameHook([&co] { co.yield(); });
            uint32_t jump_to = 0;  // desvio não local pendente (NonLocalJump)
            for (;;) {
                try {
                    if (need_boot) {
                        need_boot = false;
                        // RESET traduzido (0x4AD5) + recuperação da CMOS + reentrada 0x4B28.
                        const uint32_t next_pc = boot_game();
                        std::printf("RESET: SP=0x%06X IE=0x%X ST=0x%X, %zu rotinas pendentes, continua em 0x%04X\n",
                                    cpu.sp, cpu.ie, cpu.st, pending.counts.size(), next_pc);
                        std::printf("Vídeo: htotal=%u vtotal=%u INT0 na linha %u\n", bus.readWord(0x980026),
                                    bus.readWord(0x98002B), bus.readWord(0x980020) & 0x1FFu);
                        co.yield();
                    }
                    if (jump_to != 0) {
                        const uint32_t pc = jump_to;
                        jump_to = 0;
                        if (pc == 0x4B28) {  // reentrada do RESET (saída do menu de serviço)
                            boot.reenter(cpu);
                            main_loop.reset();
                        } else {
                            subs.call(pc, cpu, bus);  // ex.: 0x0669 (auditorias), 0x0699 (menu)
                        }
                        co.yield();
                    }
                    while (!need_boot) {
                        // Quadro do arcade: INT0 (VBLANK, lê entradas via 0x4CEF) + INT3 x3 (ADC).
                        if (runner.runFrame(cpu) == core::FrameRunner::Result::WatchdogReset) {
                            reboot("Watchdog de software estourou -> RESET");
                        } else {
                            // Código principal até a próxima espera (0x4BAF…).
                            main_loop.step(cpu);
                            if (busy_wait.resetRequested())
                                reboot("Watchdog durante espera ocupada -> RESET");
                        }
                        co.yield();
                    }
                } catch (const core::NonLocalJump& j) {
                    jump_to = j.pc;
                } catch (const core::service::Aborted&) {
                    reboot("Espera do menu interrompida -> RESET");
                    co.yield();
                }
            }
        });
        // Um quadro leva milissegundos: passar do prazo = laço sem fim. No navegador o prazo é menor que os
        // ~10 s depois dos quais o próprio navegador oferece interromper a página.
#ifdef __EMSCRIPTEN__
        constexpr int kStallSeconds = 5;
#else
        constexpr int kStallSeconds = 10;
#endif
        cpu_co.setStallLimit(std::chrono::seconds(kStallSeconds));
        // Grava save/cmos.nv quando a CMOS mudar (conferido a cada ~5 s) e no fim.
        std::vector<uint32_t> cmos_saved = bus.cmosWords();
        bool cmos_failed = false;  // o aviso de falha sai uma vez (antes repetia a cada ~5 s)
        auto saveCmos = [&]() {
            if (cmos_file.empty() || !rom_ok || bus.cmosWords() == cmos_saved) return;
            if (core::cmos::saveCmosFile(bus, cmos_file)) {
                cmos_saved = bus.cmosWords();
#ifdef __EMSCRIPTEN__
                rea_web_sync_save();  // MEMFS -> IndexedDB (sobrevive a recarregar a página)
#endif
                if (cmos_failed) std::printf("CMOS: gravada: %s\n", cmos_file.string().c_str());
                cmos_failed = false;
            } else if (!cmos_failed) {
                std::fprintf(stderr, "CMOS: falha ao gravar %s\n", cmos_file.string().c_str());
                cmos_failed = true;
            }
        };
        loop.onTick([&](const core::ArcadeInputState& in, uint64_t tick) {
            last = in;
            for (const Poke& pk : pokes)
                if (tick >= pk.from && tick <= pk.to) bus.writeWord(pk.addr, pk.value);
            for (const auto& [at, path] : shot_list)
                if (tick == at) savePpm(path);
            inputs.apply(in);
            if (rom_ok && !cpu_co.resume() && cpu_co.stalled()) {
                // A CPU não devolveu o controle no prazo: uma rotina gira sem fim (típico do programa de
                // outra versão, já que as rotinas traduzidas são as da L4.1). A thread da CPU não pode ser
                // parada nem esperada, então o processo termina aqui, sem gravar a CMOS (ela pode estar
                // sendo escrita pela CPU agora). Antes o jogo ficava travado e nem o fechamento da janela
                // nem um SIGTERM o encerravam.
                const std::string msg =
                    other_version ? "O programa de outra versão da ROM travou: as rotinas traduzidas são as da L4.1 "
                                    "e não funcionam com ele.\nO jogo será fechado; use a versão 4.1."
                                  : "O jogo travou: a CPU não devolveu o controle em " + std::to_string(kStallSeconds) +
                                    " segundos.\nO jogo será fechado.";
                std::fprintf(stderr, "Erro: %s\n", msg.c_str());
                std::fflush(stdout);
                showMessage(SDL_MESSAGEBOX_ERROR, "REA Cruis'n", msg);
#ifdef __EMSCRIPTEN__
                rea_web_exited(EXIT_FAILURE);  // a página volta à tela inicial com o aviso
#endif
                std::_Exit(EXIT_FAILURE);
            }
            if (link_wire) link_wire->frameDone(!link_wait.spinning);
            sound.frame(kArcadeRefreshHz);  // 31 250 amostras/s divididas pelos quadros de ~57,9 Hz
            if (tick % 300 == 299) saveCmos();
        });
        std::vector<uint32_t> display;             // página exibida da VRAM em RGBA
        loop.onVblank([&](uint64_t frame) {
            renderer.beginFrame();
            core::video::composeDisplay(bus, display);
            renderer.blit(display.data(), core::video::kVisibleWidth, core::video::kVisibleHeight);
            if (debug_overlay) {  // painel de depuração: entradas lidas pela ROM (não existe no arcade)
                renderer.fillRect(0, 0, platform::sdl::kScreenW, 8,
                                  (frame / 30) % 2 ? 0xFFD700FFu : 0x202020FFu); // pisca "INSERT COINS"
                // Valores FILTRADOS pela ROM (INT3): volante [C8C8], acelerador [C93B], freio [C93C].
                const int wheel = static_cast<int>(bus.readWord(core::irq::adc_vars::kWheel) & 0xFFu);
                const int gas = static_cast<int>(bus.readWord(core::irq::adc_vars::kGas) & 0xFFu);
                const int brake = static_cast<int>(bus.readWord(core::irq::adc_vars::kBrake) & 0xFFu);
                renderer.fillRect(wheel * 2 - 4, 196, 8, 8, 0xFFFFFFFFu);        // volante
                renderer.fillRect(40, 380 - gas, 20, gas, 0x00C000FFu);          // acelerador
                renderer.fillRect(70, 380 - brake, 20, brake, 0xC00000FFu);      // freio
                renderer.fillRect(470, 380 - last.gear * 40, 20, last.gear * 40, 0x3080FFFFu); // marcha
                // Pedido de menu de teste ([C939], alternado pela chave F2 via 0x4D61).
                if (bus.readWord(core::io::input_vars::kTestRequest) != 0)
                    renderer.fillRect(0, 392, platform::sdl::kScreenW, 8, 0xFF00FFFFu);
            }
            renderer.present();
            if (real_input.takeFullscreenToggle()) renderer.toggleFullscreen();  // F11 ou Alt+Enter
        });
        const uint64_t frames = loop.run(max_frames);
        saveCmos();
        if (!dump_textures.empty()) {
            const int n = core::video::TexturePack::dumpRam(bus, [&](int p) { return dma.lastPalette(p); }, dump_textures);
            std::printf("Texturas: %d página(s) em %s\n", n, dump_textures.c_str());
        }
        if (!wav.empty()) {  // WAV PCM mono 16 bits a 31 250 Hz
            const auto& pcm_all = sound.recorded();
            std::size_t nz = 0;
            for (int16_t v : pcm_all) nz += v != 0;
            std::printf("WAV: %s%s (%zu amostras, %zu != 0)\n", wav.c_str(),
                        sound.writeWav(wav) ? "" : " [erro]", pcm_all.size(), nz);
            if (sound.recordingTruncated())
                std::printf("WAV: gravação parou no limite do formato (~19 h)\n");
        }
        if (link_wire)
            std::printf("Link: leituras que esperaram o outro lado=%llu, sem o outro lado=%llu; "
                        "Master ok/erro=%u/%u Slave ok/erro=%u/%u\n",
                        static_cast<unsigned long long>(link_wire->waits()),
                        static_cast<unsigned long long>(link_wire->disconnects()), bus.readWord(0x809A17),
                        bus.readWord(0x809A16), bus.readWord(0x809A19), bus.readWord(0x809A18));
        if (const auto& dcs = sound.board(); dcs.loaded())
            std::printf("Sons externos tocados=%llu; DCS: partidas=%u banco=%u quadros decodificados=%llu bytes recebidos=%llu\n",
                        static_cast<unsigned long long>(sound.overridden()), dcs.boots(), dcs.lastBootBank(), static_cast<unsigned long long>(dcs.framesDecoded()),
                        static_cast<unsigned long long>(dcs.bytesReceived()));
        if (!screenshot.empty()) {  // PPM (P6) da última página exibida
            core::video::composeDisplay(bus, display);
            if (!writePpm(screenshot, display)) {
                std::fprintf(stderr, "Captura: falha ao gravar %s\n", screenshot.c_str());
            } else {
                std::size_t pens[2] = {0, 0}, pal = 0;
                for (uint32_t i = 0; i < core::vunit::kVideoRamWords; ++i)
                    if (bus.videoWord(i) & 0x7FFFu) ++pens[i / core::vunit::kVideoPageWords];
                for (uint32_t i = 0; i < core::vunit::kPaletteWords; ++i)
                    if (bus.paletteWord(i) & 0x7FFFu) ++pal;
                std::printf("Captura: %s (página exibida %u; pixels != 0: p0=%zu p1=%zu; paleta != 0: %zu)\n",
                            screenshot.c_str(), bus.readWord(core::vunit::kPageControl) & 1u, pens[0], pens[1], pal);
            }
        }
        std::printf("Quadros executados: %llu\n", static_cast<unsigned long long>(frames));
        for (std::size_t pos = 0; pos < peek.size();) {  // --peek: palavras da memória
            const std::size_t end = std::min(peek.find(',', pos), peek.size());
            const auto addr = static_cast<uint32_t>(std::strtoul(peek.substr(pos, end - pos).c_str(), nullptr, 0));
            std::printf("[0x%06X] = 0x%08X\n", addr, bus.readWord(addr));
            pos = end + 1;
        }
        std::printf("Pipeline 3D (0x0064): objetos=%llu descartados=%llu polígonos=%llu "
                    "faces traseiras=%llu subdivididos=%llu travas=%llu\n",
                    static_cast<unsigned long long>(pipe_stats.objects), static_cast<unsigned long long>(pipe_stats.culled),
                    static_cast<unsigned long long>(pipe_stats.polys), static_cast<unsigned long long>(pipe_stats.backfaces),
                    static_cast<unsigned long long>(pipe_stats.subdivided), static_cast<unsigned long long>(pipe_stats.hangs));
        std::size_t texels = 0;
        for (std::size_t i = 0; i < static_cast<std::size_t>(core::vunit::kTextureRamWords) * 2u; ++i)
            if (bus.textureByte(i) != 0) ++texels;
        std::printf("DMA: quads=%llu pixels=%llu; texels != 0: %zu\n", static_cast<unsigned long long>(dma.quadsDrawn()),
                    static_cast<unsigned long long>(dma.pixelsDrawn()), texels);
        if (rom_ok) {
            std::printf("INT0: contador de quadros [C930]=%u, minutos [C93A]=%.5f, rotinas pendentes=%zu\n",
                        bus.readWord(core::irq::vblank_vars::kFrameCounter),
                        core::tmsToDouble(bus.readWord(core::irq::vblank_vars::kPlayMinutes)), pending.counts.size());
            std::printf("Loop principal: %llu iterações, parado em 0x%04X\n",
                        static_cast<unsigned long long>(main_loop.iterations()),
                        static_cast<unsigned>(main_loop.pc()));
            std::printf("INT3: %d por quadro; volante=%u acelerador=%u freio=%u DIPs=0x%04X\n",
                        runner.adcInterruptsLastFrame(), bus.readWord(core::irq::adc_vars::kWheel),
                        bus.readWord(core::irq::adc_vars::kGas), bus.readWord(core::irq::adc_vars::kBrake),
                        bus.readWord(core::io::input_vars::kDipSwitches));
            std::printf("Processos: %llu despachos; PCs não traduzidos:",
                        static_cast<unsigned long long>(scheduler.dispatched()));
            for (const auto& [pc, n] : scheduler.untranslatedRuns())
                std::printf(" 0x%04X(%llu)", pc, static_cast<unsigned long long>(n));
            std::printf("\n");
            if (list_pending) {  // rotinas não traduzidas, em ordem de endereço, com o número de chamadas
                std::map<uint32_t, uint64_t> sorted(pending.counts.begin(), pending.counts.end());
                std::printf("Pendentes:");
                for (const auto& [addr, n] : sorted)
                    std::printf(" 0x%04X(%llu)", addr, static_cast<unsigned long long>(n));
                std::printf("\n");
            }
        }
    }
    SDL_Quit();
    return exit_code;
}

#ifdef __EMSCRIPTEN__
int main(int argc, char* argv[]) {
    const int code = reaMain(argc, argv);
    std::fflush(stdout);
    rea_web_exited(code);
    return code;
}
#endif

#ifdef _WIN32
/**
 * @brief Windows: o .exe é um programa de janela (só a janela do jogo abre). Com --console abre também uma
 *        janela de texto com as mensagens (as mesmas do terminal no Linux), que espera Enter ao terminar.
 * @return true se a janela de texto foi aberta.
 */
static bool openConsoleWindow() {
    if (!AllocConsole()) return false;
    if (!std::freopen("CONOUT$", "w", stdout) || !std::freopen("CONOUT$", "w", stderr) ||
        !std::freopen("CONIN$", "r", stdin))
        return false;
    SetConsoleTitleA("REA Cruis'n - mensagens");
    return true;
}

int main(int argc, char* argv[]) {
    bool console = false;
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--console") console = true;
    if (console) console = openConsoleWindow();
    const int code = reaMain(argc, argv);
    if (console) {  // sem isto a janela fecharia junto com o jogo, antes de dar tempo de ler
        std::printf("\nO jogo terminou (código %d). Pressione Enter para fechar esta janela.\n", code);
        std::fflush(stdout);
        std::getchar();
    }
    return code;
}
#endif
