#pragma once

#include "mono_runtime.hpp"
#include "overlay_layout.hpp"

namespace overlay {

struct Settings {
    bool enabled = true;
    bool background = true;
    bool show_fps = true;
    bool show_cpu = true;
    bool show_gpu = true;
    bool show_ram = true;
    bool show_ip = true;
    bool show_fan = true;
    bool per_core_cpu = false;
    int font_size = 18;
    int position = 0; // 0 = top, 2 = bottom
    int alignment = 1; // 0 = left, 1 = center, 2 = right
};

extern Settings g_settings;
extern onion::overlay::Layout g_layout;

bool init_widgets();
void remove_all_widgets();
void update_labels(const char* fps, const char* cpu_temp, const char* cpu_usage,
                   const char* gpu_temp, const char* gpu_usage, const char* ram,
                   const char* ip, const char* fan);
bool resolve_screen_size(float* width, float* height);

} // namespace overlay
