#pragma once
#include <cstdint>
#include <cstddef>

enum MetricType {
    METRIC_FPS = 0,
    METRIC_CPU = 1,
    METRIC_CPU_LOAD = 2,
    METRIC_CPU_CLK = 3,
    METRIC_GPU = 4,
    METRIC_GPU_CLK = 5,
    METRIC_RAM = 6,
    METRIC_VRAM = 7,
    METRIC_SSD = 8,
    METRIC_FAN = 9,
    METRIC_POWER = 10,
    METRIC_IP = 11,
    METRIC_COUNT = 12
};

struct MetricItemConfig {
    MetricType type;
    bool enabled;
    char hex_color[8]; // e.g. "#FFEB3B\0"
    float r, g, b;
};

struct OverlayConfig {
    bool enabled;
    int position_vertical;   // 0 = Top, 1 = Bottom
    int position_horizontal; // 0 = Left, 1 = Center, 2 = Right
    float offset_x;          // Default 24.0f
    float offset_y;          // Default 0.0f
    int font_size;           // Default 18 (range 12..36)
    float bar_height;        // Default 34.0f (range 24..60)
    bool bg_visible;         // Default true
    char bg_hex_color[8];    // Default "#000000"
    float bg_r, bg_g, bg_b;
    float bg_opacity;        // Default 0.70f (range 0.0..1.0)
    int web_port;            // Default 8585

    // Metric ordering and configurations
    int order[METRIC_COUNT]; // Array of MetricType, e.g. {0, 1, 2, 3, 4}
    MetricItemConfig metrics[METRIC_COUNT];
};

extern OverlayConfig g_config;
extern volatile bool g_config_changed;

void config_set_defaults(OverlayConfig* cfg);
bool config_load(OverlayConfig* cfg, const char* path);
bool config_save(const OverlayConfig* cfg, const char* path);
void hex_to_rgb(const char* hex, float* r, float* g, float* b);
void rgb_to_hex(float r, float g, float b, char* hex_out);
const char* metric_type_to_str(MetricType type);
MetricType str_to_metric_type(const char* str);
