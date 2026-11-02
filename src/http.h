// UniEmu - download HTTP(S) simples (WinHTTP no Windows, curl no Linux)
#pragma once
#include <cstdint>
#include <string>
#include <vector>

bool httpGet(const std::string& url, std::vector<uint8_t>& out, int* status = nullptr);
std::string urlEncodePath(const std::string& s);   // codifica tudo exceto letras, números, -_.~/
