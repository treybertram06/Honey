#include "hnpch.h"
#include "dotnet_host.h"

#include "nethost.h"
#include "coreclr_delegates.h"
#include <climits>

#if defined(HN_PLATFORM_LINUX)
    #include <dlfcn.h>
#elif defined(HN_PLATFORM_WINDOWS)
    #include <windows.h>
#else
    #error "Only Linux and Windows support C# scripting; macOS support coming eventually."
#endif

namespace Honey {

#if defined(HN_PLATFORM_WINDOWS)
    static std::wstring narrow_to_wide(std::string_view sv) {
        if (sv.empty()) return {};
        int len = MultiByteToWideChar(CP_UTF8, 0, sv.data(), (int)sv.size(), nullptr, 0);
        std::wstring ws(len, 0);
        MultiByteToWideChar(CP_UTF8, 0, sv.data(), (int)sv.size(), ws.data(), len);
        return ws;
    }
#endif

    bool DotNetHost::init(const std::filesystem::path& runtime_config_path,
                          const std::filesystem::path& dotnet_root) {

        get_hostfxr_parameters params{};
        params.size = sizeof(params);
        params.dotnet_root = dotnet_root.c_str(); // char* on Linux, wchar_t* on Windows

        char_t hostfxr_path[2048];
        size_t path_size = sizeof(hostfxr_path) / sizeof(char_t);
        if (get_hostfxr_path(hostfxr_path, &path_size, &params) != 0) {
            HN_CORE_ERROR("[DotNetHost] failed to locate hostfxr");
            return false;
        }

#if defined(HN_PLATFORM_LINUX)
        m_hostfxr_lib = dlopen(hostfxr_path, RTLD_LAZY | RTLD_LOCAL);
        if (!m_hostfxr_lib) {
            HN_CORE_ERROR("[DotNetHost] failed to load hostfxr: {}", dlerror());
            return false;
        }
        auto load_sym = [&](const char* name) -> void* {
            void* sym = dlsym(m_hostfxr_lib, name);
            if (!sym) HN_CORE_ERROR("[DotNetHost] failed to load symbol '{}': {}", name, dlerror());
            return sym;
        };
#elif defined(HN_PLATFORM_WINDOWS)
        m_hostfxr_lib = (void*)LoadLibraryW(hostfxr_path);
        if (!m_hostfxr_lib) {
            HN_CORE_ERROR("[DotNetHost] failed to load hostfxr (error 0x{:x})", GetLastError());
            return false;
        }
        auto load_sym = [&](const char* name) -> void* {
            void* sym = (void*)GetProcAddress((HMODULE)m_hostfxr_lib, name);
            if (!sym) HN_CORE_ERROR("[DotNetHost] failed to load symbol '{}' (error 0x{:x})", name, GetLastError());
            return sym;
        };
#endif

        auto init_fn = (hostfxr_initialize_for_runtime_config_fn)load_sym("hostfxr_initialize_for_runtime_config");
        auto get_del = (hostfxr_get_runtime_delegate_fn)load_sym("hostfxr_get_runtime_delegate");
        m_close_fn = load_sym("hostfxr_close");

        if (!init_fn || !get_del || !m_close_fn) {
#if defined(HN_PLATFORM_LINUX)
            dlclose(m_hostfxr_lib);
#elif defined(HN_PLATFORM_WINDOWS)
            FreeLibrary((HMODULE)m_hostfxr_lib);
#endif
            m_hostfxr_lib = nullptr;
            return false;
        }

        hostfxr_initialize_parameters init_params{};
        init_params.size = sizeof(init_params);
        init_params.dotnet_root = dotnet_root.c_str();

#if defined(HN_PLATFORM_LINUX)
        const std::string config_str = runtime_config_path.string();
        int32_t rc = init_fn(config_str.c_str(), &init_params, &m_host_ctx);
#elif defined(HN_PLATFORM_WINDOWS)
        int32_t rc = init_fn(runtime_config_path.c_str(), &init_params, &m_host_ctx);
#endif

        if (rc < 0) {
            HN_CORE_ERROR("[DotNetHost] hostfxr_initialize_for_runtime_config failed (0x{:x})", (uint32_t)rc);
#if defined(HN_PLATFORM_LINUX)
            dlclose(m_hostfxr_lib);
#elif defined(HN_PLATFORM_WINDOWS)
            FreeLibrary((HMODULE)m_hostfxr_lib);
#endif
            m_hostfxr_lib = nullptr;
            return false;
        }
        if (rc > 0) {
            HN_CORE_WARN("[DotNetHost] init returned informational code 0x{:x}", rc);
        }

        rc = get_del(m_host_ctx, hdt_load_assembly_and_get_function_pointer, &m_load_assembly_fn);
        if (rc != 0 || !m_load_assembly_fn) {
            HN_CORE_ERROR("[DotNetHost] get_runtime_delegate failed (0x{:x})", (uint32_t)rc);
            ((hostfxr_close_fn)m_close_fn)(m_host_ctx);
#if defined(HN_PLATFORM_LINUX)
            dlclose(m_hostfxr_lib);
#elif defined(HN_PLATFORM_WINDOWS)
            FreeLibrary((HMODULE)m_hostfxr_lib);
#endif
            m_host_ctx    = nullptr;
            m_hostfxr_lib = nullptr;
            return false;
        }

        HN_CORE_INFO("[DotNetHost] .NET 10 runtime initialized");
        return true;
    }

    void DotNetHost::shutdown() {
        if (m_host_ctx) {
            ((hostfxr_close_fn)m_close_fn)(m_host_ctx);
            m_host_ctx = nullptr;
        }
        if (m_hostfxr_lib) {
#if defined(HN_PLATFORM_LINUX)
            dlclose(m_hostfxr_lib);
#elif defined(HN_PLATFORM_WINDOWS)
            FreeLibrary((HMODULE)m_hostfxr_lib);
#endif
            m_hostfxr_lib = nullptr;
        }
        m_load_assembly_fn = nullptr;
        m_close_fn         = nullptr;
    }

    void* DotNetHost::load_managed_function(std::string_view assembly_path, std::string_view type_name,
        std::string_view method_name, std::string_view delegate_type_name) {

        HN_CORE_ASSERT(m_load_assembly_fn, "DotNetHost not initialized");

        auto load = (load_assembly_and_get_function_pointer_fn)m_load_assembly_fn;

        void* fn_ptr = nullptr;
        int32_t rc;

#if defined(HN_PLATFORM_LINUX)
        const char_t* delegate_arg = delegate_type_name.empty()
            ? UNMANAGEDCALLERSONLY_METHOD
            : delegate_type_name.data();

        rc = load(
            assembly_path.data(),
            type_name.data(),
            method_name.data(),
            delegate_arg,
            nullptr,
            &fn_ptr
        );
#elif defined(HN_PLATFORM_WINDOWS)
        std::wstring w_assembly = narrow_to_wide(assembly_path);
        std::wstring w_type     = narrow_to_wide(type_name);
        std::wstring w_method   = narrow_to_wide(method_name);
        std::wstring w_delegate = narrow_to_wide(delegate_type_name);

        const char_t* delegate_arg = delegate_type_name.empty()
            ? UNMANAGEDCALLERSONLY_METHOD
            : w_delegate.c_str();

        rc = load(
            w_assembly.c_str(),
            w_type.c_str(),
            w_method.c_str(),
            delegate_arg,
            nullptr,
            &fn_ptr
        );
#endif

        if (rc != 0 || !fn_ptr) {
            HN_CORE_ERROR("DotNetHost: load_managed_function failed — type='{}' method='{}' (0x{:x})",
                type_name, method_name, (uint32_t)rc);
            return nullptr;
        }
        return fn_ptr;
    }
}
