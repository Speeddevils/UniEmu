// UniEmu - hospedeiro libretro (carrega um núcleo e implementa o ambiente)
#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "libretro.h"

struct CoreOption {
    std::string desc;
    std::vector<std::string> values;
    std::string value;
};

// Regra de override: recebe os valores válidos e devolve o escolhido ("" = não mexe)
using OptionRule = std::function<std::string(const std::vector<std::string>&)>;

struct Core {
    // símbolos
    void (*retro_init)();
    void (*retro_deinit)();
    unsigned (*retro_api_version)();
    void (*retro_get_system_info)(retro_system_info*);
    void (*retro_get_system_av_info)(retro_system_av_info*);
    void (*retro_set_environment)(retro_environment_t);
    void (*retro_set_video_refresh)(retro_video_refresh_t);
    void (*retro_set_audio_sample)(retro_audio_sample_t);
    void (*retro_set_audio_sample_batch)(retro_audio_sample_batch_t);
    void (*retro_set_input_poll)(retro_input_poll_t);
    void (*retro_set_input_state)(retro_input_state_t);
    void (*retro_set_controller_port_device)(unsigned, unsigned);
    void (*retro_reset)();
    void (*retro_run)();
    size_t (*retro_serialize_size)();
    bool (*retro_serialize)(void*, size_t);
    bool (*retro_unserialize)(const void*, size_t);
    bool (*retro_load_game)(const retro_game_info*);
    void (*retro_unload_game)();
    void* (*retro_get_memory_data)(unsigned);
    size_t (*retro_get_memory_size)(unsigned);

    void* handle = nullptr;
    retro_system_info info{};
    retro_system_av_info av{};

    // estado do ambiente
    std::string systemDir, saveDir;
    retro_pixel_format pixfmt = RETRO_PIXEL_FORMAT_0RGB1555;
    bool hwRequested = false;
    retro_hw_render_callback hw{};
    std::map<std::string, CoreOption> options;
    std::vector<std::string> optionOrder;
    std::map<std::string, std::string> forced;        // overrides fixos (do .ini)
    std::vector<std::pair<std::string, OptionRule>> rules;  // overrides por sufixo
    bool optionsDirty = false;
    bool avChanged = false, geometryChanged = false;
    bool shutdownRequested = false;
    bool fastForward = false;
    std::string message;
    unsigned messageFrames = 0;
    retro_disk_control_ext_callback disk{};
    bool hasDisk = false;
    retro_frame_time_callback frameTime{};
    bool hasFrameTime = false;
    bool supportsNoGame = false;

    bool load(const std::string& path);
    void unload();
    void applyOverrides();
    bool setOption(const std::string& key, const std::string& value);
    std::string findKeyBySuffix(const std::string& suffix) const;
};

extern Core g_core;
bool coreEnvironment(unsigned cmd, void* data);
