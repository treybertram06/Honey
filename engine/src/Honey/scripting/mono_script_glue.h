#pragma once
#include "glm/vec3.hpp"

namespace Honey::Scripting {
    struct Vec3Interop;

    static void Entity_GetPosition(uint64_t entity_id, Vec3Interop* out_position);

    static void Entity_SetPosition(uint64_t entity_id, Vec3Interop* in_position);

    void register_internal_calls();


}
