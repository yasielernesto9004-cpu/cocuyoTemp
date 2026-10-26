#include "ucred.h"
#include "../common/log.h"
#include <ps5/kernel.h>
#include <unistd.h>

uintptr_t set_ucred_to_ptrace(void) {
    pid_t pid = getpid();
    uintptr_t ucred = kernel_get_proc_ucred(pid);
    if (!ucred) {
        LOG_ERROR("Failed to get ucred for PID %d", pid);
        return 0;
    }

    uintptr_t old_authid = 0;
    kernel_copyout(ucred + 0x58, &old_authid, sizeof(uintptr_t));

    uintptr_t ptrace_auth = PTRACE_AUTHID;
    kernel_copyin(&ptrace_auth, ucred + 0x58, sizeof(uintptr_t));

    return old_authid;
}

void restore_authid(uintptr_t old_authid) {
    if (!old_authid) return;
    pid_t pid = getpid();
    uintptr_t ucred = kernel_get_proc_ucred(pid);
    if (ucred) {
        kernel_copyin(&old_authid, ucred + 0x58, sizeof(uintptr_t));
    }
}

void jailbreak_process(pid_t pid) {
    uintptr_t ucred = kernel_get_proc_ucred(pid);
    if (!ucred) return;

    uint32_t zero = 0;
    int64_t caps = -1;
    uint8_t attr[] = {0x80, 0, 0, 0, 0, 0, 0, 0};

    // uid, ruid, svuid, rgid
    kernel_copyin(&zero, ucred + 0x04, 0x4);
    kernel_copyin(&zero, ucred + 0x08, 0x4);
    kernel_copyin(&zero, ucred + 0x0C, 0x4);
    kernel_copyin(&zero, ucred + 0x14, 0x4);

    // Escalate sony privileges (caps, attrs)
    kernel_copyin(&caps, ucred + 0x60, 0x8);
    kernel_copyin(&caps, ucred + 0x68, 0x8);
    kernel_copyin(attr, ucred + 0x83, 0x1);
}

void escalate_process_caps(pid_t pid) {
    uintptr_t ucred = kernel_get_proc_ucred(pid);
    if (!ucred) return;

    int64_t caps = -1;
    uint8_t attr[] = {0x80, 0, 0, 0, 0, 0, 0, 0};

    // Escalate sony privileges (caps, attrs) without altering uids
    kernel_copyin(&caps, ucred + 0x60, 0x8);
    kernel_copyin(&caps, ucred + 0x68, 0x8);
    kernel_copyin(attr, ucred + 0x83, 0x1);
}

