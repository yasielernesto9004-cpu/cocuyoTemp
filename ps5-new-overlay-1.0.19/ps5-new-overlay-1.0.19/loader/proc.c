#include "proc.h"
#include "../common/log.h"
#include <sys/sysctl.h>
#include <sys/user.h>
#include <stdlib.h>
#include <string.h>

pid_t find_process_by_name(const char* name) {
    if (!name || !*name) return -1;

    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
    size_t buf_size = 0;

    if (sysctl(mib, 4, NULL, &buf_size, NULL, 0) != 0 || buf_size == 0) {
        LOG_ERROR("sysctl failed to get kinfo size");
        return -1;
    }

    void* buf = malloc(buf_size);
    if (!buf) return -1;

    if (sysctl(mib, 4, buf, &buf_size, NULL, 0) != 0) {
        free(buf);
        return -1;
    }

    pid_t found_pid = -1;
    for (char* ptr = (char*)buf; ptr < (char*)buf + buf_size; ) {
        struct kinfo_proc* ki = (struct kinfo_proc*)ptr;
        if (ki->ki_structsize <= 0) break;
        ptr += ki->ki_structsize;

        if (ki->ki_comm[0] && strstr(ki->ki_comm, name) != NULL) {
            found_pid = ki->ki_pid;
            break;
        }
        if (ki->ki_tdname[0] && strstr(ki->ki_tdname, name) != NULL) {
            found_pid = ki->ki_pid;
            break;
        }
    }

    free(buf);
    return found_pid;
}
