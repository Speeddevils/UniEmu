#include "controllers.h"
#include <vector>
#include "util.h"

std::string controllersUserFile() { return util::joinPath(util::baseDir(), "controles.cfg"); }

std::string guidString(SDL_Joystick* js) {
    char g[64];
    SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(js), g, sizeof g);
    return g;
}

static int addMappingsFromFile(const std::string& path) {
    std::vector<uint8_t> d;
    if (!util::readFile(path, d)) return 0;
    SDL_RWops* rw = SDL_RWFromConstMem(d.data(), (int)d.size());
    return rw ? SDL_GameControllerAddMappingsFromRW(rw, 1) : 0;
}

void controllersLoadMappings() {
    int a = addMappingsFromFile(util::joinPath(util::baseDir(), "gamecontrollerdb.txt"));
    int b = addMappingsFromFile(controllersUserFile());   // os do usuário por último: têm prioridade
    util::log("Controles: %d mapeamentos do banco de dados, %d do usuario", a < 0 ? 0 : a, b < 0 ? 0 : b);
}

static std::string cleanName(const char* n) {
    std::string s = n ? n : "Controle generico";
    for (auto& c : s) if (c == ',') c = ' ';
    return s;
}

bool controllersEnsureMapping(int idx) {
    if (SDL_IsGameController(idx)) return true;
    SDL_Joystick* js = SDL_JoystickOpen(idx);
    if (!js) return false;
    std::string guid = guidString(js);
    int nb = SDL_JoystickNumButtons(js), na = SDL_JoystickNumAxes(js), nh = SDL_JoystickNumHats(js);
    std::string name = cleanName(SDL_JoystickName(js));
    SDL_JoystickClose(js);
    // Layout dos controles USB genéricos mais comuns (padrão "DragonRise"/estilo PlayStation):
    // 0=Triângulo 1=Círculo 2=Xis 3=Quadrado 4=L1 5=R1 6=L2 7=R2 8=Select 9=Start 10=L3 11=R3
    std::string m = guid + "," + name + ",";
    auto btn = [&](const char* to, int b) { if (b < nb) m += std::string(to) + ":b" + std::to_string(b) + ","; };
    btn("a", 2); btn("b", 1); btn("x", 3); btn("y", 0);
    btn("leftshoulder", 4); btn("rightshoulder", 5); btn("lefttrigger", 6); btn("righttrigger", 7);
    btn("back", 8); btn("start", 9); btn("leftstick", 10); btn("rightstick", 11);
    if (nh > 0) m += "dpup:h0.1,dpright:h0.2,dpdown:h0.4,dpleft:h0.8,";
    if (na >= 2) m += "leftx:a0,lefty:a1,";
    if (na >= 4) m += "rightx:a2,righty:a3,";
    int r = SDL_GameControllerAddMapping(m.c_str());
    util::log("Controles: '%s' (%s) sem mapeamento conhecido; usando layout generico (%d botoes, %d eixos, %d hats). "
              "Use 'Configurar controle' se os botoes estiverem trocados.", name.c_str(), guid.c_str(), nb, na, nh);
    return r >= 0 && SDL_IsGameController(idx);
}

bool controllersSaveUserMapping(const std::string& mapping) {
    std::string guid = mapping.substr(0, mapping.find(','));
    std::vector<uint8_t> d;
    std::string out;
    if (util::readFile(controllersUserFile(), d)) {
        std::string s(d.begin(), d.end());
        size_t p = 0;
        while (p < s.size()) {
            size_t e = s.find('\n', p);
            if (e == std::string::npos) e = s.size();
            std::string line = s.substr(p, e - p);
            p = e + 1;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line.rfind(guid + ",", 0) == 0) continue;  // substitui o antigo
            out += line + "\n";
        }
    }
    std::string withPlatform = mapping;
    if (withPlatform.back() != ',') withPlatform += ",";
    withPlatform += std::string("platform:") + SDL_GetPlatform() + ",";
    out += withPlatform + "\n";
    SDL_GameControllerAddMapping(mapping.c_str());
    return util::writeFile(controllersUserFile(), out.data(), out.size());
}
