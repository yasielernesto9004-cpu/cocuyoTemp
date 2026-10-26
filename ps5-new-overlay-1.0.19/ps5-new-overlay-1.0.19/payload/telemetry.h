#pragma once
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

extern int (*sceKernelGetSocSensorTemperature)(int sensorId, int* soctime);
extern int (*get_page_table_stats)(int vm, int type, int* total, int* free);
extern int (*sceKernelGetCpuUsage)(void* out, int32_t* size);
extern int (*sceKernelGetThreadName)(uint32_t id, char* out);
extern int (*sceKernelGetCpuTemperature)(int* cputemp);
extern int (*sceKernelClockGettime)(int clockId, void* tp);
extern int (*sceKernelGetCurrentFanDuty)(uint16_t* duty, uint64_t* chassis);
extern int (*sceNetCtlGetInfo)(int code, void* info);

#ifdef __cplusplus
}
#endif
