#pragma once

#include "monodef.h"
#include <cstdint>
#include <cstddef>
#include <string>

typedef int (*sceKernelDlsym_t)(int handle, const char* sym, void** addrp);

extern "C" {
    extern uintptr_t ptr_syscall;
    extern sceKernelDlsym_t p_sceKernelDlsym;
    long __syscall(long sysno, ...);
}

// Global mono pointers
extern MonoDomain* Root_Domain;
extern MonoImage* pui_img;
extern MonoImage* AppSystem_img;
extern MonoImage* Core_img;
extern MonoObject* Game;
extern uintptr_t g_libmono_mapbase;

// Function pointers to mono runtime
extern MonoDomain* (*mono_get_root_domain)(void);
extern MonoDomain* (*mono_domain_get)(void);
extern MonoAssembly* (*mono_domain_assembly_open)(MonoDomain*, const char*);
extern MonoImage* (*mono_assembly_get_image)(MonoAssembly*);
extern MonoClass* (*mono_class_from_name)(MonoImage*, const char*, const char*);
extern MonoMethod* (*mono_class_get_method_from_name)(MonoClass*, const char*, int);
extern MonoProperty* (*mono_class_get_property_from_name)(MonoClass*, const char*);
extern MonoMethod* (*mono_property_get_get_method)(MonoProperty*);
extern MonoMethod* (*mono_property_get_set_method)(MonoProperty*);
extern void* (*mono_compile_method)(MonoMethod*);
extern MonoObject* (*mono_runtime_invoke)(MonoMethod*, void*, void**, MonoObject**);
extern void (*mono_runtime_object_init)(MonoObject*);
extern MonoObject* (*mono_object_new)(MonoDomain*, MonoClass*);
extern void* (*mono_object_unbox)(MonoObject*);
extern MonoString* (*mono_string_new)(MonoDomain*, const char*);
extern char* (*mono_string_to_utf8)(MonoString*);
extern void* (*mono_thread_attach)(MonoDomain*);
extern void* (*mono_thread_current)(void);
extern void (*mono_thread_detach)(void*);

int get_module_handle(pid_t pid, const char* module_name, uintptr_t* out_mapbase = nullptr);
void* resolve_symbol(int handle, uintptr_t mapbase, const char* name);
bool resolve_mono_symbols(int libmono_handle, uintptr_t mapbase);
MonoImage* getDLLimage(const char* dll_name);
MonoObject* New_Object(MonoClass* klass);
MonoObject* CreateUIColor(float r, float g, float b, float a);
MonoObject* CreateUIFont(int size, int style, int weight);
MonoObject* CreateLabel(const char* name, float x, float y, const char* text, MonoObject* font, int horzAlign, int vertAlign, float r, float g, float b, float a);
void Widget_Append_Child(MonoObject* widget, MonoObject* child);
uint64_t Get_Address_of_Method(MonoImage* img, const char* ns, const char* cls, const char* meth, int argc);
uint64_t Get_Address_of_Method(MonoClass* klass, const char* meth, int argc);
std::string Mono_to_String(MonoString* str);
void Patch_Main_thread_Check(MonoImage* core_img);

template <typename result>
result Get_Property(MonoClass* Klass, MonoObject* Instance, const char* Property_Name) {
    if (!Klass || !Instance) return (result)0;
    MonoProperty* Prop = mono_class_get_property_from_name(Klass, Property_Name);
    if (!Prop) return (result)0;
    MonoMethod* Get_Method = mono_property_get_get_method(Prop);
    if (!Get_Method) return (result)0;
    void* thunk = mono_compile_method(Get_Method);
    if (!thunk) return (result)0;
    auto Method = reinterpret_cast<result(*)(MonoObject*)>(thunk);
    return Method(Instance);
}

template <typename result>
result Get_Property(MonoImage* Assembly_Image, const char* Namespace, const char* Class_Name, MonoObject* Instance, const char* Property_Name) {
    return Get_Property<result>(mono_class_from_name(Assembly_Image, Namespace, Class_Name), Instance, Property_Name);
}

template <typename Param>
void Set_Property(MonoClass* Klass, MonoObject* Instance, const char* Property_Name, Param Value) {
    if (!Klass || !Instance) return;
    MonoProperty* Prop = mono_class_get_property_from_name(Klass, Property_Name);
    if (!Prop) return;
    MonoMethod* Set_Method = mono_property_get_set_method(Prop);
    if (!Set_Method) return;
    uint64_t thunk = (uint64_t)mono_compile_method(Set_Method);
    if (!thunk) return;
    void(*Method)(MonoObject*, Param) = (void(*)(MonoObject*, Param))thunk;
    Method(Instance, Value);
}

template <typename Param>
void Set_Property_Invoke(MonoClass* Klass, MonoObject* Instance, const char* Property_Name, Param Value) {
    if (!Klass || !Instance) return;
    MonoProperty* Prop = mono_class_get_property_from_name(Klass, Property_Name);
    if (!Prop) return;
    MonoMethod* Set_Method = mono_property_get_set_method(Prop);
    if (!Set_Method) return;
    void* args[1] = { (void*)Value };
    mono_runtime_invoke(Set_Method, Instance, args, nullptr);
}

template <typename... Args>
void Invoke_Ctor(MonoClass* klass, MonoObject* instance, Args... args) {
    int count = sizeof...(args);
    MonoMethod* method = mono_class_get_method_from_name(klass, ".ctor", count);
    if (!method) return;
    uint64_t thunk = (uint64_t)mono_compile_method(method);
    if (!thunk) return;
    void(*fn)(MonoObject*, Args...) = (void(*)(MonoObject*, Args...))thunk;
    fn(instance, args...);
}

template <typename result, typename... Args>
result Invoke(MonoImage* Assembly_Image, MonoClass* klass, MonoObject* Instance, const char* Method_Name, Args... args) {
    (void)Assembly_Image;
    uint64_t ThunkAddress = Get_Address_of_Method(klass, Method_Name, sizeof...(Args));
    if (!ThunkAddress) return (result)0;
    if (Instance) {
        auto Method = reinterpret_cast<result(*)(MonoObject*, Args...)>(ThunkAddress);
        return Method(Instance, args...);
    } else {
        auto Method = reinterpret_cast<result(*)(Args...)>(ThunkAddress);
        return Method(args...);
    }
}
