#include "hnpch.h"
#include "csharp_script_glue.h"

#include "Honey/scene/entity.h"
#include "Honey/scene/components.h"
#include "Honey/core/input.h"
#include "Honey/core/keycodes.h"
#include <box2d/box2d.h>
#include <cstdint>
#include <cstring>

namespace Honey {
    Scene* CSharpScriptGlue::s_scene_context = nullptr;

    // -----------------------------------------------------------------------
    // Internal layout — must exactly mirror NativeFunctionTable in InternalCalls.cs
    // -----------------------------------------------------------------------
    struct NativeFunctionTable {
        void    (*Entity_GetTranslation)           (uint64_t, float*);
        void    (*Entity_SetTranslation)           (uint64_t, float*);
        void    (*Entity_GetRotation)              (uint64_t, float*);
        void    (*Entity_SetRotation)              (uint64_t, float*);
        void    (*Entity_GetScale)                 (uint64_t, float*);
        void    (*Entity_SetScale)                 (uint64_t, float*);
        void    (*Rigidbody2D_ApplyLinearImpulse)  (uint64_t, float, float, float);
        void    (*Log_Info)                        (const char*);
        void    (*Log_Warn)                        (const char*);
        void    (*Log_Error)                       (const char*);
        uint8_t (*Input_IsKeyDown)                 (int);
    };

    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------
    static Scene* get_scene() { return CSharpScriptGlue::s_scene_context; }

    static b2BodyId get_body_id(Rigidbody2DComponent& rb) {
        b2BodyId body;
        memcpy(&body, &rb.runtime_body, sizeof(b2BodyId));
        return body;
    }

    // -----------------------------------------------------------------------
    // Transform glue
    // -----------------------------------------------------------------------
    static void glue_entity_get_translation(uint64_t id, float* out) {
        Scene* scene = get_scene();
        HN_CORE_ASSERT(scene, "CSharpScriptGlue: no active scene");
        Entity e = scene->get_entity(UUID{id});
        auto& tc = e.get_component<TransformComponent>();
        out[0] = tc.translation.x;
        out[1] = tc.translation.y;
        out[2] = tc.translation.z;
    }

    static void glue_entity_set_translation(uint64_t id, float* v) {
        Scene* scene = get_scene();
        HN_CORE_ASSERT(scene, "CSharpScriptGlue: no active scene");
        Entity e = scene->get_entity(UUID{id});
        auto& tc = e.get_component<TransformComponent>();
        tc.translation = { v[0], v[1], v[2] };
        tc.dirty = true;
    }

    static void glue_entity_get_rotation(uint64_t id, float* out) {
        Scene* scene = get_scene();
        HN_CORE_ASSERT(scene, "CSharpScriptGlue: no active scene");
        Entity e = scene->get_entity(UUID{id});
        auto& tc = e.get_component<TransformComponent>();
        out[0] = tc.rotation.x;
        out[1] = tc.rotation.y;
        out[2] = tc.rotation.z;
    }

    static void glue_entity_set_rotation(uint64_t id, float* v) {
        Scene* scene = get_scene();
        HN_CORE_ASSERT(scene, "CSharpScriptGlue: no active scene");
        Entity e = scene->get_entity(UUID{id});
        auto& tc = e.get_component<TransformComponent>();
        tc.rotation = { v[0], v[1], v[2] };
        tc.dirty = true;
    }

    static void glue_entity_get_scale(uint64_t id, float* out) {
        Scene* scene = get_scene();
        HN_CORE_ASSERT(scene, "CSharpScriptGlue: no active scene");
        Entity e = scene->get_entity(UUID{id});
        auto& tc = e.get_component<TransformComponent>();
        out[0] = tc.scale.x;
        out[1] = tc.scale.y;
        out[2] = tc.scale.z;
    }

    static void glue_entity_set_scale(uint64_t id, float* v) {
        Scene* scene = get_scene();
        HN_CORE_ASSERT(scene, "CSharpScriptGlue: no active scene");
        Entity e = scene->get_entity(UUID{id});
        auto& tc = e.get_component<TransformComponent>();
        tc.scale = { v[0], v[1], v[2] };
        tc.dirty = true;
        tc.collider_dirty = true;
    }

    // -----------------------------------------------------------------------
    // Physics glue
    // -----------------------------------------------------------------------
    static void glue_rigidbody2d_apply_linear_impulse(uint64_t id, float x, float y, float wake) {
        Scene* scene = get_scene();
        HN_CORE_ASSERT(scene, "CSharpScriptGlue: no active scene");
        Entity e = scene->get_entity(UUID{id});
        if (!e.has_component<Rigidbody2DComponent>()) return;
        auto& rb = e.get_component<Rigidbody2DComponent>();
        b2BodyId body = get_body_id(rb);
        b2Body_ApplyLinearImpulseToCenter(body, { x, y }, wake != 0.0f);
    }

    // -----------------------------------------------------------------------
    // Logging glue
    // -----------------------------------------------------------------------
    static void glue_log_info(const char* msg)  { HN_INFO("[C#] {}", msg);  }
    static void glue_log_warn(const char* msg)  { HN_WARN("[C#] {}", msg);  }
    static void glue_log_error(const char* msg) { HN_ERROR("[C#] {}", msg); }

    // -----------------------------------------------------------------------
    // Input glue
    // -----------------------------------------------------------------------
    static uint8_t glue_input_is_key_down(int key) {
        return (uint8_t)Input::is_key_pressed((KeyCode)key);
    }

    // -----------------------------------------------------------------------
    // Public API
    // -----------------------------------------------------------------------
    void CSharpScriptGlue::set_scene_context(Scene* scene) {
        s_scene_context = scene;
    }

    void CSharpScriptGlue::register_functions(DotNetHost& host, std::string_view dll_path) {
        static NativeFunctionTable table = {
            glue_entity_get_translation,
            glue_entity_set_translation,
            glue_entity_get_rotation,
            glue_entity_set_rotation,
            glue_entity_get_scale,
            glue_entity_set_scale,
            glue_rigidbody2d_apply_linear_impulse,
            glue_log_info,
            glue_log_warn,
            glue_log_error,
            glue_input_is_key_down,
        };

        using BootstrapFn = void(*)(NativeFunctionTable*);
        auto bootstrap = (BootstrapFn)host.load_managed_function(
            dll_path,
            "HoneyEngine.InternalCalls, HoneyEngine",
            "Bootstrap"
        );

        if (!bootstrap) {
            HN_CORE_ERROR("[CSharpScriptGlue] failed to load Bootstrap — InternalCalls will be broken");
            return;
        }

        bootstrap(&table);
        HN_CORE_INFO("[CSharpScriptGlue] native function table registered");
    }
}