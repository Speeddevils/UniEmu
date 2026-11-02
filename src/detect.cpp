#include "detect.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include "miniz.h"

namespace fs = std::filesystem;

static bool memSearch(const uint8_t* d, size_t n, const char* s) {
    size_t l = strlen(s);
    if (n < l) return false;
    for (size_t i = 0; i + l <= n; i++)
        if (d[i] == (uint8_t)s[0] && memcmp(d + i, s, l) == 0) return true;
    return false;
}

static System bySimpleExt(const std::string& e) {
    if (e == ".gdi" || e == ".cdi") return System::Dreamcast;
    if (e == ".pbp" || e == ".ccd") return System::PS1;
    if (e == ".z64" || e == ".n64" || e == ".v64" || e == ".ndd") return System::N64;
    if (e == ".sfc" || e == ".smc" || e == ".swc" || e == ".fig" || e == ".bs") return System::SNES;
    if (e == ".a26") return System::Atari2600;
    if (e == ".md" || e == ".gen" || e == ".smd") return System::MegaDrive;
    if (e == ".wbfs" || e == ".wad") return System::Wii;
    if (e == ".gcm") return System::GameCube;
    if (e == ".cso" || e == ".zso") return System::PS2;
    if (e == ".nsp" || e == ".xci" || e == ".nsz" || e == ".xcz" || e == ".nro") return System::Switch;
    if (e == ".xex") return System::Xbox360;
    return System::Unknown;
}

bool isLibraryExtension(const std::string& e) {
    static const char* exts[] = {".cue", ".gdi", ".cdi", ".chd", ".iso", ".pbp", ".m3u", ".ccd", ".z64", ".n64",
                                 ".v64", ".sfc", ".smc", ".swc", ".fig", ".zip", ".a26", ".md", ".gen", ".smd",
                                 ".bin", ".gcm", ".gcz", ".rvz", ".wia", ".wbfs", ".wad", ".ciso", ".cso", ".zso",
                                 ".nsp", ".xci", ".nsz", ".xcz", ".nro", ".xiso", ".xex"};
    for (auto x : exts) if (e == x) return true;
    return false;
}

static System sniffDataImage(const std::string& path) {
    std::vector<uint8_t> buf(0x10000);
    size_t n = util::readHead(path, buf.data(), buf.size());
    if (memSearch(buf.data(), n, "SEGA SEGAKATANA")) return System::Dreamcast;
    if (memSearch(buf.data(), n, "PLAYSTATION") || memSearch(buf.data(), n, "Sony Computer Entertainment"))
        return System::PS1;
    return System::Unknown;
}

static uint64_t be64(const uint8_t* p) { uint64_t v = 0; for (int i = 0; i < 8; i++) v = (v << 8) | p[i]; return v; }
static uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

static System sniffChd(const std::string& path) {
    uint8_t h[0x80];
    if (util::readHead(path, h, sizeof h) < 0x40 || memcmp(h, "MComprHD", 8) != 0) return System::Unknown;
    uint32_t ver = be32(h + 12);
    uint64_t meta = ver >= 5 ? be64(h + 0x30) : be64(h + 0x24);
    for (int guard = 0; meta && guard < 256; guard++) {
        uint8_t e[16];
        if (util::readHead(path, e, 16, meta) < 16) break;
        if (!memcmp(e, "CHGD", 4)) return System::Dreamcast;
        if (!memcmp(e, "DVD ", 4)) return System::PS2;
        if (!memcmp(e, "CHT2", 4) || !memcmp(e, "CHTR", 4) || !memcmp(e, "CHCD", 4)) {
            // CD comum: quase sempre PS1 nesta biblioteca
        }
        meta = be64(e + 8);
    }
    return System::PS1;
}

static std::string firstCueFile(const std::string& cue) {
    std::vector<uint8_t> d;
    if (!util::readFile(cue, d, 1 << 20)) return "";
    std::string s(d.begin(), d.end());
    size_t p = 0;
    while (p < s.size()) {
        size_t e = s.find('\n', p);
        if (e == std::string::npos) e = s.size();
        std::string line = s.substr(p, e - p);
        p = e + 1;
        size_t f = line.find("FILE");
        if (f == std::string::npos) continue;
        size_t q1 = line.find('"', f), q2 = q1 == std::string::npos ? q1 : line.find('"', q1 + 1);
        std::string name;
        if (q1 != std::string::npos && q2 != std::string::npos) name = line.substr(q1 + 1, q2 - q1 - 1);
        else {
            size_t a = line.find_first_not_of(" \t", f + 4);
            size_t b = line.find_first_of(" \t\r", a);
            if (a != std::string::npos) name = line.substr(a, b - a);
        }
        if (!name.empty()) return util::joinPath(util::dirName(cue), name);
    }
    return "";
}

static std::string firstM3uEntry(const std::string& m3u) {
    std::vector<uint8_t> d;
    if (!util::readFile(m3u, d, 1 << 16)) return "";
    std::string s(d.begin(), d.end());
    size_t p = 0;
    while (p < s.size()) {
        size_t e = s.find('\n', p);
        if (e == std::string::npos) e = s.size();
        std::string line = s.substr(p, e - p);
        p = e + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        if (line.size() > 2 && (line[1] == ':' || line[0] == '/')) return line;
        return util::joinPath(util::dirName(m3u), line);
    }
    return "";
}

static System sniffZip(const std::string& path, std::string* inner) {
    mz_zip_archive z;
    memset(&z, 0, sizeof z);
    std::vector<uint8_t> dummy;
    FILE* f = util::fopenUtf8(path, "rb");
    if (!f) return System::Unknown;
    System found = System::Unknown;
    if (mz_zip_reader_init_cfile(&z, f, 0, 0)) {
        mz_uint n = mz_zip_reader_get_num_files(&z);
        for (mz_uint i = 0; i < n && found == System::Unknown; i++) {
            char name[1024];
            mz_zip_reader_get_filename(&z, i, name, sizeof name);
            System s = bySimpleExt(util::extLower(name));
            if (s == System::N64 || s == System::SNES || s == System::MegaDrive || s == System::Atari2600) {
                found = s;
                if (inner) *inner = name;
            }
        }
        mz_zip_reader_end(&z);
    }
    fclose(f);
    return found;
}

bool extractZipEntry(const std::string& zipPath, const std::string& inner, std::vector<uint8_t>& out) {
    mz_zip_archive z;
    memset(&z, 0, sizeof z);
    FILE* f = util::fopenUtf8(zipPath, "rb");
    if (!f) return false;
    bool ok = false;
    if (mz_zip_reader_init_cfile(&z, f, 0, 0)) {
        int idx = mz_zip_reader_locate_file(&z, inner.c_str(), nullptr, 0);
        mz_zip_archive_file_stat st;
        if (idx >= 0 && mz_zip_reader_file_stat(&z, idx, &st)) {
            out.resize((size_t)st.m_uncomp_size);
            ok = mz_zip_reader_extract_to_mem(&z, idx, out.data(), out.size(), 0);
        }
        mz_zip_reader_end(&z);
    }
    fclose(f);
    return ok;
}

// cabeçalho de disco GameCube / Wii (ISO, GCM, RVZ/WIA, GCZ)
static System sniffNintendoDisc(const std::string& path) {
    uint8_t h[0x80] = {0};
    size_t n = util::readHead(path, h, sizeof h);
    if (n < 0x60) return System::Unknown;
    if (!memcmp(h, "RVZ\x01", 4) || !memcmp(h, "WIA\x01", 4)) {
        uint32_t t = be32(h + 0x48);
        return t == 2 ? System::Wii : t == 1 ? System::GameCube : System::Unknown;
    }
    if (be32(h) == 0xB10BC001) return (h[4] | h[5] << 8 | h[6] << 16 | h[7] << 24) == 1 ? System::Wii : System::GameCube;
    if (!memcmp(h, "CISO", 4)) return System::GameCube;
    if (be32(h + 0x18) == 0x5D1C9EA3) return System::Wii;
    if (be32(h + 0x1C) == 0xC2339F3D) return System::GameCube;
    return System::Unknown;
}

// Lê SYSTEM.CNF de uma ISO 9660 (setores de 2048 bytes): "BOOT2" = PS2, "BOOT" = PS1
static std::string isoSystemCnf(const std::string& path) {
    uint8_t pvd[2048];
    if (util::readHead(path, pvd, 2048, 16 * 2048) < 2048 || memcmp(pvd + 1, "CD001", 5) != 0) return "";
    const uint8_t* root = pvd + 156;
    uint32_t lba = root[2] | root[3] << 8 | root[4] << 16 | (uint32_t)root[5] << 24;
    uint32_t size = root[10] | root[11] << 8 | root[12] << 16 | (uint32_t)root[13] << 24;
    if (size > 64 * 2048) size = 64 * 2048;
    std::vector<uint8_t> dir(size);
    if (util::readHead(path, dir.data(), size, (uint64_t)lba * 2048) < size) return "";
    for (size_t p = 0; p + 33 < size;) {
        uint8_t len = dir[p];
        if (len == 0) { p = (p / 2048 + 1) * 2048; continue; }
        uint8_t nl = dir[p + 32];
        std::string name((const char*)&dir[p + 33], nl);
        if (util::toLower(name).rfind("system.cnf", 0) == 0) {
            uint32_t flba = dir[p + 2] | dir[p + 3] << 8 | dir[p + 4] << 16 | (uint32_t)dir[p + 5] << 24;
            char buf[512] = {0};
            util::readHead(path, (uint8_t*)buf, 511, (uint64_t)flba * 2048);
            return buf;
        }
        p += len;
    }
    return "";
}

// Disco de Xbox / Xbox 360 (XDVDFS): "MICROSOFT*XBOX*MEDIA" no setor 32 da partição do jogo.
// Imagens Redump/originais começam com a partição de vídeo; a de jogo fica num deslocamento fixo.
static System sniffXboxDisc(const std::string& path) {
    static const uint64_t bases[] = {0, 0x18300000ull /*XGD1*/, 0xFD90000ull /*XGD2*/, 0x2080000ull /*XGD3*/};
    for (uint64_t base : bases) {
        uint8_t vd[32] = {0};
        if (util::readHead(path, vd, sizeof vd, base + 0x10000) < sizeof vd) continue;
        if (memcmp(vd, "MICROSOFT*XBOX*MEDIA", 20) != 0) continue;
        uint32_t sec = vd[20] | vd[21] << 8 | vd[22] << 16 | (uint32_t)vd[23] << 24;
        uint32_t size = vd[24] | vd[25] << 8 | vd[26] << 16 | (uint32_t)vd[27] << 24;
        size = std::min<uint32_t>(size, 256 * 1024);
        std::vector<uint8_t> dir(size);
        size_t n = util::readHead(path, dir.data(), size, base + (uint64_t)sec * 2048);
        std::string low(dir.begin(), dir.begin() + n);
        for (auto& c : low) c = (char)tolower((unsigned char)c);
        if (low.find("default.xex") != std::string::npos) return System::Xbox360;
        if (low.find("default.xbe") != std::string::npos) return System::Xbox;
        return base == 0x18300000ull ? System::Xbox : System::Xbox360;   // XGD1 só existe no Xbox clássico
    }
    return System::Unknown;
}

static bool isMegaDriveRom(const std::string& path) {
    uint8_t h[0x110] = {0};
    if (util::readHead(path, h, sizeof h) < 0x110) return false;
    return !memcmp(h + 0x100, "SEGA", 4) || !memcmp(h + 0x101, "SEGA", 4);
}

System detectSystem(const std::string& path, std::string* zipInner) {
    std::string e = util::extLower(path);
    System s = bySimpleExt(e);
    if (s != System::Unknown) return s;
    if (e == ".zip") return sniffZip(path, zipInner);
    if (e == ".elf") {  // homebrew de Dreamcast (ELF SuperH)
        uint8_t h[20] = {0};
        return (util::readHead(path, h, 20) == 20 && !memcmp(h, "\x7f" "ELF", 4) && h[18] == 42) ? System::Dreamcast
                                                                                              : System::Unknown;
    }
    if (e == ".exe" || e == ".psx" || e == ".psexe") {  // executável PS-X (homebrew)
        uint8_t m[8] = {0};
        return (util::readHead(path, m, 8) == 8 && !memcmp(m, "PS-X EXE", 8)) ? System::PS1 : System::Unknown;
    }
    if (e == ".chd") return sniffChd(path);
    if (e == ".m3u") {
        std::string first = firstM3uEntry(path);
        if (first.empty() || util::extLower(first) == ".m3u") return System::PS1;
        System r = detectSystem(first);
        return r == System::Unknown ? System::PS1 : r;
    }
    if (e == ".cue") {
        std::string bin = firstCueFile(path);
        System r = bin.empty() ? System::Unknown : sniffDataImage(bin);
        return r == System::Unknown ? System::PS1 : r;
    }
    if (e == ".gcz" || e == ".rvz" || e == ".wia" || e == ".ciso") {
        System r = sniffNintendoDisc(path);
        return r == System::Unknown ? System::GameCube : r;
    }
    if (e == ".bin") {   // .bin solto: só se for cartucho de Mega Drive (trilhas de CD vêm pelo .cue)
        uint64_t sz = 0;
        {
            FILE* f = util::fopenUtf8(path, "rb");
            if (f) { fseek(f, 0, SEEK_END); sz = (uint64_t)ftell(f); fclose(f); }
        }
        return (sz <= 8 * 1024 * 1024 && isMegaDriveRom(path)) ? System::MegaDrive : System::Unknown;
    }
    if (e == ".xiso") { System x = sniffXboxDisc(path); return x == System::Unknown ? System::Xbox : x; }
    if (e == ".iso" || e == ".img") {
        System n = sniffNintendoDisc(path);
        if (n != System::Unknown) return n;
        System x = sniffXboxDisc(path);
        if (x != System::Unknown) return x;
        System r = sniffDataImage(path);
        if (r == System::Dreamcast) return r;
        std::string cnf = isoSystemCnf(path);
        if (cnf.find("BOOT2") != std::string::npos) return System::PS2;
        return System::PS1;
    }
    return System::Unknown;
}

std::string sfoField(const std::string& sfoPath, const char* key) {
    std::vector<uint8_t> d;
    if (!util::readFile(sfoPath, d, 1 << 20) || d.size() < 20 || memcmp(d.data(), "\0PSF", 4) != 0) return "";
    auto le32 = [&](size_t o) { return o + 4 <= d.size() ? (uint32_t)(d[o] | d[o + 1] << 8 | d[o + 2] << 16 | (uint32_t)d[o + 3] << 24) : 0u; };
    auto le16 = [&](size_t o) { return o + 2 <= d.size() ? (uint32_t)(d[o] | d[o + 1] << 8) : 0u; };
    uint32_t keyTab = le32(8), dataTab = le32(12), n = le32(16);
    for (uint32_t i = 0; i < n && i < 256; i++) {
        size_t e = 20 + i * 16;
        size_t ko = keyTab + le16(e), dlen = le32(e + 4), dof = dataTab + le32(e + 12);
        if (ko >= d.size() || dof + dlen > d.size()) break;
        if (strcmp((const char*)&d[ko], key) == 0) {
            std::string v((const char*)&d[dof], dlen);
            while (!v.empty() && v.back() == '\0') v.pop_back();
            return v;
        }
    }
    return "";
}

// Jogos de PS3 (pasta PS3_GAME ou jogo instalado no HD) e PS4 (pasta com sce_sys/param.sfo + eboot.bin)
static bool consoleFolderGame(const fs::path& sfo, GameEntry& g) {
    std::error_code ec;
    fs::path parent = sfo.parent_path();
    auto u8s = [](const fs::path& p) { auto u = p.u8string(); return std::string(u.begin(), u.end()); };
    std::string pname = util::toLower(u8s(parent.filename()));
    fs::path root;
    if (pname == "ps3_game") {
        root = parent.parent_path();
        g.sys = System::PS3;
        g.path = u8s(parent / "USRDIR" / "EBOOT.BIN");
        g.icon = u8s(parent / "ICON0.PNG");
    } else if (pname == "sce_sys") {
        root = parent.parent_path();
        if (!fs::exists(root / "eboot.bin", ec)) return false;
        g.sys = System::PS4;
        g.path = u8s(root / "eboot.bin");
        g.icon = u8s(parent / "icon0.png");
    } else if (fs::exists(parent / "USRDIR" / "EBOOT.BIN", ec) && sfoField(u8s(sfo), "CATEGORY") == "HG") {
        root = parent;
        g.sys = System::PS3;
        g.path = u8s(parent / "USRDIR" / "EBOOT.BIN");
        g.icon = u8s(parent / "ICON0.PNG");
    } else {
        return false;
    }
    std::string title = sfoField(u8s(sfo), "TITLE");
    for (auto& c : title) if (c == '\n' || c == '\r') c = ' ';
    g.key = u8s(root.filename());
    g.name = title.empty() ? g.key : title;
    if (!fs::exists(fs::u8path(g.icon), ec)) g.icon.clear();
    return true;
}

std::vector<GameEntry> scanLibrary(const std::vector<std::string>& dirs, std::atomic<bool>* cancel,
                                   std::atomic<int>* progress) {
    std::vector<GameEntry> out;
    for (auto& d : dirs) {
        std::error_code ec;
#if defined(__cpp_char8_t)
        fs::path root(reinterpret_cast<const char8_t*>(d.c_str()));
#else
        fs::path root = fs::u8path(d);
#endif
        if (!fs::is_directory(root, ec)) continue;
        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
        for (; !ec && it != end; it.increment(ec)) {
            if (cancel && *cancel) return out;
            if (it.depth() > 6) { it.disable_recursion_pending(); continue; }
            if (!it->is_regular_file(ec)) continue;
            auto u = it->path().u8string();
            std::string p(u.begin(), u.end());
            std::string e = util::extLower(p);
            if (e == ".sfo" && util::toLower(util::fileName(p)) == "param.sfo") {
                GameEntry g;
                if (consoleFolderGame(it->path(), g)) { out.push_back(std::move(g)); if (progress) (*progress)++; }
                continue;
            }
            if (!isLibraryExtension(e)) continue;
            if (e == ".xex") {   // Xbox 360 extraído: só o default.xex conta, com o nome da pasta
                if (util::toLower(util::fileName(p)) != "default.xex") continue;
                GameEntry g;
                g.path = p;
                g.name = g.key = util::fileName(util::dirName(p));
                g.sys = System::Xbox360;
                out.push_back(std::move(g));
                if (progress) (*progress)++;
                continue;
            }
            GameEntry g;
            g.path = p;
            g.name = util::stem(p);
            g.key = g.name;
            g.sys = detectSystem(p, &g.zipInner);
            if (g.sys == System::Unknown) continue;
            out.push_back(std::move(g));
            if (progress) (*progress)++;
        }
    }
    // Esconde .cue que já estão listados dentro de um .m3u na mesma pasta
    std::vector<std::string> inM3u;
    for (auto& g : out)
        if (util::extLower(g.path) == ".m3u") {
            std::vector<uint8_t> d;
            if (util::readFile(g.path, d, 1 << 16)) {
                std::string s(d.begin(), d.end());
                for (auto& o : out)
                    if (&o != &g && util::dirName(o.path) == util::dirName(g.path) &&
                        s.find(util::fileName(o.path)) != std::string::npos)
                        inM3u.push_back(o.path);
            }
        }
    out.erase(std::remove_if(out.begin(), out.end(),
                             [&](const GameEntry& g) {
                                 return std::find(inM3u.begin(), inM3u.end(), g.path) != inM3u.end();
                             }),
              out.end());
    std::sort(out.begin(), out.end(), [](const GameEntry& a, const GameEntry& b) {
        return util::toLower(a.name) < util::toLower(b.name);
    });
    return out;
}
