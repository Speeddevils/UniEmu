// UniEmu - detecção de console e varredura da biblioteca
#pragma once
#include <atomic>
#include <string>
#include <vector>
#include "settings.h"

struct GameEntry {
    std::string path;      // arquivo no disco
    std::string name;      // nome exibido
    System sys = System::Unknown;
    std::string zipInner;  // arquivo dentro do .zip (se houver)
    std::string key;       // nome usado para a capa (padrão: nome do arquivo sem extensão)
    std::string icon;      // ícone do próprio jogo (PS3/PS4), usado se não houver capa
};

bool isLibraryExtension(const std::string& extLower);
System detectSystem(const std::string& path, std::string* zipInner = nullptr);
std::vector<GameEntry> scanLibrary(const std::vector<std::string>& dirs, std::atomic<bool>* cancel,
                                   std::atomic<int>* progress);
// Extrai um arquivo de dentro de um .zip para a memória
// Lê um campo de texto de um PARAM.SFO (PS3/PS4), ex.: "TITLE", "CATEGORY", "TITLE_ID"
std::string sfoField(const std::string& sfoPath, const char* key);
bool extractZipEntry(const std::string& zipPath, const std::string& inner, std::vector<uint8_t>& out);
