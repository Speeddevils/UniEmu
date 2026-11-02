// UniEmu - execução de um jogo
#pragma once
#include <string>
#include "settings.h"

std::string coreLibName(const char* base);       // "snes9x_libretro" -> "snes9x_libretro.dll"
bool ps1BiosPresent(std::string* found = nullptr);
bool dcBiosPresent();
int runGame(const std::string& romPath, Settings& settings);
