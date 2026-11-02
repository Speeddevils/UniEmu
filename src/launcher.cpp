// UniEmu - tela inicial (biblioteca de jogos e configurações)
#include <algorithm>
#include <atomic>
#include <cstring>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#include "detect.h"
#include "gl.h"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"
#include "appicon.h"
#include "controllers.h"
#include "external.h"
#include "gpu.h"
#include "http.h"
#include "miniz.h"
#include "library_ui.h"
#include "play.h"
#include "settings.h"
#include "ui_common.h"
#include "util.h"

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <commdlg.h>
#else
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifndef UNIEMU_VERSION
#define UNIEMU_VERSION "1.0.0"
#endif

namespace {

#ifdef _WIN32
std::wstring widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n ? n - 1 : 0, 0);
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}
std::string narrow(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n ? n - 1 : 0, 0);
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    return s;
}
#endif

std::string selfExe() {
#ifdef _WIN32
    wchar_t exe[MAX_PATH * 4];
    GetModuleFileNameW(nullptr, exe, MAX_PATH * 4);
    return narrow(exe);
#else
    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    exe[n > 0 ? n : 0] = 0;
    return exe;
#endif
}

struct Child {
#ifdef _WIN32
    HANDLE proc = nullptr;
#else
    pid_t pid = -1;
#endif
    bool running() const {
#ifdef _WIN32
        return proc != nullptr;
#else
        return pid > 0;
#endif
    }
    bool startProcess(const std::string& exe, const std::vector<std::string>& args) {
#ifdef _WIN32
        auto quote = [](const std::wstring& a) {
            if (!a.empty() && a.find_first_of(L" \t\"") == std::wstring::npos) return a;
            std::wstring o = L"\"";
            int bs = 0;
            for (wchar_t c : a) {
                if (c == L'\\') { bs++; continue; }
                if (c == L'"') { o.append(bs * 2 + 1, L'\\'); o += L'"'; bs = 0; continue; }
                o.append(bs, L'\\');
                bs = 0;
                o += c;
            }
            o.append(bs * 2, L'\\');
            return o + L"\"";
        };
        std::wstring wexe = widen(exe);
        std::wstring cmd = quote(wexe);
        for (auto& a : args) cmd += L" " + quote(widen(a));
        std::wstring cwd = widen(util::dirName(exe));
        STARTUPINFOW si{};
        si.cb = sizeof si;
        PROCESS_INFORMATION pi{};
        AllowSetForegroundWindow(ASFW_ANY);
        if (!CreateProcessW(wexe.c_str(), &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, cwd.empty() ? nullptr : cwd.c_str(),
                            &si, &pi)) {
            util::log("Falha ao iniciar %s (erro %lu)", exe.c_str(), GetLastError());
            return false;
        }
        CloseHandle(pi.hThread);
        proc = pi.hProcess;
        return true;
#else
        pid = fork();
        if (pid == 0) {
            std::string d = util::dirName(exe);
            if (!d.empty() && chdir(d.c_str())) {}
            std::vector<char*> av;
            av.push_back((char*)exe.c_str());
            for (auto& a : args) av.push_back((char*)a.c_str());
            av.push_back(nullptr);
            execv(exe.c_str(), av.data());
            _exit(127);
        }
        return pid > 0;
#endif
    }
    bool start(const std::string& rom) { return startProcess(selfExe(), {"--play", rom}); }
    // retorna true quando terminou; code = código de saída
    bool poll(int& code) {
#ifdef _WIN32
        if (!proc || WaitForSingleObject(proc, 0) != WAIT_OBJECT_0) return false;
        DWORD c = 0;
        GetExitCodeProcess(proc, &c);
        CloseHandle(proc);
        proc = nullptr;
        code = (int)c;
        return true;
#else
        int status = 0;
        if (pid <= 0 || waitpid(pid, &status, WNOHANG) != pid) return false;
        pid = -1;
        code = WIFEXITED(status) ? WEXITSTATUS(status) : 100;
        return true;
#endif
    }
};

// ---------------- downloads em segundo plano (núcleos, HD do xemu) ----------------
struct BgTask {
    std::thread th;
    std::atomic<bool> busy{false};
    std::mutex m;
    std::string msg;
    void run(const std::string& what, std::function<std::string()> fn) {
        if (busy) return;
        if (th.joinable()) th.join();
        busy = true;
        { std::lock_guard<std::mutex> l(m); msg = what + "..."; }
        th = std::thread([this, fn] {
            std::string r = fn();
            { std::lock_guard<std::mutex> l(m); msg = r; }
            busy = false;
        });
    }
    std::string text() { std::lock_guard<std::mutex> l(m); return msg; }
    ~BgTask() { if (th.joinable()) th.detach(); }
};
static BgTask g_bg;

static std::string downloadCore(const std::string& lib, const std::string& name) {
#ifdef _WIN32
    std::string url = "https://buildbot.libretro.com/nightly/windows/x86_64/latest/" + lib + ".dll.zip";
#else
    std::string url = "https://buildbot.libretro.com/nightly/linux/x86_64/latest/" + lib + ".so.zip";
#endif
    std::vector<uint8_t> d;
    int code = 0;
    if (!httpGet(url, d, &code) || d.size() < 1024)
        return name + ": não foi possível baixar (HTTP " + std::to_string(code) + "). Verifique a internet.";
    mz_zip_archive z;
    memset(&z, 0, sizeof z);
    if (!mz_zip_reader_init_mem(&z, d.data(), d.size(), 0)) return name + ": arquivo baixado inválido.";
    std::string want = util::fileName(coreLibName(lib.c_str()));
    int idx = mz_zip_reader_locate_file(&z, want.c_str(), nullptr, 0);
    size_t sz = 0;
    void* p = idx >= 0 ? mz_zip_reader_extract_to_heap(&z, (mz_uint)idx, &sz, 0) : nullptr;
    mz_zip_reader_end(&z);
    if (!p) return name + ": o arquivo baixado não contém " + want + ".";
    util::makeDirs(coresDir());
    bool ok = util::writeFile(coreLibName(lib.c_str()), p, sz);
    mz_free(p);
    util::log("Núcleo baixado: %s (%zu bytes) ok=%d", url.c_str(), sz, ok);
    return ok ? name + " instalado na pasta cores." : name + ": não foi possível gravar na pasta cores.";
}

void openUrl(const std::string& url) {
#ifdef _WIN32
    ShellExecuteW(nullptr, L"open", widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    std::string cmd = "xdg-open '" + url + "' >/dev/null 2>&1 &";
    if (system(cmd.c_str())) {}
#endif
}

bool pickExe(std::string& out) {
#ifdef _WIN32
    wchar_t buf[MAX_PATH * 4] = L"";
    OPENFILENAMEW of{};
    of.lStructSize = sizeof of;
    of.lpstrFilter = L"Programas (*.exe)\0*.exe\0";
    of.lpstrFile = buf;
    of.nMaxFile = MAX_PATH * 4;
    of.lpstrTitle = L"Escolha o executável do emulador";
    of.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&of)) return false;
    out = narrow(buf);
    return true;
#else
    (void)out;
    return false;
#endif
}

void openFolder(const std::string& path) {
    util::makeDirs(path);
#ifdef _WIN32
    ShellExecuteW(nullptr, L"open", widen(path).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    std::string cmd = "xdg-open '" + path + "' >/dev/null 2>&1 &";
    if (system(cmd.c_str())) {}
#endif
}

bool browseFolder(std::string& out) {
#ifdef _WIN32
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    BROWSEINFOW bi{};
    bi.lpszTitle = L"Escolha uma pasta com jogos (Dreamcast, PS1, N64 ou SNES)";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return false;
    wchar_t buf[MAX_PATH * 4];
    bool ok = SHGetPathFromIDListW(pidl, buf);
    CoTaskMemFree(pidl);
    if (ok) out = narrow(buf);
    return ok;
#else
    (void)out;
    return false;
#endif
}

struct Library {
    std::mutex m;
    std::vector<GameEntry> games;
    std::thread th;
    std::atomic<bool> scanning{false}, cancel{false};
    std::atomic<int> progress{0};

    void scan(const std::vector<std::string>& dirs) {
        if (scanning) return;
        if (th.joinable()) th.join();
        scanning = true;
        cancel = false;
        progress = 0;
        th = std::thread([this, dirs]() {
            auto r = scanLibrary(dirs, &cancel, &progress);
            {
                std::lock_guard<std::mutex> lk(m);
                games = std::move(r);
            }
            scanning = false;
        });
    }
    ~Library() {
        cancel = true;
        if (th.joinable()) th.join();
    }
};

const ImVec4 kSysColor[] = {
    ImVec4(0.6f, 0.6f, 0.6f, 1), ImVec4(1.0f, 0.55f, 0.25f, 1), ImVec4(0.55f, 0.65f, 1.0f, 1),
    ImVec4(0.4f, 0.85f, 0.45f, 1), ImVec4(0.85f, 0.55f, 0.95f, 1)};

std::string upscaleHint(System s, int n) {
    char b[96];
    switch (s) {
    case System::Dreamcast: snprintf(b, sizeof b, "%dx%d", 640 * n, 480 * n); break;
    case System::PS1: {
        int f = n <= 1 ? 1 : (n <= 2 ? 2 : (n <= 5 ? 4 : 8));
        snprintf(b, sizeof b, "%dx (~%dx%d)", f, 320 * f, 240 * f);
        break;
    }
    case System::N64: snprintf(b, sizeof b, "~%dx%d", 320 * n, 240 * n); break;
    default: b[0] = 0;
    }
    return b;
}

}  // namespace

void padUiDraw();

int runLauncher(Settings& st) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    int testH = getenv("UNIEMU_TEST_H") ? atoi(getenv("UNIEMU_TEST_H")) : 800;   // teste: janela mais alta
    SDL_Window* win = SDL_CreateWindow("UniEmu", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, testH,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI |
                                           (getenv("UNIEMU_TEST_SHOT") ? 0 : SDL_WINDOW_MAXIMIZED));
    if (!win) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "UniEmu", SDL_GetError(), nullptr);
        return 1;
    }
    setAppIcon(win);
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "UniEmu", "OpenGL 3.3 indisponível. Atualize o driver de vídeo.", nullptr);
        return 1;
    }
    SDL_GL_MakeCurrent(win, ctx);
    SDL_GL_SetSwapInterval(1);
    ueLoadGL();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    float dpi = 96.0f;
    if (SDL_GetDisplayDPI(SDL_GetWindowDisplayIndex(win), &dpi, nullptr, nullptr) != 0 || dpi < 50) dpi = 96.0f;
    uiSetup(20.0f * std::max(1.0f, dpi / 96.0f));
    ImGui_ImplSDL2_InitForOpenGL(win, ctx);
    ImGui_ImplOpenGL3_Init("#version 330 core");
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (controllersEnsureMapping(i)) SDL_GameControllerOpen(i);

    Library lib;
    Child child;
    int page = getenv("UNIEMU_TEST_TAB") ? atoi(getenv("UNIEMU_TEST_TAB")) : 0;   // 0-4 biblioteca, 5+ configurações
    LibraryUi libUi;
    auto scanDirs = [&]() {
        std::vector<std::string> d = st.romDirs;
        std::string r = extRpcs3GamesDir(st);   // jogos instalados no RPCS3
        if (!r.empty()) d.push_back(r);
        return d;
    };
    lib.scan(scanDirs());
    gpuApplyPreference(selfExe(), st.gpuPref);
    bool wasScanning = true;
    char newDir[1024] = "";
    std::string status;
    bool quit = false;
    std::string currentGame;

    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT) quit = true;
            if (e.type == SDL_DROPFILE) {
                std::string p = e.drop.file;
                SDL_free(e.drop.file);
                if (util::dirExists(p)) {
                    st.romDirs.push_back(p);
                    st.save();
                    lib.scan(scanDirs());
                } else if (!child.running() && child.start(p)) {
                    currentGame = util::stem(p);
                    SDL_HideWindow(win);
                }
            }
            if (e.type == SDL_JOYDEVICEADDED && !SDL_IsGameController(e.jdevice.which)) controllersEnsureMapping(e.jdevice.which);
            if (e.type == SDL_CONTROLLERDEVICEADDED) SDL_GameControllerOpen(e.cdevice.which);
        }
        if (child.running()) {
            int code = 0;
            if (child.poll(code)) {
                SDL_ShowWindow(win);
                SDL_RaiseWindow(win);
                st.load();
                util::log("Processo do jogo terminou (codigo %d)", code);
                status = code == 0 ? "Último jogo: " + currentGame
                                   : "O jogo terminou com erro (código " + std::to_string(code) + "). Veja UniEmu-jogo.log";
            } else {
                SDL_Delay(100);
                continue;
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        ImGuiIO& io = ImGui::GetIO();
        uiDrawBackground();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::Begin("##main", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                            ImGuiWindowFlags_NoSavedSettings);

        std::vector<GameEntry> games;
        {
            std::lock_guard<std::mutex> lk(lib.m);
            games = lib.games;
        }
        auto launch = [&](const GameEntry& g) {
            st.save();
            bool ok;
            if (isExternal(g.sys)) {
                std::string exe, err;
                std::vector<std::string> args;
                if (!extPrepareLaunch(g, st, exe, args, err)) {
                    status = err;
                    page = 21;
                    return;
                }
                ok = child.startProcess(exe, args);
            } else {
                gpuApplyPreference(selfExe(), st.gpuPref);
                ok = child.start(g.path);
            }
            if (ok) {
                currentGame = g.name;
                status.clear();
                SDL_HideWindow(win);
            } else status = "Não foi possível iniciar o jogo (veja UniEmu.log).";
        };

        if (const char* tl = getenv("UNIEMU_TEST_LAUNCH")) {   // teste: abre o primeiro jogo que combinar
            static bool launched = false;
            if (!launched && !lib.scanning && !games.empty()) {
                for (auto& g : games)
                    if (g.name.find(tl) != std::string::npos) { launched = true; launch(g); break; }
            }
            if (launched && !child.running() && status.empty()) quit = true;
            if (launched && !status.empty() && !child.running()) { util::log("Teste: status = %s", status.c_str()); quit = true; }
        }
        if (wasScanning && !lib.scanning) {   // varredura terminou: busca capas que faltam
            if (st.coversAuto) libUi.dl.enqueue(games, false);
            libUi.lastRefresh = 0;
        }
        wasScanning = lib.scanning;

        // ---------------- barra lateral ----------------
        float fsz = ImGui::GetFontSize();
        float sideW = fsz * 13.5f;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.05f, 0.035f, 0.13f, 0.88f));
        ImGui::BeginChild("side", ImVec2(sideW, 0), ImGuiChildFlags_Borders);
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImFont* pix = uiPixelFont() ? uiPixelFont() : ImGui::GetFont();
            ImVec2 lp = ImGui::GetCursorScreenPos();
            float ls = pix->FontSize * 1.25f;
            // logotipo com "sombra" neon
            dl->AddText(pix, ls, ImVec2(lp.x + 3, lp.y + 3), ImGui::GetColorU32(theme::cyan), "UNIEMU");
            dl->AddText(pix, ls, lp, ImGui::GetColorU32(theme::magenta), "UNIEMU");
            ImGui::Dummy(ImVec2(sideW - 32, ls + 6));
            ImGui::TextDisabled("v" UNIEMU_VERSION " - 14 consoles");
            ImGui::Spacing();
            int counts[(int)System::Count] = {0};
            for (auto& g : games) counts[(int)g.sys]++;
            auto navItem = [&](int id, const char* label, ImVec4 color, int count) {
                bool sel = page == id;
                ImVec2 p = ImGui::GetCursorScreenPos();
                float h = fsz * 1.75f, w = ImGui::GetContentRegionAvail().x;
                ImGui::PushID(id);
                if (ImGui::InvisibleButton("nav", ImVec2(w, h))) page = id;
                bool hov = ImGui::IsItemHovered() || (ImGui::IsItemFocused() && ImGui::GetIO().NavVisible);
                ImGui::PopID();
                if (sel || hov)
                    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h),
                                      ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, sel ? 0.28f : 0.14f)), 8.0f);
                if (sel) dl->AddRectFilled(p, ImVec2(p.x + 4, p.y + h), ImGui::GetColorU32(color), 2.0f);
                dl->AddCircleFilled(ImVec2(p.x + fsz * 0.9f, p.y + h / 2), fsz * 0.28f, ImGui::GetColorU32(color));
                dl->AddText(ImVec2(p.x + fsz * 1.7f, p.y + (h - fsz) / 2),
                            ImGui::GetColorU32(sel ? ImVec4(1, 1, 1, 1) : theme::text), label);
                if (count >= 0) {
                    char c[16];
                    snprintf(c, sizeof c, "%d", count);
                    ImVec2 ts = ImGui::CalcTextSize(c);
                    dl->AddText(ImVec2(p.x + w - ts.x - 10, p.y + (h - fsz) / 2), ImGui::GetColorU32(theme::dim), c);
                }
            };
            ImGui::TextDisabled("BIBLIOTECA");
            navItem(0, "Todos os jogos", theme::magenta, (int)games.size());
            ImGui::TextDisabled("CLÁSSICOS");
            for (System sx : {System::Atari2600, System::MegaDrive, System::SNES, System::PS1, System::N64, System::Dreamcast})
                navItem((int)sx, systemName(sx), systemColor(sx), counts[(int)sx]);
            ImGui::TextDisabled("MODERNOS");
            for (System sx : {System::PS2, System::GameCube, System::Wii, System::Xbox, System::Xbox360, System::PS3, System::PS4,
                               System::Switch})
                navItem((int)sx, systemName(sx), systemColor(sx), counts[(int)sx]);
            ImGui::Spacing();
            ImGui::TextDisabled("CONFIGURAÇÕES");
            navItem(20, "Vídeo e Upscale", theme::cyan, -1);
            navItem(21, "Emuladores externos", theme::cyan, -1);
            navItem(22, "Pastas de jogos", theme::cyan, -1);
            navItem(23, "BIOS e núcleos", theme::cyan, -1);
            navItem(24, "Controles", theme::cyan, -1);
            navItem(25, "Sobre", theme::cyan, -1);
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::BeginChild("main", ImVec2(0, 0), ImGuiChildFlags_None);
        if (page < (int)System::Count) {
            libUi.draw(st, games, page, lib.scanning, lib.progress.load(), launch, [&] { lib.scan(scanDirs()); }, status);
        }
        {
            static const char* ptitles[] = {"VIDEO E UPSCALE", "EMULADORES EXTERNOS", "PASTAS DE JOGOS", "BIOS E NUCLEOS",
                                            "CONTROLES", "SOBRE"};
            if (page >= 20 && page <= 25) {
                ImGui::PushFont(uiPixelFont() ? uiPixelFont() : ImGui::GetFont());
                ImGui::TextColored(theme::cyan, "%s", ptitles[page - 20]);
                ImGui::PopFont();
                ImGui::Spacing();
            }
            // ---------------- VÍDEO / UPSCALE ----------------
            if (page == 20) {
                bool changed = false;
                float fw = ImGui::GetFontSize();
                // ---- placa de vídeo ----
                ImGui::SeparatorText("Placa de vídeo usada nos jogos");
                auto gpus = gpuList();
                const GpuInfo* gInt = gpuForPref(1);
                const GpuInfo* gDed = gpuForPref(2);
                for (auto& g : gpus)
                    ImGui::BulletText("%s  (%s, %llu MB)", g.name.c_str(), g.integrated ? "integrada" : "dedicada",
                                      (unsigned long long)g.vramMB);
                if (gpus.empty()) ImGui::TextDisabled("Nenhuma placa listada (só funciona no Windows).");
                std::string opt[3] = {"Automático (o Windows decide)",
                                      std::string("Integrada do processador (economia)") + (gInt ? " - " + gInt->name : " - não encontrada"),
                                      std::string("Dedicada (alto desempenho)") + (gDed ? " - " + gDed->name : " - não encontrada")};
                ImGui::SetNextItemWidth(fw * 26);
                if (ImGui::BeginCombo("Usar", opt[st.gpuPref].c_str())) {
                    for (int i = 0; i < 3; i++)
                        if (ImGui::Selectable(opt[i].c_str(), st.gpuPref == i)) {
                            st.gpuPref = i;
                            changed = true;
                            gpuApplyPreference(selfExe(), st.gpuPref);
                        }
                    ImGui::EndCombo();
                }
                ImGui::TextWrapped("Vale para o UniEmu e para os emuladores externos (Dolphin, PCSX2, RPCS3...) a partir do próximo "
                                   "jogo. Usa a mesma configuração do Windows em Configurações > Sistema > Tela > Gráficos.");
                if (st.gpuPref == 1)
                    ImGui::TextColored(theme::yellow, "Na placa integrada, use os perfis \"Placa integrada\" abaixo e o filtro AMD FSR "
                                                      "para manter o FPS.");

                // ---- upscale ----
                ImGui::SeparatorText("Upscale (resolução interna 3D)");
                ImGui::TextWrapped("Renderiza o 3D numa resolução maior que a do console original, como o upscale do Redream.");
                struct U { System s; bool ext; };
                U ups[] = {{System::PS1, false}, {System::N64, false}, {System::Dreamcast, false}, {System::PS2, true},
                           {System::GameCube, true}, {System::Wii, true}, {System::Xbox, true}, {System::Xbox360, true},
                           {System::PS3, true}, {System::Switch, true}};
                for (auto& u : ups) {
                    ImGui::PushID((int)u.s);
                    ImGui::SetNextItemWidth(fw * 12);
                    int mx = (u.s == System::PS3 || u.s == System::Switch || u.s == System::Xbox360) ? 4 : 8;
                    changed |= ImGui::SliderInt(systemName(u.s), &st.upscale[(int)u.s], 1, mx, "%dx");
                    ImGui::SameLine(fw * 22);
                    ImGui::TextDisabled("%s%s", u.ext ? extUpscaleHint(u.s, st.upscale[(int)u.s]).c_str()
                                                      : upscaleHint(u.s, st.upscale[(int)u.s]).c_str(),
                                        u.ext ? "  (emulador externo)" : "");
                    ImGui::PopID();
                }
                ImGui::TextDisabled("PlayStation 4: o shadPS4 ainda não tem upscale. Atari, Mega Drive e SNES são 2D (use os filtros).");
                auto preset = [&](int dc, int ps1, int n64, int ps2, int gc, int ps3, int sw, int xb, int x360) {
                    st.upscale[(int)System::Xbox] = xb; st.upscale[(int)System::Xbox360] = x360;
                    st.upscale[(int)System::Dreamcast] = dc; st.upscale[(int)System::PS1] = ps1; st.upscale[(int)System::N64] = n64;
                    st.upscale[(int)System::PS2] = ps2; st.upscale[(int)System::GameCube] = st.upscale[(int)System::Wii] = gc;
                    st.upscale[(int)System::PS3] = ps3; st.upscale[(int)System::Switch] = sw;
                    changed = true;
                };
                if (ImGui::Button("Placa integrada")) preset(2, 2, 2, 2, 2, 1, 1, 2, 1);
                ImGui::SameLine();
                if (ImGui::Button("TV 1080p (recomendado)")) preset(4, 4, 4, 3, 3, 2, 2, 3, 2);
                ImGui::SameLine();
                if (ImGui::Button("TV 4K")) preset(6, 8, 6, 6, 6, 3, 3, 4, 3);

                // ---- super resolução ----
                ImGui::SeparatorText("Super resolução e DLSS");
                ImGui::TextWrapped("AMD FSR 1.0 (filtro abaixo): super resolução em tempo real que amplia a imagem para a tela com "
                                   "nitidez adaptativa. Funciona em qualquer placa (NVIDIA, AMD ou Intel) e ajuda muito na placa "
                                   "integrada: use upscale baixo + FSR.");
                ImGui::TextWrapped("DLSS: o DLSS da NVIDIA precisa de dados que só o motor do próprio jogo fornece (vetores de "
                                   "movimento e profundidade), então nenhum emulador consegue aplicá-lo nos jogos. O \"DLSS 5 Visual "
                                   "Enhancer\" do link trabalha com arquivos de foto e vídeo já gravados, não com a imagem do jogo em "
                                   "tempo real, e a licença dele não permite redistribuição.");
                ImGui::TextColored(ImVec4(0.46f, 0.73f, 0.0f, 1), "Tem placa RTX? Use o DLDSR (IA da NVIDIA que funciona com o UniEmu):");
                ImGui::TextWrapped("Painel de Controle NVIDIA > Gerenciar configurações 3D > Fatores DSR: marque \"DL 1.78x\" e "
                                   "\"DL 2.25x\". Depois escolha a resolução maior (ex.: 2880x1620) no Windows ou no modo tela cheia. "
                                   "A RTX renderiza acima da resolução da TV e reduz a imagem com IA.");

                // ---- filtros ----
                ImGui::SeparatorText("Filtro de imagem (consoles do UniEmu)");
                for (System sx : {System::Atari2600, System::MegaDrive, System::SNES, System::PS1, System::N64, System::Dreamcast}) {
                    int s2 = (int)sx;
                    ImGui::PushID(100 + s2);
                    ImGui::SetNextItemWidth(fw * 18);
                    if (ImGui::BeginCombo(systemName(sx), filterName(st.filter[s2]))) {
                        for (int f = 0; f < FILTER_COUNT; f++)
                            if (ImGui::Selectable(filterName(f), f == st.filter[s2])) { st.filter[s2] = f; changed = true; }
                        ImGui::EndCombo();
                    }
                    ImGui::PopID();
                }
                if (ImGui::Button("Usar AMD FSR em todos os 3D")) {
                    for (System sx : {System::PS1, System::N64, System::Dreamcast}) st.filter[(int)sx] = FILTER_FSR;
                    changed = true;
                }

                // ---- tela ----
                ImGui::SeparatorText("Tela: 4:3 ou 16:9 (por console)");
                for (System sx : {System::Atari2600, System::MegaDrive, System::SNES, System::PS1, System::N64, System::Dreamcast,
                                  System::PS2, System::GameCube, System::Wii, System::Xbox}) {
                    int s2 = (int)sx;
                    ImGui::PushID(200 + s2);
                    ImGui::SetNextItemWidth(fw * 20);
                    if (ImGui::BeginCombo(systemName(sx), aspectName(st.aspect[s2]))) {
                        for (int a2 = 0; a2 < ASPECT_COUNT; a2++) {
                            if (a2 == ASPECT_WIDESCREEN && !hasWidescreenHack(sx)) continue;
                            if (ImGui::Selectable(aspectName(a2), a2 == st.aspect[s2])) { st.aspect[s2] = a2; changed = true; }
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::PopID();
                }
                ImGui::TextWrapped("Xbox 360, PS3, PS4 e Switch já são 16:9 (no Xbox, ative o 16:9 no painel do próprio jogo/console). \"Widescreen real\" mostra mais cenário (no PS2 e no GameCube/Wii "
                                   "usa os patches de widescreen do PCSX2/Dolphin). Em jogos com cenários pré-desenhados "
                                   "(Alone in the Dark, Resident Evil) use \"16:9 esticado\".");

                ImGui::SeparatorText("Controle e desempenho");
                for (System sx : {System::Atari2600, System::MegaDrive, System::SNES, System::PS1, System::N64, System::Dreamcast}) {
                    char lbl[96];
                    snprintf(lbl, sizeof lbl, "%s: analógico também move o direcional", systemName(sx));
                    changed |= ImGui::Checkbox(lbl, &st.analogToDpad[(int)sx]);
                }
                changed |= ImGui::Checkbox("Mostrar FPS durante o jogo (F3)", &st.showFps);
                changed |= ImGui::Checkbox("Iniciar jogos em tela cheia (também nos emuladores externos)", &st.fullscreen);
                changed |= ImGui::Checkbox("VSync (sem cortes na imagem)", &st.vsync);
                changed |= ImGui::Checkbox("PS1: correção de geometria (PGXP, remove a \"tremedeira\" dos polígonos)", &st.pgxp);
                ImGui::SetNextItemWidth(fw * 14);
                changed |= ImGui::SliderInt("Latência de áudio", &st.audioLatencyMs, 32, 160, "%d ms");
                if (changed) st.save();
            }

            // ---------------- EMULADORES EXTERNOS ----------------
            if (page == 21) {
                ImGui::TextWrapped("Os consoles mais novos rodam nos melhores emuladores de cada um, abertos pelo UniEmu: você "
                                   "escolhe o jogo pela capa aqui e o UniEmu já entrega o upscale, a tela 16:9, a tela cheia e a "
                                   "placa de vídeo configurados. Controles e BIOS/firmware são configurados uma vez dentro de cada "
                                   "emulador.");
                ImGui::PushStyleColor(ImGuiCol_Text, theme::dim);
                ImGui::TextWrapped("Dica: coloque os emuladores em UniEmu\\emuladores\\ ou na pasta Emuladores: o UniEmu encontra sozinho.");
                ImGui::PopStyleColor();
                if (!status.empty()) ImGui::TextColored(theme::yellow, "%s", status.c_str());
                static char buf[EXT_COUNT][1024];
                static bool init = false;
                if (!init) { for (int i = 0; i < EXT_COUNT; i++) snprintf(buf[i], sizeof buf[i], "%s", st.emuPath[i].c_str()); init = true; }
                for (int i = 0; i < EXT_COUNT; i++) {
                    const ExtEmuInfo& in = extEmuInfo(i);
                    ImGui::PushID(300 + i);
                    ImGui::SeparatorText((std::string(in.name) + "  -  " + in.consoles).c_str());
                    std::string found = extEmuPath(st, i);
                    if (!found.empty()) ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1), "OK  %s", found.c_str());
                    else ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "Não encontrado");
                    ImGui::SetNextItemWidth(std::max(ImGui::GetFontSize() * 10, ImGui::GetContentRegionAvail().x - ImGui::GetFontSize() * 28));
                    if (ImGui::InputTextWithHint("##p", "Caminho do executável", buf[i], sizeof buf[i]))
                        { st.emuPath[i] = buf[i]; st.save(); }
                    ImGui::SameLine();
                    std::string picked;
                    if (ImGui::Button("Procurar...") && pickExe(picked)) {
                        st.emuPath[i] = picked;
                        snprintf(buf[i], sizeof buf[i], "%s", picked.c_str());
                        st.save();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Detectar")) {
                        std::string d = extEmuDetect(i);
                        if (!d.empty()) { st.emuPath[i] = d; snprintf(buf[i], sizeof buf[i], "%s", d.c_str()); st.save(); }
                        else status = std::string(in.name) + ": não encontrado nas pastas comuns.";
                    }
                    if (in.site[0]) {
                        ImGui::SameLine();
                        if (ImGui::Button("Baixar (site oficial)")) openUrl(in.site);
                    }
                    ImGui::TextWrapped("%s", in.notes);
                    ImGui::PopID();
                }
                if (ImGui::Button("Atualizar a biblioteca")) lib.scan(scanDirs());
            }

            // ---------------- PASTAS ----------------
            if (page == 22) {
                ImGui::TextWrapped("O UniEmu procura jogos nestas pastas (e subpastas). Os quatro consoles podem ficar "
                                   "misturados: o console é identificado automaticamente.");
                int remove = -1;
                for (int i = 0; i < (int)st.romDirs.size(); i++) {
                    ImGui::PushID(i);
                    if (ImGui::SmallButton("Remover")) remove = i;
                    ImGui::SameLine();
                    bool ex = util::dirExists(st.romDirs[i]);
                    ImGui::TextColored(ex ? ImVec4(0.9f, 0.9f, 0.9f, 1) : ImVec4(1, 0.4f, 0.4f, 1), "%s%s",
                                       st.romDirs[i].c_str(), ex ? "" : "  (não encontrada)");
                    ImGui::PopID();
                }
                if (remove >= 0) { st.romDirs.erase(st.romDirs.begin() + remove); st.save(); lib.scan(scanDirs()); }
                ImGui::Spacing();
                ImGui::SetNextItemWidth(ImGui::GetFontSize() * 26);
                ImGui::InputTextWithHint("##nd", "Ex.: D:\\Jogos\\PS1", newDir, sizeof newDir);
                ImGui::SameLine();
                if (ImGui::Button("Adicionar") && newDir[0]) {
                    st.romDirs.push_back(newDir);
                    newDir[0] = 0;
                    st.save();
                    lib.scan(scanDirs());
                }
#ifdef _WIN32
                ImGui::SameLine();
                std::string picked;
                if (ImGui::Button("Procurar...") && browseFolder(picked)) {
                    st.romDirs.push_back(picked);
                    st.save();
                    lib.scan(scanDirs());
                }
#endif
                ImGui::TextDisabled("Dica: também dá para arrastar uma pasta para esta janela.");
            }

            // ---------------- BIOS E NÚCLEOS ----------------
            if (page == 23) {
                const ImVec4 cOk(0.4f, 0.9f, 0.5f, 1), cWarn(1, 0.75f, 0.3f, 1), cBad(1, 0.4f, 0.4f, 1), cDim(0.6f, 0.6f, 0.6f, 1);
                const float fw = ImGui::GetFontSize();
                std::string bg = g_bg.text();
                if (!bg.empty()) ImGui::TextColored(g_bg.busy ? theme::cyan : theme::yellow, "%s", bg.c_str());

                // ---- núcleos internos ----
                struct C { const char* sys; const char* lib; const char* name; };
                C cores[] = {{"Dreamcast", "flycast_libretro", "Flycast"},
                             {"PlayStation", "mednafen_psx_hw_libretro", "Beetle PSX HW"},
                             {"PlayStation (sem BIOS)", "pcsx_rearmed_libretro", "PCSX ReARMed"},
                             {"Nintendo 64", "mupen64plus_next_libretro", "Mupen64Plus-Next (GLideN64)"},
                             {"Super Nintendo", "snes9x_libretro", "Snes9x"},
                             {"Mega Drive", "genesis_plus_gx_libretro", "Genesis Plus GX"},
                             {"Atari 2600", "stella_libretro", "Stella"}};
                ImGui::SeparatorText("Núcleos de emulação (pasta cores)");
                if (ImGui::BeginTable("cores", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Console", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                    ImGui::TableSetupColumn("Núcleo", ImGuiTableColumnFlags_WidthStretch, 1.3f);
                    ImGui::TableSetupColumn("Situação", ImGuiTableColumnFlags_WidthStretch, 0.6f);
                    ImGui::TableSetupColumn("Download", ImGuiTableColumnFlags_WidthStretch, 1.1f);
                    ImGui::TableHeadersRow();
                    for (auto& c : cores) {
                        bool ok = util::fileExists(coreLibName(c.lib));
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(c.sys);
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(c.name);
                        ImGui::TableNextColumn(); ImGui::TextColored(ok ? cOk : cBad, ok ? "OK" : "FALTA");
                        ImGui::TableNextColumn();
                        ImGui::PushID(c.lib);
                        if (!ok) {
                            ImGui::BeginDisabled(g_bg.busy);
                            if (ImGui::SmallButton("Baixar núcleo (libretro)")) {
                                std::string lib = c.lib, nm = c.name;
                                g_bg.run("Baixando " + nm, [lib, nm] { return downloadCore(lib, nm); });
                            }
                            ImGui::EndDisabled();
                        } else {
                            ImGui::TextDisabled("instalado");
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                ImGui::PushStyleColor(ImGuiCol_Text, theme::dim);
                ImGui::TextWrapped("%s", "Os núcleos são baixados do servidor oficial do libretro (buildbot.libretro.com), código aberto.");
                ImGui::PopStyleColor();

                // ---- BIOS / firmware por console ----
                ImGui::SeparatorText("BIOS, firmware e arquivos de sistema (por console)");
                ImGui::PushStyleColor(ImGuiCol_Text, theme::dim);
                ImGui::TextWrapped("BIOS e firmware são programas da Sony, Sega e Microsoft protegidos por direitos autorais: a forma "
                                   "legal de tê-los é copiar do seu próprio console. Por isso o UniEmu não indica sites que distribuem "
                                   "BIOS; os botões abaixo levam aos guias oficiais de cada emulador e aos arquivos que os próprios "
                                   "fabricantes/projetos oferecem de graça (firmware do PS3 no site da PlayStation e HD virtual do xemu).");
                ImGui::PopStyleColor();
                // botão pequeno que pula de linha quando não cabe na coluna
                static float actColX = 0;   // início da coluna de botões
                auto fitBtn = [&](const char* label) {
                    float w = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2;
                    if (ImGui::GetCursorPosX() > actColX + 1 && ImGui::GetContentRegionAvail().x < w) ImGui::NewLine();
                    return ImGui::SmallButton(label);
                };
                auto link = [&](const char* label, const char* url) {
                    if (fitBtn(label)) openUrl(url);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", url);
                    ImGui::SameLine();
                };
                if (ImGui::BeginTable("bios", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Console", ImGuiTableColumnFlags_WidthStretch, 0.65f);
                    ImGui::TableSetupColumn("O que precisa", ImGuiTableColumnFlags_WidthStretch, 1.5f);
                    ImGui::TableSetupColumn("Situação", ImGuiTableColumnFlags_WidthStretch, 0.9f);
                    ImGui::TableSetupColumn("Como obter", ImGuiTableColumnFlags_WidthStretch, 1.4f);
                    ImGui::TableHeadersRow();
                    auto row = [&](System s, const char* need, ImVec4 col, const std::string& state, const std::function<void()>& actions) {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::PushStyleColor(ImGuiCol_Text, systemColor(s));
                        ImGui::TextWrapped("%s", s == System::SNES ? "SNES, Mega Drive, Atari, N64" : systemName(s));
                        ImGui::PopStyleColor();
                        ImGui::TableNextColumn(); ImGui::TextWrapped("%s", need);
                        ImGui::TableNextColumn();
                        {   // uma linha por item; linhas terminadas em "OK" ficam verdes
                            size_t a = 0;
                            while (a <= state.size()) {
                                size_t b = state.find('\n', a);
                                std::string ln = state.substr(a, b == std::string::npos ? std::string::npos : b - a);
                                bool lnOk = ln.size() >= 2 && ln.compare(ln.size() - 2, 2, "OK") == 0;
                                ImGui::PushStyleColor(ImGuiCol_Text, lnOk ? cOk : col);
                                ImGui::TextWrapped("%s", ln.c_str());
                                ImGui::PopStyleColor();
                                if (b == std::string::npos) break;
                                a = b + 1;
                            }
                        }
                        ImGui::TableNextColumn();
                        actColX = ImGui::GetCursorPosX();
                        ImGui::PushID((int)s + 900);
                        if (actions) { actions(); ImGui::Dummy(ImVec2(0, 0)); }
                        ImGui::PopID();
                    };
                    std::string ps1f;
                    bool ps1 = ps1BiosPresent(&ps1f);
                    row(System::PS1, "BIOS do PS1 (recomendada): scph5501.bin (EUA), scph5500.bin (JP) ou scph5502.bin (EU) na pasta system.",
                        ps1 ? cOk : cWarn, ps1 ? "OK - " + util::fileName(ps1f) : "AUSENTE (roda no modo compatível, upscale até 2x)", [&] {
                            link("Guia: arquivos aceitos", "https://docs.libretro.com/library/beetle_psx_hw/#bios");
                            if (fitBtn("Abrir pasta system")) openFolder(systemDir());
                        });
                    bool dc = dcBiosPresent();
                    row(System::Dreamcast, "Opcional: dc_boot.bin e dc_flash.bin em system\\dc (sem elas o Flycast usa BIOS HLE).",
                        dc ? cOk : cDim, dc ? "OK" : "opcional (não precisa)", [&] {
                            link("Guia: arquivos aceitos", "https://docs.libretro.com/library/flycast/#bios");
                            if (fitBtn("Abrir pasta system\\dc")) openFolder(util::joinPath(systemDir(), "dc"));
                        });
                    row(System::SNES, "Nada.", cOk, "OK", nullptr);
                    {
                        bool emu = !extEmuPath(st, EXT_PCSX2).empty();
                        std::string where;
                        bool ok = emu && extPcsx2BiosPresent(st, &where);
                        row(System::PS2, "BIOS do PS2, copiada do seu console (configurada no PCSX2 na primeira vez).",
                            ok ? cOk : cBad, !emu ? "instale o PCSX2" : ok ? "OK - " + util::fileName(where) : "AUSENTE", [&] {
                                link("Guia oficial PCSX2", "https://pcsx2.net/docs/setup/bios/");
                                link("biosdrain (copiar do PS2)", "https://github.com/F0bes/biosdrain");
                                if (emu && fitBtn("Abrir pasta bios"))
                                    openFolder(ok ? util::dirName(where) : where);
                            });
                    }
                    row(System::GameCube, "Nada (GameCube e Wii: o Dolphin já simula a BIOS).", cOk, "OK", nullptr);
                    {
                        bool emu = !extEmuPath(st, EXT_XEMU).empty();
                        XemuFiles x = extXemuFiles(st);
                        std::string state = !emu ? "instale o xemu" : "";
                        if (emu) {
                            state += std::string("Boot ROM: ") + (x.bootromOk ? "OK" : "falta") + "\n";
                            state += std::string("BIOS (Flash): ") + (x.flashromOk ? "OK" : "falta") + "\n";
                            state += std::string("HD virtual: ") + (x.hddOk ? "OK" : "falta");
                        }
                        bool all = emu && x.bootromOk && x.flashromOk && x.hddOk;
                        row(System::Xbox, "Boot ROM do MCPX e BIOS (Flash ROM) copiadas do seu Xbox + HD virtual (este o xemu "
                                          "oferece pronto e gratuito). Os caminhos ficam em Machine > System no xemu.",
                            all ? cOk : cBad, state, [&] {
                                link("Guia oficial xemu", "https://xemu.app/docs/required-files/");
                                if (emu && !x.hddOk) {
                                    ImGui::BeginDisabled(g_bg.busy);
                                    if (fitBtn("Baixar HD oficial do xemu")) {
                                        Settings* sp = &st;
                                        g_bg.run("Baixando o HD virtual do xemu", [sp] {
                                            std::string m;
                                            extXemuInstallHdd(*sp, m);
                                            return m;
                                        });
                                    }
                                    ImGui::EndDisabled();
                                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kXemuHddUrl);
                                }
                            });
                    }
                    row(System::Xbox360, "Nada (o Xenia não usa BIOS).", cOk, "OK", nullptr);
                    {
                        bool emu = !extEmuPath(st, EXT_RPCS3).empty();
                        bool ok = emu && extRpcs3FirmwareInstalled(st);
                        row(System::PS3, "Firmware oficial do PS3 (PS3UPDAT.PUP), gratuito no site da PlayStation. No RPCS3: "
                                         "File > Install Firmware.",
                            ok ? cOk : cBad, !emu ? "instale o RPCS3" : ok ? "OK - instalado" : "NÃO INSTALADO", [&] {
                                link("Firmware oficial (Sony)",
                                     "https://www.playstation.com/en-us/support/hardware/ps3/system-software/");
                                link("Guia RPCS3", "https://rpcs3.net/quickstart");
                            });
                    }
                    row(System::PS4, "Nada obrigatório. Alguns jogos pedem módulos do firmware do seu próprio PS4.", cOk, "OK",
                        [&] { link("Guia shadPS4", "https://github.com/shadps4-emu/shadPS4#readme"); });
                    row(System::Switch, "Chaves (prod.keys) e firmware extraídos do seu próprio Switch. O UniEmu não indica "
                                        "onde baixá-los.", cDim, "configure no emulador", nullptr);
                    ImGui::EndTable();
                }

                // ---- emuladores externos ----
                ImGui::SeparatorText("Emuladores dos consoles modernos");
                if (ImGui::BeginTable("emus", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Emulador", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                    ImGui::TableSetupColumn("Consoles", ImGuiTableColumnFlags_WidthStretch, 0.9f);
                    ImGui::TableSetupColumn("Situação", ImGuiTableColumnFlags_WidthStretch, 0.8f);
                    ImGui::TableSetupColumn("Download", ImGuiTableColumnFlags_WidthStretch, 1.3f);
                    ImGui::TableHeadersRow();
                    for (int i = 0; i < EXT_COUNT; i++) {
                        const ExtEmuInfo& in = extEmuInfo(i);
                        bool ok = !extEmuPath(st, i).empty();
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(in.name);
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(in.consoles);
                        ImGui::TableNextColumn(); ImGui::TextColored(ok ? cOk : cWarn, ok ? "instalado" : "não encontrado");
                        ImGui::TableNextColumn();
                        ImGui::PushID(700 + i);
                        actColX = ImGui::GetCursorPosX();
                        if (in.site[0]) {
                            if (fitBtn(ok ? "Site oficial" : "Baixar (site oficial)")) openUrl(in.site);
                            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", in.site);
                            ImGui::SameLine();
                        } else {
                            ImGui::TextDisabled("use o que você já tem");
                            ImGui::SameLine();
                        }
                        if (fitBtn("Configurar")) page = 21;
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                ImGui::PushStyleColor(ImGuiCol_Text, theme::dim);
                ImGui::TextWrapped("%s", "Depois de baixar, extraia o emulador em UniEmu\\emuladores\\<nome> que o UniEmu encontra sozinho.");
                ImGui::PopStyleColor();
                ImGui::Spacing();
                (void)fw;
                actColX = ImGui::GetCursorPosX();
                if (fitBtn("Abrir pasta system")) openFolder(systemDir());
                ImGui::SameLine();
                if (fitBtn("Abrir pasta emuladores")) openFolder(util::joinPath(util::baseDir(), "emuladores"));
                ImGui::SameLine();
                if (fitBtn("Abrir pasta de saves")) openFolder(savesDir());
                ImGui::SameLine();
                if (fitBtn("Abrir capturas de tela")) openFolder(screenshotsDir());
            }

            // ---------------- CONTROLES ----------------
            if (page == 24) {
                ImGui::TextWrapped("Controles de Xbox, PlayStation e genéricos USB são detectados automaticamente (até 4 jogadores). "
                                   "O teclado controla o jogador 1.");
                padUiDraw();
                ImGui::SeparatorText("Mapa dos botões");
                if (ImGui::BeginTable("ctl", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
                    ImGui::TableSetupColumn("Botão (padrão RetroPad)");
                    ImGui::TableSetupColumn("Controle");
                    ImGui::TableSetupColumn("Teclado");
                    ImGui::TableHeadersRow();
                    const char* rows[][3] = {
                        {"Direcional", "D-pad", "Setas (no N64: analógico; D-pad = T F G H)"},
                        {"Analógico esquerdo", "Analógico esquerdo", "Setas (N64)"},
                        {"Analógico direito / botões C (N64)", "Analógico direito", "I J K L"},
                        {"B (baixo)", "A / Cruz", "Z"},
                        {"A (direita)", "B / Círculo", "X"},
                        {"Y (esquerda)", "X / Quadrado", "A"},
                        {"X (cima)", "Y / Triângulo", "S"},
                        {"L / R", "LB / RB", "Q / W"},
                        {"L2 / R2 (gatilhos)", "LT / RT", "E / R"},
                        {"Start / Select", "Start / Back", "Enter / Backspace"},
                        {"Menu do jogo", "Select + Start (ou Guide)", "Esc"},
                    };
                    for (auto& r : rows) {
                        ImGui::TableNextRow();
                        for (int c = 0; c < 3; c++) { ImGui::TableNextColumn(); ImGui::TextUnformatted(r[c]); }
                    }
                    ImGui::EndTable();
                }
                ImGui::SeparatorText("Atalhos durante o jogo");
                ImGui::TextUnformatted("F2 salvar estado  |  F4 carregar  |  F6/F7 trocar slot  |  Tab (segurar) acelerar\n"
                                       "P pausar  |  F11 ou Alt+Enter tela cheia  |  F12 captura de tela  |  Esc menu");
            }

            if (page == 25) {
                ImGui::TextWrapped("UniEmu v" UNIEMU_VERSION " - 14 consoles num só lugar, com upscale de resolução interna: "
                                   "Atari 2600, Mega Drive, Super Nintendo, PlayStation, Nintendo 64 e Dreamcast rodam dentro do "
                                   "UniEmu; PS2, GameCube, Wii, Xbox, Xbox 360, PS3, PS4 e Switch abrem nos emuladores externos.");
                ImGui::Spacing();
                ImGui::TextWrapped("O UniEmu é um frontend libretro. A emulação é feita por núcleos de código aberto, "
                                   "carregados como módulos separados, cada um com sua licença:");
                ImGui::BulletText("Flycast (Dreamcast) - GPLv2");
                ImGui::BulletText("Beetle PSX HW (PlayStation) - GPLv2 / PCSX ReARMed - GPLv2");
                ImGui::BulletText("Mupen64Plus-Next + GLideN64 (Nintendo 64) - GPLv2");
                ImGui::BulletText("Snes9x (Super Nintendo) - licença própria, uso NÃO comercial");
                ImGui::BulletText("Genesis Plus GX (Mega Drive) - uso NÃO comercial / Stella (Atari 2600) - GPLv2");
                ImGui::BulletText("Externos (baixados à parte): Dolphin, PCSX2, RPCS3, shadPS4, xemu, Xenia - cada um com sua licença");
                ImGui::BulletText("AMD FidelityFX FSR 1.0 (MIT), Press Start 2P (OFL)");
                ImGui::BulletText("Interface: SDL2 (zlib), Dear ImGui (MIT), miniz (MIT), shader xBR de Hyllian (MIT)");
                ImGui::Spacing();
                ImGui::TextWrapped("BIOS e jogos não acompanham o programa: use cópias dos seus próprios discos/cartuchos e consoles.");
            }
        }
        ImGui::EndChild();
        ImGui::End();

        ImGui::Render();
        int w, h;
        SDL_GL_GetDrawableSize(win, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.09f, 0.10f, 0.12f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (const char* shot = getenv("UNIEMU_TEST_SHOT")) {  // teste automatizado
            static int frames = 0;
            if (++frames >= 90 && !libUi.dl.busy() && !lib.scanning) {
                std::vector<uint8_t> px((size_t)w * h * 4);
                glReadPixels(0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, px.data());
                uint8_t hdr[54] = {'B', 'M'};
                auto w32 = [&](int o, uint32_t v) { for (int i = 0; i < 4; i++) hdr[o + i] = (v >> (8 * i)) & 0xff; };
                w32(2, 54 + (uint32_t)px.size()); w32(10, 54); w32(14, 40); w32(18, w); w32(22, h);
                hdr[26] = 1; hdr[28] = 32; w32(34, (uint32_t)px.size());
                std::vector<uint8_t> f(hdr, hdr + 54);
                f.insert(f.end(), px.begin(), px.end());
                util::writeFile(shot, f.data(), f.size());
                quit = true;
            }
        }
        SDL_GL_SwapWindow(win);
    }

    st.save();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    return 0;
}
