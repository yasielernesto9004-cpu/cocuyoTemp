#include "../common/log.h"
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>

extern "C" long __syscall(long sysno, ...);
extern "C" uintptr_t ptr_syscall;

__attribute__((section(".data"))) static int s_payload_log_init = 0;

extern "C" void overlay_log_init(void) {
    if (s_payload_log_init || !ptr_syscall) return;
    // SYS_mkdir = 136, SYS_chmod = 15
    __syscall(136, "/data", 0777);
    __syscall(136, "/data/overlay", 0777);
    __syscall(15,  "/data/overlay", 0777);
    __syscall(136, "/user/data", 0777);
    __syscall(136, "/user/data/overlay", 0777);
    __syscall(15,  "/user/data/overlay", 0777);
    s_payload_log_init = 1;
}

static void payload_write_file(const char* path, const char* data, size_t len) {
    if (!ptr_syscall) return;
    // SYS_open = 5, SYS_write = 4, SYS_close = 6
    // O_WRONLY | O_CREAT | O_APPEND = 0x0209
    long fd = __syscall(5, path, 0x0209, 0666);
    if (fd < 0) {
        fd = __syscall(5, path, 0x0201, 0666); // O_WRONLY | O_CREAT
    }
    if (fd >= 0) {
        __syscall(4, fd, data, len);
        __syscall(6, fd);
    }
}

extern "C" void overlay_log(const char* level, const char* fmt, ...) {
    char msg[1024];
    va_list ap;

    va_start(ap, fmt);
    int len = vsnprintf(msg, sizeof(msg) - 2, fmt, ap);
    va_end(ap);

    if (len < 0) return;
    if (len > (int)sizeof(msg) - 2) len = sizeof(msg) - 2;
    if (len > 0 && msg[len - 1] == '\n') {
        msg[--len] = '\0';
    }

    char line[1200];
    int line_len = snprintf(line, sizeof(line), "[PAYLOAD] [%s] %s\n", level, msg);
    if (line_len <= 0) return;

    // 1. Console / stdout (if available)
    printf("%s", line);

    // 2. Write to disk files via direct kernel syscall
    overlay_log_init();
    payload_write_file("/data/overlay/overlay.log", line, (size_t)line_len);
    payload_write_file("/data/overlay/log.txt", line, (size_t)line_len);
    payload_write_file("/user/data/overlay/overlay.log", line, (size_t)line_len);
    payload_write_file("/system_tmp/ps5_overlay.log", line, (size_t)line_len);
}
