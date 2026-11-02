// UniEmu - saída de áudio com reamostragem e controle dinâmico de taxa
#pragma once
#include <SDL.h>
#include <cstdint>
#include <vector>

class Audio {
public:
    bool open(double inputRate, int latencyMs);
    void close();
    void setInputRate(double r) { inRate_ = r > 0 ? r : 44100.0; }
    void push(const int16_t* stereo, size_t frames);   // acumula durante o quadro
    void flush(bool fastForward);                       // envia ao dispositivo
    void waitForRoom();                                 // sincronização pelo áudio
    void waitBelow(double factor);                      // espera só se a fila passar de factor x alvo
    void clear();
    void pause(bool p) { if (dev_) SDL_PauseAudioDevice(dev_, p ? 1 : 0); }

private:
    SDL_AudioDeviceID dev_ = 0;
    double inRate_ = 44100.0, outRate_ = 48000.0;
    uint32_t targetBytes_ = 0;
    std::vector<int16_t> pending_, out_;
    double phase_ = 0.0;
    int16_t prev_[2] = {0, 0};
};
