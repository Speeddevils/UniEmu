#include "input.h"
#include <algorithm>
#include <cstdlib>
#include "controllers.h"
#include "libretro.h"
#include "util.h"

void Input::init() {
    SDL_GameControllerEventState(SDL_ENABLE);
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (controllersEnsureMapping(i)) {
            SDL_GameController* c = SDL_GameControllerOpen(i);
            if (c) { pads_.push_back(c); util::log("Input: controle '%s'", SDL_GameControllerName(c)); }
        }
}

void Input::shutdown() {
    for (auto c : pads_) SDL_GameControllerClose(c);
    pads_.clear();
}

void Input::handleEvent(const SDL_Event& e) {
    if (e.type == SDL_JOYDEVICEADDED && !SDL_IsGameController(e.jdevice.which)) {
        controllersEnsureMapping(e.jdevice.which);   // vira GameController e gera CONTROLLERDEVICEADDED
        return;
    }
    if (e.type == SDL_CONTROLLERDEVICEADDED) {
        SDL_GameController* c = SDL_GameControllerOpen(e.cdevice.which);
        if (c && std::find(pads_.begin(), pads_.end(), c) == pads_.end()) {
            pads_.push_back(c);
            util::log("Input: controle conectado '%s'", SDL_GameControllerName(c));
        }
    } else if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
        for (size_t i = 0; i < pads_.size(); i++) {
            SDL_Joystick* j = SDL_GameControllerGetJoystick(pads_[i]);
            if (SDL_JoystickInstanceID(j) == e.cdevice.which) {
                SDL_GameControllerClose(pads_[i]);
                pads_.erase(pads_.begin() + i);
                break;
            }
        }
    }
}

static int16_t clampAxis(int v) { return (int16_t)std::clamp(v, -32768, 32767); }

void Input::update(System sys, bool blocked) {
    for (auto& s : st_) s = PadState();
    if (blocked) return;

    // Controles
    for (size_t p = 0; p < pads_.size() && p < (size_t)kPorts; p++) {
        SDL_GameController* c = pads_[p];
        PadState& s = st_[p];
        auto b = [&](SDL_GameControllerButton btn) { return SDL_GameControllerGetButton(c, btn) != 0; };
        auto set = [&](int id, bool on) { if (on) s.buttons |= (1u << id); };
        set(RETRO_DEVICE_ID_JOYPAD_B, b(SDL_CONTROLLER_BUTTON_A));
        set(RETRO_DEVICE_ID_JOYPAD_A, b(SDL_CONTROLLER_BUTTON_B));
        set(RETRO_DEVICE_ID_JOYPAD_Y, b(SDL_CONTROLLER_BUTTON_X));
        set(RETRO_DEVICE_ID_JOYPAD_X, b(SDL_CONTROLLER_BUTTON_Y));
        set(RETRO_DEVICE_ID_JOYPAD_SELECT, b(SDL_CONTROLLER_BUTTON_BACK));
        set(RETRO_DEVICE_ID_JOYPAD_START, b(SDL_CONTROLLER_BUTTON_START));
        set(RETRO_DEVICE_ID_JOYPAD_UP, b(SDL_CONTROLLER_BUTTON_DPAD_UP));
        set(RETRO_DEVICE_ID_JOYPAD_DOWN, b(SDL_CONTROLLER_BUTTON_DPAD_DOWN));
        set(RETRO_DEVICE_ID_JOYPAD_LEFT, b(SDL_CONTROLLER_BUTTON_DPAD_LEFT));
        set(RETRO_DEVICE_ID_JOYPAD_RIGHT, b(SDL_CONTROLLER_BUTTON_DPAD_RIGHT));
        set(RETRO_DEVICE_ID_JOYPAD_L, b(SDL_CONTROLLER_BUTTON_LEFTSHOULDER));
        set(RETRO_DEVICE_ID_JOYPAD_R, b(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER));
        set(RETRO_DEVICE_ID_JOYPAD_L3, b(SDL_CONTROLLER_BUTTON_LEFTSTICK));
        set(RETRO_DEVICE_ID_JOYPAD_R3, b(SDL_CONTROLLER_BUTTON_RIGHTSTICK));
        int lt = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
        int rt = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
        s.l2 = clampAxis(lt);
        s.r2 = clampAxis(rt);
        set(RETRO_DEVICE_ID_JOYPAD_L2, lt > 12000);
        set(RETRO_DEVICE_ID_JOYPAD_R2, rt > 12000);
        s.axis[0][0] = clampAxis(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX));
        s.axis[0][1] = clampAxis(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY));
        s.axis[1][0] = clampAxis(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTX));
        s.axis[1][1] = clampAxis(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTY));
        if (analogToDpad_) {
            // analógico esquerdo também vale como direcional (útil em jogos que só leem o D-pad)
            const int th = 16000;
            set(RETRO_DEVICE_ID_JOYPAD_LEFT, s.axis[0][0] < -th);
            set(RETRO_DEVICE_ID_JOYPAD_RIGHT, s.axis[0][0] > th);
            set(RETRO_DEVICE_ID_JOYPAD_UP, s.axis[0][1] < -th);
            set(RETRO_DEVICE_ID_JOYPAD_DOWN, s.axis[0][1] > th);
        }
    }

    // Teclado -> jogador 1
    const Uint8* k = SDL_GetKeyboardState(nullptr);
    PadState& s = st_[0];
    auto set = [&](int id, bool on) { if (on) s.buttons |= (1u << id); };
    bool up = k[SDL_SCANCODE_UP], dn = k[SDL_SCANCODE_DOWN], lf = k[SDL_SCANCODE_LEFT], rt = k[SDL_SCANCODE_RIGHT];
    if (sys == System::N64) {
        // No N64 as setas viram o direcional analógico; T/F/G/H = D-pad
        if (up) s.axis[0][1] = -32767;
        if (dn) s.axis[0][1] = 32767;
        if (lf) s.axis[0][0] = -32767;
        if (rt) s.axis[0][0] = 32767;
        set(RETRO_DEVICE_ID_JOYPAD_UP, k[SDL_SCANCODE_T]);
        set(RETRO_DEVICE_ID_JOYPAD_DOWN, k[SDL_SCANCODE_G]);
        set(RETRO_DEVICE_ID_JOYPAD_LEFT, k[SDL_SCANCODE_F]);
        set(RETRO_DEVICE_ID_JOYPAD_RIGHT, k[SDL_SCANCODE_H]);
    } else {
        set(RETRO_DEVICE_ID_JOYPAD_UP, up);
        set(RETRO_DEVICE_ID_JOYPAD_DOWN, dn);
        set(RETRO_DEVICE_ID_JOYPAD_LEFT, lf);
        set(RETRO_DEVICE_ID_JOYPAD_RIGHT, rt);
    }
    set(RETRO_DEVICE_ID_JOYPAD_B, k[SDL_SCANCODE_Z]);
    set(RETRO_DEVICE_ID_JOYPAD_A, k[SDL_SCANCODE_X]);
    set(RETRO_DEVICE_ID_JOYPAD_Y, k[SDL_SCANCODE_A]);
    set(RETRO_DEVICE_ID_JOYPAD_X, k[SDL_SCANCODE_S]);
    set(RETRO_DEVICE_ID_JOYPAD_L, k[SDL_SCANCODE_Q]);
    set(RETRO_DEVICE_ID_JOYPAD_R, k[SDL_SCANCODE_W]);
    set(RETRO_DEVICE_ID_JOYPAD_START, k[SDL_SCANCODE_RETURN]);
    set(RETRO_DEVICE_ID_JOYPAD_SELECT, k[SDL_SCANCODE_RSHIFT] || k[SDL_SCANCODE_BACKSPACE]);
    if (k[SDL_SCANCODE_E]) { set(RETRO_DEVICE_ID_JOYPAD_L2, true); s.l2 = 32767; }
    if (k[SDL_SCANCODE_R]) { set(RETRO_DEVICE_ID_JOYPAD_R2, true); s.r2 = 32767; }
    if (k[SDL_SCANCODE_I]) s.axis[1][1] = -32767;
    if (k[SDL_SCANCODE_K]) s.axis[1][1] = 32767;
    if (k[SDL_SCANCODE_J]) s.axis[1][0] = -32767;
    if (k[SDL_SCANCODE_L]) s.axis[1][0] = 32767;
}

std::string Input::names() const {
    std::string s;
    for (auto c : pads_) { if (!s.empty()) s += ", "; s += SDL_GameControllerName(c) ? SDL_GameControllerName(c) : "?"; }
    return s;
}

int16_t Input::state(unsigned port, unsigned device, unsigned index, unsigned id) const {
    if (port >= (unsigned)kPorts) return 0;
    queries_[port]++;
    const PadState& s = st_[port];
    switch (device & RETRO_DEVICE_MASK) {
    case RETRO_DEVICE_JOYPAD:
        if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return (int16_t)s.buttons;
        return id < 16 ? ((s.buttons >> id) & 1) : 0;
    case RETRO_DEVICE_ANALOG:
        if (index == RETRO_DEVICE_INDEX_ANALOG_LEFT || index == RETRO_DEVICE_INDEX_ANALOG_RIGHT) {
            if (id > 1) return 0;
            int v = s.axis[index][id];
            if (std::abs(v) > maxAnalog_) maxAnalog_ = std::abs(v);
            return (int16_t)v;
        }
        if (index == RETRO_DEVICE_INDEX_ANALOG_BUTTON) {
            if (id == RETRO_DEVICE_ID_JOYPAD_L2) return s.l2;
            if (id == RETRO_DEVICE_ID_JOYPAD_R2) return s.r2;
            return id < 16 && ((s.buttons >> id) & 1) ? 32767 : 0;
        }
        return 0;
    default:
        return 0;
    }
}

bool Input::rumble(unsigned port, int effect, uint16_t strength) {
    if (port >= pads_.size() || port >= (unsigned)kPorts) return false;
    if (effect == RETRO_RUMBLE_STRONG) rumbleStrong_[port] = strength;
    else rumbleWeak_[port] = strength;
    SDL_GameControllerRumble(pads_[port], rumbleStrong_[port], rumbleWeak_[port], 200);
    return true;
}

bool Input::menuComboPressed() {
    bool down = false;
    for (auto c : pads_) {
        bool combo = SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_BACK) &&
                     SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_START);
        if (combo || SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_GUIDE)) down = true;
    }
    bool pressed = down && !comboWasDown_;
    comboWasDown_ = down;
    return pressed;
}
