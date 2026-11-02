#include "external.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <regex>
#include <sstream>
#include "gpu.h"
#include "http.h"
#include "util.h"
#include <SDL.h>
#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

namespace fs = std::filesystem;

static const ExtEmuInfo kEmus[EXT_COUNT] = {
    {"Dolphin", "GameCube e Wii", "https://dolphin-emu.org/download/",
     {"Dolphin.exe", "dolphin-emu.exe", "dolphin-emu", nullptr},
     "Não precisa de BIOS. Jogos: .iso .gcm .rvz .wbfs .gcz .wia .ciso .wad"},
    {"PCSX2", "PlayStation 2", "https://pcsx2.net/downloads",
     {"pcsx2-qt.exe", "pcsx2.exe", "pcsx2-qtx64.exe", "pcsx2-qt", nullptr},
     "Precisa da BIOS do PS2 copiada do seu console (configure no próprio PCSX2 na primeira vez). "
     "Jogos: .iso .chd .cso .zso"},
    {"RPCS3", "PlayStation 3", "https://rpcs3.net/download", {"rpcs3.exe", "rpcs3", nullptr},
     "Instale o firmware oficial do PS3 (PS3UPDAT.PUP, do site da PlayStation) pelo RPCS3: File > Install Firmware. "
     "Jogos: pastas com PS3_GAME e jogos instalados no RPCS3 aparecem sozinhos."},
    {"shadPS4", "PlayStation 4", "https://shadps4.net", {"shadPS4.exe", "shadps4.exe", "shadps4", nullptr},
     "Emulador ainda em desenvolvimento: só parte dos jogos roda. Jogos: pasta com eboot.bin e sce_sys."},
    {"Emulador de Switch", "Nintendo Switch", "",
     {"Ryujinx.exe", "Ryubing.exe", "eden.exe", "citron.exe", "sudachi.exe", "yuzu.exe", "suyu.exe", nullptr},
     "Aponte o emulador de Switch que você usa (família Ryujinx ou yuzu). O UniEmu não fornece nem baixa chaves "
     "(prod.keys) nem firmware: elas precisam ser extraídas do seu próprio console. Jogos: .nsp .xci .nsz .xcz"},
    {"xemu", "Xbox", "https://xemu.app/docs/download/", {"xemu.exe", "xemu", nullptr},
     "Precisa de 3 arquivos: Boot ROM do MCPX e BIOS (Flash ROM), copiados do seu Xbox, e o HD virtual "
     "(o UniEmu baixa o HD pré-formatado oficial do xemu em \"BIOS e núcleos\"). Jogos: .iso (formato XISO / Redump)."},
    {"Xenia Canary", "Xbox 360", "https://github.com/xenia-canary/xenia-canary-releases/releases/latest",
     {"xenia_canary.exe", "xenia-canary.exe", "xenia.exe", "xenia_canary", nullptr},
     "Não precisa de BIOS. Pede uma placa de vídeo com DirectX 12 ou Vulkan. "
     "Jogos: .iso, pasta extraída (default.xex) ou .xex."},
};

const char* const kXemuHddUrl = "https://github.com/xemu-project/xemu-dashboard/releases/latest/download/xbox_hdd.qcow2";

const ExtEmuInfo& extEmuInfo(int id) { return kEmus[id]; }

int extEmuFor(System s) {
    switch (s) {
    case System::GameCube:
    case System::Wii: return EXT_DOLPHIN;
    case System::PS2: return EXT_PCSX2;
    case System::PS3: return EXT_RPCS3;
    case System::PS4: return EXT_SHADPS4;
    case System::Switch: return EXT_SWITCH;
    case System::Xbox: return EXT_XEMU;
    case System::Xbox360: return EXT_XENIA;
    default: return -1;
    }
}

bool extSupportsUpscale(System s) { return s != System::PS4 && isExternal(s); }

std::string extUpscaleHint(System s, int n) {
    char b[96];
    switch (s) {
    case System::GameCube:
    case System::Wii: snprintf(b, sizeof b, "%dx = ~%dx%d", n, 640 * n, 528 * n); break;
    case System::PS2: snprintf(b, sizeof b, "%dx = ~%dx%d", n, 640 * n, 448 * n); break;
    case System::PS3: snprintf(b, sizeof b, "%d%% = ~%dx%d", n * 100, 1280 * n, 720 * n); break;
    case System::Switch: snprintf(b, sizeof b, "%dx = ~%dx%d", n, 1280 * n, 720 * n); break;
    case System::PS4: snprintf(b, sizeof b, "não suportado pelo shadPS4"); break;
    case System::Xbox: snprintf(b, sizeof b, "%dx = ~%dx%d", n, 640 * n, 480 * n); break;
    case System::Xbox360: snprintf(b, sizeof b, "%dx = ~%dx%d", n, 1280 * n, 720 * n); break;
    default: b[0] = 0;
    }
    return b;
}

static std::string u8(const fs::path& p) { auto u = p.u8string(); return std::string(u.begin(), u.end()); }
static fs::path P(const std::string& s) { return fs::u8path(s); }

static bool nameMatches(const std::string& file, int id) {
    std::string f = util::toLower(file);
    for (int i = 0; kEmus[id].exeNames[i]; i++)
        if (f == util::toLower(kEmus[id].exeNames[i])) return true;
    return false;
}

static std::string findIn(const fs::path& dir, int id, int depth) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return "";
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
         it.increment(ec)) {
        if (it->is_regular_file(ec) && nameMatches(u8(it->path().filename()), id)) return u8(it->path());
    }
    if (depth <= 0) return "";
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
         it.increment(ec)) {
        if (it->is_directory(ec)) {
            std::string r = findIn(it->path(), id, depth - 1);
            if (!r.empty()) return r;
        }
    }
    return "";
}

static std::string envPath(const char* v) {
#ifdef _WIN32
    wchar_t buf[MAX_PATH * 4];
    std::wstring wv(v, v + strlen(v));
    DWORD n = GetEnvironmentVariableW(wv.c_str(), buf, MAX_PATH * 4);
    if (!n) return "";
    char o[MAX_PATH * 8];
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, o, sizeof o, nullptr, nullptr);
    return o;
#else
    const char* e = getenv(v);
    return e ? e : "";
#endif
}

std::string extEmuDetect(int id) {
    std::vector<fs::path> roots;
    fs::path base = P(util::baseDir());
    roots.push_back(base / "emuladores");
    roots.push_back(base.parent_path().parent_path());   // pasta "Emuladores" onde o UniEmu está
    for (const char* v : {"ProgramFiles", "ProgramW6432", "ProgramFiles(x86)"}) {
        std::string pf = envPath(v);
        if (!pf.empty()) roots.push_back(P(pf));
    }
    std::string la = envPath("LOCALAPPDATA");
    if (!la.empty()) roots.push_back(P(la) / "Programs");
    std::string up = envPath("USERPROFILE");
    if (!up.empty()) { roots.push_back(P(up) / "Desktop"); roots.push_back(P(up) / "Downloads"); }
    static const char* sub[EXT_COUNT][5] = {{"Dolphin", "Dolphin-x64", "Dolphin Emulator", nullptr},
                                            {"PCSX2", nullptr},
                                            {"RPCS3", nullptr},
                                            {"shadPS4", nullptr},
                                            {"Ryujinx", "Eden", "Citron", nullptr},
                                            {"xemu", "xemu-win-release", nullptr},
                                            {"xenia_canary", "xenia-canary", "Xenia Canary", "xenia"}};
    for (auto& r : roots) {
        for (int i = 0; sub[id][i]; i++) {
            std::string f = findIn(r / sub[id][i], id, 1);
            if (!f.empty()) return f;
        }
        bool shallow = r.filename() == "Program Files" || r.filename() == "Program Files (x86)";
        std::string f = findIn(r, id, shallow ? 0 : 2);
        if (!f.empty()) return f;
    }
    return "";
}

std::string extEmuPath(const Settings& st, int id) {
    if (id < 0 || id >= EXT_COUNT) return "";
    if (!st.emuPath[id].empty() && util::fileExists(st.emuPath[id])) return st.emuPath[id];
    static std::string cache[EXT_COUNT];
    static Uint32 when[EXT_COUNT] = {0};
    if (when[id] == 0 || SDL_GetTicks() - when[id] > 15000) {
        cache[id] = extEmuDetect(id);
        when[id] = SDL_GetTicks() | 1;
    }
    return cache[id];
}

std::string extRpcs3GamesDir(const Settings& st) {
    std::string exe = extEmuPath(st, EXT_RPCS3);
    if (exe.empty()) return "";
    std::string d = util::joinPath(util::joinPath(util::dirName(exe), "dev_hdd0"), "game");
    return util::dirExists(d) ? d : "";
}

// ---------------- edição segura de configurações ----------------

static bool readText(const std::string& p, std::string& out) {
    std::vector<uint8_t> d;
    if (!util::readFile(p, d)) return false;
    out.assign(d.begin(), d.end());
    return true;
}

static void backupOnce(const std::string& p) {
    std::string b = p + ".uniemu-backup";
    if (!util::fileExists(b)) {
        std::string t;
        if (readText(p, t)) util::writeFile(b, t.data(), t.size());
    }
}

static std::vector<std::string> splitLines(const std::string& s, std::string& eol) {
    eol = s.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    std::vector<std::string> v;
    std::stringstream ss(s);
    std::string l;
    while (std::getline(ss, l)) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        v.push_back(l);
    }
    return v;
}

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t");
    size_t b = s.find_last_not_of(" \t");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// INI: [secao] chave = valor (cria se não existir)
static bool iniSet(const std::string& path, const std::string& section, const std::string& key, const std::string& value,
                   bool spaced = true) {
    std::string text;
    readText(path, text);
    if (!text.empty()) backupOnce(path);
    std::string eol;
    auto lines = splitLines(text, eol);
    int secStart = -1, secEnd = (int)lines.size();
    for (int i = 0; i < (int)lines.size(); i++) {
        std::string t = trim(lines[i]);
        if (!t.empty() && t[0] == '[') {
            if (secStart >= 0) { secEnd = i; break; }
            if (t == "[" + section + "]") secStart = i;
        }
    }
    std::string newLine = key + (spaced ? " = " : "=") + value;
    if (secStart < 0) {
        if (!lines.empty() && !trim(lines.back()).empty()) lines.push_back("");
        lines.push_back("[" + section + "]");
        lines.push_back(newLine);
    } else {
        bool done = false;
        for (int i = secStart + 1; i < secEnd; i++) {
            size_t eq = lines[i].find('=');
            if (eq != std::string::npos && trim(lines[i].substr(0, eq)) == key) { lines[i] = newLine; done = true; break; }
        }
        if (!done) lines.insert(lines.begin() + secStart + 1, newLine);
    }
    std::string out;
    for (auto& l : lines) out += l + eol;
    util::makeDirs(util::dirName(path));
    return util::writeFile(path, out.data(), out.size());
}

// YAML simples (RPCS3): caminho de chaves por indentação, ex. {"Video", "Resolution Scale"}
static bool yamlSet(const std::string& path, const std::vector<std::string>& keys, const std::string& value) {
    std::string text;
    if (!readText(path, text)) return false;   // só edita se o RPCS3 já tiver criado o arquivo
    backupOnce(path);
    std::string eol;
    auto lines = splitLines(text, eol);
    size_t depth = 0;
    int indentWanted = 0;
    for (size_t i = 0; i < lines.size(); i++) {
        const std::string& l = lines[i];
        size_t ind = l.find_first_not_of(' ');
        if (ind == std::string::npos) continue;
        if ((int)ind < indentWanted && depth > 0) return false;   // saiu do bloco sem achar
        if ((int)ind != indentWanted) continue;
        std::string k = keys[depth] + ":";
        if (l.compare(ind, k.size(), k) == 0) {
            if (depth + 1 == keys.size()) {
                lines[i] = l.substr(0, ind) + k + " " + value;
                std::string out;
                for (auto& x : lines) out += x + eol;
                return util::writeFile(path, out.data(), out.size());
            }
            depth++;
            indentWanted = (int)ind + 2;
        }
    }
    return false;
}

static bool jsonSetNumber(const std::string& path, const std::string& key, int value) {
    std::string text;
    if (!readText(path, text)) return false;
    backupOnce(path);
    std::regex re("(\"" + key + "\"\\s*:\\s*)-?[0-9.]+");
    std::smatch m;
    if (!std::regex_search(text, m, re)) return false;
    std::string out = m.prefix().str() + m[1].str() + std::to_string(value) + m.suffix().str();
    return util::writeFile(path, out.data(), out.size());
}

static std::string documentsDir() {
#ifdef _WIN32
    PWSTR p = nullptr;
    std::string r;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &p))) {
        char o[MAX_PATH * 8];
        WideCharToMultiByte(CP_UTF8, 0, p, -1, o, sizeof o, nullptr, nullptr);
        r = o;
    }
    CoTaskMemFree(p);
    return r;
#else
    return envPath("HOME");
#endif
}

static std::string appDataDir() {
#ifdef _WIN32
    return envPath("APPDATA");
#else
    return util::joinPath(envPath("HOME"), ".config");
#endif
}

static std::string xemuConfigPath(const std::string& exe) {
    std::string local = util::joinPath(util::dirName(exe), "xemu.toml");
    if (util::fileExists(local)) return local;
    return util::joinPath(util::joinPath(util::joinPath(appDataDir(), "xemu"), "xemu"), "xemu.toml");
}

// lê "chave = valor" de uma seção de INI/TOML (tira aspas)
static std::string iniGet(const std::string& path, const std::string& section, const std::string& key) {
    std::string text, eol;
    if (!readText(path, text)) return "";
    bool in = false;
    for (auto& l : splitLines(text, eol)) {
        std::string t = trim(l);
        if (!t.empty() && t[0] == '[') { in = t == "[" + section + "]"; continue; }
        if (!in) continue;
        size_t eq = t.find('=');
        if (eq == std::string::npos || trim(t.substr(0, eq)) != key) continue;
        std::string v = trim(t.substr(eq + 1));
        if (v.size() >= 2 && (v[0] == '\'' || v[0] == '"')) {
            size_t e = v.find(v[0], 1);
            v = v.substr(1, e == std::string::npos ? std::string::npos : e - 1);
        }
        return v;
    }
    return "";
}

static bool fileBig(const std::string& p, uint64_t minBytes) {
    std::error_code ec;
    auto sz = fs::file_size(P(p), ec);
    return !ec && sz >= minBytes;
}

bool extPcsx2BiosPresent(const Settings& st, std::string* where) {
    std::string exe = extEmuPath(st, EXT_PCSX2);
    std::vector<std::string> dirs;
    if (!exe.empty()) dirs.push_back(util::joinPath(util::dirName(exe), "bios"));
    dirs.push_back(util::joinPath(util::joinPath(documentsDir(), "PCSX2"), "bios"));
    // pasta personalizada em PCSX2.ini [Folders] Bios
    for (std::string ini : {exe.empty() ? std::string() : util::joinPath(util::joinPath(util::dirName(exe), "inis"), "PCSX2.ini"),
                            util::joinPath(util::joinPath(util::joinPath(documentsDir(), "PCSX2"), "inis"), "PCSX2.ini")}) {
        if (ini.empty()) continue;
        std::string b = iniGet(ini, "Folders", "Bios");
        if (b.empty()) continue;
        bool abs = b.size() > 1 && (b[1] == ':' || b[0] == '/' || b[0] == '\\');
        dirs.push_back(abs ? b : util::joinPath(util::dirName(util::dirName(ini)), b));
    }
    for (auto& d : dirs) {
        std::error_code ec;
        if (!fs::is_directory(P(d), ec)) continue;
        for (fs::directory_iterator it(P(d), ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            std::string e = util::toLower(u8(it->path().extension()));
            uint64_t sz = it->file_size(ec);
            if ((e == ".bin" || e == ".rom0" || e == ".rom" || e.empty()) && sz >= 4u * 1024 * 1024 && sz <= 16u * 1024 * 1024) {
                if (where) *where = u8(it->path());
                return true;
            }
        }
    }
    if (where) *where = dirs.empty() ? "" : dirs.front();
    return false;
}

bool extRpcs3FirmwareInstalled(const Settings& st) {
    std::string exe = extEmuPath(st, EXT_RPCS3);
    if (exe.empty()) return false;
    std::string m = util::joinPath(util::joinPath(util::joinPath(util::joinPath(util::dirName(exe), "dev_flash"), "vsh"), "module"),
                                   "vsh.self");
    return util::fileExists(m);
}

XemuFiles extXemuFiles(const Settings& st) {
    XemuFiles x;
    std::string exe = extEmuPath(st, EXT_XEMU);
    x.config = exe.empty() ? util::joinPath(util::joinPath(util::joinPath(appDataDir(), "xemu"), "xemu"), "xemu.toml")
                           : xemuConfigPath(exe);
    x.bootrom = iniGet(x.config, "sys.files", "bootrom_path");
    x.flashrom = iniGet(x.config, "sys.files", "flashrom_path");
    x.hdd = iniGet(x.config, "sys.files", "hdd_path");
    x.bootromOk = !x.bootrom.empty() && fileBig(x.bootrom, 512);
    x.flashromOk = !x.flashrom.empty() && fileBig(x.flashrom, 256 * 1024);
    x.hddOk = !x.hdd.empty() && fileBig(x.hdd, 64 * 1024);
    return x;
}

bool extXemuInstallHdd(const Settings& st, std::string& msg) {
    std::string exe = extEmuPath(st, EXT_XEMU);
    if (exe.empty()) { msg = "Instale o xemu primeiro (botão \"Baixar emulador\")."; return false; }
    std::vector<uint8_t> d;
    int code = 0;
    if (!httpGet(kXemuHddUrl, d, &code) || d.size() < 64 * 1024 || memcmp(d.data(), "QFI\xfb", 4) != 0) {
        msg = "Não foi possível baixar o HD do xemu (HTTP " + std::to_string(code) + "). Verifique a internet.";
        return false;
    }
    std::string dst = util::joinPath(util::dirName(exe), "xbox_hdd.qcow2");
    if (util::fileExists(dst)) {   // nunca sobrescreve um HD com saves
        XemuFiles x = extXemuFiles(st);
        if (x.hdd != dst) { iniSet(x.config, "sys.files", "hdd_path", "'" + dst + "'"); }
        msg = "HD já existia em " + dst + " e foi configurado no xemu.";
        return true;
    }
    if (!util::writeFile(dst, d.data(), d.size())) { msg = "Não foi possível gravar " + dst; return false; }
    XemuFiles x = extXemuFiles(st);
    iniSet(x.config, "sys.files", "hdd_path", "'" + dst + "'");
    msg = "HD do Xbox baixado (" + std::to_string(d.size() / 1024) + " KB) e configurado no xemu.";
    util::log("xemu: HD em %s, config %s", dst.c_str(), x.config.c_str());
    return true;
}

// ---------------- montagem da execução ----------------

bool extPrepareLaunch(const GameEntry& g, Settings& st, std::string& exe, std::vector<std::string>& args,
                      std::string& err) {
    int id = extEmuFor(g.sys);
    if (id < 0) { err = "Console sem emulador externo."; return false; }
    exe = extEmuPath(st, id);
    if (exe.empty()) {
        err = std::string(kEmus[id].name) + " não encontrado. Configure em \"Emuladores externos\".";
        return false;
    }
    std::string dir = util::dirName(exe);
    int n = std::clamp(st.upscale[(int)g.sys], 1, 8);
    int asp = st.aspect[(int)g.sys];
    bool fs = st.fullscreen;
    const GpuInfo* gpu = st.gpuPref ? gpuForPref(st.gpuPref) : nullptr;
    gpuApplyPreference(exe, st.gpuPref);
    args.clear();
    switch (id) {
    case EXT_DOLPHIN: {
        args = {"-b", "-e", g.path};
        auto C = [&](const std::string& kv) { args.push_back("-C"); args.push_back(kv); };
        C(std::string("Dolphin.Display.Fullscreen=") + (fs ? "True" : "False"));
        C("GFX.Settings.InternalResolution=" + std::to_string(n));
        C(std::string("GFX.Settings.AspectRatio=") + (asp == ASPECT_ORIGINAL ? "0" : asp == ASPECT_STRETCH ? "3" : "1"));
        C(std::string("GFX.Settings.wideScreenHack=") + (asp == ASPECT_WIDESCREEN ? "True" : "False"));
        break;
    }
    case EXT_PCSX2: {
        bool portable = util::fileExists(util::joinPath(dir, "portable.ini")) || util::fileExists(util::joinPath(dir, "portable.txt"));
        std::string ini = portable ? util::joinPath(util::joinPath(dir, "inis"), "PCSX2.ini")
                                   : util::joinPath(util::joinPath(util::joinPath(documentsDir(), "PCSX2"), "inis"), "PCSX2.ini");
        iniSet(ini, "EmuCore/GS", "upscale_multiplier", std::to_string(n));
        iniSet(ini, "EmuCore/GS", "AspectRatio",
               asp == ASPECT_ORIGINAL ? "Auto 4:3/3:2" : asp == ASPECT_STRETCH ? "Stretch" : "16:9");
        iniSet(ini, "EmuCore", "EnableWideScreenPatches", asp == ASPECT_WIDESCREEN ? "true" : "false");
        if (gpu) iniSet(ini, "EmuCore/GS", "Adapter", gpu->name);
        util::log("Externo: PCSX2.ini = %s", ini.c_str());
        args = {"-batch", fs ? "-fullscreen" : "-nofullscreen", "--", g.path};
        break;
    }
    case EXT_RPCS3: {
        std::string yml = util::joinPath(util::joinPath(dir, "config"), "config.yml");
        if (!util::fileExists(yml)) yml = util::joinPath(dir, "config.yml");
        if (!yamlSet(yml, {"Video", "Resolution Scale"}, std::to_string(n * 100)))
            util::log("Externo: nao foi possivel ajustar Resolution Scale em %s (abra o RPCS3 uma vez antes)", yml.c_str());
        if (gpu) yamlSet(yml, {"Video", "Vulkan", "Adapter"}, gpu->name);
        args = {"--no-gui"};
        if (fs) args.push_back("--fullscreen");
        args.push_back(g.path);
        break;
    }
    case EXT_SHADPS4:
        args = {"-g", g.path, "-f", fs ? "true" : "false"};
        break;
    case EXT_XEMU: {
        // xemu.toml: modo portátil se existir ao lado do exe; senão %APPDATA%\xemu\xemu\xemu.toml
        std::string toml = xemuConfigPath(exe);
        iniSet(toml, "display.quality", "surface_scale", std::to_string(n));
        iniSet(toml, "display.ui", "fit", asp == ASPECT_STRETCH ? "'stretch'" : "'scale'");
        iniSet(toml, "display.ui", "aspect_ratio", asp == ASPECT_STRETCH ? "'16x9'" : "'auto'");
        if (gpu) iniSet(toml, "display.vulkan", "preferred_physical_device", "'" + gpu->name + "'");
        util::log("Externo: xemu.toml = %s", toml.c_str());
        args = {"-dvd_path", g.path};
        if (fs) args.push_back("-full-screen");
        break;
    }
    case EXT_XENIA: {
        // Xenia aceita qualquer opção do .config.toml na linha de comando (--opcao=valor)
        int k = std::clamp(n, 1, 4);
        args = {std::string("--fullscreen=") + (fs ? "true" : "false"), "--draw_resolution_scale_x=" + std::to_string(k),
                "--draw_resolution_scale_y=" + std::to_string(k), g.path};
        break;
    }
    case EXT_SWITCH: {
        std::string name = util::toLower(util::stem(exe));
        bool ryu = name.find("ryu") != std::string::npos;
        if (ryu) {
            std::string cfg = util::joinPath(util::joinPath(dir, "portable"), "Config.json");
            if (!util::fileExists(cfg)) cfg = util::joinPath(util::joinPath(appDataDir(), "Ryujinx"), "Config.json");
            jsonSetNumber(cfg, "res_scale", std::min(n, 4));
            if (fs) args.push_back("--fullscreen");
            args.push_back(g.path);
        } else {   // família yuzu (Eden, Citron, Sudachi...)
            static const int idx[9] = {2, 2, 4, 5, 6, 7, 8, 9, 10};   // 1x,2x,3x,4x... -> enum ResolutionSetup
            std::string cfg = util::joinPath(util::joinPath(util::joinPath(dir, "user"), "config"), "qt-config.ini");
            if (!util::fileExists(cfg))
                cfg = util::joinPath(util::joinPath(util::joinPath(appDataDir(), name), "config"), "qt-config.ini");
            if (util::fileExists(cfg)) {
                iniSet(cfg, "Renderer", "resolution_setup\\default", "false", false);
                iniSet(cfg, "Renderer", "resolution_setup", std::to_string(idx[n]), false);
            }
            if (fs) args.push_back("-f");
            args.push_back("-g");
            args.push_back(g.path);
        }
        break;
    }
    }
    std::string cmd = exe;
    for (auto& a : args) cmd += " \"" + a + "\"";
    util::log("Externo: %s", cmd.c_str());
    return true;
}
