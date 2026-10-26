#include "mono_runtime.hpp"
#include "../common/log.h"
#include "../common/bootstrap.h"
#include "config.hpp"
#include "web_server.hpp"
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "telemetry.h"

extern "C" {
    int (*sceKernelMprotect)(void *addr, size_t len, int prot) = nullptr;
    int (*sceKernelGetSocSensorTemperature)(int sensorId, int* soctime) = nullptr;
    int (*get_page_table_stats)(int vm, int type, int* total, int* free) = nullptr;
    int (*sceKernelGetCpuUsage)(void* out, int32_t* size) = nullptr;
    int (*sceKernelGetThreadName)(uint32_t id, char* out) = nullptr;
    int (*sceKernelGetCpuTemperature)(int* cputemp) = nullptr;
    int (*sceKernelClockGettime)(int clockId, void* tp) = nullptr;
    int (*sceKernelGetCurrentFanDuty)(uint16_t* duty, uint64_t* chassis) = nullptr;
    int (*sceNetCtlGetInfo)(int code, void* info) = nullptr;
    int (*sys_ioctl)(int fd, unsigned long req, ...) = nullptr;
    int (*sceKernelGetSocClock)(uint32_t* out_raw_domains) = nullptr;
    int (*sceKernelGetSocPowerConsumption)(void* out_raw) = nullptr;
    int (*sceKernelGetCpuCoreClock)(int* out_mhz_per_core) = nullptr;
    long (*sceKernelGetCpuFrequency)(void) = nullptr;
}

__attribute__((section(".data"))) PayloadBootstrapArgs g_bargs = {0};
bool has_hv_bypass = false;

static bool resolve_telemetry_symbols(pid_t pid, int libkernelsys, uintptr_t libkernelsys_mb) {
    LOG_INFO("Resolving telemetry symbols...");
    if (g_bargs.sceKernelGetCpuTemperature) {
        *(void**)&sceKernelGetCpuTemperature = (void*)g_bargs.sceKernelGetCpuTemperature;
    } else if (libkernelsys > 0) {
        *(void**)&sceKernelGetCpuTemperature = resolve_symbol(libkernelsys, libkernelsys_mb, "sceKernelGetCpuTemperature");
    }

    if (g_bargs.sceKernelGetSocSensorTemperature) {
        *(void**)&sceKernelGetSocSensorTemperature = (void*)g_bargs.sceKernelGetSocSensorTemperature;
    } else if (libkernelsys > 0) {
        *(void**)&sceKernelGetSocSensorTemperature = resolve_symbol(libkernelsys, libkernelsys_mb, "sceKernelGetSocSensorTemperature");
    }

    if (g_bargs.get_page_table_stats) {
        *(void**)&get_page_table_stats = (void*)g_bargs.get_page_table_stats;
    } else if (libkernelsys > 0) {
        *(void**)&get_page_table_stats = resolve_symbol(libkernelsys, libkernelsys_mb, "get_page_table_stats");
    }

    if (g_bargs.sceKernelGetCpuUsage) {
        *(void**)&sceKernelGetCpuUsage = (void*)g_bargs.sceKernelGetCpuUsage;
    } else if (libkernelsys > 0) {
        *(void**)&sceKernelGetCpuUsage = resolve_symbol(libkernelsys, libkernelsys_mb, "sceKernelGetCpuUsage");
    }

    if (g_bargs.sceKernelGetThreadName) {
        *(void**)&sceKernelGetThreadName = (void*)g_bargs.sceKernelGetThreadName;
    } else if (libkernelsys > 0) {
        *(void**)&sceKernelGetThreadName = resolve_symbol(libkernelsys, libkernelsys_mb, "sceKernelGetThreadName");
    }

    if (g_bargs.sceKernelClockGettime) {
        *(void**)&sceKernelClockGettime = (void*)g_bargs.sceKernelClockGettime;
    } else if (libkernelsys > 0) {
        *(void**)&sceKernelClockGettime = resolve_symbol(libkernelsys, libkernelsys_mb, "sceKernelClockGettime");
    }

    if (g_bargs.sceKernelGetCurrentFanDuty) {
        *(void**)&sceKernelGetCurrentFanDuty = (void*)g_bargs.sceKernelGetCurrentFanDuty;
    } else if (libkernelsys > 0) {
        *(void**)&sceKernelGetCurrentFanDuty = resolve_symbol(libkernelsys, libkernelsys_mb, "sceKernelGetCurrentFanDuty");
    }

    if (g_bargs.sceNetCtlGetInfo) {
        *(void**)&sceNetCtlGetInfo = (void*)g_bargs.sceNetCtlGetInfo;
    } else {
        uintptr_t libnetctl_mb = 0;
        int libnetctl = get_module_handle(pid, "libSceNetCtl.sprx", &libnetctl_mb);
        if (libnetctl > 0) {
            *(void**)&sceNetCtlGetInfo = resolve_symbol(libnetctl, libnetctl_mb, "sceNetCtlGetInfo");
        }
    }

    if (libkernelsys > 0) {
        *(void**)&sys_ioctl = resolve_symbol(libkernelsys, libkernelsys_mb, "ioctl");
        if (!sys_ioctl) {
            *(void**)&sys_ioctl = resolve_symbol(libkernelsys, libkernelsys_mb, "_ioctl");
        }
    }

    if (g_bargs.sceKernelGetSocClock) {
        *(void**)&sceKernelGetSocClock = (void*)g_bargs.sceKernelGetSocClock;
    } else if (libkernelsys > 0) {
        *(void**)&sceKernelGetSocClock = resolve_symbol(libkernelsys, libkernelsys_mb, "sceKernelGetSocClock");
    }

    if (g_bargs.sceKernelGetSocPowerConsumption) {
        *(void**)&sceKernelGetSocPowerConsumption = (void*)g_bargs.sceKernelGetSocPowerConsumption;
    } else if (libkernelsys > 0) {
        *(void**)&sceKernelGetSocPowerConsumption = resolve_symbol(libkernelsys, libkernelsys_mb, "sceKernelGetSocPowerConsumption");
    }

    if (g_bargs.sceKernelGetCpuCoreClock) {
        *(void**)&sceKernelGetCpuCoreClock = (void*)g_bargs.sceKernelGetCpuCoreClock;
    } else if (libkernelsys > 0) {
        *(void**)&sceKernelGetCpuCoreClock = resolve_symbol(libkernelsys, libkernelsys_mb, "sceKernelGetCpuCoreClock");
    }

    if (g_bargs.sceKernelGetCpuFrequency) {
        *(void**)&sceKernelGetCpuFrequency = (void*)g_bargs.sceKernelGetCpuFrequency;
    } else if (libkernelsys > 0) {
        *(void**)&sceKernelGetCpuFrequency = resolve_symbol(libkernelsys, libkernelsys_mb, "sceKernelGetCpuFrequency");
    }

    LOG_INFO("Telemetry resolved: cpu_temp=%p, soc_temp=%p, pt_stats=%p, fan=%p, ioctl=%p, soc_clk=%p, soc_pwr=%p, cpu_clk=%p, cpu_freq=%p",
             (void*)sceKernelGetCpuTemperature, (void*)sceKernelGetSocSensorTemperature,
             (void*)get_page_table_stats, (void*)sceKernelGetCurrentFanDuty, (void*)sys_ioctl,
             (void*)sceKernelGetSocClock, (void*)sceKernelGetSocPowerConsumption,
             (void*)sceKernelGetCpuCoreClock, (void*)sceKernelGetCpuFrequency);

    return true;
}

struct OverlayHudContext {
    MonoObject* bg_panel = nullptr;
    MonoObject* title_cells[METRIC_COUNT] = {nullptr};
    MonoObject* metric_cells[METRIC_COUNT] = {nullptr};
    MonoObject* metric_val_labels[METRIC_COUNT] = {nullptr};
    MonoObject* sep_cells[METRIC_COUNT] = {nullptr};
    int active_metric_count = 0;
};

static MonoObject* create_hud_cell(MonoDomain* domain, MonoClass* panel_class,
                                   MonoObject* root, const char* name,
                                   float x, float y, float w, float h) {
    MonoObject* cell = mono_object_new(domain, panel_class);
    if (!cell) return nullptr;
    mono_runtime_object_init(cell);

    float safe_w = (w < 200.0f) ? 200.0f : w;
    Set_Property(panel_class, cell, "Name", mono_string_new(domain, name));
    Set_Property(panel_class, cell, "X", x);
    Set_Property(panel_class, cell, "Y", y);
    Set_Property(panel_class, cell, "Width", safe_w);
    Set_Property(panel_class, cell, "Height", h);
    Set_Property(panel_class, cell, "BackgroundVisibility", false);
    Widget_Append_Child(root, cell);
    return cell;
}

static MonoObject* create_hud_label(MonoDomain* domain, MonoClass* label_class,
                                    MonoObject* parent, const char* name,
                                    float x, float y, float w, float h,
                                    const char* text, MonoObject* font,
                                    float r, float g, float b, float a = 1.0f) {
    MonoObject* label = mono_object_new(domain, label_class);
    if (!label) return nullptr;
    mono_runtime_object_init(label);

    float safe_w = (w < 200.0f) ? 200.0f : w;
    Set_Property(label_class, label, "Name", mono_string_new(domain, name));
    Set_Property(label_class, label, "PositionType", 1);
    Set_Property(label_class, label, "MarginLeft", x);
    Set_Property(label_class, label, "MarginTop", y);
    Set_Property(label_class, label, "Width", safe_w);
    Set_Property(label_class, label, "Height", h);
    Set_Property(label_class, label, "Text", mono_string_new(domain, text));
    if (font) {
        Set_Property_Invoke(label_class, label, "Font", font);
    }
    Set_Property(label_class, label, "HorizontalAlignment", 0);
    Set_Property(label_class, label, "VerticalAlignment", 0);
    Set_Property(label_class, label, "FitWidthToText", false);
    Set_Property(label_class, label, "FitHeightToText", true);
    Set_Property(label_class, label, "NumberOfLines", 1);
    Set_Property(label_class, label, "EnableThemedTextShadow", true);

    MonoObject* text_color = CreateUIColor(r, g, b, a);
    if (text_color) {
        Set_Property_Invoke(label_class, label, "TextColor", text_color);
    }

    Widget_Append_Child(parent, label);
    return label;
}

static void destroy_hud(MonoObject* root_widget, OverlayHudContext* ctx, MonoClass* widget_class) {
    if (!ctx) return;
    MonoMethod* remove_from_parent = widget_class ? mono_class_get_method_from_name(widget_class, "RemoveFromParent", 0) : nullptr;
    if (!remove_from_parent) {
        LOG_WARN("Widget.RemoveFromParent method not found!");
        return;
    }

    if (ctx->bg_panel) {
        mono_runtime_invoke(remove_from_parent, ctx->bg_panel, nullptr, nullptr);
        ctx->bg_panel = nullptr;
    }
    for (int i = 0; i < METRIC_COUNT; i++) {
        if (ctx->title_cells[i]) {
            mono_runtime_invoke(remove_from_parent, ctx->title_cells[i], nullptr, nullptr);
            ctx->title_cells[i] = nullptr;
        }
        if (ctx->metric_cells[i]) {
            mono_runtime_invoke(remove_from_parent, ctx->metric_cells[i], nullptr, nullptr);
            ctx->metric_cells[i] = nullptr;
        }
        if (ctx->sep_cells[i]) {
            mono_runtime_invoke(remove_from_parent, ctx->sep_cells[i], nullptr, nullptr);
            ctx->sep_cells[i] = nullptr;
        }
        ctx->metric_val_labels[i] = nullptr;
    }
    ctx->active_metric_count = 0;
    LOG_INFO("Previous HUD destroyed and detached from RootWidget successfully");
}

static void build_hud(MonoDomain* domain, MonoImage* pui_img,
                      MonoClass* widget_class, MonoClass* panel_class, MonoClass* label_class,
                      MonoObject* root_widget, OverlayHudContext* ctx) {
    if (!domain || !root_widget || !panel_class || !label_class || !ctx) return;

    for (int i = 0; i < METRIC_COUNT; i++) {
        ctx->title_cells[i] = nullptr;
        ctx->metric_cells[i] = nullptr;
        ctx->metric_val_labels[i] = nullptr;
        ctx->sep_cells[i] = nullptr;
    }
    ctx->bg_panel = nullptr;
    ctx->active_metric_count = 0;

    if (!g_config.enabled) {
        LOG_INFO("Overlay is disabled in config. Skipping HUD creation.");
        return;
    }

    float bar_y = 0.0f;
    if (g_config.position_vertical == 1) { // Bottom
        bar_y = 1080.0f - g_config.bar_height - g_config.offset_y;
    } else { // Top
        bar_y = g_config.offset_y;
    }

    // 1. Background Panel
    if (g_config.bg_visible) {
        ctx->bg_panel = mono_object_new(domain, panel_class);
        if (ctx->bg_panel) {
            mono_runtime_object_init(ctx->bg_panel);
            Set_Property(panel_class, ctx->bg_panel, "Name", mono_string_new(domain, "id_onion_overlay_bg"));
            Set_Property(panel_class, ctx->bg_panel, "X", 0.0f);
            Set_Property(panel_class, ctx->bg_panel, "Y", bar_y);
            Set_Property(panel_class, ctx->bg_panel, "Width", 1920.0f);
            Set_Property(panel_class, ctx->bg_panel, "Height", g_config.bar_height);

            MonoObject* bg_color = CreateUIColor(g_config.bg_r, g_config.bg_g, g_config.bg_b, g_config.bg_opacity);
            if (bg_color) {
                Set_Property_Invoke(panel_class, ctx->bg_panel, "BackgroundColor", bg_color);
            }
            Set_Property(panel_class, ctx->bg_panel, "BackgroundVisibility", true);
            Set_Property(panel_class, ctx->bg_panel, "BackgroundOpacity", 1.0f);
            Set_Property(panel_class, ctx->bg_panel, "BackgroundStyle", 1);
            Widget_Append_Child(root_widget, ctx->bg_panel);
        }
    }

    // 2. Font
    MonoObject* hud_font = CreateUIFont(g_config.font_size, 1, 900);
    float scale = (float)g_config.font_size / 18.0f;
    float char_w = (float)g_config.font_size * 0.70f;
    float gap_title_val = 8.0f * scale;
    float label_margin_top = (g_config.bar_height - (float)g_config.font_size) * 0.5f - 1.0f;
    if (label_margin_top < 0.0f) label_margin_top = 0.0f;

    // Calculate total width of enabled metrics
    float total_content_w = 0.0f;
    int enabled_count = 0;
    float title_widths[METRIC_COUNT] = {0};
    float val_widths[METRIC_COUNT] = {0};

    for (int i = 0; i < METRIC_COUNT; i++) {
        MetricType mt = (MetricType)g_config.order[i];
        if (mt < 0 || mt >= METRIC_COUNT) continue;
        if (!g_config.metrics[mt].enabled) continue;

        const char* title = "";
        int val_len = 0;
        switch (mt) {
            case METRIC_FPS:      title = "FPS";  val_len = 5; break;
            case METRIC_CPU:      title = "CPU";  val_len = 5; break;
            case METRIC_CPU_LOAD: title = "LOAD"; val_len = 5; break;
            case METRIC_CPU_CLK:  title = "CCLK"; val_len = 9; break;
            case METRIC_GPU:      title = "GPU";  val_len = 5; break;
            case METRIC_GPU_CLK:  title = "GCLK"; val_len = 9; break;
            case METRIC_RAM:      title = "RAM";  val_len = 10; break;
            case METRIC_VRAM:     title = "VRAM"; val_len = 9; break;
            case METRIC_SSD:      title = "SSD";  val_len = 5; break;
            case METRIC_FAN:      title = "FAN";  val_len = 5; break;
            case METRIC_POWER:    title = "PWR";  val_len = 6; break;
            case METRIC_IP:       title = "IP";   val_len = 16; break;
            default: break;
        }
        float t_w = (float)strlen(title) * char_w + 6.0f * scale;
        float v_w = (float)val_len * char_w + 8.0f * scale;

        title_widths[mt] = t_w;
        val_widths[mt] = v_w;

        if (enabled_count > 0) {
            total_content_w += 18.0f * scale; // Separator space
        }
        total_content_w += (t_w + gap_title_val + v_w);
        enabled_count++;
    }

    // Calculate starting X
    float current_x = 24.0f + g_config.offset_x;
    if (g_config.position_horizontal == 1) { // Center
        current_x = (1920.0f - total_content_w) * 0.5f + g_config.offset_x;
    } else if (g_config.position_horizontal == 2) { // Right
        current_x = 1920.0f - 24.0f - total_content_w - g_config.offset_x;
    }
    if (current_x < 10.0f) current_x = 10.0f;

    // 3. Create items in configured order with separate cells for Title and Value
    int placed_count = 0;
    for (int i = 0; i < METRIC_COUNT; i++) {
        MetricType mt = (MetricType)g_config.order[i];
        if (mt < 0 || mt >= METRIC_COUNT) continue;
        if (!g_config.metrics[mt].enabled) continue;

        const char* title = "";
        const char* def_val = "--";
        switch (mt) {
            case METRIC_FPS:      title = "FPS";  def_val = "--"; break;
            case METRIC_CPU:      title = "CPU";  def_val = "--°C"; break;
            case METRIC_CPU_LOAD: title = "LOAD"; def_val = "--%"; break;
            case METRIC_CPU_CLK:  title = "CCLK"; def_val = "---- MHz"; break;
            case METRIC_GPU:      title = "GPU";  def_val = "--°C"; break;
            case METRIC_GPU_CLK:  title = "GCLK"; def_val = "---- MHz"; break;
            case METRIC_RAM:      title = "RAM";  def_val = "-- GB"; break;
            case METRIC_VRAM:     title = "VRAM"; def_val = "-- GB"; break;
            case METRIC_SSD:      title = "SSD";  def_val = "--°C"; break;
            case METRIC_FAN:      title = "FAN";  def_val = "--%"; break;
            case METRIC_POWER:    title = "PWR";  def_val = "--- W"; break;
            case METRIC_IP:       title = "IP";   def_val = "---.---.---.---"; break;
            default: break;
        }

        float t_w = title_widths[mt];
        float v_w = val_widths[mt];

        // 1. Metric Title cell attached directly to root_widget
        char title_cell_name[64];
        snprintf(title_cell_name, sizeof(title_cell_name), "id_%s_tcell", metric_type_to_str(mt));
        MonoObject* tcell = create_hud_cell(domain, panel_class, root_widget, title_cell_name,
                                            current_x, bar_y, t_w, g_config.bar_height);
        ctx->title_cells[mt] = tcell;
        if (tcell) {
            char title_name[64];
            snprintf(title_name, sizeof(title_name), "id_%s_lbl", metric_type_to_str(mt));
            create_hud_label(domain, label_class, tcell, title_name,
                             0.0f, label_margin_top, t_w, g_config.bar_height,
                             title, hud_font,
                             g_config.metrics[mt].r, g_config.metrics[mt].g, g_config.metrics[mt].b, 1.0f);
        }
        current_x += t_w + gap_title_val;

        // 2. Metric Value cell attached directly to root_widget
        char val_cell_name[64];
        snprintf(val_cell_name, sizeof(val_cell_name), "id_%s_vcell", metric_type_to_str(mt));
        MonoObject* vcell = create_hud_cell(domain, panel_class, root_widget, val_cell_name,
                                            current_x, bar_y, v_w, g_config.bar_height);
        ctx->metric_cells[mt] = vcell;
        if (vcell) {
            char val_name[64];
            snprintf(val_name, sizeof(val_name), "id_%s_val", metric_type_to_str(mt));
            ctx->metric_val_labels[mt] = create_hud_label(domain, label_class, vcell, val_name,
                                                          0.0f, label_margin_top, v_w, g_config.bar_height,
                                                          def_val, hud_font,
                                                          1.0f, 1.0f, 1.0f, 1.0f);
        }
        current_x += v_w;

        placed_count++;

        // 3. Separator cell attached directly to root_widget (if not last item)
        if (placed_count < enabled_count) {
            float sep_w = 18.0f * scale;
            char sep_cell_name[64];
            snprintf(sep_cell_name, sizeof(sep_cell_name), "id_sep%d_cell", i);
            MonoObject* sep_cell = create_hud_cell(domain, panel_class, root_widget, sep_cell_name,
                                                   current_x, bar_y, sep_w, g_config.bar_height);
            ctx->sep_cells[mt] = sep_cell;
            if (sep_cell) {
                char sep_name[64];
                snprintf(sep_name, sizeof(sep_name), "id_sep%d_lbl", i);
                create_hud_label(domain, label_class, sep_cell, sep_name,
                                 4.0f * scale, label_margin_top, sep_w, g_config.bar_height,
                                 "|", hud_font,
                                 0.60f, 0.60f, 0.60f, 0.8f);
            }
            current_x += sep_w;
        }
    }

    ctx->active_metric_count = placed_count;
    LOG_INFO("HUD built with %d active metrics at Y=%.1f (scale=%.2f)", placed_count, bar_y, scale);
}

static float parse_float_simple(const char* s) {
    if (!s) return 0.0f;
    float res = 0.0f;
    float fact = 1.0f;
    bool point = false;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    for (; *s; s++) {
        if (*s == '.') {
            point = true;
            continue;
        }
        int d = *s - '0';
        if (d >= 0 && d <= 9) {
            if (point) fact /= 10.0f;
            res = res * 10.0f + (float)d;
        } else {
            break;
        }
    }
    return res * fact;
}

struct OrbisKernelTimespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};

struct Proc_Stats {
    int32_t lo_data;
    uint32_t td_tid;
    OrbisKernelTimespec user_cpu_usage_time;
    OrbisKernelTimespec system_cpu_usage_time;
};

struct thread_usages {
    OrbisKernelTimespec current_time;
    int Thread_Count;
    char padding0[0x4];
    Proc_Stats Threads[3072];
};

static Proc_Stats s_stat_data[3072];
static thread_usages s_thread_data[2];
static unsigned int s_idle_tid[8] = {0};
static bool s_idle_tid_discovered = false;
static int s_current_bank = 0;
static float s_cpu_usage_pct = 0.0f;

static float sample_cpu_load(void) {
    if (!sceKernelGetCpuUsage) return 0.0f;

    // Discover idle thread IDs if not yet discovered
    if (!s_idle_tid_discovered) {
        int count = 3072;
        if (sceKernelGetCpuUsage(s_stat_data, &count) == 0 && count > 0) {
            char tname[64];
            int core = 0;
            int found = 0;
            for (int i = 0; i < count; i++) {
                if (sceKernelGetThreadName && sceKernelGetThreadName(s_stat_data[i].td_tid, tname) == 0) {
                    if (sscanf(tname, "SceIdleCpu%d", &core) == 1 && core >= 0 && core < 8) {
                        s_idle_tid[core] = s_stat_data[i].td_tid;
                        found++;
                    }
                }
            }
            if (found >= 4) s_idle_tid_discovered = true;
        }
    }

    s_thread_data[s_current_bank].Thread_Count = 3072;
    if (sceKernelGetCpuUsage(s_thread_data[s_current_bank].Threads, &s_thread_data[s_current_bank].Thread_Count) != 0) {
        return s_cpu_usage_pct;
    }

    if (sceKernelClockGettime) {
        sceKernelClockGettime(0 /* CLOCK_REALTIME */, &s_thread_data[s_current_bank].current_time);
    } else {
        __syscall(232 /* SYS_clock_gettime */, 0, &s_thread_data[s_current_bank].current_time);
    }

    int other_bank = !s_current_bank;
    s_current_bank = other_bank;

    if (s_thread_data[other_bank].Thread_Count <= 0) {
        return s_cpu_usage_pct;
    }

    thread_usages* cur = &s_thread_data[!s_current_bank];
    thread_usages* prev = &s_thread_data[s_current_bank];

    double dt = (double)(cur->current_time.tv_sec - prev->current_time.tv_sec) +
                (double)(cur->current_time.tv_nsec - prev->current_time.tv_nsec) * 1e-9;
    if (dt <= 0.05) return s_cpu_usage_pct;

    if (s_idle_tid_discovered) {
        double total_idle_time = 0.0;
        int idle_found = 0;
        for (int c = 0; c < 8; c++) {
            uint32_t itid = s_idle_tid[c];
            if (itid == 0) continue;

            Proc_Stats* cur_p = nullptr;
            Proc_Stats* prev_p = nullptr;
            for (int i = 0; i < cur->Thread_Count; i++) {
                if (cur->Threads[i].td_tid == itid) { cur_p = &cur->Threads[i]; break; }
            }
            for (int i = 0; i < prev->Thread_Count; i++) {
                if (prev->Threads[i].td_tid == itid) { prev_p = &prev->Threads[i]; break; }
            }
            if (cur_p && prev_p) {
                double cur_time = (double)cur_p->user_cpu_usage_time.tv_sec + (double)cur_p->user_cpu_usage_time.tv_nsec * 1e-9 +
                                  (double)cur_p->system_cpu_usage_time.tv_sec + (double)cur_p->system_cpu_usage_time.tv_nsec * 1e-9;
                double prev_time = (double)prev_p->user_cpu_usage_time.tv_sec + (double)prev_p->user_cpu_usage_time.tv_nsec * 1e-9 +
                                   (double)prev_p->system_cpu_usage_time.tv_sec + (double)prev_p->system_cpu_usage_time.tv_nsec * 1e-9;
                double d_idle = cur_time - prev_time;
                if (d_idle >= 0.0) {
                    total_idle_time += d_idle;
                    idle_found++;
                }
            }
        }
        if (idle_found > 0) {
            double avg_idle = total_idle_time / (dt * (double)idle_found);
            if (avg_idle > 1.0) avg_idle = 1.0;
            if (avg_idle < 0.0) avg_idle = 0.0;
            s_cpu_usage_pct = (float)((1.0 - avg_idle) * 100.0);
            return s_cpu_usage_pct;
        }
    }

    // Fallback: sum active thread delta
    double total_active = 0.0;
    for (int i = 0; i < cur->Thread_Count; i++) {
        uint32_t tid = cur->Threads[i].td_tid;
        for (int j = 0; j < prev->Thread_Count; j++) {
            if (prev->Threads[j].td_tid == tid) {
                double cur_t = (double)cur->Threads[i].user_cpu_usage_time.tv_sec + (double)cur->Threads[i].user_cpu_usage_time.tv_nsec * 1e-9 +
                               (double)cur->Threads[i].system_cpu_usage_time.tv_sec + (double)cur->Threads[i].system_cpu_usage_time.tv_nsec * 1e-9;
                double prev_t = (double)prev->Threads[j].user_cpu_usage_time.tv_sec + (double)prev->Threads[j].user_cpu_usage_time.tv_nsec * 1e-9 +
                                (double)prev->Threads[j].system_cpu_usage_time.tv_sec + (double)prev->Threads[j].system_cpu_usage_time.tv_nsec * 1e-9;
                double delta = cur_t - prev_t;
                if (delta > 0.0) total_active += delta;
                break;
            }
        }
    }
    double load = (total_active / (dt * 8.0)) * 100.0;
    if (load > 100.0) load = 100.0;
    if (load < 0.0) load = 0.0;
    s_cpu_usage_pct = (float)load;
    return s_cpu_usage_pct;
}

struct DceIoctlArg {
    uint64_t selector;
    uint64_t mask;
    uint64_t output;
    uint64_t reserved[3];
};

static int s_dce_fd = -1;
static uint64_t s_prev_flip_count = 0;
static uint64_t s_prev_time_ns = 0;
static float s_dce_fps = 0.0f;
static bool s_dce_logged_open = false;
static bool s_dce_logged_ioctl = false;
static int s_fps_source_logged = -1;

static float sample_dce_hardware_fps(void) {
    if (!ptr_syscall) return 0.0f;
    if (s_dce_fd < 0 && !s_dce_logged_open) {
        s_dce_fd = (int)__syscall(5 /* SYS_open */, "/dev/dce", 2 /* O_RDWR */, 0);
        if (s_dce_fd < 0) {
            s_dce_fd = (int)__syscall(5 /* SYS_open */, "/dev/dce", 0 /* O_RDONLY */, 0);
        }
        LOG_INFO("DCE: open(/dev/dce) -> fd=%d", s_dce_fd);
        s_dce_logged_open = true;
    }
    if (s_dce_fd >= 0) {
        uint8_t out[0x60];
        memset(out, 0, sizeof(out));

        DceIoctlArg arg;
        memset(&arg, 0, sizeof(arg));
        arg.selector = 0x10000000AULL;
        arg.mask = 0x8000000000ULL;
        arg.output = (uint64_t)out;

        int rc = -1;
        if (sys_ioctl) {
            rc = sys_ioctl(s_dce_fd, 0x80308217UL, &arg);
            if (rc != 0) {
                rc = sys_ioctl(s_dce_fd, (unsigned long)0xFFFFFFFF80308217ULL, &arg);
            }
        }
        if (rc != 0) {
            long sys_rc = __syscall(54 /* SYS_ioctl */, s_dce_fd, 0x80308217UL, &arg);
            if (sys_rc != 0) {
                sys_rc = __syscall(54 /* SYS_ioctl */, s_dce_fd, 0xFFFFFFFF80308217UL, &arg);
            }
            if (sys_rc == 0) rc = 0;
        }

        if (!s_dce_logged_ioctl) {
            uint64_t initial_count = *(uint64_t*)(out + 8);
            LOG_INFO("DCE: ioctl result rc=%d, initial flip_count=%llu (sys_ioctl=%p)",
                     rc, (unsigned long long)initial_count, (void*)sys_ioctl);
            s_dce_logged_ioctl = true;
        }

        if (rc == 0) {
            uint64_t flip_count = *(uint64_t*)(out + 8);
            struct { long tv_sec; long tv_nsec; } ts = {0, 0};
            // FreeBSD CLOCK_MONOTONIC is 4 (not 1!). 1 is CLOCK_VIRTUAL.
            if (sceKernelClockGettime) {
                sceKernelClockGettime(4 /* CLOCK_MONOTONIC */, &ts);
            } else {
                __syscall(232 /* SYS_clock_gettime */, 4, &ts);
            }
            if (ts.tv_sec == 0 && ts.tv_nsec == 0) {
                if (sceKernelClockGettime) {
                    sceKernelClockGettime(0 /* CLOCK_REALTIME */, &ts);
                } else {
                    __syscall(232, 0, &ts);
                }
            }
            uint64_t now_ns = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;

            if (s_prev_time_ns != 0 && now_ns > s_prev_time_ns) {
                double dt = (double)(now_ns - s_prev_time_ns) * 1e-9;
                if (dt >= 0.20) {
                    if (flip_count >= s_prev_flip_count) {
                        uint64_t delta = flip_count - s_prev_flip_count;
                        float raw = (float)((double)delta / dt);
                        if (raw >= 0.0f && raw <= 245.0f) {
                            s_dce_fps = raw;
                        }
                    }
                    s_prev_flip_count = flip_count;
                    s_prev_time_ns = now_ns;
                }
            } else {
                s_prev_flip_count = flip_count;
                s_prev_time_ns = now_ns;
            }
            return s_dce_fps;
        }
    }
    return 0.0f;
}

static float get_fps_reading(void) {
    if (!ptr_syscall) return 0.0f;

    // 1. Try reading OnionHEN / kstuff seqlock sampler file
    const char* sample_paths[] = {
        "/system_tmp/onionhen/fps_sample",
        "/system_tmp/fps_sample"
    };
    for (size_t i = 0; i < sizeof(sample_paths) / sizeof(sample_paths[0]); i++) {
        long fd = __syscall(5, sample_paths[i], 0 /* O_RDONLY */, 0);
        if (fd >= 0) {
            uint8_t buf[128] = {0};
            long n = __syscall(3, fd, buf, sizeof(buf));
            __syscall(6, fd);
            if (n >= 32) {
                uint32_t magic = *(uint32_t*)buf;
                if (magic == 0x4F465053u) { /* 'OFPS' */
                    uint8_t valid = buf[12];
                    if (valid) {
                        float fps = *(float*)(buf + 16);
                        if (fps > 0.0f && fps <= 245.0f) {
                            if (s_fps_source_logged != 1) {
                                LOG_INFO("FPS source: seqlock file %s (fps=%.1f)", sample_paths[i], fps);
                                s_fps_source_logged = 1;
                            }
                            return fps;
                        }
                    }
                }
            }
        }
    }

    // 2. Try reading /system_tmp or /data txt files
    const char* txt_paths[] = {
        "/system_tmp/ps5_fps.txt",
        "/system_tmp/fps.txt",
        "/data/overlay/fps.txt"
    };
    for (size_t i = 0; i < sizeof(txt_paths) / sizeof(txt_paths[0]); i++) {
        long fd = __syscall(5, txt_paths[i], 0 /* O_RDONLY */, 0);
        if (fd >= 0) {
            char txt[32] = {0};
            long n = __syscall(3, fd, txt, sizeof(txt) - 1);
            __syscall(6, fd);
            if (n > 0) {
                float f = parse_float_simple(txt);
                if (f > 0.0f && f <= 245.0f) {
                    if (s_fps_source_logged != 2) {
                        LOG_INFO("FPS source: txt file %s (fps=%.1f)", txt_paths[i], f);
                        s_fps_source_logged = 2;
                    }
                    return f;
                }
            }
        }
    }

    // 3. Hardware Display Controller Engine fallback (/dev/dce flip counter)
    float dce_fps = sample_dce_hardware_fps();
    if (dce_fps > 0.0f && dce_fps <= 245.0f) {
        if (s_fps_source_logged != 3) {
            LOG_INFO("FPS source: /dev/dce hardware flips active (fps=%.1f)", dce_fps);
            s_fps_source_logged = 3;
        }
        return dce_fps;
    }

    return 0.0f;
}

static void* payload_thread_main(void* arg) {
    (void)arg;
    // Allow SceShellUI threads to settle after ptrace detach before touching internal Mono state
    usleep(300000); // 300ms safety stabilization delay

    const pid_t pid = getpid();
    LOG_INFO("PS5 Overlay Payload thread started in SceShellUI (PID: %d)", pid);

    uintptr_t libkernelsys_mb = 0;
    LOG_INFO("Getting module handle for libkernel_sys.sprx...");
    int libkernelsys = get_module_handle(pid, "libkernel_sys.sprx", &libkernelsys_mb);
    if (libkernelsys <= 0) {
        LOG_ERROR("Failed to get module handle for libkernel_sys.sprx");
        return nullptr;
    }
    LOG_INFO("libkernel_sys.sprx: handle=%d, mapbase=0x%lx", libkernelsys, (unsigned long)libkernelsys_mb);

    if (g_bargs.mprotect_addr) {
        sceKernelMprotect = (int(*)(void*, size_t, int))g_bargs.mprotect_addr;
    } else {
        *(void**)&sceKernelMprotect = resolve_symbol(libkernelsys, libkernelsys_mb, "sceKernelMprotect");
    }
    LOG_INFO("sceKernelMprotect resolved: %p", (void*)sceKernelMprotect);

    char probe[100];
    has_hv_bypass = (sceKernelMprotect && sceKernelMprotect(probe, sizeof(probe), 7) == 0);
    LOG_INFO("sceKernelMprotect test: has_hv_bypass=%d", has_hv_bypass ? 1 : 0);

    LOG_INFO("Resolving telemetry symbols...");
    resolve_telemetry_symbols(pid, libkernelsys, libkernelsys_mb);

    uintptr_t libmono_mb = 0;
    LOG_INFO("Getting module handle for libmonosgen-2.0.sprx...");
    int libmono = get_module_handle(pid, "libmonosgen-2.0.sprx", &libmono_mb);
    if (libmono <= 0 && libmono_mb == 0) {
        LOG_ERROR("Failed to get module handle for libmonosgen-2.0.sprx");
        return nullptr;
    }
    LOG_INFO("libmonosgen-2.0.sprx: handle=%d, mapbase=0x%lx", libmono, (unsigned long)libmono_mb);
    g_libmono_mapbase = libmono_mb;

    LOG_INFO("Resolving mono symbols...");
    if (!resolve_mono_symbols(libmono, libmono_mb)) {
        LOG_ERROR("Failed to resolve mono symbols");
        return nullptr;
    }
    LOG_INFO("Mono symbols resolved successfully");

    // Critical: Attach native worker pthread to Mono runtime before calling Mono functions
    if (!Root_Domain) {
        Root_Domain = mono_get_root_domain();
    }
    if (!Root_Domain) {
        LOG_ERROR("Root domain is NULL!");
        return nullptr;
    }
    LOG_INFO("Root Domain: %p. Attaching worker pthread...", Root_Domain);
    mono_thread_attach(Root_Domain);
    LOG_INFO("Attached worker pthread to Mono Root Domain (%p)", Root_Domain);

    // Load Core dll and patch main thread check
    LOG_INFO("Loading Sce.PlayStation.Core.dll...");
    Core_img = getDLLimage("Sce.PlayStation.Core.dll");
    if (!Core_img) {
        LOG_ERROR("Failed to load Sce.PlayStation.Core.dll");
        return nullptr;
    }
    LOG_INFO("Patching Main thread Check in Core.dll...");
    Patch_Main_thread_Check(Core_img);

    LOG_INFO("Loading PUI and AppSystem assemblies...");
    for (int retry = 0; retry < 15 && (!pui_img || !AppSystem_img); retry++) {
        if (!pui_img) pui_img = getDLLimage("Sce.PlayStation.PUI.dll");
        if (!AppSystem_img) AppSystem_img = getDLLimage("Sce.Vsh.ShellUI.AppSystem.dll");
        if (!pui_img || !AppSystem_img) {
            LOG_INFO("Waiting for PUI / AppSystem assemblies (%d/15)...", retry + 1);
            sleep(1);
        }
    }

    if (!pui_img || !AppSystem_img) {
        LOG_ERROR("Failed to load required Mono assemblies (pui=%p, AppSystem=%p)", pui_img, AppSystem_img);
        return nullptr;
    }
    LOG_INFO("Assemblies loaded: pui=%p, AppSystem=%p", pui_img, AppSystem_img);

    MonoClass* layer_mgr_class = mono_class_from_name(AppSystem_img, "Sce.Vsh.ShellUI.AppSystem", "LayerManager");
    MonoClass* scene_class = mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Scene");
    MonoClass* widget_class = mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Widget");
    MonoClass* panel_class = mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Panel");
    MonoClass* label_class = mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Label");

    if (!layer_mgr_class || !scene_class || !widget_class || !panel_class || !label_class) {
        LOG_ERROR("Required PUI classes not found in assemblies!");
        return nullptr;
    }

    MonoMethod* find_scene = mono_class_get_method_from_name(layer_mgr_class, "FindContainerSceneByPath", 1);
    MonoProperty* root_prop = mono_class_get_property_from_name(scene_class, "RootWidget");
    MonoMethod* get_root = root_prop ? mono_property_get_get_method(root_prop) : nullptr;

    if (!find_scene || !get_root) {
        LOG_ERROR("FindContainerSceneByPath or RootWidget property getter not found!");
        return nullptr;
    }

    // Signal readiness via /system_tmp/ps5_overlay_ready
    long rfd = __syscall(5, "/system_tmp/ps5_overlay_ready", 0x0209 /* O_WRONLY|O_CREAT|O_TRUNC */, 0666);
    if (rfd >= 0) {
        char pid_str[16];
        int plen = snprintf(pid_str, sizeof(pid_str), "%d\n", pid);
        if (plen > 0) __syscall(4, rfd, pid_str, plen);
        __syscall(6, rfd);
    }

    // Load persistent config from /data/overlay/config.ini (or create default)
    config_load(&g_config, "/data/overlay/config.ini");

    // Launch embedded HTTP web server on non-standard port (default 8585, configurable in config.ini)
    int web_port = (g_config.web_port >= 1024 && g_config.web_port <= 65535) ? g_config.web_port : 8585;
    if (g_bargs.pthread_create_addr) {
        LOG_INFO("Starting embedded HTTP web settings server on port %d...", web_port);
        web_server_start(web_port, g_bargs.pthread_create_addr);
    } else {
        LOG_WARN("pthread_create_addr is null; cannot start web server");
    }

    LOG_INFO("Overlay ready! Entering continuous game monitoring loop in SceShellUI...");

    OverlayHudContext hud_ctx;
    bool attached_to_game = false;
    MonoObject* last_attached_scene = nullptr;
    MonoObject* root_widget = nullptr;

    while (true) {
        if (g_should_unload) {
            LOG_INFO("Unload requested via Web UI! Cleaning up HUD and exiting payload thread...");
            if (attached_to_game && root_widget) {
                destroy_hud(root_widget, &hud_ctx, widget_class);
            }
            if (s_dce_fd >= 0) {
                __syscall(6 /* SYS_close */, s_dce_fd);
                s_dce_fd = -1;
            }
            web_server_stop();
            __syscall(10 /* SYS_unlink */, "/system_tmp/ps5_overlay_ready");
            LOG_INFO("PS5 Overlay payload thread exiting cleanly.");
            return nullptr;
        }

        static MonoString* s_game_str = nullptr;
        if (!s_game_str) {
            s_game_str = mono_string_new(Root_Domain, "Game");
        }
        void* scene_args[1] = { s_game_str };
        MonoObject* exc = nullptr;
        MonoObject* game_scene = mono_runtime_invoke(find_scene, nullptr, scene_args, &exc);

        if (game_scene && game_scene != last_attached_scene) {
            LOG_INFO("Found active Game container scene: %p", game_scene);
            if (attached_to_game && root_widget) {
                destroy_hud(root_widget, &hud_ctx, widget_class);
            }
            root_widget = mono_runtime_invoke(get_root, game_scene, nullptr, &exc);

            if (root_widget) {
                LOG_INFO("Game RootWidget: %p. Building dynamic HUD...", root_widget);
                build_hud(Root_Domain, pui_img, widget_class, panel_class, label_class, root_widget, &hud_ctx);
                last_attached_scene = game_scene;
                attached_to_game = true;
                g_config_changed = false;
                LOG_INFO("HUD attached to Game Scene RootWidget successfully!");
            }
        } else if (!game_scene && attached_to_game) {
            /* Game closed */
            LOG_INFO("Game closed, cleaning up HUD...");
            if (root_widget) {
                destroy_hud(root_widget, &hud_ctx, widget_class);
            }
            attached_to_game = false;
            last_attached_scene = nullptr;
            root_widget = nullptr;
        }

        /* Check for live configuration changes requested via Web Settings */
        if (attached_to_game && g_config_changed && root_widget) {
            LOG_INFO("Dynamic configuration change detected! Rebuilding HUD...");
            destroy_hud(root_widget, &hud_ctx, widget_class);
            build_hud(Root_Domain, pui_img, widget_class, panel_class, label_class, root_widget, &hud_ctx);
            g_config_changed = false;
            LOG_INFO("Dynamic HUD rebuild completed");
        }

        /* Update metrics if attached and overlay is enabled */
        if (attached_to_game && g_config.enabled) {
            char buf[32];

            static bool s_logged_hwinfo_sample = false;
            if (!s_logged_hwinfo_sample) {
                LOG_INFO("HWINFO first sample pointers: soc_clk=%p, soc_pwr=%p, cpu_core_clk=%p, cpu_freq=%p",
                         (void*)sceKernelGetSocClock, (void*)sceKernelGetSocPowerConsumption,
                         (void*)sceKernelGetCpuCoreClock, (void*)sceKernelGetCpuFrequency);
                if (sceKernelGetSocClock) {
                    uint32_t dbg_clks[32] = {0};
                    int rc = sceKernelGetSocClock(dbg_clks);
                    LOG_INFO("sceKernelGetSocClock rc=%d, GFXCLK[20]=%u MHz, UCLK[23]=%u, FCLK[24]=%u",
                             rc, dbg_clks[20], dbg_clks[23], dbg_clks[24]);
                }
                if (sceKernelGetCpuCoreClock) {
                    int dbg_cc[16] = {0};
                    int rc = sceKernelGetCpuCoreClock(dbg_cc);
                    LOG_INFO("sceKernelGetCpuCoreClock rc=%d, core0=%d MHz, core1=%d MHz", rc, dbg_cc[0], dbg_cc[1]);
                }
                if (sceKernelGetCpuFrequency) {
                    long hz = sceKernelGetCpuFrequency();
                    LOG_INFO("sceKernelGetCpuFrequency hz=%ld", hz);
                }
                if (sceKernelGetSocPowerConsumption) {
                    uint32_t pwr_dbg[28] = {0};
                    int rc = sceKernelGetSocPowerConsumption(pwr_dbg);
                    LOG_INFO("sceKernelGetSocPowerConsumption rc=%d, lane0_mw=%u, lane1_mw=%u, lane2_mw=%u",
                             rc, pwr_dbg[0], pwr_dbg[3], pwr_dbg[6]);
                }
                if (sceKernelGetSocSensorTemperature) {
                    int s0 = -1, s1 = -1, s2 = -1, s3 = -1;
                    sceKernelGetSocSensorTemperature(0, &s0);
                    sceKernelGetSocSensorTemperature(1, &s1);
                    sceKernelGetSocSensorTemperature(2, &s2);
                    sceKernelGetSocSensorTemperature(3, &s3);
                    LOG_INFO("SoC Sensors: ch0(GPU)=%d, ch1=%d, ch2(M.2 SSD)=%d, ch3=%d", s0, s1, s2, s3);
                }
                s_logged_hwinfo_sample = true;
            }

            // FPS
            if (hud_ctx.metric_val_labels[METRIC_FPS]) {
                float fps = get_fps_reading();
                if (fps > 0.5f) {
                    snprintf(buf, sizeof(buf), "%.0f", fps);
                } else {
                    snprintf(buf, sizeof(buf), "--");
                }
                Set_Property(label_class, hud_ctx.metric_val_labels[METRIC_FPS], "Text", mono_string_new(Root_Domain, buf));
            }

            // CPU
            if (hud_ctx.metric_val_labels[METRIC_CPU]) {
                int cpu_temp = 0;
                if (sceKernelGetCpuTemperature) {
                    sceKernelGetCpuTemperature(&cpu_temp);
                }
                snprintf(buf, sizeof(buf), "%d°C", cpu_temp);
                Set_Property(label_class, hud_ctx.metric_val_labels[METRIC_CPU], "Text", mono_string_new(Root_Domain, buf));
            }

            // CPU Load %
            if (hud_ctx.metric_val_labels[METRIC_CPU_LOAD]) {
                float load = sample_cpu_load();
                snprintf(buf, sizeof(buf), "%.0f%%", load);
                Set_Property(label_class, hud_ctx.metric_val_labels[METRIC_CPU_LOAD], "Text", mono_string_new(Root_Domain, buf));
            }

            // CPU Clock (CCLK)
            if (hud_ctx.metric_val_labels[METRIC_CPU_CLK]) {
                int cpu_mhz = 0;
                int core_clocks[16] = {0};
                if (sceKernelGetCpuCoreClock && sceKernelGetCpuCoreClock(core_clocks) == 0) {
                    for (int c = 0; c < 16; c++) {
                        if (core_clocks[c] > cpu_mhz) cpu_mhz = core_clocks[c];
                    }
                }
                if (cpu_mhz <= 0 && sceKernelGetCpuFrequency) {
                    long hz = sceKernelGetCpuFrequency();
                    if (hz > 0) cpu_mhz = (int)(hz / 1000000L);
                }
                if (cpu_mhz > 0) {
                    snprintf(buf, sizeof(buf), "%d MHz", cpu_mhz);
                } else {
                    snprintf(buf, sizeof(buf), "-- MHz");
                }
                Set_Property(label_class, hud_ctx.metric_val_labels[METRIC_CPU_CLK], "Text", mono_string_new(Root_Domain, buf));
            }

            // GPU
            if (hud_ctx.metric_val_labels[METRIC_GPU]) {
                int gpu_temp = 0;
                if (sceKernelGetSocSensorTemperature) {
                    sceKernelGetSocSensorTemperature(0, &gpu_temp);
                }
                snprintf(buf, sizeof(buf), "%d°C", gpu_temp);
                Set_Property(label_class, hud_ctx.metric_val_labels[METRIC_GPU], "Text", mono_string_new(Root_Domain, buf));
            }

            // GPU Clock (GCLK)
            if (hud_ctx.metric_val_labels[METRIC_GPU_CLK]) {
                uint32_t gpu_mhz = 0;
                if (sceKernelGetSocClock) {
                    uint32_t soc_clocks[32] = {0};
                    if (sceKernelGetSocClock(soc_clocks) == 0) {
                        gpu_mhz = soc_clocks[20]; // GFXCLK
                    }
                }
                if (gpu_mhz > 0) {
                    snprintf(buf, sizeof(buf), "%u MHz", gpu_mhz);
                } else {
                    snprintf(buf, sizeof(buf), "-- MHz");
                }
                Set_Property(label_class, hud_ctx.metric_val_labels[METRIC_GPU_CLK], "Text", mono_string_new(Root_Domain, buf));
            }

            // RAM
            if (hud_ctx.metric_val_labels[METRIC_RAM]) {
                int ram_total = 0, ram_free = 0;
                if (get_page_table_stats && get_page_table_stats(1, 1, &ram_total, &ram_free) == 0 && ram_total > 0) {
                    int used_mb = ram_total - ram_free;
                    snprintf(buf, sizeof(buf), "%.1f/16 GB", (float)used_mb / 1024.0f);
                } else {
                    snprintf(buf, sizeof(buf), "N/A");
                }
                Set_Property(label_class, hud_ctx.metric_val_labels[METRIC_RAM], "Text", mono_string_new(Root_Domain, buf));
            }

            // VRAM
            if (hud_ctx.metric_val_labels[METRIC_VRAM]) {
                int vram_total = 0, vram_free = 0;
                if (get_page_table_stats && get_page_table_stats(1, 2, &vram_total, &vram_free) == 0 && vram_total > 0) {
                    int used_mb = vram_total - vram_free;
                    snprintf(buf, sizeof(buf), "%.1f GB", (float)used_mb / 1024.0f);
                } else {
                    snprintf(buf, sizeof(buf), "N/A");
                }
                Set_Property(label_class, hud_ctx.metric_val_labels[METRIC_VRAM], "Text", mono_string_new(Root_Domain, buf));
            }

            // SSD (M.2 NVMe Temp via SoC Sensor 2)
            if (hud_ctx.metric_val_labels[METRIC_SSD]) {
                int ssd_temp = 0;
                if (sceKernelGetSocSensorTemperature &&
                    sceKernelGetSocSensorTemperature(2, &ssd_temp) == 0 &&
                    ssd_temp >= 20 && ssd_temp <= 130) {
                    snprintf(buf, sizeof(buf), "%d°C", ssd_temp);
                } else {
                    snprintf(buf, sizeof(buf), "N/A");
                }
                Set_Property(label_class, hud_ctx.metric_val_labels[METRIC_SSD], "Text", mono_string_new(Root_Domain, buf));
            }

            // FAN
            if (hud_ctx.metric_val_labels[METRIC_FAN]) {
                uint16_t fan_duty = 0;
                uint64_t chassis = 0;
                double fan_pct = 0.0;
                if (sceKernelGetCurrentFanDuty && sceKernelGetCurrentFanDuty(&fan_duty, &chassis) == 0) {
                    fan_pct = ((double)fan_duty * 100.0) / 1024.0;
                }
                snprintf(buf, sizeof(buf), "%.0f%%", fan_pct);
                Set_Property(label_class, hud_ctx.metric_val_labels[METRIC_FAN], "Text", mono_string_new(Root_Domain, buf));
            }

            // POWER (SoC Power in Watts)
            if (hud_ctx.metric_val_labels[METRIC_POWER]) {
                float power_w = 0.0f;
                if (sceKernelGetSocPowerConsumption) {
                    struct {
                        uint32_t u32[28];
                    } sample;
                    memset(&sample, 0, sizeof(sample));
                    if (sceKernelGetSocPowerConsumption(&sample) == 0) {
                        uint32_t total_mw = 0;
                        for (int lane = 0; lane < 8; ++lane) {
                            total_mw += sample.u32[lane * 3];
                        }
                        if (total_mw > 0) {
                            power_w = (float)total_mw / 1000.0f;
                        }
                    }
                }
                if (power_w > 0.1f) {
                    snprintf(buf, sizeof(buf), "%.0f W", power_w);
                } else {
                    snprintf(buf, sizeof(buf), "-- W");
                }
                Set_Property(label_class, hud_ctx.metric_val_labels[METRIC_POWER], "Text", mono_string_new(Root_Domain, buf));
            }

            // IP
            if (hud_ctx.metric_val_labels[METRIC_IP]) {
                union {
                    char ip_address[16];
                    char buffer[256];
                } info;
                memset(&info, 0, sizeof(info));
                if (sceNetCtlGetInfo && sceNetCtlGetInfo(14 /* SCE_NET_CTL_INFO_IP_ADDRESS */, &info) == 0 && info.ip_address[0] != '\0') {
                    info.ip_address[15] = '\0';
                    snprintf(buf, sizeof(buf), "%s", info.ip_address);
                } else {
                    snprintf(buf, sizeof(buf), "Offline");
                }
                Set_Property(label_class, hud_ctx.metric_val_labels[METRIC_IP], "Text", mono_string_new(Root_Domain, buf));
            }
        }

        sleep(1);
    }

    return nullptr;
}

// ---------------------------------------------------------------------------
// Standalone built-in memory and string primitives
// ---------------------------------------------------------------------------

extern "C" {
__attribute__((visibility("default"), noinline))
void* memset(void* dest, int c, size_t n) {
    volatile unsigned char* d = (volatile unsigned char*)dest;
    while (n--) *d++ = (unsigned char)c;
    return dest;
}

__attribute__((visibility("default"), noinline))
void* memcpy(void* dest, const void* src, size_t n) {
    volatile unsigned char* d = (volatile unsigned char*)dest;
    const volatile unsigned char* s = (const volatile unsigned char*)src;
    while (n--) *d++ = *s++;
    return dest;
}

__attribute__((visibility("default"), noinline))
void* memmove(void* dest, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else if (d > s) {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dest;
}

__attribute__((visibility("default"), noinline))
int memcmp(const void* s1, const void* s2, size_t n) {
    const unsigned char* p1 = (const unsigned char*)s1;
    const unsigned char* p2 = (const unsigned char*)s2;
    while (n--) {
        if (*p1 != *p2) return (int)*p1 - (int)*p2;
        p1++; p2++;
    }
    return 0;
}

__attribute__((visibility("default"), noinline))
size_t strlen(const char* s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

__attribute__((visibility("default"), noinline))
int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++; s2++;
    }
    return (int)(unsigned char)*s1 - (int)(unsigned char)*s2;
}

__attribute__((visibility("default"), noinline))
int strncmp(const char* s1, const char* s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) {
        s1++; s2++; n--;
    }
    if (n == 0) return 0;
    return (int)(unsigned char)*s1 - (int)(unsigned char)*s2;
}

__attribute__((visibility("default"), noinline))
char* strncpy(char* dest, const char* src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    return dest;
}

__attribute__((visibility("default"), noinline))
char* strchr(const char* s, int c) {
    while (*s != (char)c) {
        if (!*s++) return nullptr;
    }
    return (char*)s;
}

__attribute__((visibility("default"), noinline))
char* strstr(const char* haystack, const char* needle) {
    if (!*needle) return (char*)haystack;
    for (; *haystack; haystack++) {
        if (*haystack == *needle) {
            const char* h = haystack;
            const char* n = needle;
            while (*h && *n && *h == *n) {
                h++; n++;
            }
            if (!*n) return (char*)haystack;
        }
    }
    return nullptr;
}

__attribute__((visibility("default"), noinline))
int kernel_mprotect(pid_t pid, intptr_t addr, size_t size, int prot) {
    (void)pid;
    if (sceKernelMprotect) {
        return sceKernelMprotect((void*)addr, size, prot);
    }
    return 0;
}
}

// ---------------------------------------------------------------------------
// Standalone ELF entry point (replaces SDK crt1.o)
// ---------------------------------------------------------------------------

extern "C" {
    extern unsigned char __bss_start[] __attribute__((weak));
    extern unsigned char __bss_end[] __attribute__((weak));

    typedef void (*init_fn)(void);
    extern init_fn __init_array_start[] __attribute__((weak));
    extern init_fn __init_array_end[] __attribute__((weak));

    void* _start(void* arg) {
        // 1. Initialize syscall gadget & dynamic linker pointer from bootstrap args first
        PayloadBootstrapArgs* bargs = (PayloadBootstrapArgs*)arg;
        if (bargs) {
            memcpy(&g_bargs, bargs, sizeof(PayloadBootstrapArgs));
            if (bargs->getpid_addr) {
                ptr_syscall = bargs->getpid_addr + 0x0A;
            }
            if (bargs->dlsym_addr) {
                p_sceKernelDlsym = (sceKernelDlsym_t)bargs->dlsym_addr;
            }
        }

        overlay_log_init();
        LOG_INFO("==================================================");
        LOG_INFO("PS5 Overlay Payload _start entered (arg=%p)", arg);
        LOG_INFO("==================================================");
        LOG_INFO("ptr_syscall=0x%lx, p_sceKernelDlsym=%p",
                 (unsigned long)ptr_syscall, (void*)p_sceKernelDlsym);

        // 3. Run C++ global constructors (.init_array)
        if (__init_array_start && __init_array_end) {
            LOG_INFO("Running .init_array constructors...");
            for (init_fn* fn = __init_array_start; fn < __init_array_end; fn++) {
                if (*fn) (*fn)();
            }
            LOG_INFO(".init_array constructors executed");
        }

        // 4. Run main payload thread logic
        LOG_INFO("Launching payload_thread_main...");
        payload_thread_main(arg);

        // 5. Clean exit when unload is signaled
        LOG_INFO("payload_thread_main completed. Exiting injected thread cleanly.");
        return nullptr;
    }
}

