#pragma once

#include <mono/jit/jit.h>
#include <mono/metadata/assembly.h>
#include <mono/metadata/debug-helpers.h>
#include <string>

#include "Honey/core/uuid.h"

namespace Honey::Scripting {

    struct ScriptClass {
        MonoClass* klass;
        MonoMethod* on_create;
        MonoMethod* on_destroy;
        MonoMethod* on_update;
    };

    struct ScriptInstance {
        ScriptClass* klass;
        MonoObject* instance;

        void invoke_on_create() {
            if (klass->on_create)
                mono_runtime_invoke(klass->on_create, instance, nullptr, nullptr);
        }

        void invoke_on_destroy() {
            if (klass->on_destroy)
                mono_runtime_invoke(klass->on_destroy, instance, nullptr, nullptr);
        }

        void invoke_on_update(float dt) {
            if (klass->on_update) {
                void* args[1] = { &dt };
                mono_runtime_invoke(klass->on_update, instance, args, nullptr);
            }
        }
    };

    class MonoScriptEngine {
    public:
        static void init();
        static void shutdown();

        static void load_assembly(const std::string& path);
        static void execute_method(const std::string& class_namespace, const std::string& class_name,
                                  const std::string& method_name, void** params = nullptr, int param_count = 0);

        static std::unordered_map<UUID, ScriptInstance>& get_active_scripts() { return s_active_scripts; }
        static std::unordered_map<std::string, ScriptClass>& get_script_classes() { return s_script_classes; }
        static std::vector<std::string> get_available_classes();

        static MonoImage* get_image() { return s_image; }
        static MonoAssembly* get_assembly() { return s_assembly; }
        static MonoDomain* get_domain() { return s_domain; }

        static MonoClass* find_class(const std::string& full_name);

    private:
        static MonoDomain* s_domain;
        static MonoAssembly* s_assembly;
        static MonoImage* s_image;
        static std::unordered_map<UUID, ScriptInstance> s_active_scripts;
        static std::unordered_map<std::string, ScriptClass> s_script_classes;

        static MonoAssembly* load_mono_assembly(const std::string& path);
    };

}
