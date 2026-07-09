#pragma once

#include <filesystem>
#include <vector>
#include <cstdint>

#include "Honey/renderer/buffer.h"
#include <vulkan/vulkan_core.h>

#include "shader_compiler.h"

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

    enum class PipelineKind {
        Graphics,
        Compute,
        MeshShading
    };

    struct AttachmentBlendState {
        bool enabled = false;
        // You can expand later (src/dst factors, ops, separate alpha, etc.)
        bool operator==(const AttachmentBlendState& other) const {
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
        std::vector<uint32_t> locations;
    };

    // Engine-owned shader stage bits (one-hot). OR together for multi-stage bindings.
    enum ShaderStageBits : uint32_t {
        Stage_Vertex      = 1u << 0,
        Stage_Fragment    = 1u << 1,
        Stage_Compute     = 1u << 2,
        Stage_Geometry    = 1u << 3,
        Stage_TessControl = 1u << 4,
        Stage_TessEval    = 1u << 5,
        Stage_Task        = 1u << 6,
        Stage_Mesh        = 1u << 7,
        Stage_RayGen      = 1u << 8,
        Stage_Miss        = 1u << 9,
        Stage_ClosestHit  = 1u << 10,
    };

    struct ReflectedBinding {
        uint32_t set;
        uint32_t binding;
        VkDescriptorType type; // TODO: These pull vulkan headers into a renderer agnostic header, oopsie
        uint32_t stages; // bitmask of ShaderStageBits
        uint32_t count; // array size, 0 = unbounded
        // Best-effort hint from SPIR-V's image Depth operand, not a reliable "is shadow sampler"
        // signal for GLSL sources (see pick_static_sampler in vk_descriptor_mapping.cpp, which
        // treats the "shadow"/"cmp" name convention as the authoritative check).
        bool is_comparison_sampler;
        std::string name; // exact shader identifier (ex: "u_gAlbedo")
    };

    struct ReflectedShader {
        std::vector<ReflectedBinding> bindings;
    };

    struct PipelineSpec {
        // High-level identity
        std::filesystem::path shaderGLSLPath;
        PipelineKind pipelineKind = PipelineKind::Graphics;

        // Vertex input
        std::vector<VertexInputBindingSpec> vertexBindings;

        // set>=1 descriptor bindings reflected from SPIR-V (shader-derived, not part of operator==).
        ReflectedShader reflection;

        // Fixed-function state
        PrimitiveTopology topology = PrimitiveTopology::Triangles;
        CullMode          cullMode = CullMode::None;
        FrontFaceWinding  frontFace = FrontFaceWinding::CounterClockwise;
        bool              wireframe = false;
        float             depthBiasConstantFactor = 0.0f;
        float             depthBiasSlopeFactor    = 0.0f;
        std::vector<AttachmentBlendState> perColorAttachmentBlend;
        DepthStencilState depthStencil;

        RenderPassType    passType = RenderPassType::Swapchain;

        bool operator==(const PipelineSpec& other) const {
            // If shader/layout ever vary by settings, compare them too:
            // if (shaderGLSLPath != other.shaderGLSLPath) return false;
            // if (vertexBindings != other.vertexBindings) return false;

            return pipelineKind == other.pipelineKind &&
                   topology   == other.topology   &&
                   cullMode   == other.cullMode   &&
                   frontFace  == other.frontFace  &&
                   wireframe  == other.wireframe  &&
                   depthBiasConstantFactor == other.depthBiasConstantFactor &&
                   depthBiasSlopeFactor    == other.depthBiasSlopeFactor    &&
                   perColorAttachmentBlend == other.perColorAttachmentBlend &&
                   depthStencil == other.depthStencil &&
                   passType   == other.passType;
        }

        static std::vector<VertexInputBindingSpec> reflect_vertex_input_bindings_from_spirv(const std::filesystem::path& vertexSpvPath);
        // Reflects one SPIR-V module's set>=1 descriptor bindings.
        static ReflectedShader reflect_descriptor_bindings_from_spirv(const std::filesystem::path& spv_path);
        // Reflects a whole program (vert+frag, compute, or mesh+frag): reflects each present module
        // and merges bindings sharing a (set, binding), OR-ing their stage bits together.
        static ReflectedShader reflect_descriptor_bindings_from_spirv(const std::vector<std::filesystem::path>& module_spv_paths);

        static PipelineSpec from_shader(const std::filesystem::path& path);
    };
}
