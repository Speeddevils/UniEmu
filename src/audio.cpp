#include "audio.h"
#include <algorithm>
#include <cmath>
#include "util.h"

bool Audio::open(double inputRate, int latencyMs) {
    setInputRate(inputRate);
    SDL_AudioSpec want{}, have{};
    want.freq = 48000;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 512;
    dev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (!dev_) {
        util::log("Audio: falha ao abrir dispositivo: %s", SDL_GetError());
        return false;
    }
    outRate_ = have.freq;
    targetBytes_ = (uint32_t)(outRate_ * latencyMs / 1000.0) * 4;
    util::log("Audio: %d Hz (nucleo %.1f Hz), latencia alvo %d ms", have.freq, inRate_, latencyMs);
    SDL_PauseAudioDevice(dev_, 0);
    return true;
}

void Audio::close() {
    if (dev_) SDL_CloseAudioDevice(dev_);
    dev_ = 0;
}

void Audio::push(const int16_t* s, size_t frames) {
    pending_.insert(pending_.end(), s, s + frames * 2);
}

void Audio::clear() {
    pending_.clear();
    if (dev_) SDL_ClearQueuedAudio(dev_);
}

void Audio::flush(bool fastForward) {
    if (!dev_ || pending_.empty()) { pending_.clear(); return; }
    uint32_t queued = SDL_GetQueuedAudioSize(dev_);
    if (fastForward && queued > targetBytes_) { pending_.clear(); return; }

    // Controle dinâmico de taxa: ajusta até ±0,5% conforme o enchimento da fila
    double fill = targetBytes_ ? (double)queued / targetBytes_ : 1.0;
    double adj = std::clamp((1.0 - fill), -1.0, 1.0) * 0.005;
    double step = (inRate_ / outRate_) * (1.0 - adj);   // avanço no sinal de entrada por amostra de saída

    size_t inFrames = pending_.size() / 2;
    out_.clear();
    out_.reserve((size_t)(inFrames / step) * 2 + 8);
    // interpolação linear; prev_ = último quadro do bloco anterior (posição -1)
    while (true) {
        double pos = phase_;            // posição relativa ao início de pending_ (pode ser -1..0)
        int i0 = (int)std::floor(pos);
        if (i0 + 1 >= (int)inFrames) break;
        double t = pos - i0;
        for (int c = 0; c < 2; c++) {
            double a = i0 < 0 ? prev_[c] : pending_[i0 * 2 + c];
            double b = pending_[(i0 + 1) * 2 + c];
            out_.push_back((int16_t)std::lround(a + (b - a) * t));
        }
        phase_ += step;
    }
    phase_ -= inFrames;   // volta para referência do próximo bloco
    prev_[0] = pending_[(inFrames - 1) * 2];
    prev_[1] = pending_[(inFrames - 1) * 2 + 1];
    pending_.clear();

    // evita latência explosiva (ex.: após pausa longa)
    if (queued > targetBytes_ * 4) SDL_ClearQueuedAudio(dev_);
    if (!out_.empty()) SDL_QueueAudio(dev_, out_.data(), (Uint32)(out_.size() * sizeof(int16_t)));
}

void Audio::waitForRoom() {
    if (!dev_) return;
    for (int i = 0; i < 200 && SDL_GetQueuedAudioSize(dev_) > targetBytes_; i++) SDL_Delay(1);
}

void Audio::waitBelow(double factor) {
    if (!dev_) return;
    Uint32 limit = (Uint32)(targetBytes_ * factor);
    for (int i = 0; i < 100 && SDL_GetQueuedAudioSize(dev_) > limit; i++) SDL_Delay(1);
}
