#pragma once

#include <string>
#include <unordered_map>
#include <functional>
#include <filesystem>
#include "scriptable_entity.h"
#include "Honey/scripting/script_api.h"

namespace Honey {

    class ScriptRegistry {
    public:
        using ScriptCreator = std::function<ScriptableEntity*()>;

        static ScriptRegistry& get() {
            static ScriptRegistry instance;
            return instance;
        }

        // Register a script type
        template<typename T>
        void register_script(const std::string& script_name) {
            m_script_creators[script_name] = []() -> ScriptableEntity* {
                return static_cast<ScriptableEntity*>(new T());
            };
            m_script_paths[script_name] = "";
            HN_CORE_INFO("Registered script: {0}", script_name);
        }

        // Create an instance of a script by name
        ScriptableEntity* create_script(const std::string& script_name) {
            auto it = m_script_creators.find(script_name);
            if (it != m_script_creators.end()) {
                return it->second();
            }
            return nullptr;
        }

        // Check if a script is registered
        bool has_script(const std::string& script_name) const {
            return m_script_creators.find(script_name) != m_script_creators.end();
        }

        // Get all registered script names
        std::vector<std::string> get_all_script_names() const {
            std::vector<std::string> names;
            for (const auto& [name, _] : m_script_creators) {
                names.push_back(name);
            }
            return names;
        }

        // Associate a file path with a script name
        void set_script_path(const std::string& script_name, const std::filesystem::path& path) {
            m_script_paths[script_name] = path;
        }

        std::filesystem::path get_script_path(const std::string& script_name) const {
            auto it = m_script_paths.find(script_name);
            return (it != m_script_paths.end()) ? it->second : std::filesystem::path{};
        }

        void clear() {
            m_script_creators.clear();
            m_script_paths.clear();
        }

        /*
    public:
        // C interface registration for script DLLs
        static void register_script(const char* name, ScriptInstance instance) {
            auto& registry = get();

            // Defensive: ensure valid function pointers
            if (!instance.on_create && !instance.on_update && !instance.on_destroy) {
                HN_CORE_WARN("Skipping registration of script '{}' because it has no valid callbacks.", name);
                return;
            }

            registry.m_script_creators[name] = [instance]() -> ScriptableEntity* {
                struct CScriptWrapper : ScriptableEntity {
                    ScriptInstance inst;
                    CScriptWrapper(ScriptInstance i) : inst(i) {}
                    void on_create() override { if (inst.on_create) inst.on_create(inst.user_data); }
                    void on_update(Timestep ts) override { if (inst.on_update) inst.on_update(inst.user_data, ts.get_seconds()); }
                    void on_destroy() override { if (inst.on_destroy) inst.on_destroy(inst.user_data); }
                };
                return new CScriptWrapper(instance);
            };

            HN_CORE_INFO("Registered C-interface script: {0}", name);
        }*/

    private:
        ScriptRegistry() = default;
        std::unordered_map<std::string, ScriptCreator> m_script_creators;
        std::unordered_map<std::string, std::filesystem::path> m_script_paths;
    };

    // Helper macro to auto-register scripts
    #define REGISTER_SCRIPT(ScriptClass) \
        struct ScriptClass##Registrar { \
            ScriptClass##Registrar() { \
                Honey::ScriptRegistry::get().register_script<ScriptClass>(#ScriptClass); \
            } \
        }; \
        static ScriptClass##Registrar g_##ScriptClass##Registrar;
}
