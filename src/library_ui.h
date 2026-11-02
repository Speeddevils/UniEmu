// UniEmu - biblioteca com capas (grade estilo PCSX2) e lista
#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "imgui.h"
#include "covers.h"
#include "detect.h"
#include "settings.h"
#include "texcache.h"

ImVec4 systemColor(System s);
std::string displayName(const std::string& rawName);

struct LibraryUi {
    TexCache tex;
    CoverDownloader dl;
    std::map<std::string, std::string> coverOf;   // caminho do jogo -> caminho da capa ("" = sem capa)
    int lastGen = -1;
    size_t lastCount = (size_t)-1;
    Uint32 lastRefresh = 0;
    std::string selected;
    char search[128] = "";

    void refreshCovers(const std::vector<GameEntry>& games, bool force);
    // sysFilter: 0 = todos
    void draw(Settings& st, const std::vector<GameEntry>& games, int sysFilter, bool scanning, int scanProgress,
              const std::function<void(const GameEntry&)>& launch, const std::function<void()>& rescan,
              const std::string& status);
};
