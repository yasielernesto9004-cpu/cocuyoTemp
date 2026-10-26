#pragma once

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

pid_t find_process_by_name(const char* name);

#ifdef __cplusplus
}
#endif
