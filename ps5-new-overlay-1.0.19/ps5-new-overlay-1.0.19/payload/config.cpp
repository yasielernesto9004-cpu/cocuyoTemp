#include "config.hpp"
#include "../common/log.h"
#include <cstdio>
#include <cstring>

extern "C" {
    long __syscall(long sysno, ...);
}

#define SYS_read   3
#define SYS_write  4
#define SYS_open   5
#define SYS_close  6
#define SYS_chmod  15
#define SYS_fsync  95
#define SYS_mkdir  136

#define O_RDONLY   0x0000
#define O_WRONLY   0x0001
#define O_CREAT    0x0200
#define O_TRUNC    0x0400

OverlayConfig g_config;
volatile bool g_config_changed = false;

static bool ci_streq(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? (*a + 32) : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (*b + 32) : *b;
        if (ca != cb) return false;
        a++; b++;
    }
    return (*a == '\0' && *b == '\0');
}

const char* metric_type_to_str(MetricType type) {
    switch (type) {
        case METRIC_FPS: return "fps";
        case METRIC_CPU: return "cpu";
        case METRIC_CPU_LOAD: return "load";
        case METRIC_CPU_CLK: return "cclk";
        case METRIC_GPU: return "gpu";
        case METRIC_GPU_CLK: return "gclk";
        case METRIC_RAM: return "ram";
        case METRIC_VRAM: return "vram";
        case METRIC_SSD: return "ssd";
        case METRIC_FAN: return "fan";
        case METRIC_POWER: return "pwr";
        case METRIC_IP: return "ip";
        default: return "unknown";
    }
}

MetricType str_to_metric_type(const char* str) {
    if (!str) return METRIC_FPS;
    if (ci_streq(str, "fps")) return METRIC_FPS;
    if (ci_streq(str, "cpu")) return METRIC_CPU;
    if (ci_streq(str, "load") || ci_streq(str, "cpu_load") || ci_streq(str, "cpuload")) return METRIC_CPU_LOAD;
    if (ci_streq(str, "cclk") || ci_streq(str, "cpu_clk") || ci_streq(str, "cpuclk")) return METRIC_CPU_CLK;
    if (ci_streq(str, "gpu")) return METRIC_GPU;
    if (ci_streq(str, "gclk") || ci_streq(str, "gpu_clk") || ci_streq(str, "gpuclk")) return METRIC_GPU_CLK;
    if (ci_streq(str, "ram")) return METRIC_RAM;
    if (ci_streq(str, "vram")) return METRIC_VRAM;
    if (ci_streq(str, "ssd") || ci_streq(str, "m2") || ci_streq(str, "nvme")) return METRIC_SSD;
    if (ci_streq(str, "fan")) return METRIC_FAN;
    if (ci_streq(str, "pwr") || ci_streq(str, "power") || ci_streq(str, "soc_power")) return METRIC_POWER;
    if (ci_streq(str, "ip")) return METRIC_IP;
    return METRIC_FPS;
}

static int hex_char_to_int(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

void hex_to_rgb(const char* hex, float* r, float* g, float* b) {
    if (!hex || hex[0] != '#' || strlen(hex) < 7) {
        if (r) *r = 1.0f; if (g) *g = 1.0f; if (b) *b = 1.0f;
        return;
    }
    int ri = (hex_char_to_int(hex[1]) << 4) | hex_char_to_int(hex[2]);
    int gi = (hex_char_to_int(hex[3]) << 4) | hex_char_to_int(hex[4]);
    int bi = (hex_char_to_int(hex[5]) << 4) | hex_char_to_int(hex[6]);
    if (r) *r = (float)ri / 255.0f;
    if (g) *g = (float)gi / 255.0f;
    if (b) *b = (float)bi / 255.0f;
}

void rgb_to_hex(float r, float g, float b, char* hex_out) {
    if (!hex_out) return;
    int ri = (int)(r * 255.0f + 0.5f);
    int gi = (int)(g * 255.0f + 0.5f);
    int bi = (int)(b * 255.0f + 0.5f);
    if (ri < 0) ri = 0; if (ri > 255) ri = 255;
    if (gi < 0) gi = 0; if (gi > 255) gi = 255;
    if (bi < 0) bi = 0; if (bi > 255) bi = 255;
    snprintf(hex_out, 8, "#%02X%02X%02X", ri, gi, bi);
}

void config_set_defaults(OverlayConfig* cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(OverlayConfig));
    cfg->enabled = true;
    cfg->position_vertical = 0;   // Top
    cfg->position_horizontal = 0; // Left
    cfg->offset_x = 24.0f;
    cfg->offset_y = 0.0f;
    cfg->font_size = 18;
    cfg->bar_height = 34.0f;
    cfg->bg_visible = true;
    strncpy(cfg->bg_hex_color, "#000000", sizeof(cfg->bg_hex_color));
    cfg->bg_r = 0.0f;
    cfg->bg_g = 0.0f;
    cfg->bg_b = 0.0f;
    cfg->bg_opacity = 0.70f;
    cfg->web_port = 8585;

    // Default order: FPS, CPU, CPU_LOAD, GPU, RAM, VRAM, FAN, IP
    for (int i = 0; i < METRIC_COUNT; i++) {
        cfg->order[i] = i;
        cfg->metrics[i].type = (MetricType)i;
        cfg->metrics[i].enabled = true;
    }

    // Default metric colors
    strncpy(cfg->metrics[METRIC_FPS].hex_color, "#FFEB3B", sizeof(cfg->metrics[METRIC_FPS].hex_color));
    hex_to_rgb("#FFEB3B", &cfg->metrics[METRIC_FPS].r, &cfg->metrics[METRIC_FPS].g, &cfg->metrics[METRIC_FPS].b);

    strncpy(cfg->metrics[METRIC_CPU].hex_color, "#66FF66", sizeof(cfg->metrics[METRIC_CPU].hex_color));
    hex_to_rgb("#66FF66", &cfg->metrics[METRIC_CPU].r, &cfg->metrics[METRIC_CPU].g, &cfg->metrics[METRIC_CPU].b);

    strncpy(cfg->metrics[METRIC_CPU_LOAD].hex_color, "#FFA726", sizeof(cfg->metrics[METRIC_CPU_LOAD].hex_color));
    hex_to_rgb("#FFA726", &cfg->metrics[METRIC_CPU_LOAD].r, &cfg->metrics[METRIC_CPU_LOAD].g, &cfg->metrics[METRIC_CPU_LOAD].b);

    strncpy(cfg->metrics[METRIC_CPU_CLK].hex_color, "#80D8FF", sizeof(cfg->metrics[METRIC_CPU_CLK].hex_color));
    hex_to_rgb("#80D8FF", &cfg->metrics[METRIC_CPU_CLK].r, &cfg->metrics[METRIC_CPU_CLK].g, &cfg->metrics[METRIC_CPU_CLK].b);

    strncpy(cfg->metrics[METRIC_GPU].hex_color, "#B366FF", sizeof(cfg->metrics[METRIC_GPU].hex_color));
    hex_to_rgb("#B366FF", &cfg->metrics[METRIC_GPU].r, &cfg->metrics[METRIC_GPU].g, &cfg->metrics[METRIC_GPU].b);

    strncpy(cfg->metrics[METRIC_GPU_CLK].hex_color, "#EA80FC", sizeof(cfg->metrics[METRIC_GPU_CLK].hex_color));
    hex_to_rgb("#EA80FC", &cfg->metrics[METRIC_GPU_CLK].r, &cfg->metrics[METRIC_GPU_CLK].g, &cfg->metrics[METRIC_GPU_CLK].b);

    strncpy(cfg->metrics[METRIC_RAM].hex_color, "#FFB34D", sizeof(cfg->metrics[METRIC_RAM].hex_color));
    hex_to_rgb("#FFB34D", &cfg->metrics[METRIC_RAM].r, &cfg->metrics[METRIC_RAM].g, &cfg->metrics[METRIC_RAM].b);

    strncpy(cfg->metrics[METRIC_VRAM].hex_color, "#E040FB", sizeof(cfg->metrics[METRIC_VRAM].hex_color));
    hex_to_rgb("#E040FB", &cfg->metrics[METRIC_VRAM].r, &cfg->metrics[METRIC_VRAM].g, &cfg->metrics[METRIC_VRAM].b);

    strncpy(cfg->metrics[METRIC_SSD].hex_color, "#00E5FF", sizeof(cfg->metrics[METRIC_SSD].hex_color));
    hex_to_rgb("#00E5FF", &cfg->metrics[METRIC_SSD].r, &cfg->metrics[METRIC_SSD].g, &cfg->metrics[METRIC_SSD].b);

    strncpy(cfg->metrics[METRIC_FAN].hex_color, "#33E0FF", sizeof(cfg->metrics[METRIC_FAN].hex_color));
    hex_to_rgb("#33E0FF", &cfg->metrics[METRIC_FAN].r, &cfg->metrics[METRIC_FAN].g, &cfg->metrics[METRIC_FAN].b);

    strncpy(cfg->metrics[METRIC_POWER].hex_color, "#FF5252", sizeof(cfg->metrics[METRIC_POWER].hex_color));
    hex_to_rgb("#FF5252", &cfg->metrics[METRIC_POWER].r, &cfg->metrics[METRIC_POWER].g, &cfg->metrics[METRIC_POWER].b);

    strncpy(cfg->metrics[METRIC_IP].hex_color, "#76FF03", sizeof(cfg->metrics[METRIC_IP].hex_color));
    hex_to_rgb("#76FF03", &cfg->metrics[METRIC_IP].r, &cfg->metrics[METRIC_IP].g, &cfg->metrics[METRIC_IP].b);
}

static char* trim_str(char* str) {
    while (*str == ' ' || *str == '\t' || *str == '\r' || *str == '\n') str++;
    if (*str == 0) return str;
    char* end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) end--;
    end[1] = '\0';
    return str;
}

static bool parse_bool(const char* val, bool def) {
    if (!val) return def;
    if (ci_streq(val, "true") || ci_streq(val, "1") || ci_streq(val, "yes") || ci_streq(val, "on")) return true;
    if (ci_streq(val, "false") || ci_streq(val, "0") || ci_streq(val, "no") || ci_streq(val, "off")) return false;
    return def;
}

static int parse_int(const char* s, int def) {
    if (!s) return def;
    while (*s == ' ' || *s == '\t') s++;
    if (!*s) return def;
    bool neg = false;
    if (*s == '-') { neg = true; s++; }
    int res = 0;
    while (*s >= '0' && *s <= '9') {
        res = res * 10 + (*s - '0');
        s++;
    }
    return neg ? -res : res;
}

static float parse_float(const char* s, float def) {
    if (!s) return def;
    while (*s == ' ' || *s == '\t') s++;
    if (!*s) return def;
    bool neg = false;
    if (*s == '-') { neg = true; s++; }
    float res = 0.0f;
    float fact = 1.0f;
    bool point = false;
    while (*s) {
        if (*s == '.') {
            point = true;
            s++;
            continue;
        }
        if (*s >= '0' && *s <= '9') {
            if (point) fact /= 10.0f;
            res = res * 10.0f + (float)(*s - '0');
            s++;
        } else {
            break;
        }
    }
    res *= fact;
    return neg ? -res : res;
}

bool config_load(OverlayConfig* cfg, const char* path) {
    config_set_defaults(cfg);
    if (!path) return false;

    long fd = __syscall(SYS_open, path, O_RDONLY, 0);
    const char* loaded_path = path;
    if (fd < 0) {
        fd = __syscall(SYS_open, "/data/ps5_overlay.ini", O_RDONLY, 0);
        if (fd >= 0) {
            loaded_path = "/data/ps5_overlay.ini";
        }
    }
    if (fd < 0) {
        fd = __syscall(SYS_open, "/user/data/overlay/config.ini", O_RDONLY, 0);
        if (fd >= 0) {
            loaded_path = "/user/data/overlay/config.ini";
        }
    }
    if (fd < 0) {
        LOG_INFO("Config file not found on disk. Creating initial default at %s...", path);
        config_save(cfg, path);
        return false;
    }

    char buf[4096];
    memset(buf, 0, sizeof(buf));
    long n = __syscall(SYS_read, fd, buf, sizeof(buf) - 1);
    __syscall(SYS_close, fd);

    if (n <= 0) return false;
    buf[n] = '\0';

    char section[64] = "";
    char* cur = buf;

    while (*cur) {
        char* line_start = cur;
        while (*cur && *cur != '\r' && *cur != '\n') cur++;
        if (*cur) {
            *cur = '\0';
            cur++;
            while (*cur == '\r' || *cur == '\n') cur++;
        }

        char* tr = trim_str(line_start);
        if (tr[0] == '\0' || tr[0] == '#' || tr[0] == ';') continue;

        if (tr[0] == '[') {
            char* end = strchr(tr, ']');
            if (end) {
                *end = '\0';
                strncpy(section, tr + 1, sizeof(section) - 1);
            }
            continue;
        }

        char* eq = strchr(tr, '=');
        if (!eq) continue;
        *eq = '\0';
        char* key = trim_str(tr);
        char* val = trim_str(eq + 1);

        if (strcmp(section, "overlay") == 0 || section[0] == '\0') {
            if (ci_streq(key, "enabled")) cfg->enabled = parse_bool(val, cfg->enabled);
            else if (ci_streq(key, "pos_v")) {
                cfg->position_vertical = (ci_streq(val, "bottom") || strcmp(val, "1") == 0) ? 1 : 0;
            } else if (ci_streq(key, "pos_h")) {
                if (ci_streq(val, "center") || strcmp(val, "1") == 0) cfg->position_horizontal = 1;
                else if (ci_streq(val, "right") || strcmp(val, "2") == 0) cfg->position_horizontal = 2;
                else cfg->position_horizontal = 0;
            } else if (ci_streq(key, "offset_x")) {
                cfg->offset_x = parse_float(val, cfg->offset_x);
            } else if (ci_streq(key, "offset_y")) {
                cfg->offset_y = parse_float(val, cfg->offset_y);
            } else if (ci_streq(key, "font_size")) {
                int fs = parse_int(val, cfg->font_size);
                if (fs >= 10 && fs <= 48) cfg->font_size = fs;
            } else if (ci_streq(key, "bar_height")) {
                float bh = parse_float(val, cfg->bar_height);
                if (bh >= 20.0f && bh <= 80.0f) cfg->bar_height = bh;
            } else if (ci_streq(key, "bg_visible")) {
                cfg->bg_visible = parse_bool(val, cfg->bg_visible);
            } else if (ci_streq(key, "bg_color")) {
                if (val[0] == '#' && strlen(val) >= 7) {
                    strncpy(cfg->bg_hex_color, val, 7);
                    cfg->bg_hex_color[7] = '\0';
                    hex_to_rgb(cfg->bg_hex_color, &cfg->bg_r, &cfg->bg_g, &cfg->bg_b);
                }
            } else if (ci_streq(key, "bg_opacity")) {
                float op = parse_float(val, cfg->bg_opacity);
                if (op > 1.0f) op /= 100.0f;
                if (op >= 0.0f && op <= 1.0f) cfg->bg_opacity = op;
            } else if (ci_streq(key, "port") || ci_streq(key, "web_port")) {
                int p = parse_int(val, cfg->web_port);
                if (p >= 1024 && p <= 65535) cfg->web_port = p;
            }
        } else if (strcmp(section, "metrics") == 0) {
            if (ci_streq(key, "order")) {
                int o_idx = 0;
                char* token = val;
                bool seen[METRIC_COUNT] = {false};
                while (*token && o_idx < METRIC_COUNT) {
                    while (*token == ' ' || *token == '\t' || *token == ',') token++;
                    if (!*token) break;
                    char* tok_end = token;
                    while (*tok_end && *tok_end != ',' && *tok_end != ' ' && *tok_end != '\t') tok_end++;
                    char save = *tok_end;
                    *tok_end = '\0';
                    MetricType mt = str_to_metric_type(token);
                    if (!seen[mt]) {
                        cfg->order[o_idx++] = (int)mt;
                        seen[mt] = true;
                    }
                    if (!save) break;
                    token = tok_end + 1;
                }
                // Automatically append any missing metrics (e.g. from older 5-item configs)
                for (int m = 0; m < METRIC_COUNT && o_idx < METRIC_COUNT; m++) {
                    if (!seen[m]) {
                        cfg->order[o_idx++] = m;
                        seen[m] = true;
                    }
                }
            }
        } else {
            MetricType mt = str_to_metric_type(section);
            if (ci_streq(key, "enabled")) {
                cfg->metrics[mt].enabled = parse_bool(val, cfg->metrics[mt].enabled);
            } else if (ci_streq(key, "color")) {
                if (val[0] == '#' && strlen(val) >= 7) {
                    strncpy(cfg->metrics[mt].hex_color, val, 7);
                    cfg->metrics[mt].hex_color[7] = '\0';
                    hex_to_rgb(cfg->metrics[mt].hex_color, &cfg->metrics[mt].r, &cfg->metrics[mt].g, &cfg->metrics[mt].b);
                }
            }
        }
    }

    // Safety validation pass: ensure all 0..METRIC_COUNT-1 are unique and valid
    {
        bool seen[METRIC_COUNT] = {false};
        bool valid = true;
        for (int i = 0; i < METRIC_COUNT; i++) {
            int m = cfg->order[i];
            if (m < 0 || m >= METRIC_COUNT || seen[m]) {
                valid = false;
                break;
            }
            seen[m] = true;
        }
        if (!valid) {
            LOG_WARN("Config order had duplicate or invalid entries; repairing...");
            int idx = 0;
            bool fixed[METRIC_COUNT] = {false};
            for (int i = 0; i < METRIC_COUNT; i++) {
                int m = cfg->order[i];
                if (m >= 0 && m < METRIC_COUNT && !fixed[m]) {
                    cfg->order[idx++] = m;
                    fixed[m] = true;
                }
            }
            for (int m = 0; m < METRIC_COUNT && idx < METRIC_COUNT; m++) {
                if (!fixed[m]) {
                    cfg->order[idx++] = m;
                    fixed[m] = true;
                }
            }
        }
    }

    LOG_INFO("Config loaded successfully from %s", loaded_path);
    return true;
}

bool config_save(const OverlayConfig* cfg, const char* path) {
    if (!cfg || !path) return false;

    // Ensure all target directories exist
    __syscall(SYS_mkdir, "/data", 0777);
    __syscall(SYS_mkdir, "/data/overlay", 0777);
    __syscall(SYS_chmod, "/data/overlay", 0777);
    __syscall(SYS_mkdir, "/user/data", 0777);
    __syscall(SYS_mkdir, "/user/data/overlay", 0777);
    __syscall(SYS_chmod, "/user/data/overlay", 0777);

    char buf[2048];
    int len = 0;
    len += snprintf(buf + len, sizeof(buf) - len,
        "[overlay]\n"
        "enabled = %s\n"
        "pos_v = %s\n"
        "pos_h = %s\n"
        "offset_x = %.0f\n"
        "offset_y = %.0f\n"
        "font_size = %d\n"
        "bar_height = %.0f\n"
        "bg_visible = %s\n"
        "bg_color = %s\n"
        "bg_opacity = %.2f\n"
        "port = %d\n\n"
        "[metrics]\n"
        "order = ",
        cfg->enabled ? "true" : "false",
        cfg->position_vertical == 1 ? "bottom" : "top",
        cfg->position_horizontal == 1 ? "center" : (cfg->position_horizontal == 2 ? "right" : "left"),
        cfg->offset_x,
        cfg->offset_y,
        cfg->font_size,
        cfg->bar_height,
        cfg->bg_visible ? "true" : "false",
        cfg->bg_hex_color,
        cfg->bg_opacity,
        cfg->web_port
    );

    for (int i = 0; i < METRIC_COUNT; i++) {
        MetricType mt = (MetricType)cfg->order[i];
        if (mt < 0 || mt >= METRIC_COUNT) continue;
        len += snprintf(buf + len, sizeof(buf) - len, "%s%s", (i > 0) ? "," : "", metric_type_to_str(mt));
    }
    len += snprintf(buf + len, sizeof(buf) - len, "\n\n");

    for (int i = 0; i < METRIC_COUNT; i++) {
        MetricType mt = (MetricType)i;
        len += snprintf(buf + len, sizeof(buf) - len,
            "[%s]\n"
            "enabled = %s\n"
            "color = %s\n\n",
            metric_type_to_str(mt),
            cfg->metrics[mt].enabled ? "true" : "false",
            cfg->metrics[mt].hex_color
        );
    }

    long fd = __syscall(SYS_open, path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) {
        __syscall(SYS_write, fd, buf, len);
        __syscall(SYS_fsync, fd);
        __syscall(SYS_close, fd);
        LOG_INFO("Config saved successfully to %s", path);
    } else {
        LOG_ERROR("Failed to open %s for writing (fd=%ld)", path, fd);
    }

    // Mirror to /data/ps5_overlay.ini as a guaranteed root fallback
    if (strcmp(path, "/data/ps5_overlay.ini") != 0) {
        long mfd = __syscall(SYS_open, "/data/ps5_overlay.ini", O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (mfd >= 0) {
            __syscall(SYS_write, mfd, buf, len);
            __syscall(SYS_fsync, mfd);
            __syscall(SYS_close, mfd);
            LOG_INFO("Config mirror backup saved to /data/ps5_overlay.ini");
        }
    }

    return (fd >= 0);
}
