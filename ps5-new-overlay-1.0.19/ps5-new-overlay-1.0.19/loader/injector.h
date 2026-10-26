#pragma once

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void (*sceKernelDebugOutText)(int, const char*);
    void (*pthread_create_ptr)(pthread_t*, void*, void*(*)(void*), void*);
    void* elf_main;
    void* payload_args;
} SCEFunctions;

bool inject_elf_into_pid(pid_t pid, void* elf_data);

#ifdef __cplusplus
}
#endif
