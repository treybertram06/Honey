#include "hnpch.h"
#include "csharp_script_glue.h"

#include "Honey/scene/entity.h"
#include "Honey/scene/components.h"
#include "Honey/core/input.h"
#include "Honey/core/keycodes.h"
#include <box2d/box2d.h>
#include <cstdint>
#include <cstring>
#include <algorithm>

#include "csharp_script_engine.h"
#include "Honey/core/engine.h"
#include "Honey/physics/physics_engine_3d.h"
#include "Honey/scene/scene_manager.h"
#include "Honey/ui/game_ui.h"
#include "Jolt/Physics/Body/BodyID.h"

namespace Honey {
    Scene* CSharpScriptGlue::s_scene_context = nullptr;

    // -----------------------------------------------------------------------
    // Internal layout — must exactly mirror NativeFunctionTable in InternalCalls.cs
    // -----------------------------------------------------------------------
    struct NativeFunctionTable {
        // return type - name - params
        void    (*Entity_GetTranslation)           (uint64_t, float*);
        void    (*Entity_SetTranslation)           (uint64_t, float*);
        void    (*Entity_GetRotation)              (uint64_t, float*);
        void    (*Entity_SetRotation)              (uint64_t, float*);
        void    (*Entity_GetScale)                 (uint64_t, float*);
        void    (*Entity_SetScale)                 (uint64_t, float*);
        uint8_t (*Entity_IsValid)                  (uint64_t);
        int     (*Entity_GetName)                  (uint64_t, char*, int);
        void    (*Entity_SetName)                  (uint64_t, const char*);

        uint64_t (*Scene_InstantiatePrefab)         (const char*);

        void    (*Rigidbody2D_ApplyLinearImpulse)  (uint64_t, float, float, float);
        void    (*Rigidbody_ApplyForce)            (uint64_t, float, float, float);
        void    (*Rigidbody_ApplyImpulse)          (uint64_t, float, float, float);
        void    (*Rigidbody_GetVelocity)           (uint64_t, float*);
        void    (*Rigidbody_SetVelocity)           (uint64_t, float, float, float);
        void    (*Rigidbody_SetPosition)           (uint64_t, float, float, float);

        void    (*Log_Info)                        (const char*);
        void    (*Log_Warn)                        (const char*);
        void    (*Log_Error)                       (const char*);

        uint8_t (*Input_IsKeyDown)                 (int);
        uint8_t (*Input_IsMouseButtonDown)         (int);
        float   (*Input_GetMouseX)                 ();
        float   (*Input_GetMouseY)                 ();
        uint8_t (*Input_IsMouseCaptured)           ();

        void    (*Entity_Destroy)                  (uint64_t);
        uint64_t (*Scene_FindEntityByName)         (const char*);
        float   (*Input_GetMouseDeltaX)            ();
        float   (*Input_GetMouseDeltaY)            ();

        void    (*Cursor_SetLocked)                (uint8_t);
        void    (*Application_Quit)                ();
        float   (*Time_GetDeltaTime)               ();
        float   (*Time_GetElapsed)                 ();

        void    (*Scene_Load)                      (const char*);

        void    (*UI_BeginPanel)                   (const char*, uint32_t, float, float, float, float, uint8_t);
        void    (*UI_EndPanel)                     ();
        void    (*UI_Label)                        (const char*, float, float, float, float, float);
        uint8_t (*UI_Button)                       (const char*, float);
        void    (*UI_Spacing)                      (float);
        uint8_t (*UI_WantsMouse)                   ();
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
        if (!e.is_valid()) { out[0] = out[1] = out[2] = 0.0f; return; }
        auto& tc = e.get_component<TransformComponent>();
        out[0] = tc.translation.x;
        out[1] = tc.translation.y;
        out[2] = tc.translation.z;
    }

    static void glue_entity_set_translation(uint64_t id, float* v) {
        Scene* scene = get_scene();
        HN_CORE_ASSERT(scene, "CSharpScriptGlue: no active scene");
        Entity e = scene->get_entity(UUID{id});
        if (!e.is_valid()) return;
        auto& tc = e.get_component<TransformComponent>();
        tc.translation = { v[0], v[1], v[2] };
        tc.dirty = true;
    }

    static void glue_entity_get_rotation(uint64_t id, float* out) {
        Scene* scene = get_scene();
        HN_CORE_ASSERT(scene, "CSharpScriptGlue: no active scene");
        Entity e = scene->get_entity(UUID{id});
        if (!e.is_valid()) { out[0] = out[1] = out[2] = 0.0f; return; }
        auto& tc = e.get_component<TransformComponent>();
        out[0] = tc.rotation.x;
        out[1] = tc.rotation.y;
        out[2] = tc.rotation.z;
    }

    static void glue_entity_set_rotation(uint64_t id, float* v) {
        Scene* scene = get_scene();
        HN_CORE_ASSERT(scene, "CSharpScriptGlue: no active scene");
        Entity e = scene->get_entity(UUID{id});
        if (!e.is_valid()) return;
        auto& tc = e.get_component<TransformComponent>();
        tc.rotation = { v[0], v[1], v[2] };
        tc.dirty = true;
    }

    static void glue_entity_get_scale(uint64_t id, float* out) {
        Scene* scene = get_scene();
        HN_CORE_ASSERT(scene, "CSharpScriptGlue: no active scene");
        Entity e = scene->get_entity(UUID{id});
        if (!e.is_valid()) { out[0] = out[1] = out[2] = 0.0f; return; }
        auto& tc = e.get_component<TransformComponent>();
        out[0] = tc.scale.x;
        out[1] = tc.scale.y;
        out[2] = tc.scale.z;
    }

    static void glue_entity_set_scale(uint64_t id, float* v) {
        Scene* scene = get_scene();
        HN_CORE_ASSERT(scene, "CSharpScriptGlue: no active scene");
        Entity e = scene->get_entity(UUID{id});
        if (!e.is_valid()) return;
        auto& tc = e.get_component<TransformComponent>();
        tc.scale = { v[0], v[1], v[2] };
        tc.dirty = true;
        tc.collider_dirty = true;
    }

    // -----------------------------------------------------------------------
    // Scene glue
    // -----------------------------------------------------------------------
    static uint64_t glue_scene_instantiate_prefab(const char* path) {
        Scene* scene = get_scene();
        HN_CORE_ASSERT(scene, "CSharpScriptGlue: no active scene");
        Entity e = scene->instantiate_prefab(path);
        return e.is_valid() ? (uint64_t)e.get_uuid() : 0;
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

    static void glue_rigidbody_apply_force(uint64_t entity_id, float x, float y, float z) {
        Entity e = CSharpScriptEngine::get_scene_context()->get_entity(UUID{entity_id});
        if (!e.has_component<RigidbodyComponent>()) return;
        auto& rb = e.get_component<RigidbodyComponent>();
        JPH::BodyID id{rb.runtime_body_id};
        PhysicsEngine3D::get().apply_force(id, { x, y, z });
    }

    static void glue_rigidbody_apply_impulse(uint64_t entity_id, float x, float y, float z) {
        Entity e = CSharpScriptEngine::get_scene_context()->get_entity(UUID{entity_id});
        if (!e.has_component<RigidbodyComponent>()) return;
        auto& rb = e.get_component<RigidbodyComponent>();
        JPH::BodyID id{rb.runtime_body_id};
        PhysicsEngine3D::get().apply_impulse(id, { x, y, z });
    }

    static void glue_rigidbody_get_velocity(uint64_t entity_id, float* out_xyz) {
        Entity e = CSharpScriptEngine::get_scene_context()->get_entity(UUID{entity_id});
        if (!e.has_component<RigidbodyComponent>()) return;
        auto& rb = e.get_component<RigidbodyComponent>();
        JPH::BodyID id{rb.runtime_body_id};

        glm::vec3 vel = PhysicsEngine3D::get().get_velocity(id);
        HN_CORE_ASSERT(sizeof(out_xyz) == sizeof(glm::vec3), "CSharpScriptGlue: invalid output size");
        out_xyz[0] = vel.x;
        out_xyz[1] = vel.y;
        out_xyz[2] = vel.z;
    }

    static void glue_rigidbody_set_velocity(uint64_t entity_id, float x, float y, float z) {
        Entity e = CSharpScriptEngine::get_scene_context()->get_entity(UUID{entity_id});
        if (!e.has_component<RigidbodyComponent>()) return;
        auto& rb = e.get_component<RigidbodyComponent>();
        JPH::BodyID id{rb.runtime_body_id};
        PhysicsEngine3D::get().set_velocity(id, { x, y, z });
    }

    static void glue_rigidbody_set_position(uint64_t entity_id, float x, float y, float z) {
        Entity e = CSharpScriptEngine::get_scene_context()->get_entity(UUID{entity_id});
        if (!e.has_component<RigidbodyComponent>()) return;
        auto& tc = e.get_component<TransformComponent>();
        tc.translation = { x, y, z };
        tc.dirty = false;
        PhysicsEngine3D::get().sync_transform_to_body(e);
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
    static uint8_t glue_input_is_mouse_button_down(int button) {
        return (uint8_t)Input::is_mouse_button_pressed((MouseButton)button);
    }

    static float glue_input_get_mouse_x() { return Input::get_mouse_x(); }
    static float glue_input_get_mouse_y() { return Input::get_mouse_y(); }

    static uint8_t glue_input_is_mouse_captured() { return (uint8_t)Input::is_cursor_locked(); }

    static float glue_input_get_mouse_delta_x() { return Input::get_mouse_delta_x(); }
    static float glue_input_get_mouse_delta_y() { return Input::get_mouse_delta_y(); }

    static void glue_set_cursor_locked(uint8_t locked) {
        Application::get().get_window().set_cursor_captured(locked != 0);
    }
    static void glue_application_quit() { CSharpScriptEngine::s_quit_requested = true; }
    static float glue_time_get_delta_time() { return Application::get_delta_time(); }
    static float glue_time_get_elapsed() { return Application::get_elapsed_time(); }

    static void glue_scene_load(const char* portable_path) { SceneManager::request_load(portable_path); }

    // -----------------------------------------------------------------------
    // Entity lifecycle glue
    // -----------------------------------------------------------------------
    static void glue_entity_destroy(uint64_t id) {
        Scene* scene = get_scene();
        HN_CORE_ASSERT(scene, "CSharpScriptGlue: no active scene");
        Entity e = scene->get_entity(UUID{id});
        if (e.is_valid())
            scene->destroy_entity(e);
    }

    // Tolerates a null scene (scripts can call during teardown) — no assert.
    static uint8_t glue_entity_is_valid(uint64_t id) {
        if (id == 0) return 0;
        Scene* scene = get_scene();
        if (!scene) return 0;
        return scene->get_entity(UUID{id}).is_valid() ? 1 : 0;
    }

    // Returns the full UTF-8 length of the name (excluding NUL), or 0 if the entity is invalid.
    // Writes at most cap-1 bytes plus a NUL, so the caller can retry with a larger buffer.
    static int glue_entity_get_name(uint64_t id, char* buf, int cap) {
        Scene* scene = get_scene();
        if (!scene || id == 0) { if (buf && cap > 0) buf[0] = 0; return 0; }
        Entity e = scene->get_entity(UUID{id});
        if (!e.is_valid() || !e.has_component<TagComponent>()) { if (buf && cap > 0) buf[0] = 0; return 0; }
        const std::string& name = e.get_component<TagComponent>().tag;
        if (buf && cap > 0) {
            size_t n = std::min(name.size(), (size_t)(cap - 1));
            memcpy(buf, name.data(), n);
            buf[n] = 0;
        }
        return (int)name.size();
    }

    static void glue_entity_set_name(uint64_t id, const char* name) {
        Scene* scene = get_scene();
        if (!scene || id == 0 || !name) return;
        Entity e = scene->get_entity(UUID{id});
        if (!e.is_valid() || !e.has_component<TagComponent>()) return;
        e.get_component<TagComponent>().tag = name;
    }

    // -----------------------------------------------------------------------
    // UI glue
    // -----------------------------------------------------------------------
    static void glue_ui_begin_panel(const char* id, uint32_t anchor,
        float offset_x, float offset_y, float size_w, float size_h, uint8_t background) {
        glm::vec2 offset(offset_x, offset_y);
        glm::vec2 size(size_w, size_h);
        GameUI::begin_panel(id, static_cast<GameUI::UIAnchor>(anchor), offset, size, background ? true : false);
    }

    static void glue_ui_end_panel() {
        GameUI::end_panel();
    }

    static void glue_ui_label(const char* text, float size,
        float color_r, float color_g, float color_b, float color_w) {
        glm::vec4 color(color_r, color_g, color_b, color_w);
        text = text ? text : ""; // Ensure text is not null
        GameUI::label(text, size, color);
    }

    static uint8_t glue_ui_button(const char* text, float width) {
        text = text ? text : ""; // Ensure text is not null
        return GameUI::button(text, width) ? 1 : 0;
    }

    static void glue_ui_spacing(float h) {
        GameUI::spacing(h);
    }

    static uint8_t glue_ui_wants_mouse() {
        return GameUI::wants_mouse() ? 1 : 0;
    }

    // -----------------------------------------------------------------------
    // Scene query glue
    // -----------------------------------------------------------------------
    static uint64_t glue_scene_find_entity_by_name(const char* name) {
        Scene* scene = get_scene();
        HN_CORE_ASSERT(scene, "CSharpScriptGlue: no active scene");
        Entity e = scene->find_entity_by_name(name);
        return e.is_valid() ? (uint64_t)e.get_uuid() : 0;
    }

    // -----------------------------------------------------------------------
    // Public API
    // -----------------------------------------------------------------------
    void CSharpScriptGlue::set_scene_context(Scene* scene) {
        s_scene_context = scene;
    }

    void CSharpScriptGlue::register_functions(DotNetHost& host, std::string_view dll_path) {
        static NativeFunctionTable table = {
            // Entity transform
            glue_entity_get_translation,
            glue_entity_set_translation,
            glue_entity_get_rotation,
            glue_entity_set_rotation,
            glue_entity_get_scale,
            glue_entity_set_scale,
            glue_entity_is_valid,
            glue_entity_get_name,
            glue_entity_set_name,

            // Scene
            glue_scene_instantiate_prefab,

            // Physics
            glue_rigidbody2d_apply_linear_impulse,
            glue_rigidbody_apply_force,
            glue_rigidbody_apply_impulse,
            glue_rigidbody_get_velocity,
            glue_rigidbody_set_velocity,
            glue_rigidbody_set_position,

            // Logging
            glue_log_info,
            glue_log_warn,
            glue_log_error,

            // Input
            glue_input_is_key_down,
            glue_input_is_mouse_button_down,
            glue_input_get_mouse_x,
            glue_input_get_mouse_y,
            glue_input_is_mouse_captured,

            // Entity lifecycle
            glue_entity_destroy,

            // Scene queries
            glue_scene_find_entity_by_name,

            // Mouse delta
            glue_input_get_mouse_delta_x,
            glue_input_get_mouse_delta_y,

            glue_set_cursor_locked,
            glue_application_quit,
            glue_time_get_delta_time,
            glue_time_get_elapsed,

            glue_scene_load,

            glue_ui_begin_panel,
            glue_ui_end_panel,
            glue_ui_label,
            glue_ui_button,
            glue_ui_spacing,
            glue_ui_wants_mouse,
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
