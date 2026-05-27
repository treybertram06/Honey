#pragma once

#ifdef JPH_DEBUG_RENDERER

#include <Jolt/Jolt.h>
#include <Jolt/Renderer/DebugRendererSimple.h>

namespace Honey {

// Bridges JPH::DebugRendererSimple → DebugRenderer3D.
// DebugRendererSimple handles CreateTriangleBatch + DrawGeometry internally,
// decomposing everything down to DrawLine + DrawTriangle calls.
class JoltDebugRenderer final : public JPH::DebugRendererSimple {
public:
    JoltDebugRenderer();
    ~JoltDebugRenderer() override = default;

    void DrawLine(JPH::RVec3Arg from, JPH::RVec3Arg to,
                  JPH::ColorArg color) override;

    void DrawTriangle(JPH::RVec3Arg v1, JPH::RVec3Arg v2, JPH::RVec3Arg v3,
                      JPH::ColorArg color, ECastShadow) override;

    // Text rendering not supported — silently ignored.
    void DrawText3D(JPH::RVec3Arg, const std::string_view&,
                    JPH::ColorArg, float) override {}
};

} // namespace Honey

#endif // JPH_DEBUG_RENDERER
