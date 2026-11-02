// UniEmu - ponto de entrada
#include <SDL.h>
#include <cstring>
#include <string>
#include "controllers.h"
#include "play.h"
#include "settings.h"
#include "util.h"

int runLauncher(Settings& st);

int main(int argc, char** argv) {
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");  // controles funcionam mesmo sem foco
    SDL_SetHint(SDL_HINT_FORCE_RAISEWINDOW, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER) != 0) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "UniEmu", SDL_GetError(), nullptr);
        return 1;
    }
    std::string rom;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--play") && i + 1 < argc) rom = argv[++i];
        else if (argv[i][0] != '-') rom = argv[i];
    }
    Settings st;
    int rc;
    auto initPads = [] { controllersLoadMappings(); };
    if (!rom.empty()) {
        util::logInit(util::joinPath(util::baseDir(), "UniEmu-jogo.log"));
        st.load();
        initPads();
        rc = runGame(rom, st);
    } else {
        util::logInit(util::joinPath(util::baseDir(), "UniEmu.log"));
        st.load();
        initPads();
        rc = runLauncher(st);
    }
    SDL_Quit();
    return rc;
}
