#include "hnpch.h"
#include "vk_pipeline.h"
#include "Honey/renderer/buffer.h"
#include "Honey/renderer/pipeline_spec.h"

#include <fstream>
#include <vector>
#include <glm/glm.hpp>

#define GLFW_INCLUDE_VULKAN
#include <vulkan/vulkan.h>


namespace Honey {

    static VkFormat shader_data_type_to_vk_format(ShaderDataType type) {
        switch (type) {
        case ShaderDataType::Float:  return VK_FORMAT_R32_SFLOAT;
        case ShaderDataType::Float2: return VK_FORMAT_R32G32_SFLOAT;
        case ShaderDataType::Float3: return VK_FORMAT_R32G32B32_SFLOAT;
        case ShaderDataType::Float4: return VK_FORMAT_R32G32B32A32_SFLOAT;

        case ShaderDataType::Int:    return VK_FORMAT_R32_SINT;
        case ShaderDataType::Int2:   return VK_FORMAT_R32G32_SINT;
        case ShaderDataType::Int3:   return VK_FORMAT_R32G32B32_SINT;
        case ShaderDataType::Int4:   return VK_FORMAT_R32G32B32A32_SINT;

        case ShaderDataType::Bool:   return VK_FORMAT_R8_UINT;

        case ShaderDataType::Mat3:
        case ShaderDataType::Mat4:
        case ShaderDataType::None:
        default:
            HN_CORE_ASSERT(false, "shader_data_type_to_vk_format: unsupported ShaderDataType");
            return VK_FORMAT_UNDEFINED;
        }
    }

    struct VertexInputBuildResult {
        std::vector<VkVertexInputBindingDescription> bindings;
        std::vector<VkVertexInputAttributeDescription> attributes;
    };

    static VertexInputBuildResult build_vertex_input_from_spec(const PipelineSpec& spec) {
        VertexInputBuildResult out{};

        out.bindings.reserve(spec.vertexBindings.size());

        for (uint32_t bindingIndex = 0; bindingIndex < spec.vertexBindings.size(); ++bindingIndex) {
            const VertexInputBindingSpec& bindingSpec = spec.vertexBindings[bindingIndex];
            const BufferLayout& layout = bindingSpec.layout;
            const auto& elements = layout.get_elements();

            HN_CORE_ASSERT(bindingSpec.locations.size() == elements.size(),
                "VertexInputBindingSpec.locations must have same size as layout elements (binding {}).", bindingIndex);

            // Special-case: instance model matrix (4x vec4) for Renderer3D_Forward
            const auto& els = elements;
            const bool looks_like_instance_mat4 =
                els.size() == 4 &&
                els[0].name == "a_iModel0" && els[1].name == "a_iModel1" &&
                els[2].name == "a_iModel2" && els[3].name == "a_iModel3" &&
                els[0].type == ShaderDataType::Float4 &&
                els[1].type == ShaderDataType::Float4 &&
                els[2].type == ShaderDataType::Float4 &&
                els[3].type == ShaderDataType::Float4;

            if (looks_like_instance_mat4) {
                VkVertexInputBindingDescription b{};
                b.binding   = bindingIndex;
                b.stride    = sizeof(glm::mat4);
                b.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
                out.bindings.push_back(b);

                // Use reflected locations (still 4 attrs)
                for (uint32_t i = 0; i < 4; ++i) {
                    VkVertexInputAttributeDescription a{};
                    a.location = bindingSpec.locations[i];
                    a.binding  = bindingIndex;
                    a.format   = VK_FORMAT_R32G32B32A32_SFLOAT;
                    a.offset   = 16u * i;
                    out.attributes.push_back(a);
                }
                continue;
            }

            VkVertexInputBindingDescription b{};
            b.binding   = bindingIndex;
            b.stride    = layout.get_stride();
            b.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            bool any_instanced = false;
            bool any_non_instanced = false;

            for (const auto& el : elements) {
                if (el.instanced) any_instanced = true;
                else              any_non_instanced = true;
            }

            HN_CORE_ASSERT(!(any_instanced && any_non_instanced),
                "Vulkan vertex layout: cannot mix instanced and non-instanced attributes in a single buffer");

            if (any_instanced)
                b.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

            out.bindings.push_back(b);

            for (uint32_t i = 0; i < (uint32_t)elements.size(); ++i) {
                const auto& el = elements[i];

                VkVertexInputAttributeDescription a{};
                a.location = bindingSpec.locations[i];
                a.binding  = bindingIndex;
                a.format   = shader_data_type_to_vk_format(el.type);
                a.offset   = static_cast<uint32_t>(el.offset);

                HN_CORE_ASSERT(a.format != VK_FORMAT_UNDEFINED,
                    "Failed to map ShaderDataType to VkFormat for attribute '{}'", el.name);

                out.attributes.push_back(a);
            }
        }

        return out;
    }

    static std::vector<uint32_t> read_spirv_u32_file(const std::string& path) {
        std::ifstream file(path, std::ios::ate | std::ios::binary);
        HN_CORE_ASSERT(file.is_open(), "Failed to open SPIR-V file: {0}", path);

        const std::streamsize size = file.tellg();
        HN_CORE_ASSERT(size > 0, "SPIR-V file empty: {0}", path);
        HN_CORE_ASSERT((size % 4) == 0, "SPIR-V file size must be multiple of 4: {0}", path);

        std::vector<uint32_t> data(static_cast<size_t>(size / 4));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(data.data()), size);

        HN_CORE_ASSERT(!data.empty(), "SPIR-V read produced empty buffer: {0}", path);
        HN_CORE_ASSERT(data[0] == 0x07230203u, "Invalid SPIR-V magic for file: {0}", path);
        return data;
    }

    VkShaderModule VulkanPipeline::create_shader_module_from_file(VkDevice device, const std::string& path) {
        HN_PROFILE_FUNCTION();
        auto code = read_spirv_u32_file(path);

        VkShaderModuleCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ci.codeSize = code.size() * sizeof(uint32_t);
        ci.pCode = code.data();

        VkShaderModule module = VK_NULL_HANDLE;
        VkResult res = vkCreateShaderModule(device, &ci, nullptr, &module);
        HN_CORE_ASSERT(res == VK_SUCCESS, "vkCreateShaderModule failed for {0}", path);
        return module;
    }

    void VulkanPipeline::destroy(VkDevice device) {
        HN_PROFILE_FUNCTION();
        if (!device)
            return;

        vkDeviceWaitIdle(device);

        if (m_pipeline) {
            vkDestroyPipeline(device, reinterpret_cast<VkPipeline>(m_pipeline), nullptr);
            m_pipeline = nullptr;
        }
        if (m_layout) {
            vkDestroyPipelineLayout(device, reinterpret_cast<VkPipelineLayout>(m_layout), nullptr);
            m_layout = nullptr;
        }
        if (m_vert_module) {
            vkDestroyShaderModule(device, reinterpret_cast<VkShaderModule>(m_vert_module), nullptr);
            m_vert_module = nullptr;
        }
        if (m_frag_module) {
            vkDestroyShaderModule(device, reinterpret_cast<VkShaderModule>(m_frag_module), nullptr);
            m_frag_module = nullptr;
        }
        if (m_task_module) {
            vkDestroyShaderModule(device, reinterpret_cast<VkShaderModule>(m_task_module), nullptr);
            m_task_module = nullptr;
        }
        if (m_mesh_module) {
            vkDestroyShaderModule(device, reinterpret_cast<VkShaderModule>(m_mesh_module), nullptr);
            m_mesh_module = nullptr;
        }
    }

    void VulkanPipeline::create(
        VkDevice device,
        VkRenderPass render_pass,
        VkDescriptorSetLayout global_set_layout,
        const std::string& vertex_spirv_path,
        const std::string& fragment_spirv_path,
        const PipelineSpec& spec,
        VkPipelineCache pipeline_cache,
        VkDescriptorSetLayout extra_set_layout
    ) {
        HN_PROFILE_FUNCTION();

        HN_CORE_ASSERT(device, "VulkanPipeline::create called with null device");
        HN_CORE_ASSERT(render_pass, "VulkanPipeline::create called with null render pass");
        HN_CORE_ASSERT(global_set_layout, "VulkanPipeline::create called with null descriptor set layout");
        HN_CORE_ASSERT(!vertex_spirv_path.empty() && !fragment_spirv_path.empty(),
                       "VulkanPipeline::create called with empty SPIR-V paths");

        destroy(device);

        m_vert_module = create_shader_module_from_file(device, vertex_spirv_path);
        m_frag_module = create_shader_module_from_file(device, fragment_spirv_path);

        VkPipelineShaderStageCreateInfo vert_stage{};
        vert_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vert_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vert_stage.module = reinterpret_cast<VkShaderModule>(m_vert_module);
        vert_stage.pName = "main";

        VkPipelineShaderStageCreateInfo frag_stage{};
        frag_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        frag_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        frag_stage.module = reinterpret_cast<VkShaderModule>(m_frag_module);
        frag_stage.pName = "main";

        VkPipelineShaderStageCreateInfo stages[] = { vert_stage, frag_stage };

        VertexInputBuildResult vi = build_vertex_input_from_spec(spec);

        VkPipelineVertexInputStateCreateInfo vertex_input{};
        vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertex_input.vertexBindingDescriptionCount = static_cast<uint32_t>(vi.bindings.size());
        vertex_input.pVertexBindingDescriptions = vi.bindings.data();
        vertex_input.vertexAttributeDescriptionCount = static_cast<uint32_t>(vi.attributes.size());
        vertex_input.pVertexAttributeDescriptions = vi.attributes.data();

        VkPipelineInputAssemblyStateCreateInfo input_assembly{};
        input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        switch (spec.topology) {
        case PrimitiveTopology::Triangles: input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; break;
        case PrimitiveTopology::Lines:     input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;     break;
        case PrimitiveTopology::Points:    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;    break;
        default:                           input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; break;
        }
        input_assembly.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo viewport_state{};
        viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport_state.viewportCount = 1;
        viewport_state.scissorCount = 1;

        VkDynamicState dyn_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamic_state{};
        dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic_state.dynamicStateCount = 2;
        dynamic_state.pDynamicStates = dyn_states;

        VkPipelineRasterizationStateCreateInfo raster{};
        raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster.depthClampEnable = VK_FALSE;
        raster.rasterizerDiscardEnable = VK_FALSE;
        raster.polygonMode = spec.wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        raster.lineWidth = 1.0f;

        switch (spec.cullMode) {
        case CullMode::None:  raster.cullMode = VK_CULL_MODE_NONE; break;
        case CullMode::Back:  raster.cullMode = VK_CULL_MODE_BACK_BIT; break;
        case CullMode::Front: raster.cullMode = VK_CULL_MODE_FRONT_BIT; break;
        }

        switch (spec.frontFace) {
        case FrontFaceWinding::CounterClockwise: raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; break;
        case FrontFaceWinding::Clockwise:        raster.frontFace = VK_FRONT_FACE_CLOCKWISE; break;
        }

        raster.depthBiasEnable         = (spec.depthBiasConstantFactor != 0.0f || spec.depthBiasSlopeFactor != 0.0f) ? VK_TRUE : VK_FALSE;
        raster.depthBiasConstantFactor = spec.depthBiasConstantFactor;
        raster.depthBiasSlopeFactor    = spec.depthBiasSlopeFactor;
        raster.depthBiasClamp          = 0.0f;

        VkPipelineMultisampleStateCreateInfo msaa{};
        msaa.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        msaa.sampleShadingEnable = VK_FALSE;

        VkPipelineDepthStencilStateCreateInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth.depthTestEnable  = spec.depthStencil.depthTest  ? VK_TRUE : VK_FALSE;
        depth.depthWriteEnable = spec.depthStencil.depthWrite ? VK_TRUE : VK_FALSE;
        depth.depthCompareOp = VK_COMPARE_OP_LESS;
        depth.depthBoundsTestEnable = VK_FALSE;
        depth.stencilTestEnable = VK_FALSE;

        // Per‑attachment color blend state
        std::vector<VkPipelineColorBlendAttachmentState> blend_attachments;
        blend_attachments.resize(std::max<size_t>(1, spec.perColorAttachmentBlend.size()));

        for (size_t i = 0; i < blend_attachments.size(); ++i) {
            const bool enable =
                (i < spec.perColorAttachmentBlend.size())
                    ? spec.perColorAttachmentBlend[i].enabled
                    : false;

            auto& att = blend_attachments[i];
            att.colorWriteMask =
                VK_COLOR_COMPONENT_R_BIT |
                VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT |
                VK_COLOR_COMPONENT_A_BIT;

            att.blendEnable = enable ? VK_TRUE : VK_FALSE;

            if (enable) {
                att.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                att.colorBlendOp        = VK_BLEND_OP_ADD;
                att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                att.alphaBlendOp        = VK_BLEND_OP_ADD;
            } else {
                att.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
                att.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
                att.colorBlendOp        = VK_BLEND_OP_ADD;
                att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
                att.alphaBlendOp        = VK_BLEND_OP_ADD;
            }
        }

        VkPipelineColorBlendStateCreateInfo blend{};
        blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend.logicOpEnable = VK_FALSE;
        blend.attachmentCount = static_cast<uint32_t>(blend_attachments.size());
        blend.pAttachments = blend_attachments.data();

        VkPipelineLayoutCreateInfo layout_ci{};
        layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        VkDescriptorSetLayout set_layouts[] = { global_set_layout, extra_set_layout };
        layout_ci.setLayoutCount = extra_set_layout ? 2u : 1u;
        layout_ci.pSetLayouts = set_layouts;

        VkPushConstantRange pc_range{};
        pc_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pc_range.offset = 0;
        pc_range.size = k_push_constant_max_size;

        layout_ci.pushConstantRangeCount = 1;
        layout_ci.pPushConstantRanges = &pc_range;

        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkResult layout_res = vkCreatePipelineLayout(device, &layout_ci, nullptr, &layout);
        HN_CORE_ASSERT(layout_res == VK_SUCCESS, "vkCreatePipelineLayout failed");
        m_layout = layout;

        VkGraphicsPipelineCreateInfo pipe{};
        pipe.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipe.stageCount = 2;
        pipe.pStages = stages;
        pipe.pVertexInputState   = &vertex_input;
        pipe.pInputAssemblyState = &input_assembly;
        pipe.pViewportState      = &viewport_state;
        pipe.pRasterizationState = &raster;
        pipe.pMultisampleState   = &msaa;
        pipe.pDepthStencilState  = &depth;
        pipe.pColorBlendState    = &blend;
        pipe.pDynamicState       = &dynamic_state;
        pipe.layout    = (VkPipelineLayout)(m_layout);
        pipe.renderPass = render_pass;
        pipe.subpass   = 0;

        VkPipeline pipeline = VK_NULL_HANDLE;
        VkResult pr = vkCreateGraphicsPipelines(device, pipeline_cache, 1, &pipe, nullptr, &pipeline);
        HN_CORE_ASSERT(pr == VK_SUCCESS, "vkCreateGraphicsPipelines failed");
        m_pipeline = pipeline;
    }

    void VulkanPipeline::create_mesh(
        VkDevice device,
        VkRenderPass render_pass,
        VkDescriptorSetLayout global_set_layout,
        const std::string& task_spirv_path,
        const std::string& mesh_spirv_path,
        const std::string& fragment_spirv_path,
        const PipelineSpec& spec,
        VkPipelineCache pipeline_cache,
        VkDescriptorSetLayout extra_set_layout
    ) {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(device, "VulkanPipeline::create_mesh called with null device");
        HN_CORE_ASSERT(render_pass, "VulkanPipeline::create_mesh called with null render pass");
        HN_CORE_ASSERT(global_set_layout, "VulkanPipeline::create_mesh called with null descriptor set layout");
        HN_CORE_ASSERT(!mesh_spirv_path.empty() && !fragment_spirv_path.empty(),
                       "VulkanPipeline::create_mesh called with empty mesh or fragment SPIR-V path");

        destroy(device);

        const bool has_task = !task_spirv_path.empty();
        if (has_task)
            m_task_module = create_shader_module_from_file(device, task_spirv_path);
        m_mesh_module = create_shader_module_from_file(device, mesh_spirv_path);
        m_frag_module = create_shader_module_from_file(device, fragment_spirv_path);

        VkPipelineShaderStageCreateInfo mesh_stage{};
        mesh_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        mesh_stage.stage = VK_SHADER_STAGE_MESH_BIT_EXT;
        mesh_stage.module = reinterpret_cast<VkShaderModule>(m_mesh_module);
        mesh_stage.pName = "main";

        VkPipelineShaderStageCreateInfo frag_stage{};
        frag_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        frag_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        frag_stage.module = reinterpret_cast<VkShaderModule>(m_frag_module);
        frag_stage.pName = "main";

        VkPipelineShaderStageCreateInfo task_stage{};
        task_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        task_stage.stage = VK_SHADER_STAGE_TASK_BIT_EXT;
        task_stage.module = reinterpret_cast<VkShaderModule>(m_task_module);
        task_stage.pName = "main";

        // task is optional: [task,] mesh, fragment
        VkPipelineShaderStageCreateInfo stages_with_task[] = { task_stage, mesh_stage, frag_stage };
        VkPipelineShaderStageCreateInfo stages_no_task[]   = { mesh_stage, frag_stage };
        const auto* stages      = has_task ? stages_with_task : stages_no_task;
        const uint32_t stage_count = has_task ? 3u : 2u;

        VkPipelineViewportStateCreateInfo viewport_state{};
        viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport_state.viewportCount = 1;
        viewport_state.scissorCount = 1;

        VkDynamicState dyn_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamic_state{};
        dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic_state.dynamicStateCount = 2;
        dynamic_state.pDynamicStates = dyn_states;

        VkPipelineRasterizationStateCreateInfo raster{};
        raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster.depthClampEnable = VK_FALSE;
        raster.rasterizerDiscardEnable = VK_FALSE;
        raster.polygonMode = spec.wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        raster.lineWidth = 1.0f;

        switch (spec.cullMode) {
        case CullMode::None:  raster.cullMode = VK_CULL_MODE_NONE; break;
        case CullMode::Back:  raster.cullMode = VK_CULL_MODE_BACK_BIT; break;
        case CullMode::Front: raster.cullMode = VK_CULL_MODE_FRONT_BIT; break;
        }

        switch (spec.frontFace) {
        case FrontFaceWinding::CounterClockwise: raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; break;
        case FrontFaceWinding::Clockwise:        raster.frontFace = VK_FRONT_FACE_CLOCKWISE; break;
        }

        raster.depthBiasEnable         = (spec.depthBiasConstantFactor != 0.0f || spec.depthBiasSlopeFactor != 0.0f) ? VK_TRUE : VK_FALSE;
        raster.depthBiasConstantFactor = spec.depthBiasConstantFactor;
        raster.depthBiasSlopeFactor    = spec.depthBiasSlopeFactor;
        raster.depthBiasClamp          = 0.0f;

        VkPipelineMultisampleStateCreateInfo msaa{};
        msaa.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        msaa.sampleShadingEnable = VK_FALSE;

        VkPipelineDepthStencilStateCreateInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth.depthTestEnable  = spec.depthStencil.depthTest  ? VK_TRUE : VK_FALSE;
        depth.depthWriteEnable = spec.depthStencil.depthWrite ? VK_TRUE : VK_FALSE;
        depth.depthCompareOp = VK_COMPARE_OP_LESS;
        depth.depthBoundsTestEnable = VK_FALSE;
        depth.stencilTestEnable = VK_FALSE;

        std::vector<VkPipelineColorBlendAttachmentState> blend_attachments;
        blend_attachments.resize(std::max<size_t>(1, spec.perColorAttachmentBlend.size()));

        for (size_t i = 0; i < blend_attachments.size(); ++i) {
            const bool enable =
                (i < spec.perColorAttachmentBlend.size())
                    ? spec.perColorAttachmentBlend[i].enabled
                    : false;

            auto& att = blend_attachments[i];
            att.colorWriteMask =
                VK_COLOR_COMPONENT_R_BIT |
                VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT |
                VK_COLOR_COMPONENT_A_BIT;

            att.blendEnable = enable ? VK_TRUE : VK_FALSE;

            if (enable) {
                att.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                att.colorBlendOp        = VK_BLEND_OP_ADD;
                att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                att.alphaBlendOp        = VK_BLEND_OP_ADD;
            } else {
                att.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
                att.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
                att.colorBlendOp        = VK_BLEND_OP_ADD;
                att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
                att.alphaBlendOp        = VK_BLEND_OP_ADD;
            }
        }

        VkPipelineColorBlendStateCreateInfo blend{};
        blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend.logicOpEnable = VK_FALSE;
        blend.attachmentCount = static_cast<uint32_t>(blend_attachments.size());
        blend.pAttachments = blend_attachments.data();

        VkPipelineLayoutCreateInfo layout_ci{};
        layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        VkDescriptorSetLayout set_layouts[] = { global_set_layout, extra_set_layout };
        layout_ci.setLayoutCount = extra_set_layout ? 2u : 1u;
        layout_ci.pSetLayouts = set_layouts;

        // Push constants must cover all stages that read them
        VkPushConstantRange pc_range{};
        pc_range.stageFlags = VK_SHADER_STAGE_TASK_BIT_EXT |
                              VK_SHADER_STAGE_MESH_BIT_EXT |
                              VK_SHADER_STAGE_FRAGMENT_BIT;
        pc_range.offset = 0;
        pc_range.size = k_push_constant_max_size;

        layout_ci.pushConstantRangeCount = 1;
        layout_ci.pPushConstantRanges = &pc_range;

        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkResult layout_res = vkCreatePipelineLayout(device, &layout_ci, nullptr, &layout);
        HN_CORE_ASSERT(layout_res == VK_SUCCESS, "vkCreatePipelineLayout failed (mesh pipeline)");
        m_layout = layout;

        VkGraphicsPipelineCreateInfo pipe{};
        pipe.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipe.stageCount = stage_count;
        pipe.pStages = stages;
        pipe.pVertexInputState   = nullptr;  // no vertex buffer input — mesh shader reads SSBOs directly
        pipe.pInputAssemblyState = nullptr;  // no index/primitive assembly — mesh shader emits geometry
        pipe.pViewportState      = &viewport_state;
        pipe.pRasterizationState = &raster;
        pipe.pMultisampleState   = &msaa;
        pipe.pDepthStencilState  = &depth;
        pipe.pColorBlendState    = &blend;
        pipe.pDynamicState       = &dynamic_state;
        pipe.layout    = reinterpret_cast<VkPipelineLayout>(m_layout);
        pipe.renderPass = render_pass;
        pipe.subpass   = 0;

        VkPipeline pipeline = VK_NULL_HANDLE;
        VkResult pr = vkCreateGraphicsPipelines(device, pipeline_cache, 1, &pipe, nullptr, &pipeline);
        HN_CORE_ASSERT(pr == VK_SUCCESS, "vkCreateGraphicsPipelines failed (mesh pipeline)");
        m_pipeline = pipeline;
    }
}
