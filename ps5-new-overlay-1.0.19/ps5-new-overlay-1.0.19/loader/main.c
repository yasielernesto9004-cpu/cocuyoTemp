#include "proc.h"
#include "ucred.h"
#include "injector.h"
#include "notify.h"
#include "../common/log.h"
#include <stdio.h>
#include <unistd.h>

extern uint8_t overlay_payload_start[];
extern uint32_t overlay_payload_size;

int main(int argc, char const* argv[]) {
    (void)argc;
    (void)argv;

    overlay_log_init();
    LOG_INFO("==================================================");
    LOG_INFO("PS5 Overlay Loader Started");
    LOG_INFO("==================================================");
    LOG_INFO("Embedded payload size: %u bytes", overlay_payload_size);

    pid_t self = getpid();
    LOG_INFO("Current loader PID: %d. Escalating privileges...", self);

    jailbreak_process(self);
    LOG_INFO("jailbreak_process completed for PID: %d", self);

    uint64_t orig_authid = set_ucred_to_ptrace();
    LOG_INFO("set_ucred_to_ptrace original_authid: 0x%lx", (unsigned long)orig_authid);

    LOG_INFO("Searching for SceShellUI process...");
    pid_t shellui_pid = find_process_by_name("SceShellUI");
    if (shellui_pid <= 0) {
        LOG_ERROR("Could not find SceShellUI process!");
        restore_authid(orig_authid);
        notify_send("Overlay Error: SceShellUI not found!");
        return -1;
    }

    LOG_INFO("Found SceShellUI PID: %d. Injecting overlay payload...", shellui_pid);
    if (!inject_elf_into_pid(shellui_pid, overlay_payload_start)) {
        LOG_ERROR("Failed to inject overlay payload into SceShellUI!");
        restore_authid(orig_authid);
        notify_send("Overlay Error: Injection failed! Check /data/overlay/overlay.log");
        return -1;
    }

    LOG_INFO("Overlay successfully injected into SceShellUI!");
    restore_authid(orig_authid);

    // Give SceShellUI threads 300ms to settle after ptrace detach before sending notification IPC
    usleep(300000);
    notify_send("PS5 Overlay Active! Web Settings: port 8585");

    return 0;
}
