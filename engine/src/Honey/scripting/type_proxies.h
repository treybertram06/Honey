#pragma once

#include "Honey/scene/components.h"
#include <glm/glm.hpp>

namespace Honey {

    enum class TransformVec3Kind {
        Translation,
        Rotation,
        Scale
    };

    struct LuaVec3Proxy {
        TransformComponent* tc;      // Pointer to owning TransformComponent
        glm::vec3* value;            // Pointer to translation / rotation / scale
        TransformVec3Kind kind;      // Is this referring to translation / rotation / scale?

        // --- Getters ---
        float get_x() const { return value->x; }
        float get_y() const { return value->y; }
        float get_z() const { return value->z; }

        // --- Setters ---
        void set_x(float v) {
            value->x = v;
            tc->dirty = true;

            if (kind == TransformVec3Kind::Scale) {
                tc->collider_dirty = true;
            }
        }

        void set_y(float v) {
            value->y = v;
            tc->dirty = true;

            if (kind == TransformVec3Kind::Scale) {
                tc->collider_dirty = true;
            }
        }

        void set_z(float v) {
            value->z = v;
            tc->dirty = true;

            if (kind == TransformVec3Kind::Scale) {
                tc->collider_dirty = true;
            }
        }

        // --- Helper: set entire vec3 ---
        void set_vec3(const glm::vec3& v) {
            *value = v;
            tc->dirty = true;

            if (kind == TransformVec3Kind::Scale) {
                tc->collider_dirty = true;
            }
        }

        glm::vec3 get_vec3() const {
            return *value;
        }
    };

}
