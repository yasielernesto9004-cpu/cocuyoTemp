#include "overlay.hpp"
#include "overlay_widgets.hpp"
#include "detour/detour.h"
#include "../common/log.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <atomic>
#include <ps5/kernel.h>

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

struct Memory {
    int Used;
    int Free;
    int Total;
    float Percentage;
};

struct thread_usages {
    OrbisKernelTimespec current_time;
    int Thread_Count;
    char padding0[0x4];
    Proc_Stats Threads[3072];
};

#include "telemetry.h"


namespace overlay {

static std::atomic<bool> g_hooks_ready{false};

void shellui_publish_ready() {
    g_hooks_ready.store(true, std::memory_order_release);
}

bool shellui_hooks_are_ready() {
    return g_hooks_ready.load(std::memory_order_acquire);
}

constexpr int kCpuCores = 8;
constexpr int kMaxProcThreads = 3072;
constexpr int kClockIdRealtime = 4;
constexpr int kVmSystem = 1;
constexpr int kPageTableRam = 1;
constexpr int kPageTableVram = 2;

constexpr int kOverlayInitDelayFrames = 30;
constexpr int kOverlayUpdateIntervalFrames = 60;
constexpr int kFpsUpdateIntervalFrames = 2;

static Proc_Stats g_stat_data[kMaxProcThreads];
static thread_usages g_thread_data[2];
static float g_cpu_usage[kCpuCores] = { 0 };
static Memory g_ram{};
static Memory g_vram{};

static void (*OnRender_orig)(MonoObject* instance) = nullptr;

static void calc_usage(unsigned int idle_tid[kCpuCores], thread_usages* cur, thread_usages* prev, float usage_out[kCpuCores]) {
    if (cur->Thread_Count <= 0 || prev->Thread_Count <= 0) return;

    float current_time_total = ((prev->current_time.tv_sec + (prev->current_time.tv_nsec / 1000000000.0f)) -
                                (cur->current_time.tv_sec + (cur->current_time.tv_nsec / 1000000000.0f)));
    if (current_time_total <= 0.0001f) return;

    struct Data_s {
        Proc_Stats* Cur = nullptr;
        Proc_Stats* Prev = nullptr;
    } data[kCpuCores];

    for (int i = 0; i < cur->Thread_Count; i++) {
        for (int j = 0; j < kCpuCores; j++) {
            if (idle_tid[j] == cur->Threads[i].td_tid)
                data[j].Cur = &cur->Threads[i];
        }
    }

    for (int i = 0; i < prev->Thread_Count; i++) {
        for (int j = 0; j < kCpuCores; j++) {
            if (idle_tid[j] == prev->Threads[i].td_tid)
                data[j].Prev = &prev->Threads[i];
        }
    }

    for (int i = 0; i < kCpuCores; i++) {
        if (!data[i].Prev || !data[i].Cur) continue;

        float prev_time = (data[i].Prev->system_cpu_usage_time.tv_sec + (data[i].Prev->system_cpu_usage_time.tv_nsec / 1000000000.0f)) +
                          (data[i].Prev->user_cpu_usage_time.tv_sec + (data[i].Prev->user_cpu_usage_time.tv_nsec / 1000000000.0f));

        float cur_time = (data[i].Cur->system_cpu_usage_time.tv_sec + (data[i].Cur->system_cpu_usage_time.tv_nsec / 1000000000.0f)) +
                         (data[i].Cur->user_cpu_usage_time.tv_sec + (data[i].Cur->user_cpu_usage_time.tv_nsec / 1000000000.0f));

        float delta_idle = (prev_time - cur_time) / current_time_total;
        if (delta_idle > 1.0f) delta_idle = 1.0f;
        if (delta_idle < 0.0f) delta_idle = 0.0f;

        usage_out[i] = (1.0f - delta_idle) * 100.0f;
    }
}

static void discover_idle_thread_ids(unsigned int idle_tid[kCpuCores]) {
    int count = kMaxProcThreads;
    if (!sceKernelGetCpuUsage || sceKernelGetCpuUsage(reinterpret_cast<Proc_Stats*>(&g_stat_data), &count) || count <= 0)
        return;

    char thread_name[0x40];
    int core = 0;
    for (int i = 0; i < count; i++) {
        if (sceKernelGetThreadName && !sceKernelGetThreadName(g_stat_data[i].td_tid, thread_name) &&
            sscanf(thread_name, "SceIdleCpu%d", &core) == 1 && core >= 0 && core < kCpuCores) {
            idle_tid[core] = g_stat_data[i].td_tid;
        }
    }
}

static void sample_cpu(unsigned int idle_tid[kCpuCores], int& bank, char* out_usage, size_t sz) {
    if (!sceKernelGetCpuUsage || !sceKernelClockGettime) {
        snprintf(out_usage, sz, "--%%");
        return;
    }

    g_thread_data[bank].Thread_Count = kMaxProcThreads;
    if (sceKernelGetCpuUsage(reinterpret_cast<Proc_Stats*>(&g_thread_data[bank].Threads), &g_thread_data[bank].Thread_Count) != 0) {
        snprintf(out_usage, sz, "--%%");
        return;
    }

    sceKernelClockGettime(kClockIdRealtime, &g_thread_data[bank].current_time);
    bank = !bank;

    if (g_thread_data[bank].Thread_Count <= 0) {
        snprintf(out_usage, sz, "--%%");
        return;
    }

    calc_usage(idle_tid, &g_thread_data[!bank], &g_thread_data[bank], g_cpu_usage);

    if (g_settings.per_core_cpu) {
        snprintf(out_usage, sz, "%2.0f%% %2.0f%% %2.0f%% %2.0f%% %2.0f%% %2.0f%% %2.0f%% %2.0f%%",
                 g_cpu_usage[0], g_cpu_usage[1], g_cpu_usage[2], g_cpu_usage[3],
                 g_cpu_usage[4], g_cpu_usage[5], g_cpu_usage[6], g_cpu_usage[7]);
    } else {
        float avg = 0.f;
        for (int i = 0; i < kCpuCores; i++) avg += g_cpu_usage[i];
        avg /= static_cast<float>(kCpuCores);
        snprintf(out_usage, sz, "%.0f%%", avg);
    }
}

static void sample_network_ip(char* out, size_t sz) {
    struct SceNetCtlInfo {
        char ip_address[16];
    } info{};

    if (sceNetCtlGetInfo && sceNetCtlGetInfo(14 /* SCE_NET_CTL_INFO_IP_ADDRESS */, &info) == 0) {
        snprintf(out, sz, "%s", info.ip_address);
    } else {
        snprintf(out, sz, "---.---.---.---");
    }
}

static void update_all_metrics(unsigned int idle_tid[kCpuCores], int& bank,
                               char* cpu_temp, size_t cpu_temp_sz,
                               char* cpu_usage, size_t cpu_usage_sz,
                               char* gpu_temp, size_t gpu_temp_sz,
                               char* gpu_usage, size_t gpu_usage_sz,
                               char* ram_str, size_t ram_sz,
                               char* ip_str, size_t ip_sz,
                               char* fan_str, size_t fan_sz) {
    if (!g_settings.enabled) return;

    // CPU Usage
    sample_cpu(idle_tid, bank, cpu_usage, cpu_usage_sz);

    // CPU Temperature
    if (g_settings.show_cpu && sceKernelGetCpuTemperature) {
        int cpu_t = 0;
        sceKernelGetCpuTemperature(&cpu_t);
        snprintf(cpu_temp, cpu_temp_sz, "%dC", cpu_t);
    }

    // GPU / SoC Temperature & VRAM
    if (g_settings.show_gpu) {
        int soc_temp = 0;
        if (sceKernelGetSocSensorTemperature) {
            sceKernelGetSocSensorTemperature(0, &soc_temp);
            snprintf(gpu_temp, gpu_temp_sz, "%dC", soc_temp);
        }
        if (get_page_table_stats) {
            get_page_table_stats(kVmSystem, kPageTableVram, &g_vram.Total, &g_vram.Free);
            g_vram.Used = g_vram.Total - g_vram.Free;
            float pct = (g_vram.Total > 0) ? ((float)g_vram.Used / (float)g_vram.Total) * 100.0f : 0.f;
            snprintf(gpu_usage, gpu_usage_sz, "%.0f%%", pct);
        }
    }

    // RAM
    if (g_settings.show_ram && get_page_table_stats) {
        get_page_table_stats(kVmSystem, kPageTableRam, &g_ram.Total, &g_ram.Free);
        g_ram.Used = g_ram.Total - g_ram.Free;
        snprintf(ram_str, ram_sz, "%u MB", g_ram.Used);
    }

    // IP
    if (g_settings.show_ip) {
        sample_network_ip(ip_str, ip_sz);
    }

    // FAN
    if (g_settings.show_fan && sceKernelGetCurrentFanDuty) {
        uint16_t duty = 0;
        uint64_t chassis = 0;
        if (sceKernelGetCurrentFanDuty(&duty, &chassis) == 0) {
            snprintf(fan_str, fan_sz, "%.0f%%", (double)duty * 100.0 / 1024.0);
        } else {
            snprintf(fan_str, fan_sz, "--%%");
        }
    }
}

static void sample_fps(char* out, size_t sz) {
    static struct timespec last_ts{};
    static uint64_t frame_count = 0;
    static float current_fps = 0.f;

    frame_count++;
    struct timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);

    double dt = (now.tv_sec - last_ts.tv_sec) + (now.tv_nsec - last_ts.tv_nsec) / 1000000000.0;
    if (dt >= 0.5) {
        current_fps = (float)(frame_count / dt);
        frame_count = 0;
        last_ts = now;
    }

    if (current_fps >= 1.0f) {
        if (current_fps >= 100.0f)
            snprintf(out, sz, "%.0f", current_fps);
        else
            snprintf(out, sz, "%.1f", current_fps);
    } else {
        snprintf(out, sz, "--");
    }
}

static void OnRender_Hook(MonoObject* instance) {
    if (!shellui_hooks_are_ready()) {
        if (OnRender_orig) OnRender_orig(instance);
        return;
    }

    static bool inited = false;
    static unsigned int idle_thread_id[kCpuCores] = {};
    static int current_bank = 0;
    static int frames_until_update = 0;
    static int frames_until_fps = 0;
    static int frames_until_init = kOverlayInitDelayFrames;

    static char gpu_temp[32] = "--C";
    static char gpu_usage[32] = "--%";
    static char cpu_temp[32] = "--C";
    static char cpu_usage[120] = "--%";
    static char ram_str[32] = "---- MB";
    static char ip_address[64] = "---.---.---.---";
    static char fan_str[32] = "--%";
    static char fps_str[16] = "--";

    if (!inited) {
        if (frames_until_init > 0) {
            --frames_until_init;
        } else if (init_widgets()) {
            discover_idle_thread_ids(idle_thread_id);
            inited = true;
        }
        if (!inited) {
            if (OnRender_orig) OnRender_orig(instance);
            return;
        }
    }

    if (frames_until_update <= 0) {
        update_all_metrics(idle_thread_id, current_bank,
                           cpu_temp, sizeof(cpu_temp),
                           cpu_usage, sizeof(cpu_usage),
                           gpu_temp, sizeof(gpu_temp),
                           gpu_usage, sizeof(gpu_usage),
                           ram_str, sizeof(ram_str),
                           ip_address, sizeof(ip_address),
                           fan_str, sizeof(fan_str));
        frames_until_update = kOverlayUpdateIntervalFrames;
    } else {
        frames_until_update--;
    }

    if (frames_until_fps <= 0) {
        sample_fps(fps_str, sizeof(fps_str));
        update_labels(fps_str, cpu_temp, cpu_usage, gpu_temp, gpu_usage, ram_str, ip_address, fan_str);
        frames_until_fps = kFpsUpdateIntervalFrames;
    } else {
        frames_until_fps--;
    }

    if (OnRender_orig) {
        OnRender_orig(instance);
    }
}

bool install_overlay_hooks(MonoImage* pui_image) {
    if (!pui_image) return false;

    uint64_t target = Get_Address_of_Method(pui_image, "Sce.PlayStation.PUI", "Application", "Update", 0);
    if (!target) {
        LOG_ERROR("Failed to find PUI.Application.Update method address!");
        return false;
    }

    LOG_INFO("PUI.Application.Update at 0x%lx", (unsigned long)target);
    if (!InstallDetour(target, reinterpret_cast<void*>(&OnRender_Hook), reinterpret_cast<void**>(&OnRender_orig))) {
        LOG_ERROR("Failed to install detour hook on PUI.Application.Update!");
        return false;
    }

    LOG_INFO("OnRender detour successfully installed!");
    return true;
}

void uninstall_overlay_hooks() {
    remove_all_widgets();
}

} // namespace overlay
