#pragma once

#include <filesystem>
#include <vector>
#include <cstdint>
#include "Honey/renderer/buffer.h"

namespace Honey {

    enum class PrimitiveTopology {
        Triangles,
        Lines,
        Points,
    };

    enum class CullMode {
        None,
        Back,
        Front,
    };

    enum class FrontFaceWinding {
        CounterClockwise,
        Clockwise,
    };

    struct BlendState {
        bool enabled = false;
        // You can expand later (src/dst factors, ops, separate alpha, etc.)
    };

    struct DepthStencilState {
        bool depthTest = false;
        bool depthWrite = false;
        // Later: depthCompareOp, stencil, etc.
    };

    // For now you only have "swapchain pass"; offscreen can be added later.
    enum class RenderPassType {
        Swapchain,   // uses the main window swapchain pass
        // Offscreen, // add later with explicit format/attachments
    };

    struct VertexInputBindingSpec {
        BufferLayout layout;
        // binding index is implicit from order in the vector
        // (0 = first, 1 = second, as with your VertexArray).
    };

    struct PipelineSpec {
        // High-level identity
        std::filesystem::path shaderGLSLPath;

        // Vertex input
        std::vector<VertexInputBindingSpec> vertexBindings;

        // Fixed-function state
        PrimitiveTopology topology = PrimitiveTopology::Triangles;
        CullMode          cullMode = CullMode::None;
        FrontFaceWinding  frontFace = FrontFaceWinding::CounterClockwise;
        BlendState        blend;
        DepthStencilState depthStencil;

        RenderPassType    passType = RenderPassType::Swapchain;
    };

} // namespace Honey