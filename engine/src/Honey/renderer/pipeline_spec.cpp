#include "hnpch.h"
#include "pipeline_spec.h"
#include "renderer.h"
#include "Honey/core/settings.h"

#include <fstream>
#include <string>
#include <string_view>
#include <algorithm>
#include <unordered_set>

#include <spirv_reflect.hpp>

namespace Honey {

    namespace {

        static std::vector<uint32_t> read_spirv_u32_file_local(const std::filesystem::path& path) {
            std::ifstream file(path, std::ios::ate | std::ios::binary);
            HN_CORE_ASSERT(file.is_open(), "Failed to open SPIR-V file: {}", path.string());

            const std::streamsize size = file.tellg();
            HN_CORE_ASSERT(size > 0, "SPIR-V file empty: {}", path.string());
            HN_CORE_ASSERT((size % 4) == 0, "SPIR-V file size must be multiple of 4: {}", path.string());

            std::vector<uint32_t> data(static_cast<size_t>(size / 4));
            file.seekg(0);
            file.read(reinterpret_cast<char*>(data.data()), size);

            HN_CORE_ASSERT(!data.empty(), "SPIR-V read produced empty buffer: {}", path.string());
            HN_CORE_ASSERT(data[0] == 0x07230203u, "Invalid SPIR-V magic for file: {}", path.string());
            return data;
        }

        static bool looks_instanced_name(std::string_view name) {
            if (name.starts_with("a_i")) return true;
            if (name.starts_with("i_"))  return true;
            if (name.find("Instance") != std::string_view::npos) return true;
            if (name.find("instance") != std::string_view::npos) return true;
            return false;
        }

        static ShaderDataType spirv_cross_type_to_shader_data_type(const spirv_cross::SPIRType& t) {
            // Only support common vertex attribute types for now.
            // SPIRV-Cross represents vectors via vecsize, and scalars via basetype.
            const uint32_t vec = t.vecsize;

            // Disallow matrices for vertex inputs in this helper.
            // (If you need mat4, expose it as 4 separate vec4 attributes in the shader.)
            HN_CORE_ASSERT(t.columns <= 1, "Matrix vertex input is not supported (use separate vec inputs instead)");

            switch (t.basetype) {
            case spirv_cross::SPIRType::Float:
                switch (vec) {
                case 1: return ShaderDataType::Float;
                case 2: return ShaderDataType::Float2;
                case 3: return ShaderDataType::Float3;
                case 4: return ShaderDataType::Float4;
                default:
                    HN_CORE_ASSERT(false, "Unsupported float vector size: {}", vec);
                    return ShaderDataType::None;
                }

            case spirv_cross::SPIRType::Int:
                switch (vec) {
                case 1: return ShaderDataType::Int;
                case 2: return ShaderDataType::Int2;
                case 3: return ShaderDataType::Int3;
                case 4: return ShaderDataType::Int4;
                default:
                    HN_CORE_ASSERT(false, "Unsupported int vector size: {}", vec);
                    return ShaderDataType::None;
                }

            case spirv_cross::SPIRType::Boolean:
                HN_CORE_ASSERT(vec == 1, "Bool vectors not supported");
                return ShaderDataType::Bool;

            // If you ever need uint attributes, add ShaderDataType::UInt* or map to Int* explicitly.
            case spirv_cross::SPIRType::UInt:
                HN_CORE_ASSERT(false, "UInt vertex attributes not supported by ShaderDataType yet (add UInt types or map explicitly)");
                return ShaderDataType::None;

            default:
                HN_CORE_ASSERT(false, "Unsupported vertex input base type: {}", (int)t.basetype);
                return ShaderDataType::None;
            }
        }

        struct ReflectedInput {
            uint32_t location = 0;
            std::string name;
            ShaderDataType type = ShaderDataType::None;
            bool instanced = false;
        };

    } // namespace

    std::vector<VertexInputBindingSpec> PipelineSpec::reflect_vertex_input_bindings_from_spirv(
        const std::filesystem::path& vertexSpvPath
    ) {
        auto spirv = read_spirv_u32_file_local(vertexSpvPath);

        spirv_cross::Compiler compiler(spirv);
        spirv_cross::ShaderResources resources = compiler.get_shader_resources();

        std::vector<ReflectedInput> inputs;
        inputs.reserve(resources.stage_inputs.size());

        for (const auto& in : resources.stage_inputs) {
            const spirv_cross::ID id = in.id;

            if (compiler.has_decoration(id, spv::DecorationBuiltIn))
                continue;

            HN_CORE_ASSERT(compiler.has_decoration(id, spv::DecorationLocation),
                           "Vertex input '{}' has no location decoration (need layout(location=...))",
                           in.name);

            const uint32_t location = compiler.get_decoration(id, spv::DecorationLocation);

            std::string name = compiler.get_name(id);
            if (name.empty())
                name = in.name;

            HN_CORE_ASSERT(!name.empty(),
                           "SPIR-V input at location {} has no name. Ensure your shader uses named inputs.",
                           location);

            const auto& t = compiler.get_type(in.type_id);
            ShaderDataType sdt = spirv_cross_type_to_shader_data_type(t);

            ReflectedInput ri{};
            ri.location = location;
            ri.name = std::move(name);
            ri.type = sdt;
            ri.instanced = looks_instanced_name(ri.name);

            inputs.push_back(std::move(ri));
        }

        std::sort(inputs.begin(), inputs.end(), [](const ReflectedInput& a, const ReflectedInput& b) {
            return a.location < b.location;
        });

        std::vector<BufferElement> perVertexEls;
        std::vector<BufferElement> perInstanceEls;
        std::vector<uint32_t> perVertexLocs;
        std::vector<uint32_t> perInstanceLocs;

        perVertexEls.reserve(inputs.size());
        perInstanceEls.reserve(inputs.size());
        perVertexLocs.reserve(inputs.size());
        perInstanceLocs.reserve(inputs.size());

        std::unordered_set<uint32_t> seenLocations;
        seenLocations.reserve(inputs.size());

        for (const auto& in : inputs) {
            HN_CORE_ASSERT(seenLocations.insert(in.location).second,
                           "Duplicate vertex input location {} ('{}') in {}",
                           in.location, in.name, vertexSpvPath.string());

            if (in.instanced) {
                perInstanceEls.emplace_back(in.type, in.name, false, true);
                perInstanceLocs.push_back(in.location);
            } else {
                perVertexEls.emplace_back(in.type, in.name, false, false);
                perVertexLocs.push_back(in.location);
            }
        }

        BufferLayout perVertex{ std::move(perVertexEls) };
        BufferLayout perInstance{ std::move(perInstanceEls) };

        std::vector<VertexInputBindingSpec> out;

        if (perVertex.get_elements().empty() && perInstance.get_elements().empty()) {
            HN_CORE_WARN("No vertex inputs reflected from {}", vertexSpvPath.string());
        }

        if (!perVertex.get_elements().empty()) {
            VertexInputBindingSpec vb{};
            vb.layout = perVertex;
            vb.locations = std::move(perVertexLocs);
            out.push_back(std::move(vb));
        }
        if (!perInstance.get_elements().empty()) {
            VertexInputBindingSpec vb{};
            vb.layout = perInstance;
            vb.locations = std::move(perInstanceLocs);
            out.push_back(std::move(vb));
        }

        return out;
    }

     PipelineSpec PipelineSpec::from_shader(const std::filesystem::path& path) {
        auto& rs = Settings::get().renderer;

        PipelineSpec spec{};
        spec.shaderGLSLPath = path;
        spec.topology = PrimitiveTopology::Triangles;
        spec.cullMode = rs.cull_mode;
        spec.frontFace = FrontFaceWinding::CounterClockwise;
        spec.wireframe = rs.wireframe;

        spec.depthStencil.depthTest = rs.depth_test;
        spec.depthStencil.depthWrite = rs.depth_write;

        if (Renderer::get_render_target() == nullptr)
            spec.passType = RenderPassType::Swapchain;
        else
            spec.passType = RenderPassType::Offscreen;

        const auto spirv = Renderer::get_shader_cache()->get_or_compile_spirv_paths(spec.shaderGLSLPath);
        auto bindings = reflect_vertex_input_bindings_from_spirv(spirv.vertex);

        spec.vertexBindings.clear();
        spec.vertexBindings = std::move(bindings);

        spec.perColorAttachmentBlend.clear();
        AttachmentBlendState b0{};
        b0.enabled = rs.blending;
        spec.perColorAttachmentBlend.push_back(b0);

        // TEMP HACK for editor offscreen FB:
        // We know the editor framebuffer has:
        //  - color[0] = RGBA8 (blended)
        //  - color[1] = R32_SINT (entity ID, must NOT be blended)
        //
        // Until we have a proper render‑graph describing per‑attachment state,
        // we just add a second, non‑blended attachment when building pipelines
        // for an offscreen pass that uses the quad / editor shaders.
        //
        // TODO(HN‑rendergraph): derive perColorAttachmentBlend from the actual
        // render‑pass / framebuffer description instead of hardcoding this.
        if (spec.passType == RenderPassType::Offscreen) {
            const std::string glsl_name = path.filename().string();
            //if (glsl_name.find("Renderer2D_Quad") != std::string::npos) {
                AttachmentBlendState b1{};
                b1.enabled = false; // integer ID buffer, no blending
                spec.perColorAttachmentBlend.push_back(b1);
            //}
        }

        return spec;
    }

} // namespace Honey