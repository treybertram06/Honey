#include "mono_script_glue.h"
#include "mono_script_engine.h"
#include "Honey/scene/entity.h"
#include "Honey/scene/components.h"
#include <mono/jit/jit.h>
#include <mono/metadata/assembly.h>
#include <mono/metadata/debug-helpers.h>

namespace Honey::Scripting {

    struct Vec3Interop {
        float x, y, z;
    };

    static void Entity_GetPosition(uint64_t entity_id, Vec3Interop* out_position)
    {
        HN_CORE_INFO("[Mono] Entity_GetPosition called for ID: {0}", entity_id);
        Scene* scene = Scene::get_active_scene();
        if (!scene)
        {
            HN_CORE_ERROR("[Mono] Scene::get_active_scene() returned null!");
            return;
        }

        Entity entity = { (entt::entity)entity_id, scene };
        if (entity.has_component<TransformComponent>())
        {
            const auto& translation = entity.get_component<TransformComponent>().translation;
            out_position->x = translation.x;
            out_position->y = translation.y;
            out_position->z = translation.z;
        }
    }

    static void Entity_SetPosition(uint64_t entity_id, Vec3Interop* in_position)
    {
        HN_CORE_INFO("[Mono] Entity_SetPosition called for ID: {0}", entity_id);
        Scene* scene = Scene::get_active_scene();
        if (!scene)
        {
            HN_CORE_ERROR("[Mono] Scene::get_active_scene() returned null!");
            return;
        }

        Entity entity = { (entt::entity)entity_id, scene };
        if (entity.has_component<TransformComponent>())
        {
            auto& translation = entity.get_component<TransformComponent>().translation;
            translation.x = in_position->x;
            translation.y = in_position->y;
            translation.z = in_position->z;
        }
    }

    void register_internal_calls()
    {
        mono_add_internal_call("Honey.Entity::GetPosition", (const void*)Entity_GetPosition);
        mono_add_internal_call("Honey.Entity::SetPosition", (const void*)Entity_SetPosition);
        HN_CORE_INFO("[Mono] Internal calls registered!");
    }
}
