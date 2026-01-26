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
        bool operator==(const BlendState& other) const {
            return enabled == other.enabled;
        }
    };

    struct DepthStencilState {
        bool depthTest = false;
        bool depthWrite = false;
        // Later: depthCompareOp, stencil, etc.
        bool operator==(const DepthStencilState& other) const {
            return depthTest  == other.depthTest &&
                   depthWrite == other.depthWrite;
        }
    };

    // For now you only have "swapchain pass"; offscreen can be added later.
    enum class RenderPassType {
        Swapchain,   // uses the main window swapchain pass
        Offscreen,   // Editor / user-created VulkanFramebuffer
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
        bool              wireframe = false;
        BlendState        blend;
        DepthStencilState depthStencil;

        RenderPassType    passType = RenderPassType::Swapchain;

        bool operator==(const PipelineSpec& other) const {
            // If shader/layout ever vary by settings, compare them too:
            // if (shaderGLSLPath != other.shaderGLSLPath) return false;
            // if (vertexBindings != other.vertexBindings) return false;

            return topology   == other.topology   &&
                   cullMode   == other.cullMode   &&
                   frontFace  == other.frontFace  &&
                   wireframe  == other.wireframe  &&
                   blend      == other.blend      &&
                   depthStencil == other.depthStencil &&
                   passType   == other.passType;
        }
    };

} // namespace Honey