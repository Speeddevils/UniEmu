// UniEmu - controles (teclado + gamepads via SDL_GameController) -> RetroPad
#pragma once
#include <SDL.h>
#include <cstdint>
#include <string>
#include <vector>
#include "settings.h"

struct PadState {
    uint16_t buttons = 0;
    int16_t axis[2][2] = {{0, 0}, {0, 0}};   // [esquerdo/direito][x/y]
    int16_t l2 = 0, r2 = 0;
};

class Input {
public:
    static const int kPorts = 4;
    void init();
    void shutdown();
    void handleEvent(const SDL_Event& e);
    void update(System sys, bool blocked);
    int16_t state(unsigned port, unsigned device, unsigned index, unsigned id) const;
    bool rumble(unsigned port, int effect, uint16_t strength);
    // Select+Start (ou botão Guide) em qualquer controle: abre o menu
    bool menuComboPressed();
    int connectedPads() const { return (int)pads_.size(); }
    std::string names() const;
    unsigned long queries(int port) const { return queries_[port]; }
    void setAnalogToDpad(bool on) { analogToDpad_ = on; }
    int maxAnalogSent() const { return maxAnalog_; }

private:
    std::vector<SDL_GameController*> pads_;
    PadState st_[kPorts];
    uint16_t rumbleStrong_[kPorts] = {0}, rumbleWeak_[kPorts] = {0};
    bool comboWasDown_ = false;
    mutable unsigned long queries_[kPorts] = {0};
    mutable int maxAnalog_ = 0;
    bool analogToDpad_ = true;
};
