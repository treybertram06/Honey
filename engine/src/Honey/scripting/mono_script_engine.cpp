#include "mono_script_engine.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <mono/metadata/mono-config.h>

#include "mono_script_glue.h"

#ifndef MONO_TOKEN_TYPE_DEF
#define MONO_TOKEN_TYPE_DEF 0x02000000
#endif

namespace Honey::Scripting {

    MonoDomain* MonoScriptEngine::s_domain = nullptr;
    MonoAssembly* MonoScriptEngine::s_assembly = nullptr;
    MonoImage* MonoScriptEngine::s_image = nullptr;
    std::unordered_map<UUID, ScriptInstance> MonoScriptEngine::s_active_scripts;
    std::unordered_map<std::string, ScriptClass> MonoScriptEngine::s_script_classes;


    void MonoScriptEngine::init() {
        HN_CORE_INFO("[Mono] Initializing runtime...");

        // Preload native library to ensure it's available
        void* handle = dlopen("/usr/lib/libmono-native.so", RTLD_LAZY | RTLD_GLOBAL);
        if (!handle)
            HN_CORE_ERROR("[Mono] Failed to preload libmono-native.so: {0}", dlerror());

        // Set Mono directories
        mono_set_dirs("/usr/lib", "/etc/mono");
        mono_set_assemblies_path("/usr/lib/mono/4.5");

        // Set environment variables for config and library resolution
        setenv("MONO_PATH", "/usr/lib/mono/4.5", 1);
        setenv("LD_LIBRARY_PATH", "/usr/lib", 1);
        setenv("MONO_CFG_DIR", "/etc/mono", 1);

        mono_config_parse("/etc/mono/config");


        // Initialize the JIT
        s_domain = mono_jit_init_version("Honey Engine", "v4.0.30319");

        if (!s_domain) {
            HN_CORE_ERROR("[Mono] Failed to initialize runtime!");
            return;
        }

        register_internal_calls();

        HN_CORE_INFO("[Mono] Runtime initialized successfully!");
    }


    void MonoScriptEngine::shutdown() {
        if (s_domain) {
            mono_jit_cleanup(s_domain);
            s_domain = nullptr;
            HN_CORE_INFO("[Mono] Runtime shutdown successfully!");
        }
    }

    MonoAssembly* MonoScriptEngine::load_mono_assembly(const std::string& path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            HN_CORE_ERROR("[Mono] Failed to open assembly file: {0}", path);
            return nullptr;
        }

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<char> buffer(size);
        if (!file.read(buffer.data(), size)) {
            HN_CORE_ERROR("[Mono] Failed to read assembly file: {0}", path);
            return nullptr;
        }
        buffer.push_back('\0');

        MonoImageOpenStatus status;
        MonoImage* image = mono_image_open_from_data_full(buffer.data(), (uint32_t)size, 1, &status, 0);
        if (status != MONO_IMAGE_OK) {
            HN_CORE_ERROR("[Mono] Failed to load assembly: {0}", mono_image_get_name(image));
            return nullptr;
        }

        MonoAssembly* assembly = mono_assembly_load_from_full(image, path.c_str(), &status, 0);
        mono_image_close(image);

        if (!assembly) {
            HN_CORE_ERROR("Failed to load assembly: {0}", path);
            return nullptr;
        }

        return assembly;
    }

    void MonoScriptEngine::load_assembly(const std::string &path) {
        s_assembly = load_mono_assembly(path);
        if (!s_assembly) {
            HN_CORE_ERROR("Could not load assembly: {0}", path);
            return;
        }

        s_image = mono_assembly_get_image(s_assembly);
        MonoAssemblyName* assembly_name = mono_assembly_get_name(s_assembly);
        const char* name = mono_assembly_name_get_name(assembly_name);
        HN_CORE_INFO("[Mono] Loaded assembly: {0}", name);

        int type_count = mono_image_get_table_rows(s_image, MONO_TABLE_TYPEDEF);
        for (int i = 1; i <= type_count; i++) {
            uint32_t token = 0x02000000 | i;
            MonoClass* klass = mono_class_get(s_image, token);
            if (klass) {
                const char* ns = mono_class_get_namespace(klass);
                const char* name = mono_class_get_name(klass);
                HN_CORE_INFO("[Mono] Found class: {0}.{1}", ns, name);
            }
        }
    }

    void MonoScriptEngine::execute_method(const std::string &class_namespace, const std::string &class_name, const std::string &method_name, void **params, int param_count) {
        if (!s_image) {
            HN_CORE_ERROR("No assembly loaded!");
            return;
        }

        MonoClass* klass = mono_class_from_name(s_image, class_namespace.c_str(), class_name.c_str());
        if (!klass) {
            HN_CORE_ERROR("Failed to find class: {0}.{1}", class_namespace, class_name);
            return;
        }

        MonoMethod* method = mono_class_get_method_from_name(klass, method_name.c_str(), param_count);
        if (!method) {
            HN_CORE_ERROR("Failed to find method: {0}.{1}({2})", class_namespace, class_name, method_name);
            return;
        }

        MonoObject* instance = mono_object_new(s_domain, klass);
        mono_runtime_object_init(instance);

        mono_runtime_invoke(method, instance, params, nullptr);
    }

    std::vector<std::string> MonoScriptEngine::get_available_classes() {
        std::vector<std::string> class_names;

        if (!s_image)
        {
            HN_CORE_WARN("[Mono] No assembly loaded, cannot enumerate classes.");
            return class_names;
        }

        // Get the number of type definitions in the assembly
        int type_count = mono_image_get_table_rows(s_image, MONO_TABLE_TYPEDEF);

        for (int i = 1; i <= type_count; i++)
        {
            // Each type has a token (MONO_TOKEN_TYPE_DEF | index)

            uint32_t token = MONO_TOKEN_TYPE_DEF | i;
            MonoClass* klass = mono_class_get(s_image, token);
            if (!klass)
                continue;

            const char* namespace_name = mono_class_get_namespace(klass);
            const char* class_name = mono_class_get_name(klass);

            // Filter out internal Mono classes and system types
            if (namespace_name && std::string(namespace_name).rfind("System", 0) == 0)
                continue;

            // Combine namespace and class name
            std::string full_name;
            if (namespace_name && strlen(namespace_name) > 0)
                full_name = std::string(namespace_name) + "." + class_name;
            else
                full_name = class_name;

            class_names.push_back(full_name);
        }

        return class_names;
    }

    MonoClass* MonoScriptEngine::find_class(const std::string& full_name) {
        // Example input: "Honey.TestScript"
        size_t dot_pos = full_name.find_last_of('.');
        std::string ns, name;

        if (dot_pos != std::string::npos) {
            ns = full_name.substr(0, dot_pos);
            name = full_name.substr(dot_pos + 1);
        } else {
            ns = "";
            name = full_name;
        }

        MonoClass* klass = mono_class_from_name(s_image, ns.c_str(), name.c_str());
        if (!klass)
            HN_CORE_ERROR("[Mono] Could not find class: {0}.{1}", ns, name);
        return klass;
    }


}
