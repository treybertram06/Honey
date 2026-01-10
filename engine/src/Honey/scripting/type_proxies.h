#pragma once

#include "Honey/scene/components.h"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp> // if not already included elsewhere

namespace Honey {

    enum class TransformVec3Kind {
        Translation,
        Rotation,
        Scale
    };

    struct LuaVec3Proxy {
        TransformComponent* tc;      // Pointer to owning TransformComponent
        glm::vec3* value;           // Pointer to translation / rotation / scale
        TransformVec3Kind kind;     // Is this referring to translation / rotation / scale?

        // --- Internal helpers ---
        static float to_degrees(float radians) {
            return radians * (180.0f / glm::pi<float>());
        }

        static float to_radians(float degrees) {
            return degrees * (glm::pi<float>() / 180.0f);
        }

        float raw_get_x() const { return value->x; }
        float raw_get_y() const { return value->y; }
        float raw_get_z() const { return value->z; }

        void raw_set_x(float v) { value->x = v; }
        void raw_set_y(float v) { value->y = v; }
        void raw_set_z(float v) { value->z = v; }

        // --- Getters exposed to Lua ---
        float get_x() const {
            if (kind == TransformVec3Kind::Rotation)
                return to_degrees(raw_get_x());
            return raw_get_x();
        }

        float get_y() const {
            if (kind == TransformVec3Kind::Rotation)
                return to_degrees(raw_get_y());
            return raw_get_y();
        }

        float get_z() const {
            if (kind == TransformVec3Kind::Rotation)
                return to_degrees(raw_get_z());
            return raw_get_z();
        }

        // --- Setters exposed to Lua ---
        void set_x(float v) {
            if (kind == TransformVec3Kind::Rotation)
                raw_set_x(to_radians(v));
            else
                raw_set_x(v);

            tc->dirty = true;
            if (kind == TransformVec3Kind::Scale) {
                tc->collider_dirty = true;
            }
        }

        void set_y(float v) {
            if (kind == TransformVec3Kind::Rotation)
                raw_set_y(to_radians(v));
            else
                raw_set_y(v);

            tc->dirty = true;
            if (kind == TransformVec3Kind::Scale) {
                tc->collider_dirty = true;
            }
        }

        void set_z(float v) {
            if (kind == TransformVec3Kind::Rotation)
                raw_set_z(to_radians(v));
            else
                raw_set_z(v);

            tc->dirty = true;
            if (kind == TransformVec3Kind::Scale) {
                tc->collider_dirty = true;
            }
        }

        // --- Helper: set entire vec3 from Lua ---
        void set_vec3(const glm::vec3& v) {
            if (kind == TransformVec3Kind::Rotation) {
                // v is in degrees coming from Lua
                value->x = to_radians(v.x);
                value->y = to_radians(v.y);
                value->z = to_radians(v.z);
            } else {
                *value = v;
            }

            tc->dirty = true;
            if (kind == TransformVec3Kind::Scale) {
                tc->collider_dirty = true;
            }
        }

        glm::vec3 get_vec3() const {
            if (kind == TransformVec3Kind::Rotation) {
                // Return degrees to Lua
                return glm::vec3(
                    to_degrees(value->x),
                    to_degrees(value->y),
                    to_degrees(value->z)
                );
            }
            return *value;
        }
    };

}