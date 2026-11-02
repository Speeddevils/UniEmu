#ifdef _WIN32
#define NOMINMAX
#endif
#include "play.h"
#include "appicon.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>
#include "audio.h"
#include "core.h"
#include "detect.h"
#include "gl.h"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"
#include "input.h"
#include "ui_common.h"
#include "util.h"
#include "video.h"
#include <SDL_syswm.h>
#ifdef _WIN32
#include <windows.h>
#endif

static Presenter g_video;
static Audio g_audio;
static Input g_input;

uintptr_t playGetCurrentFramebuffer() { return g_video.fbo(); }
bool playRumble(unsigned port, int effect, uint16_t strength) { return g_input.rumble(port, effect, strength); }

std::string coreLibName(const char* base) {
#ifdef _WIN32
    return util::joinPath(coresDir(), std::string(base) + ".dll");
#elif defined(__APPLE__)
    return util::joinPath(coresDir(), std::string(base) + ".dylib");
#else
    return util::joinPath(coresDir(), std::string(base) + ".so");
#endif
}

static bool fileExistsCI(const std::string& dir, const char* name) {
    if (util::fileExists(util::joinPath(dir, name))) return true;
    std::string up = name;
    for (auto& c : up) c = (char)toupper((unsigned char)c);
    return util::fileExists(util::joinPath(dir, up));
}

bool ps1BiosPresent(std::string* found) {
    const char* names[] = {"scph5501.bin", "scph5500.bin", "scph5502.bin", "scph1001.bin", "scph7001.bin"};
    for (auto n : names)
        if (fileExistsCI(systemDir(), n)) { if (found) *found = n; return true; }
    return false;
}

bool dcBiosPresent() { return fileExistsCI(util::joinPath(systemDir(), "dc"), "dc_boot.bin"); }

// ---------- regras de upscale ----------

static int leadingNumber(const std::string& s) { return atoi(s.c_str()); }

// escolhe o valor "LxA" mais próximo de alvoW x alvoH
static OptionRule closestRes(int tw, int th) {
    return [tw, th](const std::vector<std::string>& vals) -> std::string {
        std::string best;
        long long bestD = 1LL << 60;
        for (auto& v : vals) {
            int w = 0, h = 0;
            if (sscanf(v.c_str(), "%dx%d", &w, &h) != 2 || w <= 0 || h <= 0) continue;
            long long d = (tw ? (long long)(w - tw) * (w - tw) : 0) + (long long)(h - th) * (h - th) * 4;
            if (d < bestD) { bestD = d; best = v; }
        }
        return best;
    };
}
// escolhe o valor numérico ("2x", "4", "1x(native)") mais próximo, preferindo o maior em empate
static OptionRule closestFactor(int target) {
    return [target](const std::vector<std::string>& vals) -> std::string {
        std::string best;
        int bestD = 1 << 30, bestN = 0;
        for (auto& v : vals) {
            int n = leadingNumber(v);
            if (n <= 0) continue;
            int d = std::abs(n - target);
            if (d < bestD || (d == bestD && n > bestN)) { bestD = d; bestN = n; best = v; }
        }
        return best;
    };
}
static OptionRule fixed(const std::string& value) {
    return [value](const std::vector<std::string>& vals) -> std::string {
        for (auto& v : vals) if (v == value) return v;
        return "";
    };
}
static OptionRule firstOf(std::vector<std::string> prefs) {
    return [prefs](const std::vector<std::string>& vals) -> std::string {
        for (auto& p : prefs)
            for (auto& v : vals) if (v == p) return v;
        return "";
    };
}

static void configureRules(System sys, const Settings& st, bool ps1Fallback) {
    auto& R = g_core.rules;
    R.clear();
    int n = st.upscale[(int)sys];
    bool wide = st.aspect[(int)sys] == ASPECT_WIDESCREEN;
    switch (sys) {
    case System::Dreamcast:
        R.push_back({"_internal_resolution", closestRes(640 * n, 480 * n)});
        R.push_back({"_widescreen_hack", fixed(wide ? "enabled" : "disabled")});
        R.push_back({"_widescreen_cheats", fixed(wide ? "enabled" : "disabled")});  // patches 16:9 por jogo
        R.push_back({"_threaded_rendering", fixed("enabled")});
        if (!dcBiosPresent()) R.push_back({"_hle_bios", fixed("enabled")});
        break;
    case System::PS1:
        if (!ps1Fallback) {
            R.push_back({"_renderer", fixed("hardware_gl")});
            R.push_back({"_internal_resolution", closestFactor(n)});
            R.push_back({"_pgxp_mode", fixed(st.pgxp ? "memory only" : "disabled")});
            R.push_back({"_pgxp_texture", fixed(st.pgxp ? "enabled" : "disabled")});
            R.push_back({"_widescreen_hack", fixed(wide ? "enabled" : "disabled")});
            R.push_back({"_widescreen_hack_aspect_ratio", fixed("16:9")});
        } else {
            R.push_back({"_neon_enhancement_enable", fixed(n >= 2 ? "enabled" : "disabled")});
        }
        break;
    case System::N64:
        R.push_back({"-rdp-plugin", fixed("gliden64")});
        R.push_back({"-rsp-plugin", fixed("hle")});
        R.push_back({"-EnableNativeResFactor", closestFactor(n)});
        R.push_back({"-43screensize", closestRes(320 * n, 240 * n)});
        R.push_back({"-169screensize", closestRes(0, 240 * n)});  // largura livre: casa pela altura
        R.push_back({"-aspect", firstOf(wide ? std::vector<std::string>{"16:9 adjusted", "16:9"}
                                              : std::vector<std::string>{"4:3"})});
        break;
    default:
        break;
    }
    g_core.forced.clear();
    for (auto& [k, v] : st.raw.kv)
        if (k.rfind("core.", 0) == 0) g_core.forced[k.substr(5)] = v;
}

// ---------- callbacks libretro ----------

static void RETRO_CALLCONV cbVideo(const void* data, unsigned w, unsigned h, size_t pitch) {
    if (!data) return;  // quadro duplicado
    if (data == RETRO_HW_FRAME_BUFFER_VALID) g_video.setHwFrame(w, h, g_core.hw.bottom_left_origin);
    else g_video.uploadSoftware(data, w, h, pitch, g_core.pixfmt);
}
static void RETRO_CALLCONV cbAudioSample(int16_t l, int16_t r) {
    int16_t s[2] = {l, r};
    g_audio.push(s, 1);
}
static size_t RETRO_CALLCONV cbAudioBatch(const int16_t* d, size_t frames) {
    g_audio.push(d, frames);
    return frames;
}
static void RETRO_CALLCONV cbInputPoll() {}
static int16_t RETRO_CALLCONV cbInputState(unsigned port, unsigned dev, unsigned idx, unsigned id) {
    return g_input.state(port, dev, idx, id);
}
static bool RETRO_CALLCONV cbEnv(unsigned cmd, void* data) { return coreEnvironment(cmd, data); }

// ---------- utilidades ----------

static void fail(const std::string& msg) {
    util::log("ERRO: %s", msg.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "UniEmu", msg.c_str(), nullptr);
}

struct Osd {
    std::string text;
    Uint32 until = 0;
    void show(const std::string& t, Uint32 ms = 2500) { text = t; until = SDL_GetTicks() + ms; }
};

static std::string timestamp() {
    time_t t = time(nullptr);
    char b[32];
    strftime(b, sizeof b, "%Y%m%d-%H%M%S", localtime(&t));
    return b;
}

int runGame(const std::string& romPath, Settings& st) {
    std::string zipInner;
    System sys = detectSystem(romPath, &zipInner);
    if (isExternal(sys)) {
        fail(std::string(systemName(sys)) + " roda num emulador externo: abra pelo UniEmu.");
        return 2;
    }
    if (sys == System::Unknown) {
        fail("Não foi possível identificar o console deste arquivo:\n" + romPath);
        return 2;
    }
    util::log("Jogo: %s -> %s", romPath.c_str(), systemName(sys));

    // escolher núcleo
    std::string corePath;
    bool ps1Fallback = false;
    std::string notice;
    switch (sys) {
    case System::Dreamcast: corePath = coreLibName("flycast_libretro"); break;
    case System::N64: corePath = coreLibName("mupen64plus_next_libretro"); break;
    case System::SNES: corePath = coreLibName("snes9x_libretro"); break;
    case System::Atari2600: corePath = coreLibName("stella_libretro"); break;
    case System::MegaDrive: corePath = coreLibName("genesis_plus_gx_libretro"); break;
    case System::PS1:
        corePath = coreLibName("mednafen_psx_hw_libretro");
        if (!ps1BiosPresent() || !util::fileExists(corePath)) {
            std::string alt = coreLibName("pcsx_rearmed_libretro");
            if (util::fileExists(alt)) {
                corePath = alt;
                ps1Fallback = true;
                notice = "BIOS do PS1 não encontrada: usando modo compatível (upscale máx. 2x)";
            }
        }
        break;
    default: break;
    }
    if (!util::fileExists(corePath)) {
        fail("Núcleo não encontrado:\n" + corePath);
        return 3;
    }

    // diretórios
    g_core.systemDir = systemDir();
    g_core.saveDir = util::joinPath(savesDir(), systemShort(sys));
    util::makeDirs(g_core.systemDir);
    util::makeDirs(util::joinPath(g_core.systemDir, "dc"));
    util::makeDirs(g_core.saveDir);
    configureRules(sys, st, ps1Fallback);

    if (!g_core.load(corePath)) {
        fail("Falha ao carregar o núcleo:\n" + corePath);
        return 3;
    }
    g_core.retro_set_environment(cbEnv);
    g_core.retro_set_video_refresh(cbVideo);
    g_core.retro_set_audio_sample(cbAudioSample);
    g_core.retro_set_audio_sample_batch(cbAudioBatch);
    g_core.retro_set_input_poll(cbInputPoll);
    g_core.retro_set_input_state(cbInputState);
    g_core.retro_init();
    g_core.applyOverrides();

    // conteúdo
    retro_game_info gi{};
    std::vector<uint8_t> data;
    std::string contentPath = romPath;
    if (!zipInner.empty()) {
        if (!extractZipEntry(romPath, zipInner, data)) { fail("Falha ao extrair do .zip:\n" + romPath); return 4; }
        if (g_core.info.need_fullpath) {
            contentPath = util::joinPath(util::joinPath(savesDir(), "tmp"), util::fileName(zipInner));
            util::writeFile(contentPath, data.data(), data.size());
            data.clear();
        } else {
            contentPath = util::joinPath(util::dirName(romPath), util::fileName(zipInner));
        }
    } else if (!g_core.info.need_fullpath) {
        if (!util::readFile(romPath, data)) { fail("Não foi possível ler o arquivo:\n" + romPath); return 4; }
    }
    gi.path = contentPath.c_str();
    gi.data = data.empty() ? nullptr : data.data();
    gi.size = data.size();
    if (!g_core.retro_load_game(&gi)) {
        std::string m = "O núcleo não conseguiu carregar o jogo.\nDetalhes em UniEmu.log";
        if (sys == System::PS1 && !ps1BiosPresent()) m += "\n\nDica: coloque a BIOS do PS1 (ex.: scph5501.bin) na pasta system.";
        fail(m);
        g_core.retro_deinit();
        return 5;
    }
    // conecta um controle padrão em cada porta (sem isso o Flycast não liga nenhum controle)
    for (unsigned p = 0; p < 4; p++) g_core.retro_set_controller_port_device(p, RETRO_DEVICE_JOYPAD);
    g_core.retro_get_system_av_info(&g_core.av);
    const retro_game_geometry& geo = g_core.av.geometry;
    util::log("AV: %ux%u (max %ux%u) aspect %.3f, %.3f fps, %.1f Hz", geo.base_width, geo.base_height, geo.max_width,
              geo.max_height, geo.aspect_ratio, g_core.av.timing.fps, g_core.av.timing.sample_rate);

    // janela + contexto GL (tenta a versão mais alta: o Flycast usa GL 4.3+ para ordenação de transparência por pixel)
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    bool compat = g_core.hwRequested && g_core.hw.context_type == RETRO_HW_CONTEXT_OPENGL;
    unsigned reqMaj = g_core.hwRequested ? g_core.hw.version_major : 3, reqMin = g_core.hwRequested ? g_core.hw.version_minor : 3;
    struct V { int maj, min; } versions[] = {{4, 6}, {4, 5}, {4, 3}, {3, 3}};

    std::string title = "UniEmu - " + util::stem(romPath) + " (" + systemName(sys) + ")";
    Uint32 wflags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    if (st.fullscreen) wflags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    SDL_Window* win = nullptr;
    SDL_GLContext ctx = nullptr;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, compat ? SDL_GL_CONTEXT_PROFILE_COMPATIBILITY : SDL_GL_CONTEXT_PROFILE_CORE);
    win = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, st.windowW, st.windowH, wflags);
    setAppIcon(win);
    for (auto v : versions) {
        if (!win) break;
        if (v.maj < (int)reqMaj || (v.maj == (int)reqMaj && v.min < (int)reqMin)) continue;
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, v.maj);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, v.min);
        ctx = SDL_GL_CreateContext(win);   // mesma janela: só a versão do contexto muda
        if (ctx) break;
        util::log("GL %d.%d indisponivel: %s", v.maj, v.min, SDL_GetError());
    }
    if (!win || !ctx) { fail(std::string("Falha ao criar janela/contexto OpenGL: ") + SDL_GetError()); return 6; }
    SDL_GL_MakeCurrent(win, ctx);
    SDL_GL_SetSwapInterval(st.vsync ? 1 : 0);
    // garante que a janela do jogo receba o foco (teclado e controles)
    SDL_RaiseWindow(win);
#ifdef _WIN32
    {
        SDL_SysWMinfo wm;
        SDL_VERSION(&wm.version);
        if (SDL_GetWindowWMInfo(win, &wm)) {
            HWND h = wm.info.win.window;
            DWORD fg = GetWindowThreadProcessId(GetForegroundWindow(), nullptr), me = GetCurrentThreadId();
            if (fg != me) AttachThreadInput(fg, me, TRUE);
            ShowWindow(h, SW_SHOW);
            SetForegroundWindow(h);
            SetFocus(h);
            if (fg != me) AttachThreadInput(fg, me, FALSE);
        }
    }
#endif
    if (!ueLoadGL()) { fail("Seu driver de vídeo não oferece OpenGL 3.3."); return 6; }
    util::log("GL: %s | %s", (const char*)glGetString(GL_RENDERER), (const char*)glGetString(GL_VERSION));
    if (!g_video.init()) { fail("Falha ao compilar shaders de vídeo (veja UniEmu.log)."); return 6; }

    if (g_core.hwRequested) {
        unsigned mw = std::max(geo.max_width, geo.base_width), mh = std::max(geo.max_height, geo.base_height);
        g_video.createFBO(mw, mh, g_core.hw.depth, g_core.hw.stencil);
        if (g_core.hw.context_reset) g_core.hw.context_reset();
    }

    // ImGui para o menu dentro do jogo
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    int dw = 0, dh = 0;
    SDL_GL_GetDrawableSize(win, &dw, &dh);
    uiSetup(std::max(18.0f, dh / 42.0f));
    ImGui_ImplSDL2_InitForOpenGL(win, ctx);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    bool audioOk = g_audio.open(g_core.av.timing.sample_rate, st.audioLatencyMs);
    SDL_Joystick* testPad = nullptr;
    if (getenv("UNIEMU_TEST_VPAD")) {  // teste: controle virtual com o analógico esquerdo para a direita
        int vi = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 6, 15, 0);
        testPad = vi >= 0 ? SDL_JoystickOpen(vi) : nullptr;
        if (testPad) SDL_JoystickSetVirtualAxis(testPad, 0, 30000);
    }
    g_input.init();
    g_input.setAnalogToDpad(st.analogToDpad[(int)sys]);

    // SRAM
    std::string sramPath = util::joinPath(g_core.saveDir, util::stem(romPath) + ".srm");
    std::vector<uint8_t> sramLast;
    if (void* p = g_core.retro_get_memory_data(RETRO_MEMORY_SAVE_RAM)) {
        size_t sz = g_core.retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
        std::vector<uint8_t> f;
        if (sz && util::readFile(sramPath, f)) memcpy(p, f.data(), std::min(sz, f.size()));
        sramLast.assign((uint8_t*)p, (uint8_t*)p + sz);
    }
    auto saveSram = [&]() {
        void* p = g_core.retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
        size_t sz = g_core.retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
        if (!p || !sz) return;
        if (sramLast.size() == sz && memcmp(sramLast.data(), p, sz) == 0) return;
        if (util::writeFile(sramPath, p, sz)) sramLast.assign((uint8_t*)p, (uint8_t*)p + sz);
    };

    Osd osd;
    std::string scaleInfo = is2D(sys) ? std::string("Filtro ") + filterName(st.filter[(int)sys])
                                                : "Upscale " + std::to_string(st.upscale[(int)sys]) + "x";
    std::string pads = g_input.connectedPads() ? std::to_string(g_input.connectedPads()) + " controle(s): " + g_input.names()
                                               : "Nenhum controle detectado - usando o teclado";
    util::log("Input: %s", pads.c_str());
    osd.show(notice.empty() ? "Esc ou Select+Start: menu  |  " + scaleInfo + "\n" + pads : notice + "\n" + pads,
             notice.empty() ? 5000 : 7000);
    int slot = 0;
    bool menu = false, quit = false, ffHeld = false, paused = false;
    std::string stateBase = util::joinPath(util::joinPath(statesDir(), systemShort(sys)), util::stem(romPath));
    auto statePath = [&](int s) { return stateBase + ".state" + std::to_string(s); };
    auto saveState = [&]() {
        size_t sz = g_core.retro_serialize_size();
        std::vector<uint8_t> buf(sz);
        if (sz && g_core.retro_serialize(buf.data(), sz) && util::writeFile(statePath(slot), buf.data(), sz))
            osd.show("Estado salvo no slot " + std::to_string(slot));
        else osd.show("Este jogo/núcleo não permitiu salvar o estado agora");
    };
    auto loadState = [&]() {
        std::vector<uint8_t> buf;
        if (!util::readFile(statePath(slot), buf)) { osd.show("Slot " + std::to_string(slot) + " vazio"); return; }
        if (g_core.retro_unserialize(buf.data(), buf.size())) { osd.show("Estado carregado do slot " + std::to_string(slot)); g_audio.clear(); }
        else osd.show("Falha ao carregar o estado");
    };
    auto toggleFullscreen = [&]() {
        st.fullscreen = !(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP);
        SDL_SetWindowFullscreen(win, st.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    };
    auto takeScreenshot = [&]() {
        std::string p = util::joinPath(screenshotsDir(), util::stem(romPath) + "-" + timestamp() + ".bmp");
        osd.show(g_video.screenshot(p) ? "Captura salva em screenshots" : "Falha na captura");
    };
    float nativeLines = sys == System::Dreamcast ? 480.0f : (is2D(sys) ? 0.0f : 240.0f);
    std::string gpuName = (const char*)glGetString(GL_RENDERER);

    // modo de teste automático (desenvolvimento): UNIEMU_TEST_FRAMES=n UNIEMU_TEST_SHOT=arquivo.bmp
    long testFrames = getenv("UNIEMU_TEST_FRAMES") ? atol(getenv("UNIEMU_TEST_FRAMES")) : 0;
    const char* testShot = getenv("UNIEMU_TEST_SHOT");
    long frameCount = 0;

    Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 lastRun = SDL_GetPerformanceCounter(), nextFrame = lastRun;
    // Ritmo: se a TV/monitor roda na mesma frequência do console (~60 Hz), o VSync dita o ritmo e o áudio
    // se ajusta sozinho (controle dinâmico de taxa). Senão, um relógio de alta precisão marca os quadros.
    auto vsyncMatches = [&]() {
        SDL_DisplayMode dm;
        if (!st.vsync || SDL_GetWindowDisplayMode(win, &dm) != 0 || dm.refresh_rate <= 0) return false;
        double fps = g_core.av.timing.fps > 1 ? g_core.av.timing.fps : 60.0;
        return std::fabs(dm.refresh_rate - fps) / fps < 0.02;
    };
    bool vsyncPaced = vsyncMatches();
    {
        SDL_DisplayMode dm{};
        SDL_GetWindowDisplayMode(win, &dm);
        util::log("Ritmo: monitor %d Hz, nucleo %.3f fps -> %s", dm.refresh_rate, g_core.av.timing.fps,
                  vsyncPaced ? "VSync" : "relogio de alta precisao");
    }
    // medidor de FPS
    Uint64 fpsT0 = SDL_GetPerformanceCounter();
    int fpsFrames = 0;
    double fpsValue = 0, runMsAcc = 0, runMsShow = 0;
    Uint32 lastSramCheck = SDL_GetTicks();

    while (!quit && !g_core.shutdownRequested) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (menu) ImGui_ImplSDL2_ProcessEvent(&e);
            g_input.handleEvent(e);
            if (e.type == SDL_QUIT) quit = true;
            if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_FOCUS_LOST && !menu) {}
            if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                switch (e.key.keysym.sym) {
                case SDLK_ESCAPE: menu = !menu; break;
                case SDLK_F2: if (!menu) saveState(); break;
                case SDLK_F4: if (!menu) loadState(); break;
                case SDLK_F6: slot = (slot + 9) % 10; osd.show("Slot " + std::to_string(slot)); break;
                case SDLK_F7: slot = (slot + 1) % 10; osd.show("Slot " + std::to_string(slot)); break;
                case SDLK_F11: toggleFullscreen(); break;
                case SDLK_F12: takeScreenshot(); break;
                case SDLK_F3: st.showFps = !st.showFps; st.save(); break;
                case SDLK_TAB: ffHeld = true; break;
                case SDLK_p: if (!menu) { paused = !paused; osd.show(paused ? "Pausado" : "Continuando", 1200); } break;
                case SDLK_RETURN:
                    if (SDL_GetModState() & KMOD_ALT) { toggleFullscreen(); }
                    break;
                default: break;
                }
            }
            if (e.type == SDL_KEYUP && e.key.keysym.sym == SDLK_TAB) ffHeld = false;
        }
        if (g_input.menuComboPressed()) menu = !menu;
        bool running = !menu && !paused;
        g_core.fastForward = ffHeld && running;
        g_input.update(sys, !running);
        SDL_ShowCursor((menu || !st.fullscreen) ? SDL_ENABLE : SDL_DISABLE);

        if (running) {
            int runs = g_core.fastForward ? 4 : 1;
            for (int i = 0; i < runs; i++) {
                if (g_core.hasFrameTime) {
                    Uint64 now = SDL_GetPerformanceCounter();
                    retro_usec_t dt = (retro_usec_t)((now - lastRun) * 1000000 / freq);
                    lastRun = now;
                    g_core.frameTime.callback(g_core.fastForward ? g_core.frameTime.reference : dt);
                }
                Uint64 r0 = SDL_GetPerformanceCounter();
                g_core.retro_run();
                runMsAcc += (SDL_GetPerformanceCounter() - r0) * 1000.0 / freq;
                fpsFrames++;
                if (g_core.avChanged) {
                    g_core.avChanged = false;
                    const auto& ng = g_core.av.geometry;
                    if (g_core.hwRequested && (ng.max_width > g_video.fboWidth() || ng.max_height > g_video.fboHeight()))
                        g_video.createFBO(ng.max_width, ng.max_height, g_core.hw.depth, g_core.hw.stencil);
                    g_audio.setInputRate(g_core.av.timing.sample_rate);
                    vsyncPaced = vsyncMatches();
                }
                g_audio.flush(g_core.fastForward);
            }
            if (g_core.messageFrames) { osd.show(g_core.message, g_core.messageFrames * 1000 / 60); g_core.messageFrames = 0; }
        }

        // apresentação
        SDL_GL_GetDrawableSize(win, &dw, &dh);
        float aspect = -1.0f;
        int asp = st.aspect[(int)sys];
        if (asp == ASPECT_STRETCH) aspect = 16.0f / 9.0f;   // ocupa a tela 16:9 inteira
        else {
            const auto& g = g_core.av.geometry;
            aspect = g.aspect_ratio > 0.0f ? g.aspect_ratio : (g.base_height ? (float)g.base_width / g.base_height : 4.0f / 3.0f);
        }
        g_video.draw(dw, dh, st.filter[(int)sys], aspect, nativeLines);

        {
            Uint64 now = SDL_GetPerformanceCounter();
            double el = (now - fpsT0) / (double)freq;
            if (el >= 0.5) {
                fpsValue = fpsFrames / el;
                runMsShow = fpsFrames ? runMsAcc / fpsFrames : 0;
                fpsFrames = 0;
                runMsAcc = 0;
                fpsT0 = now;
            }
        }
        bool showOsd = SDL_GetTicks() < osd.until;
        if (menu || showOsd || st.showFps) {
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplSDL2_NewFrame();
            ImGui::NewFrame();
            ImGuiIO& io = ImGui::GetIO();
            if (st.showFps) {
                ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 16, 16), ImGuiCond_Always, ImVec2(1, 0));
                ImGui::SetNextWindowBgAlpha(0.55f);
                ImGui::Begin("##fps", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                                   ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                                                   ImGuiWindowFlags_NoFocusOnAppearing);
                double target = g_core.av.timing.fps;
                ImVec4 col = fpsValue >= target * 0.97 ? ImVec4(0.4f, 1, 0.5f, 1) : ImVec4(1, 0.5f, 0.3f, 1);
                ImGui::TextColored(col, "%.1f FPS", fpsValue);
                ImGui::TextDisabled("emulação %.1f ms/quadro | %ux%u", runMsShow, g_video.frameWidth(), g_video.frameHeight());
                ImGui::TextDisabled("%s%s", gpuName.c_str(), st.filter[(int)sys] == FILTER_FSR && g_video.fsrAvailable() ? " | FSR 1.0" : "");
                ImGui::End();
            }
            if (showOsd) {
                ImGui::SetNextWindowPos(ImVec2(16, 16));
                ImGui::SetNextWindowBgAlpha(0.6f);
                ImGui::Begin("##osd", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                                   ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                                                   ImGuiWindowFlags_NoFocusOnAppearing);
                ImGui::TextUnformatted(osd.text.c_str());
                ImGui::End();
            }
            if (menu) {
                ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
                ImGui::SetNextWindowBgAlpha(0.92f);
                ImGui::Begin("Menu", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
                ImGui::Text("%s", util::stem(romPath).c_str());
                ImGui::TextDisabled("%s  |  %s  |  %s", systemName(sys), scaleInfo.c_str(), g_core.info.library_name);
                ImGui::Separator();
                float bw = ImGui::GetFontSize() * 16;
                if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
                if (ImGui::Button("Continuar", ImVec2(bw, 0))) menu = false;
                ImGui::SetNextItemWidth(bw);
                ImGui::SliderInt("Slot", &slot, 0, 9);
                if (ImGui::Button("Salvar estado", ImVec2(bw * 0.5f - 5, 0))) { saveState(); menu = false; }
                ImGui::SameLine();
                if (ImGui::Button("Carregar estado", ImVec2(bw * 0.5f - 5, 0))) { loadState(); menu = false; }
                if (g_core.hasDisk && g_core.disk.get_num_images) {
                    unsigned nimg = g_core.disk.get_num_images(), cur = g_core.disk.get_image_index();
                    if (nimg > 1) {
                        ImGui::Text("Disco %u de %u", cur + 1, nimg);
                        if (ImGui::Button("Trocar para o próximo disco", ImVec2(bw, 0))) {
                            g_core.disk.set_eject_state(true);
                            g_core.disk.set_image_index((cur + 1) % nimg);
                            g_core.disk.set_eject_state(false);
                            osd.show("Disco " + std::to_string((cur + 1) % nimg + 1) + " inserido");
                            menu = false;
                        }
                    }
                }
                ImGui::SetNextItemWidth(bw);
                int& flt = st.filter[(int)sys];
                if (ImGui::BeginCombo("Filtro", filterName(flt))) {
                    for (int f = 0; f < FILTER_COUNT; f++)
                        if (ImGui::Selectable(filterName(f), f == flt)) { flt = f; st.save(); }
                    ImGui::EndCombo();
                }
                ImGui::SetNextItemWidth(bw);
                int& asp = st.aspect[(int)sys];
                if (ImGui::BeginCombo("Tela", aspectName(asp))) {
                    for (int a = 0; a < ASPECT_COUNT; a++) {
                        if (a == ASPECT_WIDESCREEN && !hasWidescreenHack(sys)) continue;
                        if (ImGui::Selectable(aspectName(a), a == asp)) {
                            bool hackChanged = (a == ASPECT_WIDESCREEN) != (asp == ASPECT_WIDESCREEN);
                            asp = a;
                            st.save();
                            if (hackChanged) {
                                configureRules(sys, st, ps1Fallback);
                                g_core.applyOverrides();
                                osd.show("Widescreen real: se não mudar na hora, reinicie o jogo", 4000);
                            }
                        }
                    }
                    ImGui::EndCombo();
                }
                if (ImGui::Checkbox("Analógico também move o direcional", &st.analogToDpad[(int)sys])) {
                    g_input.setAnalogToDpad(st.analogToDpad[(int)sys]);
                    st.save();
                }
                if (ImGui::Checkbox("Mostrar FPS (F3)", &st.showFps)) st.save();
                bool fs = st.fullscreen;
                if (ImGui::Checkbox("Tela cheia (F11)", &fs)) { toggleFullscreen(); st.save(); }
                if (ImGui::Button("Captura de tela (F12)", ImVec2(bw, 0))) takeScreenshot();
                if (ImGui::Button("Reiniciar jogo", ImVec2(bw, 0))) { g_core.retro_reset(); menu = false; }
                if (ImGui::Button("Sair do jogo", ImVec2(bw, 0))) quit = true;
                ImGui::Separator();
                ImGui::TextDisabled("F2 salvar | F4 carregar | F6/F7 slot | Tab acelerar | P pausar");
                ImGui::End();
            }
            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }
        // estado GL limpo para o núcleo
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glUseProgram(0);
        glBindVertexArray(0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_BLEND);
        glDisable(GL_SCISSOR_TEST);

        if (testFrames && getenv("UNIEMU_TEST_MENU") && frameCount == testFrames / 2) menu = true;
        if (testFrames && ++frameCount >= testFrames) {
            if (testShot) g_video.screenshot(testShot);
            util::log("Teste: %ld quadros, saida %ux%u (FBO %ux%u)", frameCount, g_video.frameWidth(),
                      g_video.frameHeight(), g_video.fboWidth(), g_video.fboHeight());
            quit = true;
        }
        SDL_GL_SwapWindow(win);

        // ritmo
        if (running && !g_core.fastForward) {
            if (vsyncPaced && audioOk) {
                g_audio.waitBelow(3.0);   // só segura se o áudio acumular demais (raro)
                nextFrame = SDL_GetPerformanceCounter();
            } else {
                double fps = g_core.av.timing.fps > 1 ? g_core.av.timing.fps : 60.0;
                nextFrame += (Uint64)(freq / fps);
                Uint64 now = SDL_GetPerformanceCounter();
                if (nextFrame + freq / 10 < now) nextFrame = now;          // atrasou muito: recomeça
                while (true) {
                    now = SDL_GetPerformanceCounter();
                    if (now >= nextFrame) break;
                    double ms = (nextFrame - now) * 1000.0 / freq;
                    if (ms > 2.0) SDL_Delay((Uint32)(ms - 1.5));
                    else SDL_Delay(0);
                }
            }
        } else if (!running) {
            if (!st.vsync) SDL_Delay(12);
            g_audio.clear();
            nextFrame = SDL_GetPerformanceCounter();
        } else {
            nextFrame = SDL_GetPerformanceCounter();
        }
        if (SDL_GetTicks() - lastSramCheck > 10000) { saveSram(); lastSramCheck = SDL_GetTicks(); }
    }

    saveSram();
    util::log("Input: consultas do nucleo por porta: %lu %lu %lu %lu | maior valor analogico enviado: %d",
              g_input.queries(0), g_input.queries(1), g_input.queries(2), g_input.queries(3), g_input.maxAnalogSent());
    if (testPad) SDL_JoystickClose(testPad);
    {
        int w, h;
        if (!(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP)) {
            SDL_GetWindowSize(win, &w, &h);
            st.windowW = w;
            st.windowH = h;
        }
        st.save();
    }
    g_audio.close();
    g_input.shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    if (g_core.hwRequested && g_core.hw.context_destroy) g_core.hw.context_destroy();
    g_core.retro_unload_game();
    g_core.retro_deinit();
    g_video.shutdown();
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    // não descarregamos a DLL do núcleo: alguns núcleos têm threads que terminam depois
    return 0;
}
