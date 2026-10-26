#include "overlay_widgets.hpp"
#include "overlay_text_metrics.hpp"
#include "../common/log.h"
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace overlay {

Settings g_settings;
onion::overlay::Layout g_layout;

static MonoObject* g_font = nullptr;
static constexpr const char* kBgPanelName = "id_overlay_bg";

// Colors
constexpr float kCpuR = 102.0f / 255.0f, kCpuG = 1.0f, kCpuB = 102.0f / 255.0f;
constexpr float kGpuR = 179.0f / 255.0f, kGpuG = 102.0f / 255.0f, kGpuB = 1.0f;
constexpr float kMemR = 1.0f, kMemG = 179.0f / 255.0f, kMemB = 77.0f / 255.0f;
constexpr float kNetR = 128.0f / 255.0f, kNetG = 179.0f / 255.0f, kNetB = 1.0f;
constexpr float kFanR = 51.0f / 255.0f, kFanG = 224.0f / 255.0f, kFanB = 1.0f;
constexpr float kFpsR = 1.0f, kFpsG = 230.0f / 255.0f, kFpsB = 0.0f;
constexpr float kValR = 1.0f, kValG = 1.0f, kValB = 1.0f;
constexpr float kSepR = 0.75f, kSepG = 0.75f, kSepB = 0.75f;
constexpr float kBgR = 0.0f, kBgG = 0.0f, kBgB = 0.0f, kBgA = 180.0f / 255.0f;
constexpr float kA = 1.0f;

static MonoObject* find_root_widget() {
    if (!Game || !pui_img) return nullptr;
    return Get_Property<MonoObject*>(pui_img, "Sce.PlayStation.PUI.UI2", "Scene", Game, "RootWidget");
}

bool resolve_screen_size(float* width, float* height) {
    MonoObject* root = find_root_widget();
    if (!root) return false;

    MonoClass* widgetClass = mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Widget");
    if (!widgetClass) return false;

    float w = Get_Property<float>(widgetClass, root, "Width");
    float h = Get_Property<float>(widgetClass, root, "Height");

    if (!std::isfinite(w) || !std::isfinite(h) || w <= 1.0f || h <= 1.0f) {
        *width = 1920.0f;
        *height = 1080.0f;
        return true;
    }
    *width = w;
    *height = h;
    return true;
}

static MonoObject* find_widget_by_name(MonoObject* root, const char* name) {
    if (!root || !name || !pui_img) return nullptr;
    MonoClass* widgetClass = mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Widget");
    if (!widgetClass) return nullptr;
    return Invoke<MonoObject*>(pui_img, widgetClass, root, "FindWidgetByName", mono_string_new(Root_Domain, name));
}

static void remove_named_widget(MonoObject* root, const char* name) {
    MonoObject* child = find_widget_by_name(root, name);
    if (child) {
        MonoClass* widgetClass = mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Widget");
        Invoke<void>(pui_img, widgetClass, child, "RemoveFromParent");
    }
}

static void remove_cell(MonoObject* root, const char* label_name) {
    char cell_name[96];
    std::snprintf(cell_name, sizeof(cell_name), "%s_cell", label_name);
    remove_named_widget(root, cell_name);
    remove_named_widget(root, label_name);
}

static MonoObject* create_metric_cell(MonoObject* root, const char* label_name, float x) {
    MonoClass* panelClass = mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Panel");
    if (!panelClass) return nullptr;

    MonoObject* cell = New_Object(panelClass);
    if (!cell) return nullptr;
    mono_runtime_object_init(cell);

    char cell_name[96];
    std::snprintf(cell_name, sizeof(cell_name), "%s_cell", label_name);
    Set_Property(panelClass, cell, "Name", mono_string_new(Root_Domain, cell_name));
    Set_Property(panelClass, cell, "X", x);
    Set_Property(panelClass, cell, "Y", g_layout.bar_y);
    Set_Property(panelClass, cell, "Width", 500.0f);
    Set_Property(panelClass, cell, "Height", g_layout.bar_h);
    Set_Property(panelClass, cell, "BackgroundVisibility", false);
    Widget_Append_Child(root, cell);
    return cell;
}

static void ensure_bg_panel(MonoObject* root) {
    if (!root || !g_settings.background) return;
    if (find_widget_by_name(root, kBgPanelName)) return;

    MonoClass* panelClass = mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Panel");
    if (!panelClass) return;

    MonoObject* panel = New_Object(panelClass);
    if (!panel) return;
    mono_runtime_object_init(panel);

    Set_Property(panelClass, panel, "Name", mono_string_new(Root_Domain, kBgPanelName));
    Set_Property(panelClass, panel, "X", 0.0f);
    Set_Property(panelClass, panel, "Y", g_layout.bar_y);
    Set_Property(panelClass, panel, "Width", g_layout.bar_w);
    Set_Property(panelClass, panel, "Height", g_layout.bar_h);

    MonoObject* bg = CreateUIColor(kBgR, kBgG, kBgB, kBgA);
    if (bg) Set_Property_Invoke(panelClass, panel, "BackgroundColor", bg);

    Set_Property(panelClass, panel, "BackgroundVisibility", true);
    Set_Property(panelClass, panel, "BackgroundOpacity", 1.0f);
    Set_Property(panelClass, panel, "BackgroundStyle", 1);

    Widget_Append_Child(root, panel);
}

static void set_label_layout(const char* widget_name, float margin_left, float margin_top, float width) {
    MonoObject* root = find_root_widget();
    MonoObject* label = find_widget_by_name(root, widget_name);
    if (!label) return;

    MonoClass* label_cls = mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Label");
    MonoClass* panel_cls = mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Panel");

    char cell_name[96];
    std::snprintf(cell_name, sizeof(cell_name), "%s_cell", widget_name);
    MonoObject* cell = find_widget_by_name(root, cell_name);
    if (cell && panel_cls) {
        Set_Property(panel_cls, cell, "X", margin_left);
        Set_Property(panel_cls, cell, "Y", g_layout.bar_y);
        Set_Property(panel_cls, cell, "Width", width);
        Set_Property(panel_cls, cell, "Height", g_layout.bar_h);
    }

    if (label_cls) {
        Set_Property(label_cls, label, "PositionType", 1);
        Set_Property(label_cls, label, "MarginLeft", 0.0f);
        Set_Property(label_cls, label, "MarginTop", margin_top);
        Set_Property(label_cls, label, "Width", width);
        Set_Property(label_cls, label, "HorizontalAlignment", 0);
        Set_Property(label_cls, label, "VerticalAlignment", 0);
        Set_Property(label_cls, label, "FitWidthToText", false);
        Set_Property(label_cls, label, "FitHeightToText", true);
        Set_Property(label_cls, label, "NumberOfLines", 1);
    }
}

static void set_label_text(const char* widget_name, const char* text) {
    MonoObject* root = find_root_widget();
    MonoObject* label = find_widget_by_name(root, widget_name);
    if (label) {
        MonoClass* label_cls = mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Label");
        if (label_cls) {
            Set_Property(label_cls, label, "Text", mono_string_new(Root_Domain, text));
        }
    }
}

struct MetricConfig {
    const char* id;
    const char* default_text;
    float r, g, b;
};

static void create_label_widget(MonoObject* root, const MetricConfig& cfg) {
    MonoObject* cell = create_metric_cell(root, cfg.id, 0.0f);
    MonoObject* label = CreateLabel(cfg.id, 0.0f, g_layout.label_margin_top, cfg.default_text,
                                    g_font, 0, 0, cfg.r, cfg.g, cfg.b, kA);
    if (label) {
        MonoClass* labelClass = mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Label");
        if (labelClass) {
            Set_Property(labelClass, label, "PositionType", 1);
            Set_Property(labelClass, label, "MarginLeft", 0.0f);
            Set_Property(labelClass, label, "MarginTop", g_layout.label_margin_top);
            Set_Property(labelClass, label, "Width", 500.0f);
            Set_Property(labelClass, label, "HorizontalAlignment", 0);
            Set_Property(labelClass, label, "VerticalAlignment", 0);
            Set_Property(labelClass, label, "FitWidthToText", false);
            Set_Property(labelClass, label, "FitHeightToText", true);
            Set_Property(labelClass, label, "NumberOfLines", 1);
            Set_Property(labelClass, label, "EnableThemedTextShadow", true);
        }
        Widget_Append_Child(cell ? cell : root, label);
    }
}

void remove_all_widgets() {
    MonoObject* root = find_root_widget();
    if (!root) return;

    static const char* kAllNames[] = {
        kBgPanelName,
        "id_fps_label", "id_fps_value", "id_fps_sep",
        "id_cpu_label", "id_cpu_temp_value", "id_cpu_usage_value", "id_cpu_sep",
        "id_gpu_label", "id_gpu_temp_value", "id_gpu_usage_value", "id_gpu_sep",
        "id_ram_label", "id_ram_value", "id_ram_sep",
        "id_ip_label", "id_ip_value", "id_ip_sep",
        "id_fan_label", "id_fan_value", "id_fan_sep",
    };
    for (const char* name : kAllNames) {
        if (std::strcmp(name, kBgPanelName) == 0)
            remove_named_widget(root, name);
        else
            remove_cell(root, name);
    }
}

bool init_widgets() {
    MonoObject* root = find_root_widget();
    if (!root) return false;

    float screen_w = 1920.0f, screen_h = 1080.0f;
    resolve_screen_size(&screen_w, &screen_h);

    onion::overlay::Metrics m{};
    m.enabled = g_settings.enabled;
    m.show_fps = g_settings.show_fps;
    m.show_cpu = g_settings.show_cpu;
    m.show_gpu = g_settings.show_gpu;
    m.show_ram = g_settings.show_ram;
    m.show_ip = g_settings.show_ip;
    m.show_fan = g_settings.show_fan;
    m.per_core_cpu = g_settings.per_core_cpu;
    m.font_size_pt = g_settings.font_size;

    g_layout = onion::overlay::compute_overlay_layout(
        screen_w, screen_h,
        onion::overlay::bar_edge_from_pos(g_settings.position),
        onion::overlay::bar_align_from_value(g_settings.alignment),
        m
    );

    g_font = CreateUIFont(g_settings.font_size, 1, 900); // Bold, Weight 900
    if (!g_font) {
        LOG_ERROR("Failed to create UIFont!");
        return false;
    }

    remove_all_widgets();
    ensure_bg_panel(root);

    static const MetricConfig configs[] = {
        {"id_fps_label", "FPS", kFpsR, kFpsG, kFpsB},
        {"id_fps_value", "--", kValR, kValG, kValB},
        {"id_fps_sep", "|", kSepR, kSepG, kSepB},

        {"id_cpu_label", "CPU", kCpuR, kCpuG, kCpuB},
        {"id_cpu_temp_value", "--C", kValR, kValG, kValB},
        {"id_cpu_usage_value", "--%", kValR, kValG, kValB},
        {"id_cpu_sep", "|", kSepR, kSepG, kSepB},

        {"id_gpu_label", "GPU", kGpuR, kGpuG, kGpuB},
        {"id_gpu_temp_value", "--C", kValR, kValG, kValB},
        {"id_gpu_usage_value", "--%", kValR, kValG, kValB},
        {"id_gpu_sep", "|", kSepR, kSepG, kSepB},

        {"id_ram_label", "RAM", kMemR, kMemG, kMemB},
        {"id_ram_value", "---- MB", kValR, kValG, kValB},
        {"id_ram_sep", "|", kSepR, kSepG, kSepB},

        {"id_ip_label", "IP", kNetR, kNetG, kNetB},
        {"id_ip_value", "---.---.---.---", kValR, kValG, kValB},
        {"id_ip_sep", "|", kSepR, kSepG, kSepB},

        {"id_fan_label", "FAN", kFanR, kFanG, kFanB},
        {"id_fan_value", "--%", kValR, kValG, kValB},
        {"id_fan_sep", "|", kSepR, kSepG, kSepB},
    };

    for (const auto& cfg : configs) {
        create_label_widget(root, cfg);
    }

    LOG_INFO("Overlay widgets initialized successfully.");
    return true;
}

void update_labels(const char* fps_str, const char* cpu_temp, const char* cpu_usage,
                   const char* gpu_temp, const char* gpu_usage, const char* ram_str,
                   const char* ip_str, const char* fan_str) {
    const float font_pt = static_cast<float>(g_settings.font_size);
    const float scale = font_pt / onion::overlay::kRefFontSize;
    const float kPairGap = 8.0f * scale;
    const float kValGap = 10.0f * scale;
    const float kSepGap = 10.0f * scale;
    const float kAfterSep = 16.0f * scale;
    constexpr float kOff = -4096.0f;

    struct Piece {
        const char* id;
        const char* text;
        float width;
        bool is_label;
        bool is_sep;
    };
    std::vector<Piece> pieces;
    pieces.reserve(24);

    struct GroupSpec {
        bool on;
        const char* label_id;
        const char* label_text;
        const char* v0_id;
        const char* v0_text;
        const char* v1_id;
        const char* v1_text;
        const char* sep_id;
    };

    GroupSpec groups[] = {
        { g_settings.show_fps && fps_str && fps_str[0], "id_fps_label", "FPS", "id_fps_value", fps_str, nullptr, nullptr, "id_fps_sep" },
        { g_settings.show_cpu && cpu_temp && cpu_temp[0], "id_cpu_label", "CPU", "id_cpu_temp_value", cpu_temp, "id_cpu_usage_value", cpu_usage, "id_cpu_sep" },
        { g_settings.show_gpu && gpu_temp && gpu_temp[0], "id_gpu_label", "GPU", "id_gpu_temp_value", gpu_temp, "id_gpu_usage_value", gpu_usage, "id_gpu_sep" },
        { g_settings.show_ram && ram_str && ram_str[0], "id_ram_label", "RAM", "id_ram_value", ram_str, nullptr, nullptr, "id_ram_sep" },
        { g_settings.show_ip && ip_str && ip_str[0], "id_ip_label", "IP", "id_ip_value", ip_str, nullptr, nullptr, "id_ip_sep" },
        { g_settings.show_fan && fan_str && fan_str[0], "id_fan_label", "FAN", "id_fan_value", fan_str, nullptr, nullptr, "id_fan_sep" },
    };

    size_t on_groups = 0;
    for (const auto& g : groups) { if (g.on) on_groups++; }

    size_t seen_groups = 0;
    for (const auto& g : groups) {
        if (!g.on) continue;
        seen_groups++;
        bool is_last_group = (seen_groups == on_groups);

        pieces.push_back({ g.label_id, g.label_text, onion::overlay::estimate_text_width(g.label_text, font_pt), true, false });
        if (g.v0_text) {
            pieces.push_back({ g.v0_id, g.v0_text, onion::overlay::estimate_text_width(g.v0_text, font_pt), false, false });
        }
        if (g.v1_text) {
            pieces.push_back({ g.v1_id, g.v1_text, onion::overlay::estimate_text_width(g.v1_text, font_pt), false, false });
        }
        if (!is_last_group) {
            pieces.push_back({ g.sep_id, "|", onion::overlay::estimate_text_width("|", font_pt), false, true });
        }
    }

    auto gap_after = [&](size_t i) -> float {
        if (i + 1 >= pieces.size()) return 0.f;
        if (pieces[i].is_label) return kPairGap;
        if (pieces[i].is_sep) return kAfterSep;
        if (pieces[i + 1].is_sep) return kSepGap;
        if (pieces[i + 1].is_label) return kAfterSep;
        return kValGap;
    };

    float total = 0.f;
    for (size_t i = 0; i < pieces.size(); ++i) {
        total += pieces[i].width + gap_after(i);
    }

    float screen_w = g_layout.bar_w;
    if (screen_w <= 1.0f) screen_w = 1920.0f;
    float x = onion::overlay::pack_origin(screen_w, total, onion::overlay::bar_align_from_value(g_settings.alignment));
    const float margin_top = g_layout.label_margin_top;

    static const char* kAll[] = {
        "id_fps_label", "id_fps_value", "id_fps_sep",
        "id_cpu_label", "id_cpu_temp_value", "id_cpu_usage_value", "id_cpu_sep",
        "id_gpu_label", "id_gpu_temp_value", "id_gpu_usage_value", "id_gpu_sep",
        "id_ram_label", "id_ram_value", "id_ram_sep",
        "id_ip_label", "id_ip_value", "id_ip_sep",
        "id_fan_label", "id_fan_value", "id_fan_sep",
    };
    for (const char* id : kAll) {
        set_label_layout(id, kOff, margin_top, 0.0f);
    }

    for (size_t i = 0; i < pieces.size(); ++i) {
        set_label_layout(pieces[i].id, x, margin_top, pieces[i].width);
        set_label_text(pieces[i].id, pieces[i].text);
        x += pieces[i].width + gap_after(i);
    }
}

} // namespace overlay
