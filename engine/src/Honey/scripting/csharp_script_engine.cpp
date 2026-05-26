#include "hnpch.h"
#include "csharp_script_engine.h"
#include "csharp_script_glue.h"

#include "Honey/scene/scene.h"
#include "Honey/scene/entity.h"
#include "Honey/scene/components.h"

#include <cstdint>
#include <filesystem>
#include <unordered_map>

namespace Honey {
    static constexpr const char* k_registry_type =
        "HoneyEngine.ScriptRegistry, HoneyEngine";

    // ---------------------------------------------------------------------------
    // Internal data
    // ---------------------------------------------------------------------------
    struct CSharpScriptEngine::Data {
        DotNetHost host;
        Scene* scene_context = nullptr;
        bool initialized = false;
        std::string user_scripts_dll;

        // fn ptrs loaded from HoneyEngine.dll at init time
        using RegisterAssemblyFn  = void     (*)(const char*);
        using UnloadAssemblyFn    = void     (*)();
        using ClassExistsFn       = uint8_t  (*)(const char*);
        using CreateInstanceFn    = intptr_t (*)(const char*);
        using DestroyInstanceFn   = void     (*)(intptr_t);
        using CallOnCreateFn      = void     (*)(intptr_t, uint64_t);
        using CallOnUpdateFn      = void     (*)(intptr_t, uint64_t, float);
        using CallOnDestroyFn     = void     (*)(intptr_t, uint64_t);
        using CallCollisionFn     = void     (*)(intptr_t, uint64_t, uint64_t);

        RegisterAssemblyFn  register_assembly    = nullptr;
        UnloadAssemblyFn    unload_assembly      = nullptr;
        ClassExistsFn       class_exists         = nullptr;
        CreateInstanceFn    create_instance      = nullptr;
        DestroyInstanceFn   destroy_instance     = nullptr;
        CallOnCreateFn      call_on_create       = nullptr;
        CallOnUpdateFn      call_on_update       = nullptr;
        CallOnDestroyFn     call_on_destroy      = nullptr;
        CallCollisionFn     call_collision_begin = nullptr;
        CallCollisionFn     call_collision_end   = nullptr;

        // Per-entity GCHandles (nint stored as intptr_t)
        std::unordered_map<UUID, intptr_t> entity_instances;
    };

    std::unique_ptr<CSharpScriptEngine::Data> CSharpScriptEngine::s_data = nullptr;

    // ---------------------------------------------------------------------------
    // Public API
    // ---------------------------------------------------------------------------
    void CSharpScriptEngine::init() {
        s_data = std::make_unique<Data>();

        namespace fs = std::filesystem;

        // Build absolute, normalized paths from CMake-injected defines.
        const fs::path asset_root   = fs::path(ASSET_ROOT).lexically_normal();
        const fs::path config_root  = fs::path(CONFIG_ROOT).lexically_normal();
        const fs::path project_root = asset_root.parent_path();

        const std::string runtimeconfig = (config_root / "HoneyScripting.runtimeconfig.json").string();
        const std::string dotnet_root   = fs::path(HN_DOTNET_RUNTIME_ROOT).lexically_normal().string();
        const std::string honey_dll     = (project_root / "Honey/managed/HoneyEngine/bin/Release/net10.0/HoneyEngine.dll").string();
        s_data->user_scripts_dll        = (asset_root  / "scripts/bin/Debug/net10.0/UserScripts.dll").string();

        HN_CORE_INFO("[CSharpScriptEngine] init — runtimeconfig: {}", runtimeconfig);
        HN_CORE_INFO("[CSharpScriptEngine] init — HoneyEngine.dll: {}", honey_dll);

        if (!s_data->host.init(runtimeconfig, dotnet_root)) {
            HN_CORE_ERROR("[CSharpScriptEngine] .NET runtime init failed — C# scripting disabled");
            return;
        }

        CSharpScriptGlue::register_functions(s_data->host, honey_dll);

        auto load = [&](const char* method) -> void* {
            return s_data->host.load_managed_function(honey_dll, k_registry_type, method);
        };

        s_data->register_assembly    = (Data::RegisterAssemblyFn) load("RegisterAssembly");
        s_data->unload_assembly      = (Data::UnloadAssemblyFn)   load("UnloadScriptAssembly");
        s_data->class_exists         = (Data::ClassExistsFn)      load("ClassExists");
        s_data->create_instance      = (Data::CreateInstanceFn)   load("CreateInstance");
        s_data->destroy_instance     = (Data::DestroyInstanceFn)  load("DestroyInstance");
        s_data->call_on_create       = (Data::CallOnCreateFn)     load("CallOnCreate");
        s_data->call_on_update       = (Data::CallOnUpdateFn)     load("CallOnUpdate");
        s_data->call_on_destroy      = (Data::CallOnDestroyFn)    load("CallOnDestroy");
        s_data->call_collision_begin = (Data::CallCollisionFn)    load("CallCollisionBegin");
        s_data->call_collision_end   = (Data::CallCollisionFn)    load("CallCollisionEnd");

        auto& d = *s_data;
        if (!d.register_assembly || !d.unload_assembly || !d.class_exists
            || !d.create_instance || !d.destroy_instance
            || !d.call_on_create  || !d.call_on_update  || !d.call_on_destroy
            || !d.call_collision_begin || !d.call_collision_end) {
            HN_CORE_ERROR("[CSharpScriptEngine] one or more managed fn ptrs failed to load");
            return;
            }

        s_data->initialized = true;
        HN_CORE_INFO("[CSharpScriptEngine] initialized");
    }

    void CSharpScriptEngine::shutdown() {
        s_data.reset();
    }

    Scene* CSharpScriptEngine::get_scene_context() {
        return s_data ? s_data->scene_context : nullptr;
    }

    void CSharpScriptEngine::on_runtime_start(Scene* scene) {
        if (!s_data || !s_data->initialized) return;
        s_data->scene_context = scene;
        CSharpScriptGlue::set_scene_context(scene);

        if (!std::filesystem::exists(s_data->user_scripts_dll)) {
            HN_CORE_WARN("[CSharpScriptEngine] UserScripts.dll not found at '{}' — build the script project first",
                s_data->user_scripts_dll);
            return;
        }

        s_data->register_assembly(s_data->user_scripts_dll.c_str());
    }

    void CSharpScriptEngine::on_runtime_stop() {
        if (!s_data || !s_data->initialized) return;

        // Destroy all live instances before unloading the assembly
        for (auto& [uuid, handle] : s_data->entity_instances) {
            if (handle) s_data->destroy_instance(handle);
        }
        s_data->entity_instances.clear();

        s_data->unload_assembly();
        CSharpScriptGlue::set_scene_context(nullptr);
        s_data->scene_context = nullptr;
    }

    bool CSharpScriptEngine::entity_class_exists(const std::string& class_name) {
        if (!s_data || !s_data->initialized || !s_data->class_exists) return false;
        return s_data->class_exists(class_name.c_str()) != 0;
    }

    void CSharpScriptEngine::on_create_entity(Entity entity) {
        if (!s_data || !s_data->initialized) return;

        auto& sc = entity.get_component<ScriptComponent>();
        const std::string& class_name = sc.script_name;

        intptr_t handle = s_data->create_instance(class_name.c_str());
        if (!handle) {
            HN_CORE_ERROR("[CSharpScriptEngine] failed to create instance of '{}'", class_name);
            return;
        }

        UUID uuid = entity.get_uuid();
        s_data->entity_instances[uuid] = handle;
        s_data->call_on_create(handle, (uint64_t)uuid);
    }

    void CSharpScriptEngine::on_update_entity(Entity entity, Timestep ts) {
        if (!s_data || !s_data->initialized) return;
        if (!entity.is_valid()) return;

        UUID uuid = entity.get_uuid();
        auto it = s_data->entity_instances.find(uuid);
        if (it == s_data->entity_instances.end()) return;

        s_data->call_on_update(it->second, (uint64_t)uuid, (float)ts);
    }

    void CSharpScriptEngine::on_destroy_entity(Entity entity) {
        if (!s_data || !s_data->initialized) return;

        UUID uuid = entity.get_uuid();
        auto it = s_data->entity_instances.find(uuid);
        if (it == s_data->entity_instances.end()) return;

        s_data->call_on_destroy(it->second, (uint64_t)uuid);
        s_data->destroy_instance(it->second);
        s_data->entity_instances.erase(it);
    }

    void CSharpScriptEngine::on_collision_begin(Entity a, Entity b) {
        if (!s_data || !s_data->initialized) return;

        auto it = s_data->entity_instances.find(a.get_uuid());
        if (it == s_data->entity_instances.end()) return;

        s_data->call_collision_begin(it->second, (uint64_t)a.get_uuid(), (uint64_t)b.get_uuid());
    }

    void CSharpScriptEngine::on_collision_end(Entity a, Entity b) {
        if (!s_data || !s_data->initialized) return;

        auto it = s_data->entity_instances.find(a.get_uuid());
        if (it == s_data->entity_instances.end()) return;

        s_data->call_collision_end(it->second, (uint64_t)a.get_uuid(), (uint64_t)b.get_uuid());
    }

    bool CSharpScriptEngine::build_and_reload() {
        if (!s_data || !s_data->initialized) return false;

        namespace fs = std::filesystem;
        // user_scripts_dll: <project>/bin/Debug/net10.0/UserScripts.dll — go up 4 levels
        fs::path project_dir = fs::path(s_data->user_scripts_dll)
            .parent_path().parent_path().parent_path().parent_path();
        std::string cmd = "dotnet build \"" + project_dir.string() + "\" -c Debug --nologo -v q 2>&1";

        HN_CORE_INFO("[CSharpScriptEngine] building scripts: {}", cmd);
        int rc = std::system(cmd.c_str());
        if (rc != 0) {
            HN_CORE_ERROR("[CSharpScriptEngine] dotnet build failed (exit {})", rc);
            return false;
        }

        reload_scripts();
        HN_CORE_INFO("[CSharpScriptEngine] scripts built and reloaded");
        return true;
    }

    void CSharpScriptEngine::reload_scripts() {
        if (!s_data || !s_data->initialized) return;

        // Destroy live instances
        for (auto& [uuid, handle] : s_data->entity_instances) {
            if (handle) s_data->destroy_instance(handle);
        }
        s_data->entity_instances.clear();

        s_data->unload_assembly();

        if (std::filesystem::exists(s_data->user_scripts_dll))
            s_data->register_assembly(s_data->user_scripts_dll.c_str());
        else
            HN_CORE_WARN("[CSharpScriptEngine] reload: DLL not found at '{}'", s_data->user_scripts_dll);
    }
}