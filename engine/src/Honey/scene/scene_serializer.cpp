#include "hnpch.h"
#include "scene_serializer.h"
#include "entity.h"
#include "components.h"
#include "scriptable_entity.h"
#include "Honey/math/yaml_glm.h"

#include <filesystem>
#include <fstream>
#include <yaml-cpp/yaml.h>

#include "Honey/core/task_system.h"
#include "Honey/loaders/gltf_loader.h"


namespace Honey {

    YAML::Emitter& operator<<(YAML::Emitter& out, const glm::vec2& v) {
        out << YAML::Flow;
        out << YAML::BeginSeq << v.x << v.y << YAML::EndSeq;
        return out;
    }

    YAML::Emitter& operator<<(YAML::Emitter& out, const glm::vec3& v) {
        out << YAML::Flow;
        out << YAML::BeginSeq << v.x << v.y << v.z << YAML::EndSeq;
        return out;
    }

    YAML::Emitter& operator<<(YAML::Emitter& out, const glm::vec4& v) {
        out << YAML::Flow;
        out << YAML::BeginSeq << v.x << v.y << v.z << v.w << YAML::EndSeq;
        return out;
    }

    SceneSerializer::SceneSerializer(const Ref<Scene> &scene) : m_scene(scene) {}

    static void serialize_entity(Entity entity, YAML::Emitter &out) {
        HN_CORE_ASSERT(entity.has_component<IDComponent>(), "Entity is missing IDComponent!");

        out << YAML::BeginMap; // Entity
        out << YAML::Key << "Entity" << YAML::Value << entity.get_uuid();

        if (entity.has_component<TagComponent>()) {
            out << YAML::Key << "TagComponent";
            out << YAML::BeginMap; // TagComponent

            auto& tag = entity.get_component<TagComponent>().tag;
            out << YAML::Key << "Tag" << YAML::Value << tag;

            out << YAML::EndMap; // TagComponent
        }

        if (entity.has_component<TransformComponent>()) {
            out << YAML::Key << "TransformComponent";
            out << YAML::BeginMap; // TransformComponent

            auto& tc = entity.get_component<TransformComponent>();
            out << YAML::Key << "Translation" << YAML::Value << tc.translation;
            out << YAML::Key << "Rotation" << YAML::Value << tc.rotation;
            out << YAML::Key << "Scale" << YAML::Value << tc.scale;
            //out << YAML::Key << "Dirty" << YAML::Value << tc.dirty;

            out << YAML::EndMap; // TransformComponent
        }

        if (entity.has_component<SpriteRendererComponent>()) {
            out << YAML::Key << "SpriteRendererComponent";
            out << YAML::BeginMap; // SpriteRendererComponent

            auto& sprite = entity.get_component<SpriteRendererComponent>();
            out << YAML::Key << "Color" << YAML::Value << sprite.color;

            if (sprite.sprite) {
                out << YAML::Key << "Texture" << YAML::Value
                    << (sprite.sprite_path.empty() ? "" : sprite.sprite_path.string());

                out << YAML::Key << "PPU" << YAML::Value << sprite.sprite->get_pixels_per_unit();
                out << YAML::Key << "Pivot" << YAML::Value << sprite.sprite->get_pivot();

            } else {
                out << YAML::Key << "Texture" << YAML::Value << "";
            }


            out << YAML::EndMap; // SpriteRendererComponent
        }

        if (entity.has_component<MeshRendererComponent>()) {
            out << YAML::Key << "MeshRendererComponent";
            out << YAML::BeginMap;

            auto& mr = entity.get_component<MeshRendererComponent>();
            out << YAML::Key << "Color" << YAML::Value << mr.color;
            out << YAML::Key << "MeshPath" << YAML::Value
                << (mr.mesh_path.empty() ? "" : mr.mesh_path.string());

            // Optionally: material overrides here

            out << YAML::EndMap;
        }

        if (entity.has_component<CircleRendererComponent>()) {
            out << YAML::Key << "CircleRendererComponent";
            out << YAML::BeginMap; // CircleRendererComponent

            auto& sprite = entity.get_component<CircleRendererComponent>();
            out << YAML::Key << "Color" << YAML::Value << sprite.color;
            out << YAML::Key << "Thickness" << YAML::Value << sprite.thickness;
            out << YAML::Key << "Fade" << YAML::Value << sprite.fade;

            if (sprite.texture) {
                out << YAML::Key << "Texture" << YAML::Value
                    << (sprite.texture_path.empty() ? "" : sprite.texture_path.string());
            } else {
                out << YAML::Key << "Texture" << YAML::Value << "";
            }


            out << YAML::EndMap; // CircleRendererComponent
        }

        if (entity.has_component<LineRendererComponent>()) {
            out << YAML::Key << "LineRendererComponent";
            out << YAML::BeginMap; // CircleRendererComponent

            auto& sprite = entity.get_component<LineRendererComponent>();
            out << YAML::Key << "Color" << YAML::Value << sprite.color;
            out << YAML::Key << "Fade" << YAML::Value << sprite.fade;

            if (sprite.texture) {
                out << YAML::Key << "Texture" << YAML::Value
                    << (sprite.texture_path.empty() ? "" : sprite.texture_path.string());
            } else {
                out << YAML::Key << "Texture" << YAML::Value << "";
            }


            out << YAML::EndMap; // LineRendererComponent
        }

        if (entity.has_component<CameraComponent>()) {
            out << YAML::Key << "CameraComponent";
            out << YAML::BeginMap; // CameraComponent

            auto& camera_component = entity.get_component<CameraComponent>();

            // Serialize projection type
            out << YAML::Key << "ProjectionType" << YAML::Value <<
                (camera_component.projection_type == CameraComponent::ProjectionType::Orthographic ? "Orthographic" : "Perspective");

            // Serialize orthographic parameters
            out << YAML::Key << "OrthographicSize" << YAML::Value << camera_component.orthographic_size;
            out << YAML::Key << "OrthographicNear" << YAML::Value << camera_component.orthographic_near;
            out << YAML::Key << "OrthographicFar" << YAML::Value << camera_component.orthographic_far;

            // Serialize perspective parameters
            out << YAML::Key << "PerspectiveFOV" << YAML::Value << camera_component.perspective_fov;
            out << YAML::Key << "PerspectiveNear" << YAML::Value << camera_component.perspective_near;
            out << YAML::Key << "PerspectiveFar" << YAML::Value << camera_component.perspective_far;

            // Serialize common parameters
            out << YAML::Key << "FixedAspectRatio" << YAML::Value << camera_component.fixed_aspect_ratio;
            out << YAML::Key << "Primary" << YAML::Value << camera_component.primary;

            // Serialize camera-specific data
            auto* camera = camera_component.get_camera();
            if (camera) {
                out << YAML::Key << "Camera";
                out << YAML::BeginMap; // Camera

                // Common camera properties
                out << YAML::Key << "Position";
                out << YAML::BeginSeq;
                out << camera->get_position();
                out << YAML::EndSeq;

                out << YAML::Key << "AspectRatio" << YAML::Value << camera->get_aspect_ratio();

                // Type-specific properties
                if (camera_component.projection_type == CameraComponent::ProjectionType::Orthographic) {
                    auto* ortho_camera = dynamic_cast<OrthographicCamera*>(camera);
                    if (ortho_camera) {
                        out << YAML::Key << "Rotation" << YAML::Value << ortho_camera->get_rotation();
                        out << YAML::Key << "Size" << YAML::Value << ortho_camera->get_size();
                    }
                } else if (camera_component.projection_type == CameraComponent::ProjectionType::Perspective) {
                    auto* persp_camera = dynamic_cast<PerspectiveCamera*>(camera);
                    if (persp_camera) {
                        out << YAML::Key << "FOV" << YAML::Value << persp_camera->get_fov();
                        out << YAML::Key << "Rotation" << YAML::Value << persp_camera->get_rotation();
                    }
                }

                out << YAML::EndMap; // Camera
            }

            out << YAML::EndMap; // CameraComponent
        }

        if (entity.has_component<NativeScriptComponent>()) {
            out << YAML::Key << "NativeScriptComponent";
            out << YAML::BeginMap;

            auto& nsc = entity.get_component<NativeScriptComponent>();
            out << YAML::Key << "ScriptName" << YAML::Value << nsc.script_name;

            // (Optional) If you support script properties (see Section 3 below),
            // you can emit them here:
            // out << YAML::Key << "Properties" << YAML::Value << nsc.serialize_properties();

            out << YAML::EndMap;
        }

        if (entity.has_component<ScriptComponent>()) {
            out << YAML::Key << "ScriptComponent";
            out << YAML::BeginMap;

            auto& script = entity.get_component<ScriptComponent>();
            out << YAML::Key << "ScriptName" << YAML::Value << script.script_name;

            out << YAML::EndMap;
        }

        if (entity.has_component<RelationshipComponent>()) {
            out << YAML::Key << "RelationshipComponent";
            out << YAML::BeginMap;

            auto& rc = entity.get_component<RelationshipComponent>();

            if (rc.parent != entt::null) {
                Entity parent{ rc.parent, entity.get_scene() };
                out << YAML::Key << "Parent" << YAML::Value << parent.get_uuid();
            } else {
                out << YAML::Key << "Parent" << YAML::Value << YAML::Null;
            }

            out << YAML::EndMap;
        }

        if (entity.has_component<Rigidbody2DComponent>()) {
            out << YAML::Key << "Rigidbody2DComponent";
            out << YAML::BeginMap;

            auto& rb = entity.get_component<Rigidbody2DComponent>();
            out << YAML::Key << "BodyType" << YAML::Value << (int)rb.body_type; // Rigidbody2DComponent::BodyType is an enum class and can be serialized as an integer index
            out << YAML::Key << "FixedRotation" << YAML::Value << rb.fixed_rotation;
            out << YAML::EndMap;
        }

        if (entity.has_component<BoxCollider2DComponent>()) {
            out << YAML::Key << "BoxCollider2DComponent";
            out << YAML::BeginMap;

            auto& bc = entity.get_component<BoxCollider2DComponent>();
            out << YAML::Key << "Offset" << YAML::Value << bc.offset;
            out << YAML::Key << "Size" << YAML::Value << bc.size;

            out << YAML::Key << "Density" << YAML::Value << bc.density;
            out << YAML::Key << "Friction" << YAML::Value << bc.friction;
            out << YAML::Key << "Restitution" << YAML::Value << bc.restitution;
            out << YAML::EndMap;
        }

        if (entity.has_component<CircleCollider2DComponent>()) {
            out << YAML::Key << "CircleCollider2DComponent";
            out << YAML::BeginMap;

            auto& cc = entity.get_component<CircleCollider2DComponent>();
            out << YAML::Key << "Offset" << YAML::Value << cc.offset;
            out << YAML::Key << "Radius" << YAML::Value << cc.radius;

            out << YAML::Key << "Density" << YAML::Value << cc.density;
            out << YAML::Key << "Friction" << YAML::Value << cc.friction;
            out << YAML::Key << "Restitution" << YAML::Value << cc.restitution;
            out << YAML::EndMap;
        }

        if (entity.has_component<AudioSourceComponent>()) {
            out << YAML::Key << "AudioSourceComponent";
            out << YAML::BeginMap;

            auto& audio = entity.get_component<AudioSourceComponent>();
            out << YAML::Key << "FilePath" << YAML::Value << (audio.file_path.empty() ? "" : audio.file_path.string());
            out << YAML::Key << "Loop" << YAML::Value << audio.loop;
            out << YAML::Key << "Pitch" << YAML::Value << audio.pitch;
            out << YAML::Key << "PlayOnSceneStart" << YAML::Value << audio.play_on_scene_start;
            out << YAML::Key << "Volume" << YAML::Value << audio.volume;
            out << YAML::EndMap;
        }



        out <<YAML::EndMap; // Entity
    }

    void SceneSerializer::serialize(const std::filesystem::path &path) {
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "Scene" << YAML::Value << "Untitled";
        out << YAML::Key << "Entities" << YAML::Value << YAML::BeginSeq;

        auto view = m_scene->m_registry.view<entt::entity>();
        for (auto entity_id : view) {
            Entity entity = { entity_id, m_scene.get() };
            if (!entity)
                return;

            serialize_entity(entity, out); // Children?
        }

        out << YAML::EndSeq;
        out << YAML::EndMap;

        std::filesystem::path file_path(path);
        std::filesystem::create_directories(file_path.parent_path());

        std::ofstream fout(path);
        fout << out.c_str();
        fout.close();

        HN_CORE_INFO("Serialized scene to {0}", path.generic_string());
    }

    void SceneSerializer::serialize_entity_prefab(const Entity& entity, const std::filesystem::path& path) {
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "Prefab" << YAML::Value << entity.get_tag();

        if (!entity) return;

        out << YAML::Key << "Entity";
        serialize_entity(entity, out); // Children?

        out << YAML::EndMap;

        std::filesystem::path file_path(path);
        std::filesystem::create_directories(file_path.parent_path());

        std::ofstream fout(path);
        fout << out.c_str();
        fout.close();

        HN_CORE_INFO("Serialized prefab to {0}", path.generic_string());
    }

    void SceneSerializer::serialize_runtime(const std::filesystem::path &path) {
        //not implemented
        HN_CORE_ASSERT(false, "Not implemented!");
    }

    Entity SceneSerializer::deserialize_entity_node(YAML::Node& entity_node, bool generate_new_uuid) {
        UUID uuid = generate_new_uuid
        ? UUID()
        : (UUID)entity_node["Entity"].as<uint64_t>();

        std::string name;
        auto tag_node = entity_node["TagComponent"];
        if (tag_node)
            name = tag_node["Tag"].as<std::string>();

        Entity deserialized_entity = m_scene->create_entity(name, uuid);

        auto transform_node = entity_node["TransformComponent"];
        if (transform_node) {
            m_pending_transforms.push_back({
                deserialized_entity,
                transform_node["Translation"].as<glm::vec3>(),
                transform_node["Rotation"].as<glm::vec3>(),
                transform_node["Scale"].as<glm::vec3>()
            });
        }

        auto sprite_node = entity_node["SpriteRendererComponent"];
        if (sprite_node) {
            auto& sprite = deserialized_entity.add_component<SpriteRendererComponent>();
            sprite.color = sprite_node["Color"].as<glm::vec4>();

            std::string texture_path_str = sprite_node["Texture"].as<std::string>("");
            int ppu = sprite_node["PPU"].as<int>(100);
            glm::vec2 pivot = sprite_node["Pivot"].as<glm::vec2>(glm::vec2(0.5f));

            if (!texture_path_str.empty()) {
                sprite.sprite_path = std::filesystem::path(texture_path_str);

                Ref<Texture2D::AsyncHandle> tex_handle = Texture2D::create_async_manual(texture_path_str);

                // If texture was already in cache or loaded instantly, this is valid now.
                if (tex_handle && tex_handle->texture) {
                    sprite.sprite = Sprite::create_from_texture(tex_handle->texture, ppu, pivot);
                } else {
                    // Optional: create a placeholder sprite if you want something visible
                    // sprite.sprite = Sprite::create_placeholder(ppu, pivot);
                }

                TaskHandle wait_task = TaskSystem::run_async(
                    [tex_handle, sprite_ptr = &sprite, ppu, pivot]() {
                        // Wait until backend async completes this handle
                        while (!tex_handle->done.load(std::memory_order_acquire)) {
                            // Small sleep to avoid a tight busy-wait loop
                            std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        }

                        // If it failed, leave whatever fallback we had
                        if (tex_handle->failed.load(std::memory_order_acquire) ||
                            !tex_handle->texture) {
                            return;
                        }

                        Ref<Texture2D> final_tex = tex_handle->texture;

                        // Schedule sprite update on main thread
                        TaskSystem::enqueue_main(
                            [sprite_ptr, final_tex, ppu, pivot]() {
                                if (!sprite_ptr)
                                    return;

                                if (!sprite_ptr->sprite) {
                                    // Sprite wasn't created yet; make it now.
                                    sprite_ptr->sprite = Sprite::create_from_texture(
                                        final_tex, ppu, pivot);
                                } else {
                                    // If Sprite supports changing texture in-place.
                                    sprite_ptr->sprite->set_texture(final_tex);
                                    sprite_ptr->sprite->recalc_size();
                                }
                            });
                    });

                // If you want to eventually join this specific wait_task somewhere, you can
                // store TaskHandle(s). In many cases you can just let it run to completion.
                (void)wait_task;
            }
        }

        auto mesh_node = entity_node["MeshRendererComponent"];
        if (mesh_node) {
            auto& mr = deserialized_entity.add_component<MeshRendererComponent>();
            mr.color = mesh_node["Color"].as<glm::vec4>();

            std::string mesh_path_str = mesh_node["MeshPath"].as<std::string>("");
            if (!mesh_path_str.empty()) {
                mr.mesh_path = std::filesystem::path(mesh_path_str);

                auto handle = load_gltf_mesh_async(mr.mesh_path);
                mr.async_load_handle = handle;
            }

            // Optionally: material overrides
        }

        auto circle_node = entity_node["CircleRendererComponent"];
        if (circle_node) {
            auto& sprite = deserialized_entity.add_component<CircleRendererComponent>();
            sprite.color = circle_node["Color"].as<glm::vec4>();
            sprite.thickness = circle_node["Thickness"].as<float>();
            sprite.fade = circle_node["Fade"].as<float>();

            std::string texture_path_str = circle_node["Texture"].as<std::string>("");
            if (!texture_path_str.empty()) {
                sprite.texture_path = std::filesystem::path(texture_path_str); // <-- keep it!
                sprite.texture = Texture2D::create_async(texture_path_str);
            }
        }

        auto line_node = entity_node["LineRendererComponent"];
        if (line_node) {
            auto& sprite = deserialized_entity.add_component<LineRendererComponent>();
            sprite.color = line_node["Color"].as<glm::vec4>();
            sprite.fade = line_node["Fade"].as<float>();

            std::string texture_path_str = line_node["Texture"].as<std::string>("");
            if (!texture_path_str.empty()) {
                sprite.texture_path = std::filesystem::path(texture_path_str); // <-- keep it!
                sprite.texture = Texture2D::create_async(texture_path_str);
            }
        }

        auto camera_node = entity_node["CameraComponent"];
        if (camera_node) {
            auto& camera_component = deserialized_entity.add_component<CameraComponent>();

            // Deserialize component parameters
            camera_component.fixed_aspect_ratio = camera_node["FixedAspectRatio"].as<bool>();
            camera_component.primary = camera_node["Primary"].as<bool>();
            camera_component.projection_type = camera_node["ProjectionType"].as<std::string>() == "Orthographic" ?
                CameraComponent::ProjectionType::Orthographic : CameraComponent::ProjectionType::Perspective;

            camera_component.orthographic_size = camera_node["OrthographicSize"].as<float>();
            camera_component.orthographic_near = camera_node["OrthographicNear"].as<float>();
            camera_component.orthographic_far = camera_node["OrthographicFar"].as<float>();

            camera_component.perspective_fov = camera_node["PerspectiveFOV"].as<float>();
            camera_component.perspective_near = camera_node["PerspectiveNear"].as<float>();
            camera_component.perspective_far = camera_node["PerspectiveFar"].as<float>();

            // Recreate the camera with the correct type and parameters
            if (camera_component.projection_type == CameraComponent::ProjectionType::Orthographic) {
                camera_component.camera = std::make_unique<OrthographicCamera>(
                    camera_component.orthographic_size,
                    1.6f, // Default aspect ratio, will be updated below
                    camera_component.orthographic_near,
                    camera_component.orthographic_far
                );
            } else {
                camera_component.camera = std::make_unique<PerspectiveCamera>(
                    camera_component.perspective_fov,
                    1.6f, // Default aspect ratio, will be updated below
                    camera_component.perspective_near,
                    camera_component.perspective_far
                );
            }

            // Deserialize camera-specific data
            auto* camera = camera_component.get_camera();
            if (camera) {
                auto camera_data_node = camera_node["Camera"];
                if (camera_data_node && !camera_data_node.IsNull()) {
                    // Fix the position parsing - your YAML has nested array
                    auto position_node = camera_data_node["Position"];
                    if (position_node && position_node.IsSequence() && position_node[0].IsSequence()) {
                        auto pos_array = position_node[0].as<std::vector<float>>();
                        if (pos_array.size() >= 3) {
                            camera->set_position(glm::vec3(pos_array[0], pos_array[1], pos_array[2]));
                        }
                    }

                    // Set aspect ratio
                    if (camera_data_node["AspectRatio"]) {
                        camera->set_aspect_ratio(camera_data_node["AspectRatio"].as<float>());
                    }

                    // Handle type-specific camera properties
                    if (camera_component.projection_type == CameraComponent::ProjectionType::Orthographic) {
                        auto* ortho_camera = dynamic_cast<OrthographicCamera*>(camera);
                        if (ortho_camera) {
                            if (camera_data_node["Rotation"]) {
                                ortho_camera->set_rotation(camera_data_node["Rotation"].as<float>());
                            }
                            if (camera_data_node["Size"]) {
                                ortho_camera->set_size(camera_data_node["Size"].as<float>());
                            }
                            if (camera_data_node["NearClip"]) {
                                ortho_camera->set_near_clip(camera_data_node["NearClip"].as<float>());
                            }
                            if (camera_data_node["FarClip"]) {
                                ortho_camera->set_far_clip(camera_data_node["FarClip"].as<float>());
                            }
                        }
                    } else if (camera_component.projection_type == CameraComponent::ProjectionType::Perspective) {
                        auto* persp_camera = dynamic_cast<PerspectiveCamera*>(camera);
                        if (persp_camera) {
                            if (camera_data_node["FOV"]) {
                                persp_camera->set_fov(camera_data_node["FOV"].as<float>());
                            }
                            if (camera_data_node["NearClip"]) {
                                persp_camera->set_near_clip(camera_data_node["NearClip"].as<float>());
                            }
                            if (camera_data_node["FarClip"]) {
                                persp_camera->set_far_clip(camera_data_node["FarClip"].as<float>());
                            }
                            if (camera_data_node["Rotation"] && camera_data_node["Rotation"].IsSequence()) {
                                persp_camera->set_rotation(camera_data_node["Rotation"].as<glm::vec2>());
                            }
                        }
                    }
                }
            }
        }

        auto native_script = entity_node["NativeScriptComponent"];
        if (native_script) {
            auto& nsc = deserialized_entity.add_component<NativeScriptComponent>();

            std::string script_name = native_script["ScriptName"].as<std::string>("");
            if (!script_name.empty()) {
                nsc.bind_by_name(script_name); // sets instantiate/destroy closures
            }

            // (Optional) If you support properties:
            // if (auto props = native_script["Properties"]) {
            //     nsc.deserialize_properties(props);
            // }
        }

        auto script_component = entity_node["ScriptComponent"];
        if (script_component) {

            std::string script_name = script_component["ScriptName"].as<std::string>("");
            if (!script_name.empty()) {
                auto& sc = deserialized_entity.add_component<ScriptComponent>();
                sc.script_name = script_name;
            }
        }

        auto relationship_node = entity_node["RelationshipComponent"];
        if (relationship_node && relationship_node["Parent"] && !relationship_node["Parent"].IsNull()) {
            UUID parent_uuid = relationship_node["Parent"].as<uint64_t>();
            m_pending_relationships.push_back({
                deserialized_entity,
                parent_uuid
            });
        }


        auto rigidbody_node = entity_node["Rigidbody2DComponent"];
        if (rigidbody_node) {
            auto& rb = deserialized_entity.add_component<Rigidbody2DComponent>();
            rb.body_type = (Rigidbody2DComponent::BodyType)rigidbody_node["BodyType"].as<int>();
            rb.fixed_rotation = rigidbody_node["FixedRotation"].as<bool>();
        }

        auto box_collider_node = entity_node["BoxCollider2DComponent"];
        if (box_collider_node) {
            auto& bc = deserialized_entity.add_component<BoxCollider2DComponent>();
            bc.offset = box_collider_node["Offset"].as<glm::vec2>();
            bc.size = box_collider_node["Size"].as<glm::vec2>();
            bc.density = box_collider_node["Density"].as<float>();
            bc.friction = box_collider_node["Friction"].as<float>();
            bc.restitution = box_collider_node["Restitution"].as<float>();
        }

        auto circle_collider_node = entity_node["CircleCollider2DComponent"];
        if (circle_collider_node) {
            auto& cc = deserialized_entity.add_component<CircleCollider2DComponent>();
            cc.offset = circle_collider_node["Offset"].as<glm::vec2>();
            cc.radius = circle_collider_node["Radius"].as<float>();
            cc.density = circle_collider_node["Density"].as<float>();
            cc.friction = circle_collider_node["Friction"].as<float>();
            cc.restitution = circle_collider_node["Restitution"].as<float>();
        }

        auto audio_source_node = entity_node["AudioSourceComponent"];
        if (audio_source_node) {
            auto& as = deserialized_entity.add_component<AudioSourceComponent>();
            as.file_path = audio_source_node["FilePath"].as<std::string>();
            as.loop = audio_source_node["Loop"].as<bool>();
            as.pitch = audio_source_node["Pitch"].as<float>();
            as.play_on_scene_start = audio_source_node["PlayOnSceneStart"].as<bool>();
            as.volume = audio_source_node["Volume"].as<float>();
        }

        return deserialized_entity;
    }

    bool SceneSerializer::deserialize(const std::filesystem::path &path) {
        std::ifstream stream(path);
        std::stringstream str_stream;
        str_stream << stream.rdbuf();

        YAML::Node data = YAML::Load(str_stream.str());
        if (!data["Scene"])
            return false;

        std::string scene_name = data["Scene"].as<std::string>();
        HN_CORE_INFO("Deserializing scene '{0}'", scene_name);

        auto entities_node = data["Entities"];
        for (auto entity_node : entities_node)
            deserialize_entity_node(entity_node);

        for (auto& rel : m_pending_relationships) {
            Entity parent = m_scene->get_entity(rel.parent_uuid);
            if (parent.is_valid()) {
                rel.child.set_parent(parent);
            } else {
                HN_CORE_WARN("Missing parent UUID {}", (uint64_t)rel.parent_uuid);
            }
        }

        for (auto& t : m_pending_transforms) {
            if (!t.entity.is_valid())
                continue;

            auto& tc = t.entity.get_component<TransformComponent>();
            tc.translation      = t.translation;
            tc.rotation         = t.rotation;
            tc.scale            = t.scale;
            tc.dirty            = true;
            tc.collider_dirty   = true;
        }

        m_pending_relationships.clear();
        m_pending_transforms.clear();

        return true;
    }

    Entity SceneSerializer::deserialize_entity_prefab(const std::filesystem::path& path) {
        std::ifstream stream(path);
        if (!stream.is_open()) {
            HN_CORE_ERROR("Failed to open prefab file: {}", path.string());
            return {};
        }

        std::stringstream str_stream;
        str_stream << stream.rdbuf();

        YAML::Node data = YAML::Load(str_stream.str());
        if (!data["Prefab"]) {
            HN_CORE_ERROR("Invalid prefab file: missing 'Prefab' node");
            return {};
        }

        std::string prefab_name = data["Prefab"].as<std::string>();
        //HN_CORE_INFO("Deserializing prefab '{}'", prefab_name);

        // This must exist
        YAML::Node entity_node = data["Entity"];
        if (!entity_node) {
            HN_CORE_ERROR("Prefab '{}' has no 'Entity' node", prefab_name);
            return {};
        }

        return deserialize_entity_node(entity_node, true);
    }

    bool SceneSerializer::deserialize_runtime(const std::filesystem::path &path) {

        //not implemented
        HN_CORE_ASSERT(false, "Not implemented!");
        return false;
    }
}
