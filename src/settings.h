// UniEmu - configurações (UniEmu.ini)
#pragma once
#include <string>
#include <vector>
#include "util.h"

enum class System {
    Unknown = 0, Dreamcast, PS1, N64, SNES,          // núcleos libretro (dentro do UniEmu)
    Atari2600, MegaDrive,                            // núcleos libretro (2D)
    GameCube, Wii, PS2, PS3, PS4, Switch,            // emuladores externos
    Xbox, Xbox360,                                   // emuladores externos (xemu / Xenia)
    Count
};
bool isExternal(System s);      // roda num emulador externo (Dolphin, PCSX2, RPCS3...)
bool is2D(System s);            // sem resolução interna (usa filtros)
bool hasWidescreenHack(System s);

const char* systemName(System s);       // "Dreamcast"
const char* systemShort(System s);      // "dc", "ps1", "n64", "snes"

// Filtros de apresentação (frontend)
enum Filter { FILTER_NEAREST = 0, FILTER_BILINEAR, FILTER_SHARP, FILTER_CRT, FILTER_XBR, FILTER_FSR, FILTER_COUNT };
const char* filterName(int f);

enum Aspect { ASPECT_ORIGINAL = 0, ASPECT_STRETCH, ASPECT_WIDESCREEN, ASPECT_COUNT };
const char* aspectName(int a);

struct Settings {
    std::vector<std::string> romDirs;
    // Por console (os padrões ficam no construtor)
    int upscale[(int)System::Count];      // resolução interna 1..8
    int filter[(int)System::Count];       // filtro de imagem (núcleos internos)
    int aspect[(int)System::Count];       // 4:3 / esticado / widescreen real
    bool analogToDpad[(int)System::Count];
    std::string emuPath[8];               // executáveis dos emuladores externos (ver external.h)
    int gpuPref = 0;                      // 0 = automático, 1 = integrada, 2 = dedicada
    Settings();
    bool showFps = false;
    int coverSize = 180;         // largura das capas na biblioteca (px)
    int viewMode = 0;            // 0 = capas, 1 = lista
    bool coversAuto = true;      // baixar capas que faltam automaticamente
    bool fullscreen = true;
    bool vsync = true;
    bool pgxp = true;            // correção de geometria do PS1
    int audioLatencyMs = 64;
    int windowW = 1280, windowH = 960;
    util::Ini raw;               // inclui overrides "core.<opcao> = valor"

    std::string path() const;
    void load();
    void save();
};

std::string systemDir();
std::string savesDir();
std::string statesDir();
std::string coresDir();
std::string screenshotsDir();
