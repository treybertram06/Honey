#include "hnpch.h"
#include "scene_manager.h"

#include "scene_serializer.h"
#include "Honey/core/engine.h"

namespace Honey {

    std::string SceneManager::s_pending_load;

    Ref<Scene> SceneManager::load_scene(const std::filesystem::path& path) {
        if (path.empty()) {
            HN_CORE_WARN("[SceneManager] No scene path specified!");
            return nullptr;
        }
        if (path.extension() != ".hns") {
            HN_CORE_WARN("[SceneManager] File {0} is not a Honey Scene file!", path.string());
            return nullptr;
        }

        Ref<Scene> new_scene = CreateRef<Scene>();
        SceneSerializer serializer(new_scene);
        const bool loaded = serializer.deserialize(path);

        if (!loaded) {
            HN_CORE_WARN("[SceneManager] Failed to load scene {0}!", path.string());
            return nullptr;
        }

        auto load_report_lines = serializer.get_load_report().to_lines();
        for (const auto& l : load_report_lines)
            HN_CORE_WARN("[SceneManager] {}", l);

        return new_scene;
    }

    Ref<Scene> SceneManager::perform_pending_load(const Ref<Scene>& current, uint32_t viewport_w, uint32_t viewport_h) {

        Ref<Scene> new_scene = load_scene(take_pending());
        if (new_scene == nullptr) {
            HN_CORE_WARN("[SceneManager] Error loading pending scene. Existing scene will continue to run.");
            return current;
        }

        //HN_TRACE("[SceneManager] Replacing scene: {}, with: {}.", current.get_name(), new_scene.get_name()); /* TODO: Store scene name in the scene, then enable this log */
        if (current == nullptr)
            HN_CORE_WARN("[SceneManager] Loaded pending scene where current scene was null. In the event of a load error, no safe fallback is possible. Avoid doing this.");
        else
            current->on_runtime_stop();
        // Wait for GPU idle in case there's a frame-in-flight referencing old scene resources
        Application::get().get_window().get_context()->wait_idle();

        new_scene->on_viewport_resize(viewport_w, viewport_h);
        new_scene->on_runtime_start();
        return new_scene;
    }

}
