#include "covers.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <regex>
#include <SDL.h>
#include <sstream>
#include "http.h"
#include "util.h"

namespace {

struct Repo { const char* system; const char* github; };
Repo repoFor(System s) {
    switch (s) {
    case System::Dreamcast: return {"Sega - Dreamcast", "Sega_-_Dreamcast"};
    case System::PS1: return {"Sony - PlayStation", "Sony_-_PlayStation"};
    case System::N64: return {"Nintendo - Nintendo 64", "Nintendo_-_Nintendo_64"};
    case System::SNES: return {"Nintendo - Super Nintendo Entertainment System", "Nintendo_-_Super_Nintendo_Entertainment_System"};
    case System::Atari2600: return {"Atari - 2600", "Atari_-_2600"};
    case System::MegaDrive: return {"Sega - Mega Drive - Genesis", "Sega_-_Mega_Drive_-_Genesis"};
    case System::GameCube: return {"Nintendo - GameCube", "Nintendo_-_GameCube"};
    case System::Wii: return {"Nintendo - Wii", "Nintendo_-_Wii"};
    case System::PS2: return {"Sony - PlayStation 2", "Sony_-_PlayStation_2"};
    case System::PS3: return {"Sony - PlayStation 3", "Sony_-_PlayStation_3"};
    case System::PS4: return {"Sony - PlayStation 4", "Sony_-_PlayStation_4"};
    case System::Xbox: return {"Microsoft - Xbox", "Microsoft_-_Xbox"};
    case System::Xbox360: return {"Microsoft - Xbox 360", "Microsoft_-_Xbox_360"};
    default: return {"", ""};
    }
}

std::string lower(std::string s) { return util::toLower(s); }

// título "limpo": tira (tags) [tags], versão, artigos, pontuação; II->2 etc.
std::vector<std::string> titleTokens(const std::string& raw) {
    static const std::regex ver("\\b[vV]\\d+(\\.\\d+)*\\b");
    std::string s = std::regex_replace(raw, ver, " ");
    size_t cut = s.find_first_of("([");
    if (cut != std::string::npos) s = s.substr(0, cut);
    s = lower(s);
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = s[i];
        if (c == '&') o += " and ";
        else if (c == '\'' || c == '`') {}
        else if (isalnum(c) || c >= 128) o += (char)c;
        else o += ' ';
    }
    std::istringstream is(o);
    std::vector<std::string> t;
    std::string w;
    static const std::map<std::string, std::string> roman = {{"ii", "2"}, {"iii", "3"}, {"iv", "4"}, {"v", "5"},
                                                             {"vi", "6"}, {"vii", "7"}, {"viii", "8"}, {"ix", "9"}};
    while (is >> w) {
        if (w == "the" || w == "a" || w == "of") continue;
        auto r = roman.find(w);
        t.push_back(r != roman.end() ? r->second : w);
    }
    return t;
}

std::string joinTok(const std::vector<std::string>& t) {
    std::string s;
    for (auto& x : t) { if (!s.empty()) s += ' '; s += x; }
    return s;
}

enum Region { R_ANY, R_USA, R_EUR, R_JPN, R_BRA };
Region regionOf(const std::string& n) {
    std::string s = lower(n);
    auto has = [&](const char* x) { return s.find(x) != std::string::npos; };
    if (has("(usa") || has("(us)") || has("(u)") || has("ntsc-u") || has("(us,") || has(", usa")) return R_USA;
    if (has("brazil") || has("(br)") || has("(b)")) return R_BRA;
    if (has("(europe") || has("(eu)") || has("(e)") || has("pal") || has("(uk)")) return R_EUR;
    if (has("(japan") || has("(jp)") || has("(j)") || has("ntsc-j")) return R_JPN;
    return R_ANY;
}

int discOf(const std::string& n) {
    std::string s = lower(n);
    for (const char* key : {"disc ", "disk ", "cd"}) {
        size_t p = s.find(key);
        if (p != std::string::npos) {
            int d = atoi(s.c_str() + p + strlen(key));
            if (d > 0) return d;
        }
    }
    return 0;
}

double dice(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    if (a.empty() || b.empty()) return 0;
    std::multiset<std::string> mb(b.begin(), b.end());
    int common = 0;
    for (auto& x : a) {
        auto it = mb.find(x);
        if (it != mb.end()) { common++; mb.erase(it); }
    }
    return 2.0 * common / (a.size() + b.size());
}

std::string sanitize(const std::string& name) {
    std::string s = name;
    for (auto& c : s) if (strchr("&*/:`<>?\\|\"", c)) c = '_';
    return s;
}

}  // namespace

static std::string gkey(const GameEntry& g) { return g.key.empty() ? util::stem(g.path) : g.key; }
static std::string gquery(const GameEntry& g) {
    return (g.sys == System::PS3 || g.sys == System::PS4) ? g.name : gkey(g);
}

std::string coversDir(System s) { return util::joinPath(util::joinPath(util::baseDir(), "covers"), systemShort(s)); }

std::string coverSavePath(const GameEntry& g) { return util::joinPath(coversDir(g.sys), sanitize(gkey(g)) + ".png"); }

std::string coverPathFor(const GameEntry& g) {
    std::string base = util::joinPath(coversDir(g.sys), sanitize(gkey(g)));
    for (const char* e : {".png", ".jpg", ".jpeg"})
        if (util::fileExists(base + e)) return base + e;
    if (!g.icon.empty() && util::fileExists(g.icon)) return g.icon;   // ícone do próprio jogo (PS3/PS4)
    return "";
}

static std::string notFoundFile(System s) { return util::joinPath(coversDir(s), "_sem_capa.txt"); }

bool coverMarkedNotFound(const GameEntry& g) {
    static std::map<int, std::pair<Uint32, std::string>> cache;
    auto& c = cache[(int)g.sys];
    if (SDL_GetTicks() - c.first > 3000 || c.first == 0) {
        std::vector<uint8_t> d;
        c.second = util::readFile(notFoundFile(g.sys), d) ? std::string(d.begin(), d.end()) : "";
        c.first = SDL_GetTicks() | 1;
    }
    return c.second.find("\n" + gkey(g) + "\n") != std::string::npos;
}

std::string coverBestMatch(const std::string& romName, const std::vector<std::string>& index) {
    {   // nome idêntico ao do acervo (padrão No-Intro/Redump): vence direto
        std::string want = util::toLower(romName) + ".png";
        for (auto& entry : index)
            if (util::toLower(entry) == want) return entry;
    }
    auto rt = titleTokens(romName);
    if (rt.empty()) return "";
    std::string rjoin = joinTok(rt);
    Region rr = regionOf(romName);
    int rd = discOf(romName);
    double best = 0;
    std::string bestName;
    for (auto& entry : index) {
        auto et = titleTokens(entry);
        if (et.empty()) continue;
        double sim;
        std::string ej = joinTok(et);
        auto squash = [](std::string x) { x.erase(std::remove(x.begin(), x.end(), ' '), x.end()); return x; };
        auto mainPart = [](const std::string& n) {   // título antes do subtítulo " - "
            size_t c = n.find_first_of("([");
            std::string t = n.substr(0, c);
            size_t d = t.find(" - ");
            return joinTok(titleTokens(d == std::string::npos ? t : t.substr(0, d)));
        };
        if (ej == rjoin || squash(ej) == squash(rjoin)) sim = 1.0;
        else if (mainPart(entry) == rjoin || mainPart(romName) == ej) sim = 0.95;
        else {
            sim = dice(rt, et);
            // subtítulo faltando de um dos lados ("Resident Evil 2" x "Resident Evil 2 - Dual Shock Ver.")
            if (ej.rfind(rjoin + " ", 0) == 0 || rjoin.rfind(ej + " ", 0) == 0) {
                double extra = std::abs((double)et.size() - (double)rt.size());
                sim = std::max(sim, 0.88 - 0.015 * extra);
            }
        }
        if (sim < 0.80) continue;
        double score = sim * 10;
        Region er = regionOf(entry);
        if (rr != R_ANY && er == rr) score += 2;
        else if (er == R_USA) score += 1;
        else if (er == R_BRA && rr == R_ANY) score += 0.8;
        int ed = discOf(entry);
        if (ed == (rd ? rd : 1)) score += 1.5;
        else if (ed == 0) score += 1.0;
        if (lower(entry).find("(beta") != std::string::npos || lower(entry).find("(proto") != std::string::npos ||
            lower(entry).find("(demo") != std::string::npos || lower(entry).find(" - copy") != std::string::npos)
            score -= 3;
        score -= std::count(entry.begin(), entry.end(), '(') * 0.05;   // prefere nomes mais "limpos"
        if (score > best) { best = score; bestName = entry; }
    }
    return bestName;
}

// ---------------- downloader ----------------

CoverDownloader::~CoverDownloader() {
    stop_ = true;
    if (th_.joinable()) th_.join();
}

std::string CoverDownloader::current() {
    std::lock_guard<std::mutex> lk(m_);
    return current_;
}

void CoverDownloader::enqueue(const std::vector<GameEntry>& games, bool force) {
    std::lock_guard<std::mutex> lk(m_);
    if (!busy_) { done_ = 0; total_ = 0; found_ = 0; }
    for (auto& g : games) {
        if (!coverPathFor(g).empty()) continue;
        if (!force && coverMarkedNotFound(g)) continue;
        bool dup = false;
        for (auto& x : q_) if (x.path == g.path) dup = true;
        if (dup) continue;
        q_.push_back(g);
        total_++;
    }
    if (!busy_ && !q_.empty()) {
        if (th_.joinable()) th_.join();
        busy_ = true;
        th_ = std::thread(&CoverDownloader::worker, this);
    }
}

static bool loadIndex(System s, std::vector<std::string>& out) {
    Repo r = repoFor(s);
    out.clear();
    std::string cached = util::joinPath(util::joinPath(util::joinPath(util::baseDir(), "covers"), "_index"),
                                        std::string(r.github) + ".txt");
    std::vector<uint8_t> d;
    if (util::readFile(cached, d)) {
        std::string s2(d.begin(), d.end());
        std::istringstream is(s2);
        std::string line;
        while (std::getline(is, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) out.push_back(line);
        }
    }
    return !out.empty();
}

// Baixa a lista atualizada do servidor (quando o índice que vem com o programa não tem o jogo)
static bool refreshIndex(System s, std::vector<std::string>& out) {
    Repo r = repoFor(s);
    std::vector<uint8_t> d;
    std::string url = "https://thumbnails.libretro.com/" + urlEncodePath(r.system) + "/Named_Boxarts/";
    if (!httpGet(url, d)) return false;
    std::string html(d.begin(), d.end());
    std::vector<std::string> names;
    size_t p = 0;
    while ((p = html.find("href=\"", p)) != std::string::npos) {
        p += 6;
        size_t e = html.find('"', p);
        std::string h = html.substr(p, e - p);
        p = e;
        if (h.size() < 5 || h.compare(h.size() - 4, 4, ".png") != 0) continue;
        std::string dec;  // decodifica %XX
        for (size_t i = 0; i < h.size(); i++) {
            if (h[i] == '%' && i + 2 < h.size()) { dec += (char)strtol(h.substr(i + 1, 2).c_str(), nullptr, 16); i += 2; }
            else dec += h[i];
        }
        names.push_back(dec);
    }
    if (names.size() < 50) return false;
    out = names;
    std::string all;
    for (auto& n : names) all += n + "\n";
    util::writeFile(util::joinPath(util::joinPath(util::joinPath(util::baseDir(), "covers"), "_index"),
                                   std::string(r.github) + ".txt"), all.data(), all.size());
    util::log("Capas: indice de %s atualizado (%zu capas)", r.system, names.size());
    return true;
}

static bool isImage(const std::vector<uint8_t>& d) {
    return d.size() > 8 && ((d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G') || (d[0] == 0xFF && d[1] == 0xD8));
}

static bool downloadCover(System s, const std::string& file, std::vector<uint8_t>& img) {
    Repo r = repoFor(s);
    std::string a = "https://thumbnails.libretro.com/" + urlEncodePath(r.system) + "/Named_Boxarts/" + urlEncodePath(file);
    if (httpGet(a, img) && isImage(img)) return true;
    std::string name = file;
    for (int hop = 0; hop < 3; hop++) {   // no GitHub, cópias alternativas são links simbólicos (o conteúdo é o caminho)
        std::string b = "https://raw.githubusercontent.com/libretro-thumbnails/" + std::string(r.github) +
                        "/master/Named_Boxarts/" + urlEncodePath(name);
        if (!httpGet(b, img)) return false;
        if (isImage(img)) return true;
        if (img.size() > 512) return false;
        std::string target(img.begin(), img.end());
        size_t sl = target.find_last_of('/');
        name = sl == std::string::npos ? target : target.substr(sl + 1);
    }
    return false;
}

void CoverDownloader::worker() {
    std::map<int, std::vector<std::string>> indexes;
    std::map<int, bool> refreshed;
    while (!stop_) {
        GameEntry g;
        {
            std::lock_guard<std::mutex> lk(m_);
            if (q_.empty()) { current_.clear(); busy_ = false; return; }
            g = q_.front();
            q_.pop_front();
            current_ = g.name;
        }
        int key = (int)g.sys;
        if (!indexes.count(key)) loadIndex(g.sys, indexes[key]);
        std::string match = repoFor(g.sys).github[0] ? coverBestMatch(gquery(g), indexes[key]) : "";
        if (match.empty() && !refreshed[key] && repoFor(g.sys).github[0]) {
            refreshed[key] = true;
            std::vector<std::string> fresh;
            if (refreshIndex(g.sys, fresh)) { indexes[key] = fresh; match = coverBestMatch(gquery(g), fresh); }
        }
        bool ok = false;
        if (!match.empty()) {
            std::vector<uint8_t> img;
            if (downloadCover(g.sys, match, img)) {
                std::string out = coverSavePath(g);
                if (img[0] == 0xFF) out = out.substr(0, out.size() - 4) + ".jpg";
                ok = util::writeFile(out, img.data(), img.size());
            }
        }
        util::log("Capas: %s -> %s %s", g.name.c_str(), match.empty() ? "(sem correspondencia)" : match.c_str(),
                  ok ? "OK" : "FALHOU");
        if (ok) { found_++; gen_++; }
        else if (match.empty()) {   // lembra para não tentar de novo a cada abertura
            std::vector<uint8_t> d;
            std::string s = util::readFile(notFoundFile(g.sys), d) ? std::string(d.begin(), d.end()) : "\n";
            if (s.empty() || s[0] != '\n') s = "\n" + s;
            s += gkey(g) + "\n";
            util::writeFile(notFoundFile(g.sys), s.data(), s.size());
        }
        done_++;
    }
    std::lock_guard<std::mutex> lk(m_);
    current_.clear();
    busy_ = false;   // interrompido (stop_)
}
