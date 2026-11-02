#include "settings.h"

const char* systemName(System s) {
    switch (s) {
    case System::Dreamcast: return "Dreamcast";
    case System::PS1: return "PlayStation";
    case System::N64: return "Nintendo 64";
    case System::SNES: return "Super Nintendo";
    case System::Atari2600: return "Atari 2600";
    case System::MegaDrive: return "Mega Drive";
    case System::GameCube: return "GameCube";
    case System::Wii: return "Wii";
    case System::PS2: return "PlayStation 2";
    case System::PS3: return "PlayStation 3";
    case System::PS4: return "PlayStation 4";
    case System::Switch: return "Nintendo Switch";
    case System::Xbox: return "Xbox";
    case System::Xbox360: return "Xbox 360";
    default: return "Desconhecido";
    }
}

bool isExternal(System s) { return s >= System::GameCube && s < System::Count; }
bool is2D(System s) { return s == System::SNES || s == System::Atari2600 || s == System::MegaDrive; }
bool hasWidescreenHack(System s) {
    return s == System::Dreamcast || s == System::PS1 || s == System::N64 || s == System::GameCube || s == System::Wii ||
           s == System::PS2;
}

Settings::Settings() {
    for (int i = 0; i < (int)System::Count; i++) {
        System s = (System)i;
        upscale[i] = is2D(s) ? 1 : 4;
        filter[i] = is2D(s) ? FILTER_XBR : FILTER_BILINEAR;
        aspect[i] = ASPECT_ORIGINAL;
        analogToDpad[i] = s != System::N64 && !isExternal(s);
    }
    upscale[(int)System::GameCube] = upscale[(int)System::Wii] = 3;   // 3x = ~1080p
    upscale[(int)System::PS2] = 3;
    upscale[(int)System::PS3] = 2;      // 2x de 720p = 1440p
    upscale[(int)System::Switch] = 2;
    upscale[(int)System::PS4] = 1;
    upscale[(int)System::Xbox] = 3;     // 3x de 640x480 = 1920x1440
    upscale[(int)System::Xbox360] = 2;  // 2x de 720p = 1440p
    filter[(int)System::Atari2600] = FILTER_SHARP;
}

const char* systemShort(System s) {
    switch (s) {
    case System::Dreamcast: return "dc";
    case System::PS1: return "ps1";
    case System::N64: return "n64";
    case System::SNES: return "snes";
    case System::Atari2600: return "a26";
    case System::MegaDrive: return "md";
    case System::GameCube: return "gc";
    case System::Wii: return "wii";
    case System::PS2: return "ps2";
    case System::PS3: return "ps3";
    case System::PS4: return "ps4";
    case System::Switch: return "switch";
    case System::Xbox: return "xbox";
    case System::Xbox360: return "x360";
    default: return "unknown";
    }
}

const char* filterName(int f) {
    static const char* n[] = {"Pixels nítidos (nearest)", "Suave (bilinear)", "Sharp bilinear", "CRT (scanlines)",
                              "xBR (suavização 2D)", "AMD FSR 1.0 (super resolução + nitidez)"};
    return (f >= 0 && f < FILTER_COUNT) ? n[f] : "?";
}

const char* aspectName(int a) {
    static const char* n[] = {"4:3 original", "16:9 esticado (ocupa a TV toda)", "16:9 widescreen real (mostra mais cenário)"};
    return (a >= 0 && a < ASPECT_COUNT) ? n[a] : "?";
}

std::string systemDir() { return util::joinPath(util::baseDir(), "system"); }
std::string savesDir() { return util::joinPath(util::baseDir(), "saves"); }
std::string statesDir() { return util::joinPath(util::baseDir(), "states"); }
std::string coresDir() { return util::joinPath(util::baseDir(), "cores"); }
std::string screenshotsDir() { return util::joinPath(util::baseDir(), "screenshots"); }

std::string Settings::path() const { return util::joinPath(util::baseDir(), "UniEmu.ini"); }

static int clampi(int v, int a, int b) { return v < a ? a : (v > b ? b : v); }

void Settings::load() {
    raw = util::Ini();
    bool existed = raw.load(path());
    romDirs.clear();
    for (int i = 0; i < 32; i++) {
        std::string d = raw.get("rom_dir" + std::to_string(i));
        if (!d.empty()) romDirs.push_back(d);
    }
    if (!existed) {
#ifdef _WIN32
        // Primeira execução: aproveita a pasta de jogos que o Redream já usava, se existir
        const char* guesses[] = {"D:\\DreamCast\\Sega Dreamcast", "D:\\DreamCast"};
        for (auto g : guesses) if (util::dirExists(g)) { romDirs.push_back(g); break; }
#endif
        std::string local = util::joinPath(util::baseDir(), "roms");
        util::makeDirs(local);
        romDirs.push_back(local);
    }
    for (int s = 1; s < (int)System::Count; s++) {
        std::string k = systemShort((System)s);
        upscale[s] = clampi(raw.getInt("upscale_" + k, upscale[s]), 1, 8);
        filter[s] = clampi(raw.getInt("filter_" + k, filter[s]), 0, FILTER_COUNT - 1);
    }
    int oldAspect = clampi(raw.getInt("aspect", 0), 0, ASPECT_COUNT - 1);
    for (int s = 1; s < (int)System::Count; s++) {
        std::string k = systemShort((System)s);
        aspect[s] = clampi(raw.getInt("aspect_" + k, oldAspect), 0, ASPECT_COUNT - 1);
        if (!hasWidescreenHack((System)s) && aspect[s] == ASPECT_WIDESCREEN) aspect[s] = ASPECT_STRETCH;
        analogToDpad[s] = raw.getBool("analog_dpad_" + k, analogToDpad[s]);
    }
    showFps = raw.getBool("show_fps", showFps);
    for (int i = 0; i < 8; i++) emuPath[i] = raw.get("emu_path" + std::to_string(i));
    gpuPref = clampi(raw.getInt("gpu_pref", gpuPref), 0, 2);
    coverSize = clampi(raw.getInt("cover_size", coverSize), 110, 320);
    viewMode = clampi(raw.getInt("view_mode", viewMode), 0, 1);
    coversAuto = raw.getBool("covers_auto", coversAuto);
    fullscreen = raw.getBool("fullscreen", fullscreen);
    vsync = raw.getBool("vsync", vsync);
    pgxp = raw.getBool("ps1_pgxp", pgxp);
    audioLatencyMs = clampi(raw.getInt("audio_latency_ms", audioLatencyMs), 16, 256);
    windowW = clampi(raw.getInt("window_w", windowW), 320, 16384);
    windowH = clampi(raw.getInt("window_h", windowH), 240, 16384);
    if (!existed) save();
}

void Settings::save() {
    // remove pastas antigas e regrava
    for (int i = 0; i < 32; i++) raw.kv.erase("rom_dir" + std::to_string(i));
    for (size_t i = 0; i < romDirs.size() && i < 32; i++) raw.set("rom_dir" + std::to_string(i), romDirs[i]);
    for (int s = 1; s < (int)System::Count; s++) {
        std::string k = systemShort((System)s);
        raw.setInt("upscale_" + k, upscale[s]);
        raw.setInt("filter_" + k, filter[s]);
    }
    raw.kv.erase("aspect");
    for (int s = 1; s < (int)System::Count; s++) {
        std::string k = systemShort((System)s);
        raw.setInt("aspect_" + k, aspect[s]);
        raw.setBool("analog_dpad_" + k, analogToDpad[s]);
    }
    raw.setBool("show_fps", showFps);
    for (int i = 0; i < 8; i++) raw.set("emu_path" + std::to_string(i), emuPath[i]);
    raw.setInt("gpu_pref", gpuPref);
    raw.setInt("cover_size", coverSize);
    raw.setInt("view_mode", viewMode);
    raw.setBool("covers_auto", coversAuto);
    raw.setBool("fullscreen", fullscreen);
    raw.setBool("vsync", vsync);
    raw.setBool("ps1_pgxp", pgxp);
    raw.setInt("audio_latency_ms", audioLatencyMs);
    raw.setInt("window_w", windowW);
    raw.setInt("window_h", windowH);
    raw.save(path(),
             "; UniEmu - configuracoes\n"
             "; upscale_*: 1 a 8 (resolucao interna). filter_*: 0=nearest 1=bilinear 2=sharp 3=CRT 4=xBR 5=FSR\n"
             "; gpu_pref: 0=automatico 1=integrada 2=dedicada\n"
             "; aspect_*: 0=4:3 original 1=16:9 esticado 2=16:9 widescreen real (hack)\n"
             "; Usuarios avancados: 'core.<nome_da_opcao> = valor' forca qualquer opcao de nucleo libretro.\n\n");
}
