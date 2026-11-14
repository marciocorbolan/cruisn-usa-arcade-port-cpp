/**
 * @file input_script.cpp
 * @brief Entrada roteirizada por faixas de quadros.
 */
#include "core/io/input_script.hpp"

#include <cstdlib>
#include <sstream>

namespace rea::core::io {

ScriptedInput::ScriptedInput(IInput& base, const std::string& spec) : base_(base), actions_(parse(spec)) {}

std::vector<ScriptedAction> ScriptedInput::parse(const std::string& spec) {
    std::vector<ScriptedAction> out;
    std::stringstream ss(spec);
    std::string item;
    while (std::getline(ss, item, ',')) {
        const auto at = item.find('@');
        if (at == std::string::npos) continue;
        ScriptedAction a;
        a.name = item.substr(0, at);
        std::string range = item.substr(at + 1);
        if (const auto colon = range.find(':'); colon != std::string::npos) {
            a.value = static_cast<uint32_t>(std::strtoul(range.c_str() + colon + 1, nullptr, 0));
            range.resize(colon);
        }
        const auto dash = range.find('-');
        a.first = std::strtoull(range.c_str(), nullptr, 10);
        a.last = dash == std::string::npos ? a.first : std::strtoull(range.c_str() + dash + 1, nullptr, 10);
        if (!a.name.empty() && a.last >= a.first) out.push_back(a);
    }
    return out;
}

bool ScriptedInput::knownAction(const std::string& name) {
    static const char* const kNames[] = {"coin1", "coin2", "coin3", "coin4", "start", "service", "view1",
                                         "view2", "view3", "radio", "test",  "volup", "voldown", "gas",
                                         "brake", "wheel", "gear"};
    for (const char* n : kNames)
        if (name == n) return true;
    return false;
}

std::string ScriptedInput::firstInvalid(const std::string& spec) {
    std::stringstream ss(spec);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (item.empty()) continue;
        const auto at = item.find('@');
        if (at == std::string::npos || !knownAction(item.substr(0, at))) return item;
        const auto one = parse(item);
        if (one.empty()) return item;  // faixa inválida (fim antes do início)
    }
    return {};
}

void ScriptedInput::apply(const std::vector<ScriptedAction>& actions, uint64_t frame, ArcadeInputState& s) {
    for (const auto& a : actions) {
        if (frame < a.first || frame > a.last) continue;
        const auto v = static_cast<uint8_t>(a.value > 255 ? 255 : a.value);
        if (a.name == "coin1") s.coin1 = true;
        else if (a.name == "coin2") s.coin2 = true;
        else if (a.name == "coin3") s.coin3 = true;
        else if (a.name == "coin4") s.coin4 = true;
        else if (a.name == "start") s.start = true;
        else if (a.name == "service") s.service = true;
        else if (a.name == "view1") s.view1 = true;
        else if (a.name == "view2") s.view2 = true;
        else if (a.name == "view3") s.view3 = true;
        else if (a.name == "radio") s.radio = true;
        else if (a.name == "test") s.test = true;
        else if (a.name == "volup") s.volume_up = true;
        else if (a.name == "voldown") s.volume_down = true;
        else if (a.name == "gas") s.gas = v;
        else if (a.name == "brake") s.brake = v;
        else if (a.name == "wheel") s.wheel = v;
        else if (a.name == "gear") s.gear = v;
    }
}

ArcadeInputState ScriptedInput::poll() {
    ArcadeInputState s = base_.poll();
    apply(actions_, ++frame_, s);
    return s;
}

} // namespace rea::core::io
