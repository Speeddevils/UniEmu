#include "gl.h"
#include "util.h"

#define UE_DEF(type, name) type ue_##name = nullptr;
UE_GL_FUNCS(UE_DEF)
#undef UE_DEF

bool ueLoadGL() {
    bool ok = true;
#define UE_LOAD(type, name)                                              \
    ue_##name = (type)SDL_GL_GetProcAddress(#name);                      \
    if (!ue_##name) { util::log("GL: funcao ausente %s", #name); ok = false; }
    UE_GL_FUNCS(UE_LOAD)
#undef UE_LOAD
    return ok;
}
