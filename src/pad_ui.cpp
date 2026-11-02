// UniEmu - aba de controles: lista, teste ao vivo e assistente de configuração de botões
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
#include "controllers.h"
#include "imgui.h"
#include "util.h"

namespace {

struct Step { const char* key; const char* label; bool axis; };
const Step kSteps[] = {
    {"a", "Botão de BAIXO  (Xbox: A  |  PlayStation: Xis)", false},
    {"b", "Botão da DIREITA  (Xbox: B  |  PlayStation: Círculo)", false},
    {"x", "Botão da ESQUERDA  (Xbox: X  |  PlayStation: Quadrado)", false},
    {"y", "Botão de CIMA  (Xbox: Y  |  PlayStation: Triângulo)", false},
    {"leftshoulder", "L1 / LB", false},
    {"rightshoulder", "R1 / RB", false},
    {"lefttrigger", "L2 / LT (gatilho esquerdo)", false},
    {"righttrigger", "R2 / RT (gatilho direito)", false},
    {"back", "SELECT / Back", false},
    {"start", "START", false},
    {"dpup", "Direcional para CIMA", false},
    {"dpdown", "Direcional para BAIXO", false},
    {"dpleft", "Direcional para a ESQUERDA", false},
    {"dpright", "Direcional para a DIREITA", false},
    {"leftstick", "Clique do analógico ESQUERDO (L3)", false},
    {"rightstick", "Clique do analógico DIREITO (R3)", false},
    {"leftx", "Mova o analógico ESQUERDO para a DIREITA", true},
    {"lefty", "Mova o analógico ESQUERDO para BAIXO", true},
    {"rightx", "Mova o analógico DIREITO para a DIREITA", true},
    {"righty", "Mova o analógico DIREITO para BAIXO", true},
};
const int kNumSteps = sizeof(kSteps) / sizeof(kSteps[0]);

struct Wizard {
    bool active = false;
    SDL_Joystick* js = nullptr;
    std::string guid, name;
    int step = 0;
    std::vector<std::string> binds;
    std::vector<int> baseline;
    bool waitRelease = true;
    Uint32 nextAccept = 0;
} wz;

std::string g_msg;
std::map<SDL_JoystickID, SDL_GameController*> g_open;

void startWizard(int index) {
    wz = Wizard();
    wz.js = SDL_JoystickOpen(index);
    if (!wz.js) { g_msg = "Não foi possível abrir o controle."; return; }
    wz.guid = guidString(wz.js);
    const char* n = SDL_JoystickName(wz.js);
    wz.name = n ? n : "Controle";
    for (auto& c : wz.name) if (c == ',') c = ' ';
    for (int a = 0; a < SDL_JoystickNumAxes(wz.js); a++) wz.baseline.push_back(SDL_JoystickGetAxis(wz.js, a));
    wz.active = true;
    wz.nextAccept = SDL_GetTicks() + 400;
}

void finishWizard() {
    std::string m = wz.guid + "," + wz.name + ",";
    for (int i = 0; i < (int)wz.binds.size(); i++)
        if (!wz.binds[i].empty()) m += std::string(kSteps[i].key) + ":" + wz.binds[i] + ",";
    bool ok = controllersSaveUserMapping(m);
    util::log("Controles: mapeamento do usuario salvo: %s", m.c_str());
    g_msg = ok ? "Controle configurado e salvo! Já vale para os próximos jogos." : "Falha ao salvar controles.cfg";
    SDL_JoystickClose(wz.js);
    wz = Wizard();
}

bool neutral() {
    SDL_Joystick* j = wz.js;
    for (int b = 0; b < SDL_JoystickNumButtons(j); b++) if (SDL_JoystickGetButton(j, b)) return false;
    for (int h = 0; h < SDL_JoystickNumHats(j); h++) if (SDL_JoystickGetHat(j, h) != SDL_HAT_CENTERED) return false;
    for (int a = 0; a < SDL_JoystickNumAxes(j) && a < (int)wz.baseline.size(); a++)
        if (std::abs(SDL_JoystickGetAxis(j, a) - wz.baseline[a]) > 12000) return false;
    return true;
}

std::string capture(bool axisTarget) {
    SDL_Joystick* j = wz.js;
    if (!axisTarget) {
        for (int b = 0; b < SDL_JoystickNumButtons(j); b++)
            if (SDL_JoystickGetButton(j, b)) return "b" + std::to_string(b);
        for (int h = 0; h < SDL_JoystickNumHats(j); h++) {
            Uint8 v = SDL_JoystickGetHat(j, h);
            if (v == SDL_HAT_UP || v == SDL_HAT_DOWN || v == SDL_HAT_LEFT || v == SDL_HAT_RIGHT)
                return "h" + std::to_string(h) + "." + std::to_string(v);
        }
    }
    for (int a = 0; a < SDL_JoystickNumAxes(j) && a < (int)wz.baseline.size(); a++) {
        int v = SDL_JoystickGetAxis(j, a), base = wz.baseline[a];
        if (std::abs(v - base) < 20000) continue;
        if (axisTarget) return "a" + std::to_string(a) + (v < base ? "~" : "");
        if (base < -20000) return "a" + std::to_string(a);          // gatilho que repousa em -32768
        return (v > base ? "+a" : "-a") + std::to_string(a);         // direcional/gatilho em meio eixo
    }
    return "";
}

void drawWizard() {
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
    ImGui::OpenPopup("Configurar controle");
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Configurar controle", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s", wz.name.c_str());
        ImGui::TextDisabled("Passo %d de %d", wz.step + 1, kNumSteps);
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::SetWindowFontScale(1.3f);
        ImGui::TextColored(ImVec4(1, 0.85f, 0.3f, 1), "%s", kSteps[wz.step].label);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::Spacing();
        bool released = !wz.waitRelease || neutral();
        if (wz.waitRelease && released) wz.waitRelease = false;
        if (wz.waitRelease) ImGui::TextDisabled("Solte todos os botões...");
        else ImGui::TextDisabled("Aperte agora (ou clique em Pular se o controle não tiver)");
        if (!wz.waitRelease && SDL_GetTicks() > wz.nextAccept) {
            std::string b = capture(kSteps[wz.step].axis);
            if (!b.empty()) {
                wz.binds.push_back(b);
                wz.step++;
                wz.waitRelease = true;
                wz.nextAccept = SDL_GetTicks() + 250;
            }
        }
        ImGui::Spacing();
        if (ImGui::Button("Pular")) { wz.binds.push_back(""); wz.step++; wz.waitRelease = true; }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            SDL_JoystickClose(wz.js);
            wz = Wizard();
            g_msg = "Configuração cancelada.";
        }
        if (wz.active && wz.step >= kNumSteps) finishWizard();
        if (!wz.active) {
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

const char* kBtnNames[SDL_CONTROLLER_BUTTON_MAX] = {"A", "B", "X", "Y", "Back", "Guide", "Start", "L3", "R3", "LB", "RB",
                                                    "Cima", "Baixo", "Esquerda", "Direita"};

}  // namespace

void padUiDraw() {
    ImGui::SeparatorText("Controles conectados");
    int n = SDL_NumJoysticks();
    if (n == 0) ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "Nenhum controle detectado. Conecte o controle na USB (pode ser com o UniEmu aberto).");
    for (int i = 0; i < n; i++) {
        ImGui::PushID(i);
        const char* nm = SDL_JoystickNameForIndex(i);
        bool gc = SDL_IsGameController(i);
        ImGui::Text("Jogador %d:", i + 1);
        ImGui::SameLine();
        ImGui::TextColored(gc ? ImVec4(0.4f, 0.9f, 0.5f, 1) : ImVec4(1, 0.7f, 0.3f, 1), "%s", nm ? nm : "Controle sem nome");
        ImGui::SameLine();
        if (ImGui::SmallButton("Configurar botões")) startWizard(i);
        // teste ao vivo
        if (gc) {
            SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(i);
            if (!g_open.count(id)) g_open[id] = SDL_GameControllerOpen(i);
            SDL_GameController* c = g_open[id];
            std::string live;
            if (c) {
                for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX && b < 15; b++)
                    if (SDL_GameControllerGetButton(c, (SDL_GameControllerButton)b)) live += std::string(kBtnNames[b]) + " ";
                if (SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 12000) live += "LT ";
                if (SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 12000) live += "RT ";
                int lx = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX), ly = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY);
                if (std::abs(lx) > 12000 || std::abs(ly) > 12000) live += "Analógico ";
            }
            ImGui::TextDisabled("    Teste: %s", live.empty() ? "(aperte qualquer botão)" : live.c_str());
        }
        ImGui::PopID();
    }
    if (!g_msg.empty()) ImGui::TextColored(ImVec4(0.5f, 0.85f, 1, 1), "%s", g_msg.c_str());
    ImGui::TextDisabled("Se algum botão estiver trocado (comum em controles genéricos), clique em \"Configurar botões\".");
    if (wz.active) drawWizard();
}
