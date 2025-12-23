#include "hnpch.h"
#include "script_glue.h"
#include "script_engine.h"

#include "Honey/scene/scene.h"
#include "Honey/scene/entity.h"
#include "Honey/scene/components.h"
#include "Honey/core/input.h"
#include "Honey/core/keycodes.h"
#include <sol/sol.hpp>
#include <box2d/box2d.h>

#include "type_proxies.h"
#include "Honey/scene/scene_serializer.h"

namespace Honey {

    static Scene* get_active_scene() {
        return ScriptEngine::get_scene_context();
    }

    static b2BodyId get_body_id(Rigidbody2DComponent& rb) {
        b2BodyId body;
        memcpy(&body, &rb.runtime_body, sizeof(b2BodyId));
        return body;
    }

    void ScriptGlue::register_functions() {
        sol::state& lua = ScriptEngine::get_lua_state();

        // vec3
        lua.new_usertype<glm::vec3>("vec3",
            sol::constructors<glm::vec3(), glm::vec3(float,float,float)>(),
            "x", &glm::vec3::x,
            "y", &glm::vec3::y,
            "z", &glm::vec3::z
        );

        // vec2 helper
        lua.new_usertype<glm::vec2>("vec2",
            sol::constructors<glm::vec2(), glm::vec2(float,float)>(),
            "x", &glm::vec2::x,
            "y", &glm::vec2::y
        );

        lua.set_function("vec2", [](float x, float y) {
            return glm::vec2(x, y);
        });


        // Entity
        lua.new_usertype<Entity>("Entity",
            sol::constructors<Entity>(),

            "AddComponent", [](Entity e, const std::string& type) {
                if (type == "Transform") return (void)e.add_component<TransformComponent>();
                if (type == "SpriteRenderer") return (void)e.add_component<SpriteRendererComponent>();
                if (type == "Rigidbody2D") return (void)e.add_component<Rigidbody2DComponent>();
                if (type == "BoxCollider2D") return (void)e.add_component<BoxCollider2DComponent>();
            },

            "HasComponent", [](Entity e, const std::string& type) {
                if (type == "Transform") return e.has_component<TransformComponent>();
                if (type == "SpriteRenderer") return e.has_component<SpriteRendererComponent>();
                if (type == "Rigidbody2D") return e.has_component<Rigidbody2DComponent>();
                if (type == "BoxCollider2D") return e.has_component<BoxCollider2DComponent>();
                return false;
            },

            "GetComponent", [](Entity e, const std::string& type) -> sol::object {
                sol::state& L = ScriptEngine::get_lua_state();

                if (type == "Transform") {
                    if (!e.has_component<TransformComponent>()) return sol::nil;
                    return sol::make_object(L, &e.get_component<TransformComponent>());
                }
                if (type == "SpriteRenderer") {
                    if (!e.has_component<SpriteRendererComponent>()) return sol::nil;
                    return sol::make_object(L, &e.get_component<SpriteRendererComponent>());
                }
                if (type == "Rigidbody2D") {
                    if (!e.has_component<Rigidbody2DComponent>()) return sol::nil;
                    return sol::make_object(L, &e.get_component<Rigidbody2DComponent>());
                }
                if (type == "BoxCollider2D") {
                    if (!e.has_component<BoxCollider2DComponent>()) return sol::nil;
                    return sol::make_object(L, &e.get_component<BoxCollider2DComponent>());
                }
                return sol::nil;
            },

            "GetTransform", [](Entity e) { return &e.get_component<TransformComponent>(); }
        );

        lua.new_usertype<LuaVec3Proxy>("LuaVec3",
            "x", sol::property(&LuaVec3Proxy::get_x, &LuaVec3Proxy::set_x),
            "y", sol::property(&LuaVec3Proxy::get_y, &LuaVec3Proxy::set_y),
            "z", sol::property(&LuaVec3Proxy::get_z, &LuaVec3Proxy::set_z)
        );


        // TransformComponent
        lua.new_usertype<TransformComponent>("TransformComponent",
            sol::constructors<TransformComponent()>(),

            "translation", sol::property(
                [](TransformComponent& tc) {
                    return LuaVec3Proxy{ &tc, &tc.translation };
                },
                [](TransformComponent& tc, glm::vec3 val) {
                    tc.translation = val;
                    tc.dirty = true;
                }
            ),

            "rotation", sol::property(
                [](TransformComponent& tc) {
                    return LuaVec3Proxy{ &tc, &tc.rotation };
                },
                [](TransformComponent& tc, glm::vec3 val) {
                    tc.rotation = val;
                    tc.dirty = true;
                }
            ),

            "scale", sol::property(
                [](TransformComponent& tc) {
                    return LuaVec3Proxy{ &tc, &tc.scale };
                },
                [](TransformComponent& tc, glm::vec3 val) {
                    tc.scale = val;
                    tc.dirty = true;
                }
            )
        );



        // SpriteRendererComponent
        lua.new_usertype<SpriteRendererComponent>("SpriteRendererComponent",
            sol::constructors<SpriteRendererComponent()>(),
            "color", &SpriteRendererComponent::color
        );

        // Rigidbody2DComponent with Box2D-backed velocity
        lua.new_usertype<Rigidbody2DComponent>("Rigidbody2DComponent",
            sol::constructors<Rigidbody2DComponent()>(),

            "GetVelocity", [](Rigidbody2DComponent& rb) {
                b2BodyId body = get_body_id(rb);
                b2Vec2 v = b2Body_GetLinearVelocity(body);
                return glm::vec2(v.x, v.y);
            },

            "SetVelocity", [](Rigidbody2DComponent& rb, glm::vec2 vel) {
                b2BodyId body = get_body_id(rb);
                b2Body_SetLinearVelocity(body, { vel.x, vel.y });
            },

            "GetAngularVelocity", [](Rigidbody2DComponent& rb) {
                b2BodyId body = get_body_id(rb);
                return b2Body_GetAngularVelocity(body);
            },

            "SetAngularVelocity", [](Rigidbody2DComponent& rb, float value) {
                b2BodyId body = get_body_id(rb);
                b2Body_SetAngularVelocity(body, value);
            },

            "fixed_rotation", &Rigidbody2DComponent::fixed_rotation
        );

        // BoxCollider2DComponent
        lua.new_usertype<BoxCollider2DComponent>("BoxCollider2DComponent",
            sol::constructors<BoxCollider2DComponent()>(),
            "size", &BoxCollider2DComponent::size,
            "density", &BoxCollider2DComponent::density,
            "friction", &BoxCollider2DComponent::friction,
            "restitution", &BoxCollider2DComponent::restitution
        );

        // Honey namespace
        sol::table honey = lua.create_named_table("Honey");

        honey.set_function("Log", [](const std::string& msg){ HN_INFO("[Lua] {}", msg); });

        honey.set_function("CreateEntity", [](const std::string& name){
            Scene* scene = get_active_scene();
            if (!scene) return Entity();
            return scene->create_entity(name);
        });

        honey.set_function("DestroyEntity", [](Entity e){
            Scene* scene = get_active_scene();
            if (scene) scene->destroy_entity(e);
        });

        honey.set_function("InstantiatePrefab", [](const std::string& name) {
            Scene* scene = get_active_scene();
            if (!scene) return Entity();

            auto path = std::filesystem::path("..") / "assets" / "prefabs" / name;
            path.replace_extension(".hnp");

            //HN_CORE_INFO("Instantiating prefab '{0}'", path.string());
            return scene->instantiate_prefab(path.string());
        });

        honey.set_function("IsKeyPressed", [](int key){ return Input::is_key_pressed((KeyCode)key); });
        honey.set_function("IsMouseButtonPressed", [](int button){ return Input::is_mouse_button_pressed((MouseButton)button); });
        honey.set_function("GetMousePosition", [](){ return Input::get_mouse_position(); });

        honey.set_function("Random", [](float a, float b){ return a + static_cast<float>(rand()) / RAND_MAX * (b - a); });

        // Key enum
        sol::table key = lua.create_named_table("Key");
        key["Space"] = KeyCode::Space;
        key["A"] = KeyCode::A;
        key["D"] = KeyCode::D;
        key["W"] = KeyCode::W;
        key["S"] = KeyCode::S;

        // Mouse enum — based on your engine
        sol::table mouse = lua.create_named_table("Mouse");
        mouse["Left"] = MouseButton::Button1;
        mouse["Right"] = MouseButton::Button2;
    }

}
