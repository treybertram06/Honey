#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Honey {

// Immediate-mode 3D debug draw. Call begin_scene() / end_scene() each frame
// around any draw_* calls. All primitives decompose to lines — one pipeline,
// one draw call per frame.
//
// Extensibility: to add a new primitive, add a draw_* method that calls
// draw_line() in a loop. No other changes required.
class DebugRenderer3D {
public:
    static void init();
    static void shutdown();

    // Must be called once per frame before any draw_* calls.
    static void begin_scene(const glm::mat4& view_proj);
    // Uploads the accumulated vertices and issues a single draw call.
    static void end_scene();

    // Returns true if begin_scene() has been called and end_scene() has not.
    static bool is_active();

    // ── Primitives ────────────────────────────────────────────────────────────

    // Fundamental — all composed primitives call this.
    static void draw_line(glm::vec3 from, glm::vec3 to, glm::vec4 color);

    // Axis-aligned bounding box (12 edges).
    static void draw_wire_aabb(glm::vec3 min, glm::vec3 max, glm::vec4 color);

    // Oriented box: center + half-extents in local space, rotation applied.
    static void draw_wire_box(glm::vec3 center, glm::vec3 half_extents,
                              glm::quat rotation, glm::vec4 color);

    // Wire sphere via three great circles (XY, XZ, YZ planes).
    static void draw_wire_sphere(glm::vec3 center, float radius, glm::vec4 color,
                                 int segments = 16);

    // Wire capsule: a cylinder plus two hemisphere outlines.
    // base/tip are the hemisphere centres (i.e. the flat ends of the cylindrical
    // section, NOT the extreme poles of the capsule).
    static void draw_wire_capsule(glm::vec3 base, glm::vec3 tip, float radius,
                                  glm::vec4 color, int segments = 16);

    // View frustum from inverse view-projection matrix.
    static void draw_wire_frustum(const glm::mat4& inv_view_proj, glm::vec4 color);

    // Three-axis cross at center (one segment per axis in ±direction).
    static void draw_cross(glm::vec3 center, float size, glm::vec4 color);

    // Arrow from → to with a small cone head approximated by two line pairs.
    static void draw_arrow(glm::vec3 from, glm::vec3 to, glm::vec4 color,
                           float head_fraction = 0.15f);

    // ── Stats ─────────────────────────────────────────────────────────────────

    struct Stats {
        uint32_t line_count  = 0;
        uint32_t draw_calls  = 0;
        uint32_t dropped_lines = 0; // lines silently dropped due to buffer full
    };
    static void  reset_stats();
    static Stats get_stats();
};

} // namespace Honey
