#pragma once

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

void overlay_log(const char* level, const char* fmt, ...);
void overlay_log_init(void);

#define LOG_INFO(fmt, ...)  overlay_log("INFO", fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) overlay_log("DEBUG", fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  overlay_log("WARN", fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) overlay_log("ERROR", fmt, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

