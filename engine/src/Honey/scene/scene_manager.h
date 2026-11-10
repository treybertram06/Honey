#pragma once
#include "scene.h"
#include "Honey/core/asset_paths.h"

namespace Honey {

    class SceneManager {
    public:

        static void request_load(const std::string& portable_path) {
            if (!s_pending_load.empty())
                HN_CORE_WARN("[SceneManager] Requested scene load while one was already queued - previous request will be overwritten.");
            s_pending_load = AssetPaths::resolve(portable_path);
        }

        static bool has_pending() { return !s_pending_load.empty(); }
        static std::string take_pending() {
            auto copy = s_pending_load;
            s_pending_load.clear();
            return copy;
        }
        static void clear_pending() { s_pending_load.clear(); }

        static Ref<Scene> load_scene(const std::filesystem::path& path);
        // Stops the current scene, loads the pending scene, then starts it
        static Ref<Scene> perform_pending_load(const Ref<Scene>& current, uint32_t viewport_w, uint32_t viewport_h);

    private:
        static std::string s_pending_load;
    };

}
