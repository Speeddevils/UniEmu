// UniEmu - cache de texturas das capas (decodifica em segundo plano, envia à GPU aos poucos)
#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "gl.h"

struct CoverTex {
    GLuint tex = 0;
    int w = 0, h = 0;
    bool failed = false;
    Uint32 lastUse = 0;
};

class TexCache {
public:
    TexCache();
    ~TexCache();
    // devolve a textura (ou nullptr enquanto carrega). Pede o carregamento se for a primeira vez.
    const CoverTex* get(const std::string& path);
    void invalidate(const std::string& path);   // capa trocada/baixada
    void pump();                                 // chamar 1x por quadro (envia à GPU, libera as antigas)

private:
    struct Decoded { std::string path; std::vector<uint8_t> rgba; int w = 0, h = 0; bool ok = false; };
    void worker();
    std::map<std::string, CoverTex> cache_;
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<std::string> todo_;
    std::deque<Decoded> ready_;
    std::thread th_;
    std::atomic<bool> stop_{false};
};
