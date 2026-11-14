/**
 * @file input_manager.cpp
 * @brief Implementação do InputManager.
 */
#include "core/io/input_manager.hpp"

namespace rea::core::io {

InputManager::InputManager(IInput& input, CabinetIo& cabinet, uint16_t dsw)
    : input_(input), cabinet_(cabinet), dsw_(dsw) {
    cabinet_.setDsw(dsw_);
}

uint16_t InputManager::encodeIn0(const ArcadeInputState& s) {
    uint16_t v = 0xFFFF;  // tudo solto (ativo em 0)
    auto press = [&v](bool on, uint16_t bit) { if (on) v = static_cast<uint16_t>(v & ~bit); };
    press(s.coin1, in0::kCoin1);
    press(s.coin2, in0::kCoin2);
    press(s.start, in0::kStart);
    press(s.tilt, in0::kTilt);
    press(s.test, in0::kTest);
    press(s.service, in0::kService);
    press(s.coin3, in0::kCoin3);
    press(s.volume_down, in0::kVolumeDown);
    press(s.volume_up, in0::kVolumeUp);
    press(s.coin4, in0::kCoin4);
    // Câmbio H: exatamente um bit em 0 para a marcha engatada; nenhum = neutro.
    switch (s.gear) {
        case 1: press(true, in0::kGear1); break;
        case 2: press(true, in0::kGear2); break;
        case 3: press(true, in0::kGear3); break;
        case 4: press(true, in0::kGear4); break;
        default: break;
    }
    return v;
}

uint16_t InputManager::encodeIn1(const ArcadeInputState& s) {
    uint16_t v = 0xFFFF;
    auto press = [&v](bool on, uint16_t bit) { if (on) v = static_cast<uint16_t>(v & ~bit); };
    press(s.radio, in1::kRadio);
    press(s.view1, in1::kView1);
    press(s.view2, in1::kView2);
    press(s.view3, in1::kView3);
    return v;
}

void InputManager::apply(const ArcadeInputState& s) {
    cabinet_.setIn0(encodeIn0(s));
    cabinet_.setIn1(encodeIn1(s));
    cabinet_.setDsw(dsw_);
    cabinet_.adc().setChannel(1, clampWheel(s.wheel));
    cabinet_.adc().setChannel(2, s.gas);
    cabinet_.adc().setChannel(3, s.brake);
}

ArcadeInputState InputManager::update() {
    const ArcadeInputState s = input_.poll();
    apply(s);
    return s;
}

} // namespace rea::core::io
