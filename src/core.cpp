#include "core.h"
#include <SDL.h>
#include <cstdarg>
#include <cstring>
#include "util.h"

Core g_core;

// implementados em play.cpp
uintptr_t playGetCurrentFramebuffer();
bool playRumble(unsigned port, int effect, uint16_t strength);

static std::string g_corePath;

bool Core::load(const std::string& path) {
    g_corePath = path;
    handle = SDL_LoadObject(path.c_str());
    if (!handle) {
        util::log("Core: nao foi possivel carregar %s: %s", path.c_str(), SDL_GetError());
        return false;
    }
#define SYM(name)                                                            \
    *(void**)&name = SDL_LoadFunction(handle, #name);                        \
    if (!name) { util::log("Core: simbolo ausente %s", #name); return false; }
    SYM(retro_init) SYM(retro_deinit) SYM(retro_api_version) SYM(retro_get_system_info)
    SYM(retro_get_system_av_info) SYM(retro_set_environment) SYM(retro_set_video_refresh)
    SYM(retro_set_audio_sample) SYM(retro_set_audio_sample_batch) SYM(retro_set_input_poll)
    SYM(retro_set_input_state) SYM(retro_set_controller_port_device) SYM(retro_reset) SYM(retro_run)
    SYM(retro_serialize_size) SYM(retro_serialize) SYM(retro_unserialize) SYM(retro_load_game)
    SYM(retro_unload_game) SYM(retro_get_memory_data) SYM(retro_get_memory_size)
#undef SYM
    retro_get_system_info(&info);
    util::log("Core: %s %s (need_fullpath=%d) extensoes=%s", info.library_name, info.library_version,
              info.need_fullpath, info.valid_extensions ? info.valid_extensions : "");
    return true;
}

void Core::unload() {
    if (handle) SDL_UnloadObject(handle);
    handle = nullptr;
}

std::string Core::findKeyBySuffix(const std::string& suffix) const {
    for (auto& k : optionOrder)
        if (k.size() >= suffix.size() && k.compare(k.size() - suffix.size(), suffix.size(), suffix) == 0)
            return k;
    return "";
}

bool Core::setOption(const std::string& key, const std::string& value) {
    auto it = options.find(key);
    if (it == options.end()) return false;
    bool valid = it->second.values.empty();
    for (auto& v : it->second.values) if (v == value) valid = true;
    if (!valid) {
        util::log("Core: valor '%s' invalido para %s", value.c_str(), key.c_str());
        return false;
    }
    if (it->second.value != value) {
        it->second.value = value;
        optionsDirty = true;
    }
    return true;
}

void Core::applyOverrides() {
    for (auto& [suffix, rule] : rules) {
        std::string key = findKeyBySuffix(suffix);
        if (key.empty()) continue;
        std::string v = rule(options[key].values);
        if (!v.empty() && setOption(key, v)) util::log("Core: %s = %s", key.c_str(), v.c_str());
    }
    for (auto& [k, v] : forced)
        if (options.count(k) && setOption(k, v)) util::log("Core: (ini) %s = %s", k.c_str(), v.c_str());
}

static void addOption(const char* key, const char* desc, const retro_core_option_value* vals, const char* def) {
    if (!key) return;
    CoreOption o;
    o.desc = desc ? desc : "";
    for (int i = 0; i < RETRO_NUM_CORE_OPTION_VALUES_MAX && vals[i].value; i++) o.values.push_back(vals[i].value);
    o.value = def ? def : (o.values.empty() ? "" : o.values[0]);
    if (!g_core.options.count(key)) g_core.optionOrder.push_back(key);
    g_core.options[key] = o;
}

static void RETRO_CALLCONV coreLog(enum retro_log_level level, const char* fmt, ...) {
    static const char* lv[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    if (level == RETRO_LOG_DEBUG) return;
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    util::log("[core %s] %s", lv[level < 4 ? level : 3], buf);
}

static retro_time_t RETRO_CALLCONV perfTimeUsec() {
    return (retro_time_t)(SDL_GetPerformanceCounter() * 1000000.0 / SDL_GetPerformanceFrequency());
}
static uint64_t RETRO_CALLCONV perfCpuFeatures() {
    uint64_t f = 0;
    if (SDL_HasSSE()) f |= RETRO_SIMD_SSE;
    if (SDL_HasSSE2()) f |= RETRO_SIMD_SSE2;
    if (SDL_HasSSE3()) f |= RETRO_SIMD_SSE3;
    if (SDL_HasSSE41()) f |= RETRO_SIMD_SSE4;
    if (SDL_HasSSE42()) f |= RETRO_SIMD_SSE42;
    if (SDL_HasAVX()) f |= RETRO_SIMD_AVX;
    if (SDL_HasAVX2()) f |= RETRO_SIMD_AVX2;
    if (SDL_HasMMX()) f |= RETRO_SIMD_MMX;
    return f;
}
static retro_perf_tick_t RETRO_CALLCONV perfCounter() { return SDL_GetPerformanceCounter(); }
static void RETRO_CALLCONV perfRegister(retro_perf_counter*) {}
static void RETRO_CALLCONV perfStart(retro_perf_counter*) {}
static void RETRO_CALLCONV perfStop(retro_perf_counter*) {}
static void RETRO_CALLCONV perfLog() {}

static uintptr_t RETRO_CALLCONV hwGetFramebuffer() { return playGetCurrentFramebuffer(); }
static retro_proc_address_t RETRO_CALLCONV hwGetProc(const char* sym) {
    return (retro_proc_address_t)SDL_GL_GetProcAddress(sym);
}
static bool RETRO_CALLCONV setRumble(unsigned port, enum retro_rumble_effect e, uint16_t s) {
    return playRumble(port, (int)e, s);
}

bool coreEnvironment(unsigned cmd, void* data) {
    Core& c = g_core;
    switch (cmd) {   // comandos que precisam de dados: com NULL, apenas recusa
    case RETRO_ENVIRONMENT_GET_CAN_DUPE: case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_LIBRETRO_PATH: case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: case RETRO_ENVIRONMENT_GET_PERF_INTERFACE:
    case RETRO_ENVIRONMENT_SET_HW_RENDER: case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER: case RETRO_ENVIRONMENT_GET_VARIABLE:
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: case RETRO_ENVIRONMENT_SET_GEOMETRY: case RETRO_ENVIRONMENT_GET_LANGUAGE:
    case RETRO_ENVIRONMENT_GET_INPUT_MAX_USERS: case RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE:
    case RETRO_ENVIRONMENT_GET_FASTFORWARDING: case RETRO_ENVIRONMENT_GET_TARGET_REFRESH_RATE:
    case RETRO_ENVIRONMENT_GET_THROTTLE_STATE: case RETRO_ENVIRONMENT_GET_JIT_CAPABLE: case RETRO_ENVIRONMENT_GET_USERNAME:
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME: case RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION:
    case RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION: case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
        if (!data) return false;
        break;
    default: break;
    }
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        *(bool*)data = true;
        return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
        auto f = *(const retro_pixel_format*)data;
        if (f > RETRO_PIXEL_FORMAT_RGB565) return false;
        c.pixfmt = f;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        *(const char**)data = c.systemDir.c_str();
        return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:
        *(const char**)data = c.saveDir.c_str();
        return true;
    case RETRO_ENVIRONMENT_GET_LIBRETRO_PATH:
        *(const char**)data = g_corePath.c_str();
        return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        ((retro_log_callback*)data)->log = coreLog;
        return true;
    case RETRO_ENVIRONMENT_GET_PERF_INTERFACE: {
        auto* p = (retro_perf_callback*)data;
        p->get_time_usec = perfTimeUsec;
        p->get_cpu_features = perfCpuFeatures;
        p->get_perf_counter = perfCounter;
        p->perf_register = perfRegister;
        p->perf_start = perfStart;
        p->perf_stop = perfStop;
        p->perf_log = perfLog;
        return true;
    }
    case RETRO_ENVIRONMENT_SET_HW_RENDER: {
        auto* h = (retro_hw_render_callback*)data;
        if (h->context_type != RETRO_HW_CONTEXT_OPENGL && h->context_type != RETRO_HW_CONTEXT_OPENGL_CORE) {
            util::log("Core: contexto de hardware %d nao suportado", (int)h->context_type);
            return false;
        }
        h->get_current_framebuffer = hwGetFramebuffer;
        h->get_proc_address = hwGetProc;
        c.hw = *h;
        c.hwRequested = true;
        util::log("Core: render por hardware OpenGL%s %u.%u depth=%d stencil=%d",
                  h->context_type == RETRO_HW_CONTEXT_OPENGL_CORE ? " core" : "", h->version_major,
                  h->version_minor, h->depth, h->stencil);
        return true;
    }
    case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
        *(unsigned*)data = RETRO_HW_CONTEXT_OPENGL_CORE;
        return true;
    case RETRO_ENVIRONMENT_SET_HW_SHARED_CONTEXT:
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        auto* v = (retro_variable*)data;
        if (!v->key) return false;
        auto it = c.options.find(v->key);
        if (it == c.options.end()) { v->value = nullptr; return false; }
        v->value = it->second.value.c_str();
        return true;
    }
    case RETRO_ENVIRONMENT_SET_VARIABLES: {
        for (auto* v = (const retro_variable*)data; v && v->key; v++) {
            std::string s = v->value ? v->value : "";
            CoreOption o;
            size_t semi = s.find(';');
            o.desc = s.substr(0, semi);
            std::string list = semi == std::string::npos ? "" : s.substr(semi + 1);
            while (!list.empty() && list[0] == ' ') list.erase(0, 1);
            size_t p = 0;
            while (p <= list.size()) {
                size_t bar = list.find('|', p);
                if (bar == std::string::npos) bar = list.size();
                if (bar > p) o.values.push_back(list.substr(p, bar - p));
                p = bar + 1;
            }
            o.value = o.values.empty() ? "" : o.values[0];
            if (!c.options.count(v->key)) c.optionOrder.push_back(v->key);
            c.options[v->key] = o;
        }
        c.applyOverrides();
        return true;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *(bool*)data = c.optionsDirty;
        c.optionsDirty = false;
        return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        *(unsigned*)data = 2;
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS: {
        for (auto* d = (const retro_core_option_definition*)data; d && d->key; d++)
            addOption(d->key, d->desc, d->values, d->default_value);
        c.applyOverrides();
        return true;
    }
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL: {
        auto* intl = (const retro_core_options_intl*)data;
        for (auto* d = intl->us; d && d->key; d++) addOption(d->key, d->desc, d->values, d->default_value);
        c.applyOverrides();
        return true;
    }
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL: {
        const retro_core_options_v2* o2 = cmd == RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2
                                              ? (const retro_core_options_v2*)data
                                              : ((const retro_core_options_v2_intl*)data)->us;
        if (o2)
            for (auto* d = o2->definitions; d && d->key; d++)
                addOption(d->key, d->desc, d->values, d->default_value);
        c.applyOverrides();
        return true;
    }
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
        return true;
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
        c.av = *(const retro_system_av_info*)data;
        c.avChanged = true;
        util::log("Core: nova geometria %ux%u (max %ux%u) %.3f fps", c.av.geometry.base_width,
                  c.av.geometry.base_height, c.av.geometry.max_width, c.av.geometry.max_height, c.av.timing.fps);
        return true;
    case RETRO_ENVIRONMENT_SET_GEOMETRY: {
        auto* g = (const retro_game_geometry*)data;
        c.av.geometry.base_width = g->base_width;
        c.av.geometry.base_height = g->base_height;
        c.av.geometry.aspect_ratio = g->aspect_ratio;
        c.geometryChanged = true;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_LANGUAGE:
        *(unsigned*)data = RETRO_LANGUAGE_PORTUGUESE_BRAZIL;
        return true;
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
        return true;
    case RETRO_ENVIRONMENT_GET_INPUT_MAX_USERS:
        *(unsigned*)data = 4;
        return true;
    case RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE:
        ((retro_rumble_interface*)data)->set_rumble_state = setRumble;
        return true;
    case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
        if (data) *(int*)data = 3;
        return true;
    case RETRO_ENVIRONMENT_GET_FASTFORWARDING:
        *(bool*)data = c.fastForward;
        return true;
    case RETRO_ENVIRONMENT_GET_TARGET_REFRESH_RATE:
        *(float*)data = 60.0f;
        return true;
    case RETRO_ENVIRONMENT_GET_THROTTLE_STATE: {
        auto* t = (retro_throttle_state*)data;
        t->mode = c.fastForward ? RETRO_THROTTLE_FAST_FORWARD : RETRO_THROTTLE_NONE;
        t->rate = (float)c.av.timing.fps;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_SAVESTATE_CONTEXT:
        if (data) *(int*)data = RETRO_SAVESTATE_CONTEXT_NORMAL;
        return true;
    case RETRO_ENVIRONMENT_GET_JIT_CAPABLE:
        *(bool*)data = true;
        return true;
    case RETRO_ENVIRONMENT_GET_USERNAME:
        *(const char**)data = "Jogador";
        return true;
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
        c.supportsNoGame = *(const bool*)data;
        return true;
    case RETRO_ENVIRONMENT_SHUTDOWN:
        c.shutdownRequested = true;
        return true;
    case RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION:
        *(unsigned*)data = 1;
        return true;
    case RETRO_ENVIRONMENT_SET_MESSAGE: {
        auto* m = (const retro_message*)data;
        if (m && m->msg) { c.message = m->msg; c.messageFrames = m->frames ? m->frames : 180; }
        return true;
    }
    case RETRO_ENVIRONMENT_SET_MESSAGE_EXT: {
        auto* m = (const retro_message_ext*)data;
        if (m && m->msg && m->target != RETRO_MESSAGE_TARGET_LOG) {
            c.message = m->msg;
            c.messageFrames = m->duration ? (m->duration * 60 / 1000) : 180;
        }
        if (m && m->msg) util::log("[core msg] %s", m->msg);
        return true;
    }
    case RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION:
        *(unsigned*)data = 1;
        return true;
    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE: {
        memset(&c.disk, 0, sizeof c.disk);
        if (!data) { c.hasDisk = false; return true; }
        memcpy(&c.disk, data, sizeof(retro_disk_control_callback));
        c.hasDisk = true;
        return true;
    }
    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE:
        if (!data) { memset(&c.disk, 0, sizeof c.disk); c.hasDisk = false; return true; }
        c.disk = *(const retro_disk_control_ext_callback*)data;
        c.hasDisk = true;
        return true;
    case RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK:
        if (!data) { c.hasFrameTime = false; return true; }
        c.frameTime = *(const retro_frame_time_callback*)data;
        c.hasFrameTime = true;
        return true;
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
    case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
    case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
    case RETRO_ENVIRONMENT_SET_CONTENT_INFO_OVERRIDE:
    case RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK:
    case RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY:
    case RETRO_ENVIRONMENT_SET_FASTFORWARDING_OVERRIDE:
        return true;
    default:
        return false;
    }
}
