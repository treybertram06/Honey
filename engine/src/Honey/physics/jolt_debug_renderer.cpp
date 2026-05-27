#include "hnpch.h"

#ifdef JPH_DEBUG_RENDERER

#include "jolt_debug_renderer.h"
#include "Honey/renderer/renderer_3d/debug_renderer_3d.h"

namespace Honey {

namespace {

glm::vec4 jph_color_to_glm(JPH::ColorArg c)
{
    return {
        c.r / 255.0f,
        c.g / 255.0f,
        c.b / 255.0f,
        c.a / 255.0f,
    };
}

glm::vec3 jph_vec3(JPH::RVec3Arg v)
{
    return { (float)v.GetX(), (float)v.GetY(), (float)v.GetZ() };
}

} // anonymous namespace

JoltDebugRenderer::JoltDebugRenderer()
{
    // DebugRendererSimple's constructor calls DebugRenderer::Initialize() for us.
    // PhysicsEngine3D passes this instance explicitly to DrawBodies/DrawConstraints
    // rather than relying on the global sInstance.
}

void JoltDebugRenderer::DrawLine(JPH::RVec3Arg from, JPH::RVec3Arg to,
                                  JPH::ColorArg color)
{
    DebugRenderer3D::draw_line(jph_vec3(from), jph_vec3(to), jph_color_to_glm(color));
}

void JoltDebugRenderer::DrawTriangle(JPH::RVec3Arg v1, JPH::RVec3Arg v2,
                                      JPH::RVec3Arg v3, JPH::ColorArg color,
                                      ECastShadow)
{
    // Wireframe only — draw three edges
    DebugRenderer3D::draw_line(jph_vec3(v1), jph_vec3(v2), jph_color_to_glm(color));
    DebugRenderer3D::draw_line(jph_vec3(v2), jph_vec3(v3), jph_color_to_glm(color));
    DebugRenderer3D::draw_line(jph_vec3(v3), jph_vec3(v1), jph_color_to_glm(color));
}

} // namespace Honey

#endif // JPH_DEBUG_RENDERER
