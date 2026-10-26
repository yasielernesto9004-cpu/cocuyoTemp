#include "injector.h"
#include "pt.h"
#include "elfldr.h"
#include "ucred.h"
#include "../common/log.h"
#include "../common/bootstrap.h"
#include <ps5/kernel.h>
#include <ps5/nid.h>
#include <pthread.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>

static uint32_t find_module_by_substring(pid_t pid, const char* substr, uint64_t* out_mapbase) {
    intptr_t kproc = kernel_get_proc(pid);
    if (!kproc) return 0;
    intptr_t kaddr = 0;
    if (kernel_copyout(kproc + 0x3e8, &kaddr, sizeof(kaddr)) < 0) return 0;
    char path[1024];
    long handle = 0;
    while (1) {
        if (kernel_copyout(kaddr, &kaddr, sizeof(kaddr)) < 0 || !kaddr) break;
        intptr_t kpath = 0;
        if (kernel_copyout(kaddr + 0x08 /* path */, &kpath, sizeof(kpath)) < 0) continue;
        if (kernel_copyout(kpath, path, sizeof(path)) < 0) continue;
        if (kernel_copyout(kaddr + 0x28 /* handle */, &handle, sizeof(handle)) < 0) continue;
        if (strstr(path, substr) != NULL) {
            intptr_t mapbase = 0;
            kernel_copyout(kaddr + 0x30 /* mapbase */, &mapbase, sizeof(mapbase));
            if (out_mapbase) *out_mapbase = (uint64_t)mapbase;
            return (uint32_t)handle;
        }
    }
    return 0;
}

static void dump_target_modules(pid_t pid) {
    intptr_t kproc = kernel_get_proc(pid);
    if (!kproc) return;
    intptr_t kaddr = 0;
    if (kernel_copyout(kproc + 0x3e8, &kaddr, sizeof(kaddr)) < 0) return;
    char path[512];
    long handle = 0;
    int count = 0;
    while (count < 35) {
        if (kernel_copyout(kaddr, &kaddr, sizeof(kaddr)) < 0 || !kaddr) break;
        intptr_t kpath = 0;
        if (kernel_copyout(kaddr + 0x08, &kpath, sizeof(kpath)) < 0) continue;
        if (kernel_copyout(kpath, path, sizeof(path)) < 0) continue;
        if (kernel_copyout(kaddr + 0x28, &handle, sizeof(handle)) < 0) continue;
        intptr_t mapbase = 0;
        kernel_copyout(kaddr + 0x30, &mapbase, sizeof(mapbase));
        LOG_INFO("  [MOD #%d] handle=0x%lx, mapbase=0x%lx, path=%s", count++, handle, (unsigned long)mapbase, path);
    }
}

#include <elf.h>
#include <stdlib.h>

typedef struct dynlib_dynsec_s {
  struct {
    unsigned long le_next;
    unsigned long le_prev;
  } list_entry;

  unsigned long sysvec;
  unsigned int refcount;
  unsigned long size;

  unsigned long symtab;
  unsigned long symtabsize;

  unsigned long strtab;
  unsigned long strtabsize;

  unsigned long pltrela;
  unsigned long pltrelasize;

  unsigned long rela;
  unsigned long relasize;

  unsigned long hash;
  unsigned long hashsize;

  unsigned long dynamic;
  unsigned long dynamicsize;

  unsigned long sce_comment;
  unsigned long sce_commentsize;

  unsigned long sce_dynlib;
  unsigned long sce_dynlibsize;

  unsigned long unknown1;
  unsigned long unknown1size;

  unsigned long buckets;
  unsigned long bucketssize;
  unsigned int nbuckets;

  unsigned long chains;
  unsigned long chainssize;
  unsigned int nchains;

  unsigned long unknown2[7];
} dynlib_dynsec_s_t;

typedef struct dynlib_obj_s {
  unsigned long next;
  unsigned long path;
  unsigned long unknown0[2];
  unsigned int refcount;
  unsigned long handle;

  unsigned long mapbase;
  unsigned long mapsize;
  unsigned long textsize;

  unsigned long database;
  unsigned long datasize;

  unsigned long unknown1;
  unsigned long unknown1size;

  unsigned long entry;
  unsigned long unknown2;
  unsigned long vaddrbase;

  unsigned int tlsindex;
  unsigned long tlsinit;
  unsigned long tlsinitsize;
  unsigned long tlssize;
  unsigned long tlsoffset;
  unsigned long tlsalign;

  unsigned long pltgot;

  unsigned long unknown3[6];

  unsigned long init;
  unsigned long fini;

  unsigned long eh_frame_hdr;
  unsigned long eh_frame_hdr_size;

  unsigned long eh_frame;
  unsigned long eh_frame_size;

  int status;
  int flags;

  unsigned long unknown4[5];
  unsigned long dynsec;
  unsigned long unknown5[6];
} dynlib_obj_s_t;

extern int kernel_dynlib_obj(int pid, unsigned int handle, void* obj);

static uint64_t resolve_symbol_in_proc(pid_t pid, uint32_t handle, const char* name) {
    if (!handle || !name) return 0;

    char nid[12] = {0};
    nid_encode(name, nid);

    dynlib_obj_s_t obj;
    memset(&obj, 0, sizeof(obj));
    if (kernel_dynlib_obj(pid, handle, &obj) != 0) {
        return 0;
    }

    dynlib_dynsec_s_t dynsec;
    memset(&dynsec, 0, sizeof(dynsec));
    if (kernel_copyout(obj.dynsec, &dynsec, sizeof(dynsec)) < 0) {
        return 0;
    }

    long buf_size = dynsec.symtabsize + dynsec.strtabsize;
    if (buf_size <= 0 || buf_size > 32 * 1024 * 1024) {
        return 0;
    }

    char* buf = (char*)malloc(buf_size);
    if (!buf) return 0;

    char* symtab = buf;
    char* strtab = buf + dynsec.symtabsize;

    if (kernel_copyout(dynsec.symtab, symtab, dynsec.symtabsize) < 0 ||
        kernel_copyout(dynsec.strtab, strtab, dynsec.strtabsize) < 0) {
        free(buf);
        return 0;
    }

    uint64_t vaddr = 0;
    size_t num_syms = dynsec.symtabsize / sizeof(Elf64_Sym);
    for (size_t i = 0; i < num_syms; i++) {
        Elf64_Sym* sym = (Elf64_Sym*)(symtab + i * sizeof(Elf64_Sym));
        if (!sym->st_value || sym->st_name >= dynsec.strtabsize) continue;
        const char* sym_name = strtab + sym->st_name;
        if (strncmp(nid, sym_name, 11) == 0 || strcmp(name, sym_name) == 0) {
            vaddr = obj.mapbase + sym->st_value;
            break;
        }
    }

    free(buf);
    return vaddr;
}

int __attribute__((section(".stager_shellcode$1"))) stager(SCEFunctions* functions) {
    pthread_t thread;
    functions->pthread_create_ptr(&thread, 0, (void*(*)(void*))functions->elf_main, functions->payload_args);
    asm("int3");
    return 0;
}

int __attribute__((section(".stager_shellcode$2"))) stager_end() {
    return 0;
}

static uint32_t get_shellcode_size() {
    return (uint32_t)((intptr_t)&stager_end - (intptr_t)&stager);
}

bool inject_elf_into_pid(pid_t pid, void* elf_data) {
    if (pid <= 0 || !elf_data) {
        LOG_ERROR("Invalid arguments for injection (pid=%d, elf=%p)", pid, elf_data);
        return false;
    }

    uint64_t original_authid = set_ucred_to_ptrace();
    escalate_process_caps(pid);
    LOG_INFO("Attaching to target process (PID: %d)...", pid);

    if (pt_attach(pid) < 0) {
        LOG_ERROR("Failed to pt_attach to PID %d (errno: %d)", pid, errno);
        restore_authid(original_authid);
        return false;
    }

    char nid[12] = {0};
    nid_encode("pthread_create", nid);
    intptr_t remote_pthread_create = pt_resolve(pid, nid);
    if (!remote_pthread_create) {
        LOG_ERROR("Failed to resolve remote pthread_create in PID %d", pid);
        pt_detach(pid, 0);
        restore_authid(original_authid);
        return false;
    }

    nid_encode("sceKernelDebugOutText", nid);
    intptr_t remote_debug_out = pt_resolve(pid, nid);

    SCEFunctions funcs;
    funcs.pthread_create_ptr = (void(*)(pthread_t*, void*, void*(*)(void*), void*))remote_pthread_create;
    funcs.sceKernelDebugOutText = (void(*)(int, const char*))remote_debug_out;

    LOG_INFO("Loading ELF into target process memory...");
    intptr_t entry = elfldr_load(pid, (uint8_t*)elf_data);
    if (entry <= 0) {
        LOG_ERROR("elfldr_load failed for PID %d", pid);
        pt_detach(pid, 0);
        restore_authid(original_authid);
        return false;
    }
    LOG_INFO("Remote ELF loaded. Entrypoint: 0x%lx", (unsigned long)entry);

    nid_encode("getpid", nid);
    intptr_t remote_getpid = pt_resolve(pid, nid);

    nid_encode("sceKernelDlsym", nid);
    intptr_t remote_dlsym = pt_resolve(pid, nid);

    LOG_INFO("Remote pointers: getpid=0x%lx, dlsym=0x%lx, debug_out=0x%lx",
             (unsigned long)remote_getpid, (unsigned long)remote_dlsym, (unsigned long)remote_debug_out);

    PayloadBootstrapArgs p_args = {0};
    p_args.getpid_addr = (uint64_t)remote_getpid;
    p_args.dlsym_addr = (uint64_t)remote_dlsym;
    p_args.debug_out_addr = (uint64_t)remote_debug_out;
    p_args.pthread_create_addr = (uint64_t)remote_pthread_create;

    LOG_INFO("Listing modules loaded in SceShellUI (PID: %d)...", pid);
    dump_target_modules(pid);

    LOG_INFO("Resolving target process modules...");

    // 1. libkernel.sprx (handle 1)
    p_args.libkernel_handle = 1;
    p_args.libkernel_mapbase = (uint64_t)kernel_dynlib_mapbase_addr(pid, 1);
    LOG_INFO("libkernel.sprx: handle=0x%x, mapbase=0x%lx",
             p_args.libkernel_handle, (unsigned long)p_args.libkernel_mapbase);

    // 2. libkernel_sys.sprx (usually handle 0x2001)
    uint32_t h_ksys = 0x2001;
    if (kernel_dynlib_handle(pid, "libkernel_sys.sprx", &h_ksys) == 0) {
        p_args.libkernelsys_handle = h_ksys;
    } else {
        p_args.libkernelsys_handle = 0x2001;
    }
    p_args.libkernelsys_mapbase = (uint64_t)kernel_dynlib_mapbase_addr(pid, p_args.libkernelsys_handle);
    LOG_INFO("libkernel_sys.sprx: handle=0x%x, mapbase=0x%lx",
             p_args.libkernelsys_handle, (unsigned long)p_args.libkernelsys_mapbase);

    // 3. libmonosgen-2.0.sprx
    uint32_t h_mono = 0;
    if (kernel_dynlib_handle(pid, "libmonosgen-2.0.sprx", &h_mono) == 0) {
        p_args.libmono_handle = h_mono;
        p_args.libmono_mapbase = (uint64_t)kernel_dynlib_mapbase_addr(pid, h_mono);
        LOG_INFO("libmonosgen-2.0.sprx: handle=0x%x, mapbase=0x%lx",
                 h_mono, (unsigned long)p_args.libmono_mapbase);
    } else {
        uint64_t mb_mono = 0;
        h_mono = find_module_by_substring(pid, "mono", &mb_mono);
        if (h_mono) {
            p_args.libmono_handle = h_mono;
            p_args.libmono_mapbase = mb_mono;
            LOG_INFO("libmonosgen found by substr: handle=0x%x, mapbase=0x%lx",
                     h_mono, (unsigned long)mb_mono);
        } else {
            LOG_WARN("Could not find libmonosgen in PID %d", pid);
        }
    }

    // 4. libSceNetCtl.sprx
    uint32_t h_netctl = 0;
    if (kernel_dynlib_handle(pid, "libSceNetCtl.sprx", &h_netctl) == 0) {
        p_args.libnetctl_handle = h_netctl;
        p_args.libnetctl_mapbase = (uint64_t)kernel_dynlib_mapbase_addr(pid, h_netctl);
        LOG_INFO("libSceNetCtl.sprx: handle=0x%x, mapbase=0x%lx",
                 h_netctl, (unsigned long)p_args.libnetctl_mapbase);
    }

    // 5. sceKernelMprotect
    nid_encode("sceKernelMprotect", nid);
    intptr_t remote_mprotect = pt_resolve(pid, nid);
    p_args.mprotect_addr = (uint64_t)remote_mprotect;
    LOG_INFO("sceKernelMprotect: 0x%lx", (unsigned long)remote_mprotect);

    // 6. Pre-resolve Mono symbols using kernel-level symbol table parser
    if (h_mono) {
        LOG_INFO("Pre-resolving Mono symbols in target process (PID: %d, handle: 0x%x)...", pid, h_mono);
        p_args.mono_get_root_domain = resolve_symbol_in_proc(pid, h_mono, "mono_get_root_domain");
        p_args.mono_domain_get = resolve_symbol_in_proc(pid, h_mono, "mono_domain_get");
        p_args.mono_domain_assembly_open = resolve_symbol_in_proc(pid, h_mono, "mono_domain_assembly_open");
        p_args.mono_assembly_get_image = resolve_symbol_in_proc(pid, h_mono, "mono_assembly_get_image");
        p_args.mono_class_from_name = resolve_symbol_in_proc(pid, h_mono, "mono_class_from_name");
        p_args.mono_class_get_method_from_name = resolve_symbol_in_proc(pid, h_mono, "mono_class_get_method_from_name");
        p_args.mono_class_get_property_from_name = resolve_symbol_in_proc(pid, h_mono, "mono_class_get_property_from_name");
        p_args.mono_property_get_get_method = resolve_symbol_in_proc(pid, h_mono, "mono_property_get_get_method");
        p_args.mono_property_get_set_method = resolve_symbol_in_proc(pid, h_mono, "mono_property_get_set_method");
        p_args.mono_compile_method = resolve_symbol_in_proc(pid, h_mono, "mono_compile_method");
        p_args.mono_runtime_invoke = resolve_symbol_in_proc(pid, h_mono, "mono_runtime_invoke");
        p_args.mono_runtime_object_init = resolve_symbol_in_proc(pid, h_mono, "mono_runtime_object_init");
        p_args.mono_object_new = resolve_symbol_in_proc(pid, h_mono, "mono_object_new");
        p_args.mono_object_unbox = resolve_symbol_in_proc(pid, h_mono, "mono_object_unbox");
        p_args.mono_string_new = resolve_symbol_in_proc(pid, h_mono, "mono_string_new");
        p_args.mono_string_to_utf8 = resolve_symbol_in_proc(pid, h_mono, "mono_string_to_utf8");
        p_args.mono_thread_attach = resolve_symbol_in_proc(pid, h_mono, "mono_thread_attach");
        p_args.mono_thread_current = resolve_symbol_in_proc(pid, h_mono, "mono_thread_current");
        p_args.mono_thread_detach = resolve_symbol_in_proc(pid, h_mono, "mono_thread_detach");

        LOG_INFO("Mono symbols: root_domain=0x%lx, class_from_name=0x%lx, runtime_invoke=0x%lx, thread_attach=0x%lx",
                 (unsigned long)p_args.mono_get_root_domain, (unsigned long)p_args.mono_class_from_name,
                 (unsigned long)p_args.mono_runtime_invoke, (unsigned long)p_args.mono_thread_attach);
    }

    // 7. Pre-resolve Telemetry symbols
    LOG_INFO("Pre-resolving Telemetry symbols...");
    p_args.sceKernelGetCpuTemperature = resolve_symbol_in_proc(pid, p_args.libkernelsys_handle, "sceKernelGetCpuTemperature");
    if (!p_args.sceKernelGetCpuTemperature) p_args.sceKernelGetCpuTemperature = resolve_symbol_in_proc(pid, 1, "sceKernelGetCpuTemperature");

    p_args.sceKernelGetSocSensorTemperature = resolve_symbol_in_proc(pid, p_args.libkernelsys_handle, "sceKernelGetSocSensorTemperature");
    if (!p_args.sceKernelGetSocSensorTemperature) p_args.sceKernelGetSocSensorTemperature = resolve_symbol_in_proc(pid, 1, "sceKernelGetSocSensorTemperature");

    p_args.get_page_table_stats = resolve_symbol_in_proc(pid, p_args.libkernelsys_handle, "get_page_table_stats");
    if (!p_args.get_page_table_stats) p_args.get_page_table_stats = resolve_symbol_in_proc(pid, 1, "get_page_table_stats");

    p_args.sceKernelGetCpuUsage = resolve_symbol_in_proc(pid, p_args.libkernelsys_handle, "sceKernelGetCpuUsage");
    if (!p_args.sceKernelGetCpuUsage) p_args.sceKernelGetCpuUsage = resolve_symbol_in_proc(pid, 1, "sceKernelGetCpuUsage");

    p_args.sceKernelGetThreadName = resolve_symbol_in_proc(pid, p_args.libkernelsys_handle, "sceKernelGetThreadName");
    if (!p_args.sceKernelGetThreadName) p_args.sceKernelGetThreadName = resolve_symbol_in_proc(pid, 1, "sceKernelGetThreadName");

    p_args.sceKernelClockGettime = resolve_symbol_in_proc(pid, p_args.libkernelsys_handle, "sceKernelClockGettime");
    if (!p_args.sceKernelClockGettime) p_args.sceKernelClockGettime = resolve_symbol_in_proc(pid, 1, "sceKernelClockGettime");

    p_args.sceKernelGetCurrentFanDuty = resolve_symbol_in_proc(pid, p_args.libkernelsys_handle, "sceKernelGetCurrentFanDuty");
    if (!p_args.sceKernelGetCurrentFanDuty) p_args.sceKernelGetCurrentFanDuty = resolve_symbol_in_proc(pid, 1, "sceKernelGetCurrentFanDuty");

    if (h_netctl) {
        p_args.sceNetCtlGetInfo = resolve_symbol_in_proc(pid, h_netctl, "sceNetCtlGetInfo");
    }

    p_args.sceKernelGetSocClock = resolve_symbol_in_proc(pid, p_args.libkernelsys_handle, "sceKernelGetSocClock");
    if (!p_args.sceKernelGetSocClock) p_args.sceKernelGetSocClock = resolve_symbol_in_proc(pid, 1, "sceKernelGetSocClock");

    p_args.sceKernelGetSocPowerConsumption = resolve_symbol_in_proc(pid, p_args.libkernelsys_handle, "sceKernelGetSocPowerConsumption");
    if (!p_args.sceKernelGetSocPowerConsumption) p_args.sceKernelGetSocPowerConsumption = resolve_symbol_in_proc(pid, 1, "sceKernelGetSocPowerConsumption");

    p_args.sceKernelGetCpuCoreClock = resolve_symbol_in_proc(pid, p_args.libkernelsys_handle, "sceKernelGetCpuCoreClock");
    if (!p_args.sceKernelGetCpuCoreClock) p_args.sceKernelGetCpuCoreClock = resolve_symbol_in_proc(pid, 1, "sceKernelGetCpuCoreClock");

    p_args.sceKernelGetCpuFrequency = resolve_symbol_in_proc(pid, p_args.libkernelsys_handle, "sceKernelGetCpuFrequency");
    if (!p_args.sceKernelGetCpuFrequency) p_args.sceKernelGetCpuFrequency = resolve_symbol_in_proc(pid, 1, "sceKernelGetCpuFrequency");

    LOG_INFO("Telemetry symbols: cpu_temp=0x%lx, soc_temp=0x%lx, pt_stats=0x%lx, cpu_usage=0x%lx, thread_name=0x%lx, netctl=0x%lx",
             (unsigned long)p_args.sceKernelGetCpuTemperature, (unsigned long)p_args.sceKernelGetSocSensorTemperature,
             (unsigned long)p_args.get_page_table_stats, (unsigned long)p_args.sceKernelGetCpuUsage,
             (unsigned long)p_args.sceKernelGetThreadName, (unsigned long)p_args.sceNetCtlGetInfo);
    LOG_INFO("Hardware symbols: soc_clk=0x%lx, soc_pwr=0x%lx, cpu_core_clk=0x%lx, cpu_freq=0x%lx",
             (unsigned long)p_args.sceKernelGetSocClock, (unsigned long)p_args.sceKernelGetSocPowerConsumption,
             (unsigned long)p_args.sceKernelGetCpuCoreClock, (unsigned long)p_args.sceKernelGetCpuFrequency);

    uint64_t remote_args = pt_mmap(pid, 0, sizeof(PayloadBootstrapArgs), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (!remote_args || remote_args == (uint64_t)-1) {
        LOG_ERROR("Failed to mmap remote bootstrap args memory in PID %d", pid);
        pt_detach(pid, 0);
        restore_authid(original_authid);
        return false;
    }
    pt_copyin(pid, &p_args, remote_args, sizeof(PayloadBootstrapArgs));

    funcs.elf_main = (void*)entry;
    funcs.payload_args = (void*)remote_args;

    uint32_t shellcode_size = get_shellcode_size();
    uint64_t bootstrap = pt_mmap(pid, 0, shellcode_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (!bootstrap || bootstrap == (uint64_t)-1) {
        LOG_ERROR("Failed to mmap bootstrap memory in PID %d", pid);
        pt_detach(pid, 0);
        restore_authid(original_authid);
        return false;
    }

    if (kernel_mprotect(pid, bootstrap, shellcode_size, PROT_EXEC | PROT_WRITE | PROT_READ) != 0) {
        LOG_ERROR("kernel_mprotect failed on bootstrap memory");
        pt_detach(pid, 0);
        restore_authid(original_authid);
        return false;
    }

    if (pt_copyin(pid, stager, bootstrap, shellcode_size) != 0) {
        LOG_ERROR("Failed to copy stager shellcode into target");
        pt_detach(pid, 0);
        restore_authid(original_authid);
        return false;
    }

    uint64_t sce_mem = pt_mmap(pid, 0, sizeof(SCEFunctions), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (!sce_mem || sce_mem == (uint64_t)-1) {
        LOG_ERROR("Failed to mmap SCEFunctions memory in target");
        pt_detach(pid, 0);
        restore_authid(original_authid);
        return false;
    }

    if (pt_copyin(pid, &funcs, sce_mem, sizeof(SCEFunctions)) != 0) {
        LOG_ERROR("Failed to copy SCEFunctions struct into target");
        pt_detach(pid, 0);
        restore_authid(original_authid);
        return false;
    }

    LOG_INFO("Triggering remote stager execution...");
    if (pt_call2(pid, bootstrap, sce_mem) == -1) {
        LOG_ERROR("pt_call2 stager execution failed (errno: %d)", errno);
        pt_detach(pid, 0);
        restore_authid(original_authid);
        return false;
    }

    LOG_INFO("Stager executed successfully. Detaching...");
    pt_detach(pid, 0);
    restore_authid(original_authid);

    LOG_INFO("Injection completed successfully!");
    return true;
}
