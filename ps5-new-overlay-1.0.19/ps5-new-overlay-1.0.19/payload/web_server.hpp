#pragma once
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

extern volatile bool g_should_unload;

bool web_server_start(int port, uint64_t pthread_create_addr);
void web_server_stop(void);

#ifdef __cplusplus
}
#endif
