#include "util.h"
#include <SDL.h>
#include <cstdarg>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <mutex>
#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace util {

static fs::path u8(const std::string& s) {
#if defined(__cpp_char8_t)
    return fs::path(reinterpret_cast<const char8_t*>(s.c_str()));
#else
    return fs::u8path(s);
#endif
}

const std::string& baseDir() {
    static std::string dir;
    if (dir.empty()) {
        char* p = SDL_GetBasePath();
        dir = p ? p : "./";
        SDL_free(p);
    }
    return dir;
}

std::string joinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    char last = a.back();
    if (last == '/' || last == '\\') return a + b;
#ifdef _WIN32
    return a + "\\" + b;
#else
    return a + "/" + b;
#endif
}

static size_t lastSep(const std::string& p) {
    size_t a = p.find_last_of("/\\");
    return a;
}

std::string fileName(const std::string& path) {
    size_t s = lastSep(path);
    return s == std::string::npos ? path : path.substr(s + 1);
}

std::string stem(const std::string& path) {
    std::string f = fileName(path);
    size_t d = f.find_last_of('.');
    return d == std::string::npos ? f : f.substr(0, d);
}

std::string extLower(const std::string& path) {
    std::string f = fileName(path);
    size_t d = f.find_last_of('.');
    return d == std::string::npos ? "" : toLower(f.substr(d));
}

std::string dirName(const std::string& path) {
    size_t s = lastSep(path);
    return s == std::string::npos ? "" : path.substr(0, s);
}

std::string toLower(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
    return s;
}

bool fileExists(const std::string& path) {
    std::error_code ec;
    return fs::is_regular_file(u8(path), ec);
}

bool dirExists(const std::string& path) {
    std::error_code ec;
    return fs::is_directory(u8(path), ec);
}

void makeDirs(const std::string& path) {
    std::error_code ec;
    fs::create_directories(u8(path), ec);
}

bool removeFile(const std::string& path) {
    std::error_code ec;
    return fs::remove(u8(path), ec);
}

FILE* fopenUtf8(const std::string& path, const char* mode) {
#ifdef _WIN32
    int n = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &w[0], n);
    wchar_t wm[8] = {0};
    for (int i = 0; mode[i] && i < 7; i++) wm[i] = (wchar_t)mode[i];
    return _wfopen(w.c_str(), wm);
#else
    return fopen(path.c_str(), mode);
#endif
}

bool readFile(const std::string& path, std::vector<uint8_t>& out, size_t maxBytes) {
    FILE* f = fopenUtf8(path, "rb");
    if (!f) return false;
    out.clear();
    uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
        out.insert(out.end(), buf, buf + n);
        if (out.size() >= maxBytes) break;
    }
    fclose(f);
    return true;
}

bool writeFile(const std::string& path, const void* data, size_t size) {
    makeDirs(dirName(path));
    std::string tmp = path + ".tmp";
    FILE* f = fopenUtf8(tmp, "wb");
    if (!f) return false;
    bool ok = fwrite(data, 1, size, f) == size;
    ok = (fclose(f) == 0) && ok;
    if (!ok) return false;
    std::error_code ec;
    fs::rename(u8(tmp), u8(path), ec);
    return !ec;
}

size_t readHead(const std::string& path, uint8_t* buf, size_t n, uint64_t offset) {
    FILE* f = fopenUtf8(path, "rb");
    if (!f) return 0;
#ifdef _WIN32
    _fseeki64(f, (long long)offset, SEEK_SET);
#else
    fseeko(f, (off_t)offset, SEEK_SET);
#endif
    size_t r = fread(buf, 1, n, f);
    fclose(f);
    return r;
}

static FILE* g_log = nullptr;
static std::mutex g_logMutex;

void logInit(const std::string& file) {
    g_log = fopenUtf8(file, "w");
}

void log(const char* fmt, ...) {
    std::lock_guard<std::mutex> lk(g_logMutex);
    va_list ap;
    va_start(ap, fmt);
    char buf[4096];
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    size_t len = strlen(buf);
    bool nl = len && buf[len - 1] == '\n';
    if (g_log) { fputs(buf, g_log); if (!nl) fputc('\n', g_log); fflush(g_log); }
    fputs(buf, stderr); if (!nl) fputc('\n', stderr);
}

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool Ini::load(const std::string& path) {
    std::vector<uint8_t> d;
    if (!readFile(path, d)) return false;
    std::string s(d.begin(), d.end());
    size_t pos = 0;
    while (pos < s.size()) {
        size_t e = s.find('\n', pos);
        if (e == std::string::npos) e = s.size();
        std::string line = trim(s.substr(pos, e - pos));
        pos = e + 1;
        if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '[') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        kv[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
    return true;
}

bool Ini::save(const std::string& path, const std::string& header) const {
    std::string s = header;
    for (auto& [k, v] : kv) s += k + " = " + v + "\n";
    return writeFile(path, s.data(), s.size());
}

std::string Ini::get(const std::string& k, const std::string& def) const {
    auto it = kv.find(k);
    return it == kv.end() ? def : it->second;
}

int Ini::getInt(const std::string& k, int def) const {
    auto it = kv.find(k);
    if (it == kv.end() || it->second.empty()) return def;
    return atoi(it->second.c_str());
}

bool Ini::getBool(const std::string& k, bool def) const {
    auto it = kv.find(k);
    if (it == kv.end() || it->second.empty()) return def;
    char c = it->second[0];
    return c == '1' || c == 't' || c == 'T' || c == 's' || c == 'S' || c == 'y' || c == 'Y';
}

}
