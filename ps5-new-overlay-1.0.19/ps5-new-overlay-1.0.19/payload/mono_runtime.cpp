#include "mono_runtime.hpp"
#include "../common/log.h"
#include "../common/nid.h"
#include "../common/bootstrap.h"
#include <elf.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <string>

extern "C" {
    extern int (*sceKernelMprotect)(void *addr, size_t len, int prot);
    int kernel_mprotect(pid_t pid, intptr_t addr, size_t size, int prot);
}

#ifndef SYS_dl_get_list
#define SYS_dl_get_list 0x217
#endif

#ifndef SYS_dl_get_info_2
#define SYS_dl_get_info_2 0x2cd
#endif

#ifndef MODULE_INFO_NAME_LENGTH
#define MODULE_INFO_NAME_LENGTH 128
#endif
#ifndef MODULE_INFO_SANDBOXED_PATH_LENGTH
#define MODULE_INFO_SANDBOXED_PATH_LENGTH 1024
#endif
#ifndef MODULE_INFO_MAX_SECTIONS
#define MODULE_INFO_MAX_SECTIONS 4
#endif
#ifndef FINGERPRINT_LENGTH
#define FINGERPRINT_LENGTH 20
#endif

typedef struct {
    uint64_t vaddr;
    uint32_t size;
    uint32_t prot;
} module_section_t;

typedef struct {
    char filename[MODULE_INFO_NAME_LENGTH];
    uint64_t handle;
    uint8_t unknown0[32];
    uint64_t init;
    uint64_t fini;
    uint64_t eh_frame_hdr;
    uint64_t eh_frame_hdr_sz;
    uint64_t eh_frame;
    uint64_t eh_frame_sz;
    module_section_t sections[MODULE_INFO_MAX_SECTIONS];
    uint8_t unknown7[1176];
    uint8_t fingerprint[FINGERPRINT_LENGTH];
    uint32_t unknown8;
    char libname[MODULE_INFO_NAME_LENGTH];
    uint32_t unknown9;
    char sandboxed_path[MODULE_INFO_SANDBOXED_PATH_LENGTH];
    uint64_t sdk_version;
} module_info_t;

extern "C" {
    __attribute__((section(".data"))) uintptr_t ptr_syscall = 0;
    __attribute__((section(".data"))) sceKernelDlsym_t p_sceKernelDlsym = nullptr;

    __attribute__((naked)) long __syscall(long sysno, ...) {
        asm(".intel_syntax noprefix\n"
            "  mov rax, rdi\n"
            "  mov rdi, rsi\n"
            "  mov rsi, rdx\n"
            "  mov rdx, rcx\n"
            "  mov r10, r8\n"
            "  mov r8,  r9\n"
            "  mov r9,  qword ptr [rsp + 8]\n"
            "  call qword ptr [rip + ptr_syscall]\n"
            "  ret\n");
    }
}

uintptr_t g_libmono_mapbase = 0;
extern PayloadBootstrapArgs g_bargs;

int get_module_handle(pid_t pid, const char* module_name, uintptr_t* out_mapbase) {
    // 1. Check if provided by loader bootstrap args
    if (strcmp(module_name, "libkernel_sys.sprx") == 0) {
        int h = g_bargs.libkernelsys_handle ? (int)g_bargs.libkernelsys_handle : 0x2001;
        if (out_mapbase) *out_mapbase = (uintptr_t)g_bargs.libkernelsys_mapbase;
        LOG_INFO("get_module_handle: '%s' -> handle=%d, mapbase=0x%lx (from bootstrap)",
                 module_name, h, (unsigned long)(out_mapbase ? *out_mapbase : 0));
        return h;
    }
    if (strcmp(module_name, "libmonosgen-2.0.sprx") == 0) {
        if (g_bargs.libmono_handle) {
            if (out_mapbase) *out_mapbase = (uintptr_t)g_bargs.libmono_mapbase;
            LOG_INFO("get_module_handle: '%s' -> handle=%d, mapbase=0x%lx (from bootstrap)",
                     module_name, (int)g_bargs.libmono_handle, (unsigned long)(out_mapbase ? *out_mapbase : 0));
            return (int)g_bargs.libmono_handle;
        }
    }
    if (strcmp(module_name, "libSceNetCtl.sprx") == 0) {
        if (g_bargs.libnetctl_handle) {
            if (out_mapbase) *out_mapbase = (uintptr_t)g_bargs.libnetctl_mapbase;
            LOG_INFO("get_module_handle: '%s' -> handle=%d, mapbase=0x%lx (from bootstrap)",
                     module_name, (int)g_bargs.libnetctl_handle, (unsigned long)(out_mapbase ? *out_mapbase : 0));
            return (int)g_bargs.libnetctl_handle;
        }
    }
    if (strcmp(module_name, "libkernel.sprx") == 0) {
        if (out_mapbase) *out_mapbase = (uintptr_t)g_bargs.libkernel_mapbase;
        LOG_INFO("get_module_handle: '%s' -> handle=1, mapbase=0x%lx (from bootstrap)",
                 module_name, (unsigned long)(out_mapbase ? *out_mapbase : 0));
        return 1;
    }

    // 2. Fallback: try SYS_dl_get_list
    size_t num_handles = 0;
    long ret = __syscall(SYS_dl_get_list, pid, NULL, 0, &num_handles);
    LOG_INFO("SYS_dl_get_list query for '%s': ret=%ld, num_handles=%zu", module_name, ret, num_handles);

    if (num_handles > 0) {
        uintptr_t* handles = (uintptr_t*)calloc(num_handles, sizeof(uintptr_t));
        if (handles) {
            __syscall(SYS_dl_get_list, pid, handles, num_handles, &num_handles);
            int found_handle = 0;
            module_info_t mod_info;
            for (size_t i = 0; i < num_handles; ++i) {
                memset(&mod_info, 0, sizeof(mod_info));
                __syscall(SYS_dl_get_info_2, pid, 1, handles[i], &mod_info);
                const char* fn = mod_info.filename;
                const char* slash = strrchr(fn, '/');
                if (slash) fn = slash + 1;
                if (strcmp(fn, module_name) == 0 || strcmp(mod_info.filename, module_name) == 0) {
                    found_handle = (int)mod_info.handle;
                    if (out_mapbase) {
                        *out_mapbase = mod_info.sections[0].vaddr;
                    }
                    break;
                }
            }
            free(handles);
            if (found_handle > 0) {
                LOG_INFO("get_module_handle: '%s' found via dl_get_list -> handle=%d", module_name, found_handle);
                return found_handle;
            }
        }
    }

    LOG_WARN("get_module_handle: could not find '%s'", module_name);
    return 0;
}

void* resolve_symbol(int handle, uintptr_t mapbase, const char* name) {
    (void)mapbase;
    void* addr = nullptr;
    if (!p_sceKernelDlsym) return nullptr;

    // 1. Try by plaintext name in provided handle
    if (handle > 0 && p_sceKernelDlsym(handle, name, &addr) == 0 && addr) {
        return addr;
    }

    // 2. Try by encoded NID in provided handle
    char nid[12] = {0};
    nid_encode(name, nid);
    if (handle > 0 && p_sceKernelDlsym(handle, nid, &addr) == 0 && addr) {
        return addr;
    }

    // 3. Fallback: try libkernel_sys (0x2001) if not already searched
    if (handle != 0x2001) {
        if (p_sceKernelDlsym(0x2001, name, &addr) == 0 && addr) return addr;
        if (p_sceKernelDlsym(0x2001, nid, &addr) == 0 && addr) return addr;
    }

    // 4. Fallback: try libkernel (0x1) if not already searched
    if (handle != 0x1) {
        if (p_sceKernelDlsym(0x1, name, &addr) == 0 && addr) return addr;
        if (p_sceKernelDlsym(0x1, nid, &addr) == 0 && addr) return addr;
    }

    return nullptr;
}

MonoDomain* Root_Domain = nullptr;
MonoImage* pui_img = nullptr;
MonoImage* AppSystem_img = nullptr;
MonoImage* Core_img = nullptr;
MonoObject* Game = nullptr;

MonoDomain* (*mono_get_root_domain)(void) = nullptr;
MonoDomain* (*mono_domain_get)(void) = nullptr;
MonoAssembly* (*mono_domain_assembly_open)(MonoDomain*, const char*) = nullptr;
MonoImage* (*mono_assembly_get_image)(MonoAssembly*) = nullptr;
MonoClass* (*mono_class_from_name)(MonoImage*, const char*, const char*) = nullptr;
MonoMethod* (*mono_class_get_method_from_name)(MonoClass*, const char*, int) = nullptr;
MonoProperty* (*mono_class_get_property_from_name)(MonoClass*, const char*) = nullptr;
MonoMethod* (*mono_property_get_get_method)(MonoProperty*) = nullptr;
MonoMethod* (*mono_property_get_set_method)(MonoProperty*) = nullptr;
void* (*mono_compile_method)(MonoMethod*) = nullptr;
MonoObject* (*mono_runtime_invoke)(MonoMethod*, void*, void**, MonoObject**) = nullptr;
void (*mono_runtime_object_init)(MonoObject*) = nullptr;
MonoObject* (*mono_object_new)(MonoDomain*, MonoClass*) = nullptr;
void* (*mono_object_unbox)(MonoObject*) = nullptr;
MonoString* (*mono_string_new)(MonoDomain*, const char*) = nullptr;
char* (*mono_string_to_utf8)(MonoString*) = nullptr;
void* (*mono_thread_attach)(MonoDomain*) = nullptr;
void* (*mono_thread_current)(void) = nullptr;
void (*mono_thread_detach)(void*) = nullptr;

#define RESOLVE_SYM(h, mb, sym) *(void**)&sym = resolve_symbol(h, mb, #sym)

bool resolve_mono_symbols(int libmono_handle, uintptr_t mapbase) {
    if (g_bargs.mono_get_root_domain) {
        LOG_INFO("Using pre-resolved Mono symbols from bootstrap args...");
        mono_get_root_domain = (MonoDomain* (*)(void))g_bargs.mono_get_root_domain;
        mono_domain_get = (MonoDomain* (*)(void))g_bargs.mono_domain_get;
        mono_domain_assembly_open = (MonoAssembly* (*)(MonoDomain*, const char*))g_bargs.mono_domain_assembly_open;
        mono_assembly_get_image = (MonoImage* (*)(MonoAssembly*))g_bargs.mono_assembly_get_image;
        mono_class_from_name = (MonoClass* (*)(MonoImage*, const char*, const char*))g_bargs.mono_class_from_name;
        mono_class_get_method_from_name = (MonoMethod* (*)(MonoClass*, const char*, int))g_bargs.mono_class_get_method_from_name;
        mono_class_get_property_from_name = (MonoProperty* (*)(MonoClass*, const char*))g_bargs.mono_class_get_property_from_name;
        mono_property_get_get_method = (MonoMethod* (*)(MonoProperty*))g_bargs.mono_property_get_get_method;
        mono_property_get_set_method = (MonoMethod* (*)(MonoProperty*))g_bargs.mono_property_get_set_method;
        mono_compile_method = (void* (*)(MonoMethod*))g_bargs.mono_compile_method;
        mono_runtime_invoke = (MonoObject* (*)(MonoMethod*, void*, void**, MonoObject**))g_bargs.mono_runtime_invoke;
        mono_runtime_object_init = (void (*)(MonoObject*))g_bargs.mono_runtime_object_init;
        mono_object_new = (MonoObject* (*)(MonoDomain*, MonoClass*))g_bargs.mono_object_new;
        mono_object_unbox = (void* (*)(MonoObject*))g_bargs.mono_object_unbox;
        mono_string_new = (MonoString* (*)(MonoDomain*, const char*))g_bargs.mono_string_new;
        mono_string_to_utf8 = (char* (*)(MonoString*))g_bargs.mono_string_to_utf8;
        mono_thread_attach = (void* (*)(MonoDomain*))g_bargs.mono_thread_attach;
        mono_thread_current = (void* (*)(void))g_bargs.mono_thread_current;
        mono_thread_detach = (void (*)(void*))g_bargs.mono_thread_detach;
    } else {
        if (libmono_handle <= 0 && mapbase == 0) return false;

        RESOLVE_SYM(libmono_handle, mapbase, mono_get_root_domain);
        RESOLVE_SYM(libmono_handle, mapbase, mono_domain_get);
        RESOLVE_SYM(libmono_handle, mapbase, mono_domain_assembly_open);
        RESOLVE_SYM(libmono_handle, mapbase, mono_assembly_get_image);
        RESOLVE_SYM(libmono_handle, mapbase, mono_class_from_name);
        RESOLVE_SYM(libmono_handle, mapbase, mono_class_get_method_from_name);
        RESOLVE_SYM(libmono_handle, mapbase, mono_class_get_property_from_name);
        RESOLVE_SYM(libmono_handle, mapbase, mono_property_get_get_method);
        RESOLVE_SYM(libmono_handle, mapbase, mono_property_get_set_method);
        RESOLVE_SYM(libmono_handle, mapbase, mono_compile_method);
        RESOLVE_SYM(libmono_handle, mapbase, mono_runtime_invoke);
        RESOLVE_SYM(libmono_handle, mapbase, mono_runtime_object_init);
        RESOLVE_SYM(libmono_handle, mapbase, mono_object_new);
        RESOLVE_SYM(libmono_handle, mapbase, mono_object_unbox);
        RESOLVE_SYM(libmono_handle, mapbase, mono_string_new);
        RESOLVE_SYM(libmono_handle, mapbase, mono_string_to_utf8);
        RESOLVE_SYM(libmono_handle, mapbase, mono_thread_attach);
        RESOLVE_SYM(libmono_handle, mapbase, mono_thread_current);
        RESOLVE_SYM(libmono_handle, mapbase, mono_thread_detach);
    }

    LOG_INFO("Mono symbols: root_domain=%p, domain_get=%p, class_from_name=%p, runtime_invoke=%p, thread_attach=%p",
             (void*)mono_get_root_domain, (void*)mono_domain_get, (void*)mono_class_from_name,
             (void*)mono_runtime_invoke, (void*)mono_thread_attach);

    if (!mono_get_root_domain || !mono_class_from_name || !mono_runtime_invoke || !mono_thread_attach) {
        LOG_ERROR("Failed to resolve critical mono symbols! (root_domain=%p, class_from_name=%p, runtime_invoke=%p, thread_attach=%p)",
                  (void*)mono_get_root_domain, (void*)mono_class_from_name,
                  (void*)mono_runtime_invoke, (void*)mono_thread_attach);
        return false;
    }

    Root_Domain = mono_get_root_domain();
    LOG_INFO("Mono symbols resolved. Root domain: %p", Root_Domain);
    return true;
}

MonoImage* getDLLimage(const char* dll_file) {
    std::string dll_path = std::string("/system_ex/common_ex/lib/") + dll_file;
    MonoAssembly* assembly = mono_domain_assembly_open(Root_Domain, dll_path.c_str());
    if (!assembly) {
        LOG_ERROR("Failed to open assembly %s", dll_path.c_str());
        return nullptr;
    }
    MonoImage* img = mono_assembly_get_image(assembly);
    if (!img) {
        LOG_ERROR("Failed to get image for %s", dll_path.c_str());
        return nullptr;
    }
    return img;
}

MonoObject* New_Object(MonoClass* klass) {
    if (!klass) return nullptr;
    return mono_object_new(Root_Domain, klass);
}

MonoObject* CreateUIColor(float r, float g, float b, float a) {
    MonoClass* uIColor = mono_class_from_name(pui_img, "Sce.PlayStation.PUI", "UIColor");
    if (!uIColor) return nullptr;
    MonoObject* uIColorInstance = New_Object(uIColor);
    if (!uIColorInstance) return nullptr;
    MonoObject* realInstance = (MonoObject*)mono_object_unbox(uIColorInstance);

    Invoke_Ctor(uIColor, realInstance, r, g, b, a);
    return realInstance;
}

MonoObject* CreateUIFont(int size, int style, int weight) {
    MonoClass* uIFont = mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "UIFont");
    if (!uIFont) return nullptr;
    MonoObject* uIFontInstance = New_Object(uIFont);
    if (!uIFontInstance) return nullptr;
    MonoObject* realInstance = (MonoObject*)mono_object_unbox(uIFontInstance);

    Invoke_Ctor(uIFont, realInstance, size, style, weight);
    return realInstance;
}

MonoObject* CreateLabel(const char* name, float x, float y, const char* text, MonoObject* font, int horzAlign, int vertAlign, float r, float g, float b, float a) {
    MonoClass* labelClass = mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Label");
    if (!labelClass) return nullptr;

    MonoObject* labelInstance = New_Object(labelClass);
    if (!labelInstance) return nullptr;

    mono_runtime_object_init(labelInstance);

    Set_Property(labelClass, labelInstance, "Name", mono_string_new(Root_Domain, name));
    Set_Property(labelClass, labelInstance, "PositionType", 1);
    Set_Property(labelClass, labelInstance, "MarginLeft", x);
    Set_Property(labelClass, labelInstance, "MarginTop", y);
    Set_Property(labelClass, labelInstance, "Text", mono_string_new(Root_Domain, text));
    if (font) {
        Set_Property_Invoke(labelClass, labelInstance, "Font", font);
    }
    Set_Property(labelClass, labelInstance, "HorizontalAlignment", horzAlign);
    Set_Property(labelClass, labelInstance, "VerticalAlignment", vertAlign);
    MonoObject* textColor = CreateUIColor(r, g, b, a);
    if (textColor) {
        Set_Property_Invoke(labelClass, labelInstance, "TextColor", textColor);
    }

    Set_Property(labelClass, labelInstance, "FitWidthToText", false);
    Set_Property(labelClass, labelInstance, "FitHeightToText", true);
    Set_Property(labelClass, labelInstance, "NumberOfLines", 1);
    Set_Property(labelClass, labelInstance, "EnableThemedTextShadow", true);

    return labelInstance;
}

void Widget_Append_Child(MonoObject* widget, MonoObject* child) {
    if (!widget || !child) return;
    MonoClass* widgetClass = mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Widget");
    if (!widgetClass) return;
    MonoMethod* appendChild = mono_class_get_method_from_name(widgetClass, "AppendChild", 1);
    if (!appendChild) return;

    void* args[1] = { child };
    mono_runtime_invoke(appendChild, widget, args, nullptr);
}

uint64_t Get_Address_of_Method(MonoImage* img, const char* ns, const char* cls, const char* meth, int argc) {
    if (!img) return 0;
    MonoClass* klass = mono_class_from_name(img, ns, cls);
    if (!klass) return 0;
    return Get_Address_of_Method(klass, meth, argc);
}

uint64_t Get_Address_of_Method(MonoClass* klass, const char* meth, int argc) {
    if (!klass) return 0;
    MonoMethod* method = mono_class_get_method_from_name(klass, meth, argc);
    if (!method) return 0;
    return (uint64_t)mono_compile_method(method);
}

std::string Mono_to_String(MonoString* str) {
    if (!str || !mono_string_to_utf8) return "";
    char* cstr = mono_string_to_utf8(str);
    if (!cstr) return "";
    std::string s(cstr);
    return s;
}

void Patch_Main_thread_Check(MonoImage* core_img) {
    if (!core_img) return;
    MonoClass* diag_class = mono_class_from_name(core_img, "Sce.PlayStation.Core.Runtime", "Diagnostics");
    if (!diag_class) {
        LOG_ERROR("Diagnostics class not found in Sce.PlayStation.Core");
        return;
    }
    MonoMethod* check_method = mono_class_get_method_from_name(diag_class, "CheckRunningOnMainThread", 0);
    if (!check_method) {
        LOG_ERROR("CheckRunningOnMainThread method not found");
        return;
    }
    uint64_t real_addr = (uint64_t)mono_compile_method(check_method);
    if (!real_addr) {
        LOG_ERROR("Failed to compile CheckRunningOnMainThread");
        return;
    }
    LOG_INFO("CheckRunningOnMainThread address: 0x%lx", (unsigned long)real_addr);

    uint64_t page_addr = real_addr & ~0xFFFULL;
    int r = -1;
    if (sceKernelMprotect) {
        r = sceKernelMprotect((void*)page_addr, 0x1000, 7); // PROT_READ | PROT_WRITE | PROT_EXEC
    }
    if (r != 0) {
        r = kernel_mprotect(getpid(), page_addr, 0x1000, 7);
    }

    if (r == 0) {
        *(volatile uint8_t*)real_addr = 0xC3; // x86 'ret'
        if (sceKernelMprotect) {
            sceKernelMprotect((void*)page_addr, 0x1000, 5); // PROT_READ | PROT_EXEC
        } else {
            kernel_mprotect(getpid(), page_addr, 0x1000, 5);
        }
        LOG_INFO("CheckRunningOnMainThread successfully patched with ret (0xC3)!");
    } else {
        LOG_ERROR("Failed to mprotect CheckRunningOnMainThread page (r=%d)", r);
    }
}
