// UniEmu - escolha da placa de vídeo (integrada x dedicada)
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct GpuInfo {
    std::string name;
    uint64_t vramMB = 0;
    uint32_t vendor = 0;
    bool integrated = false;
};

std::vector<GpuInfo> gpuList();                       // placas físicas detectadas (Windows/DXGI)
const GpuInfo* gpuForPref(int pref);                  // 1 = integrada, 2 = dedicada; nullptr se não houver
// Grava a preferência do Windows (Configurações > Sistema > Tela > Gráficos) para um executável.
// pref 0 = deixar o Windows decidir, 1 = economia de energia (integrada), 2 = alto desempenho (dedicada)
bool gpuApplyPreference(const std::string& exePath, int pref);
