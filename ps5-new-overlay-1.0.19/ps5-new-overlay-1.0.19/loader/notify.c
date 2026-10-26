#include "notify.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>

typedef struct {
    int32_t type;
    int32_t req_id;
    int32_t priority;
    int32_t msg_id;
    int32_t target_id;
    int32_t user_id;
    int32_t unk1;
    int32_t unk2;
    int32_t app_id;
    int32_t error_num;
    int32_t unk3;
    char use_icon_image_uri;
    char message[1024];
    char uri[1024];
    char unkstr[1024];
} OrbisNotificationRequest;

extern int sceKernelSendNotificationRequest(int device, const void* req, size_t size, int blocking);

void notify_send(const char* fmt, ...) {
    OrbisNotificationRequest req;
    memset(&req, 0, sizeof(req));

    req.target_id = -1;
    req.use_icon_image_uri = 1;
    strncpy(req.uri, "cxml://psnotification/tex_icon_system", sizeof(req.uri) - 1);

    va_list args;
    va_start(args, fmt);
    vsnprintf(req.message, sizeof(req.message), fmt, args);
    va_end(args);

    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}
