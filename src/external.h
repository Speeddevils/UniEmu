// UniEmu - integração com emuladores externos (consoles modernos)
//   GameCube / Wii -> Dolphin | PS2 -> PCSX2 | PS3 -> RPCS3 | PS4 -> shadPS4 | Switch -> emulador do usuário
//   Xbox -> xemu | Xbox 360 -> Xenia Canary
#pragma once
#include <string>
#include <vector>
#include "detect.h"
#include "settings.h"

enum ExtEmuId { EXT_DOLPHIN = 0, EXT_PCSX2, EXT_RPCS3, EXT_SHADPS4, EXT_SWITCH, EXT_XEMU, EXT_XENIA, EXT_COUNT };
static_assert(EXT_COUNT <= 8, "Settings::emuPath tem 8 posições");

struct ExtEmuInfo {
    const char* name;          // "Dolphin"
    const char* consoles;      // "GameCube e Wii"
    const char* site;          // página oficial ("" = não indicamos)
    const char* exeNames[8];   // nomes de executável aceitos
    const char* notes;         // o que o usuário precisa providenciar
};

const ExtEmuInfo& extEmuInfo(int id);
int extEmuFor(System s);                                   // -1 se o console não usa emulador externo
std::string extEmuPath(const Settings& st, int id);        // caminho configurado (ou detectado)
std::string extEmuDetect(int id);                          // procura em locais comuns
std::string extUpscaleHint(System s, int n);               // "3x = 1920x1584"
bool extSupportsUpscale(System s);

// Monta a linha de comando, aplica as configurações (upscale, tela, placa de vídeo) e devolve o
// comando (exe + argumentos). err recebe a mensagem se não der.
bool extPrepareLaunch(const GameEntry& g, Settings& st, std::string& exe, std::vector<std::string>& args,
                      std::string& err);
// Pasta de jogos instalados do RPCS3 (dev_hdd0/game), para incluir na biblioteca
std::string extRpcs3GamesDir(const Settings& st);

// ---------------- arquivos de sistema exigidos pelos emuladores externos ----------------
// (só verifica o que está instalado: BIOS/firmware não são distribuídos pelo UniEmu)
bool extPcsx2BiosPresent(const Settings& st, std::string* where = nullptr);   // pasta bios do PCSX2
bool extRpcs3FirmwareInstalled(const Settings& st);                          // dev_flash instalado
struct XemuFiles {
    std::string config;                        // xemu.toml usado
    std::string bootrom, flashrom, hdd;        // caminhos configurados
    bool bootromOk = false, flashromOk = false, hddOk = false;
};
XemuFiles extXemuFiles(const Settings& st);
// Baixa o HD virtual pré-formatado oficial do projeto xemu (xemu-dashboard, livre) e o configura
bool extXemuInstallHdd(const Settings& st, std::string& msg);
extern const char* const kXemuHddUrl;
