#pragma once

#include <sys/types.h>
#include <stdint.h>

#define PTRACE_AUTHID 0x4800000000010003ull

#ifdef __cplusplus
extern "C" {
#endif

uintptr_t set_ucred_to_ptrace(void);
void restore_authid(uintptr_t old_authid);
void jailbreak_process(pid_t pid);
void escalate_process_caps(pid_t pid);

#ifdef __cplusplus
}
#endif
