/**
 * @file sdl_input.cpp
 * @brief Entrada SDL2 → portas do gabinete.
 */
#include "platform/sdl/sdl_platform.hpp"

namespace rea::platform::sdl {

namespace {
/**
 * @brief Converte eixo SDL (−32768…32767) para o A/D de 8 bits do arcade.
 * @param v Valor do eixo.
 * @return 0x00…0xFF, centro em 0x80.
 */
uint8_t axisToAdc(Sint16 v) { return static_cast<uint8_t>((static_cast<int>(v) + 32768) >> 8); }

/**
 * @brief Converte gatilho SDL (0…32767) para pedal de 8 bits.
 */
uint8_t triggerToAdc(Sint16 v) { return static_cast<uint8_t>(v < 0 ? 0 : (v >> 7)); }
} // namespace

SdlInput::SdlInput() {
    for (int i = 0; i < SDL_NumJoysticks() && !pad_; ++i) openPad(i);
}

SdlInput::~SdlInput() { closePad(); }

void SdlInput::openPad(int index) {
    if (pad_ || !SDL_IsGameController(index)) return;
    pad_ = SDL_GameControllerOpen(index);
    if (!pad_) return;
    pad_id_ = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad_));
    const char* name = SDL_GameControllerName(pad_);
    SDL_Log("Controle: %s", name ? name : "?");
}

void SdlInput::closePad() {
    if (pad_) SDL_GameControllerClose(pad_);
    pad_ = nullptr;
    pad_id_ = -1;
}

core::ArcadeInputState SdlInput::poll() {
    core::ArcadeInputState s;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) s.quit = true;
        // Controle conectado ou desconectado com o jogo aberto.
        if (e.type == SDL_CONTROLLERDEVICEADDED) openPad(e.cdevice.which);
        if (e.type == SDL_CONTROLLERDEVICEREMOVED && e.cdevice.which == pad_id_) {
            closePad();
            for (int i = 0; i < SDL_NumJoysticks() && !pad_; ++i) openPad(i);  // outro que já estava ligado
        }
        // Marchas no controle: RB/D-pad ↑ sobe, LB/D-pad ↓ desce (1–4; o neutro fica na tecla 0).
        if (e.type == SDL_CONTROLLERBUTTONDOWN && e.cbutton.which == pad_id_) {
            const auto b = e.cbutton.button;
            if ((b == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER || b == SDL_CONTROLLER_BUTTON_DPAD_UP) && gear_ < 4) ++gear_;
            if ((b == SDL_CONTROLLER_BUTTON_LEFTSHOULDER || b == SDL_CONTROLLER_BUTTON_DPAD_DOWN) && gear_ > 1) --gear_;
        }
        if (e.type == SDL_KEYDOWN && !e.key.repeat) {
            const bool alt = (e.key.keysym.mod & KMOD_ALT) != 0;
            if (e.key.keysym.sym == SDLK_F11 || (alt && e.key.keysym.sym == SDLK_RETURN)) fullscreen_toggle_ = true;
            switch (e.key.keysym.sym) {
#ifndef __EMSCRIPTEN__
                case SDLK_ESCAPE: s.quit = true; break;  // no navegador o Esc só sai da tela cheia
#endif
                case SDLK_1: gear_ = 1; break;
                case SDLK_2: gear_ = 2; break;
                case SDLK_3: gear_ = 3; break;
                case SDLK_4: gear_ = 4; break;
                case SDLK_0: gear_ = 0; break;  // neutro
                default: break;
            }
        }
    }

    const Uint8* k = SDL_GetKeyboardState(nullptr);
    // Teclado digital → valores analógicos extremos (como um volante "batido").
    if (k[SDL_SCANCODE_LEFT]) s.wheel = 0x00;
    else if (k[SDL_SCANCODE_RIGHT]) s.wheel = 0xFF;
    if (k[SDL_SCANCODE_UP]) s.gas = 0xFF;
    if (k[SDL_SCANCODE_DOWN]) s.brake = 0xFF;
    s.coin1 = k[SDL_SCANCODE_5];
    s.coin2 = k[SDL_SCANCODE_6];
    s.coin3 = k[SDL_SCANCODE_7];
    s.coin4 = k[SDL_SCANCODE_8];
    s.service = k[SDL_SCANCODE_9];
    s.tilt = k[SDL_SCANCODE_T];
    s.volume_up = k[SDL_SCANCODE_EQUALS] || k[SDL_SCANCODE_KP_PLUS];
    s.volume_down = k[SDL_SCANCODE_MINUS] || k[SDL_SCANCODE_KP_MINUS];
    s.start = k[SDL_SCANCODE_RETURN] && !(SDL_GetModState() & KMOD_ALT);  // Alt+Enter é a tela cheia
    s.view1 = k[SDL_SCANCODE_F1];
    s.view2 = k[SDL_SCANCODE_F3];
    s.view3 = k[SDL_SCANCODE_F4];
    s.radio = k[SDL_SCANCODE_R];
    s.test = k[SDL_SCANCODE_F2];

    if (pad_) {
        const Sint16 lx = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTX);
        if (lx < -4000 || lx > 4000) s.wheel = axisToAdc(lx);
        const uint8_t rt = triggerToAdc(SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_TRIGGERRIGHT));
        const uint8_t lt = triggerToAdc(SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_TRIGGERLEFT));
        if (rt > s.gas) s.gas = rt;
        if (lt > s.brake) s.brake = lt;
        auto btn = [&](SDL_GameControllerButton b) { return SDL_GameControllerGetButton(pad_, b) != 0; };
        s.start = s.start || btn(SDL_CONTROLLER_BUTTON_START);
        s.coin1 = s.coin1 || btn(SDL_CONTROLLER_BUTTON_BACK);
        s.view1 = s.view1 || btn(SDL_CONTROLLER_BUTTON_X);
        s.view2 = s.view2 || btn(SDL_CONTROLLER_BUTTON_Y);
        s.view3 = s.view3 || btn(SDL_CONTROLLER_BUTTON_B);
        s.radio = s.radio || btn(SDL_CONTROLLER_BUTTON_A);
    }
    s.gear = gear_;
    return s;
}

} // namespace rea::platform::sdl
