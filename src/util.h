// UniEmu - utilitários (caminhos, arquivos UTF-8, log, INI)
#pragma once
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
#include <map>

namespace util {

// Diretório do executável (termina com separador)
const std::string& baseDir();
std::string joinPath(const std::string& a, const std::string& b);
std::string fileName(const std::string& path);          // "C:/x/jogo.cue" -> "jogo.cue"
std::string stem(const std::string& path);              // -> "jogo"
std::string extLower(const std::string& path);          // -> ".cue"
std::string dirName(const std::string& path);
std::string toLower(std::string s);
bool fileExists(const std::string& path);
bool dirExists(const std::string& path);
void makeDirs(const std::string& path);
bool removeFile(const std::string& path);

FILE* fopenUtf8(const std::string& path, const char* mode);
bool readFile(const std::string& path, std::vector<uint8_t>& out, size_t maxBytes = SIZE_MAX);
bool writeFile(const std::string& path, const void* data, size_t size);
size_t readHead(const std::string& path, uint8_t* buf, size_t n, uint64_t offset = 0);

void logInit(const std::string& file);
void log(const char* fmt, ...);

// INI simples: chave=valor, linhas começando com ; ou # são comentários
struct Ini {
    std::map<std::string, std::string> kv;
    bool load(const std::string& path);
    bool save(const std::string& path, const std::string& header = "") const;
    std::string get(const std::string& k, const std::string& def = "") const;
    int getInt(const std::string& k, int def) const;
    bool getBool(const std::string& k, bool def) const;
    void set(const std::string& k, const std::string& v) { kv[k] = v; }
    void setInt(const std::string& k, int v) { kv[k] = std::to_string(v); }
    void setBool(const std::string& k, bool v) { kv[k] = v ? "1" : "0"; }
};

}
