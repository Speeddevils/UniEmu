// UniEmu - capas dos jogos (procura local + download automático do acervo libretro-thumbnails)
#pragma once
#include <atomic>
#include <deque>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include "detect.h"

std::string coversDir(System s);
std::string coverPathFor(const GameEntry& g);        // "" se não existir capa local
std::string coverSavePath(const GameEntry& g);       // onde a capa baixada é gravada
bool coverMarkedNotFound(const GameEntry& g);
// Nome do arquivo no acervo que melhor combina com o jogo ("" se nada servir). Exposto para testes.
std::string coverBestMatch(const std::string& romName, const std::vector<std::string>& index);

class CoverDownloader {
public:
    ~CoverDownloader();
    void enqueue(const std::vector<GameEntry>& games, bool force);
    bool busy() const { return busy_; }
    int done() const { return done_; }
    int total() const { return total_; }
    int found() const { return found_; }
    std::string current();
    // incrementa sempre que uma capa nova é gravada (a interface recarrega)
    int generation() const { return gen_; }

private:
    void worker();
    std::mutex m_;
    std::deque<GameEntry> q_;
    std::thread th_;
    std::atomic<bool> busy_{false}, stop_{false};
    std::atomic<int> done_{0}, total_{0}, found_{0}, gen_{0};
    std::string current_;
};
