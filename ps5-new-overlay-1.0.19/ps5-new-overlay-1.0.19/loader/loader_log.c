#include "../common/log.h"
#include <stdio.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <ps5/klog.h>

static int s_log_initialized = 0;

void overlay_log_init(void) {
    if (s_log_initialized) return;
    mkdir("/data", 0777);
    mkdir("/data/overlay", 0777);
    chmod("/data/overlay", 0777);
    mkdir("/user/data", 0777);
    mkdir("/user/data/overlay", 0777);
    chmod("/user/data/overlay", 0777);
    s_log_initialized = 1;
}

void overlay_log(const char* level, const char* fmt, ...) {
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
    int line_len = snprintf(line, sizeof(line), "[LOADER] [%s] %s\n", level, msg);
    if (line_len <= 0) return;

    // 1. Console / stdout
    fputs(line, stdout);
    fflush(stdout);

    // 2. Kernel log
    klog_printf("%s", line);

    // 3. Write to file
    overlay_log_init();
    static const char* const paths[] = {
        "/data/overlay/overlay.log",
        "/data/overlay/log.txt",
        "/user/data/overlay/overlay.log",
        "/system_tmp/ps5_overlay.log",
        NULL
    };
    for (int i = 0; paths[i]; i++) {
        int fd = open(paths[i], O_WRONLY | O_CREAT | O_APPEND, 0666);
        if (fd >= 0) {
            write(fd, line, (size_t)line_len);
            close(fd);
        }
    }
}
