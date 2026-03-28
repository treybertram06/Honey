#include "hnpch.h"
#include "vk_cloth_sim.h"

#include "Honey/core/engine.h"
#include "Honey/renderer/renderer.h"

#include <array>
#include <vector>

namespace Honey {
    namespace {
        static bool create_shader_module_from_spv_file(VkDevice device,
                                                       const std::filesystem::path& path,
                                                       VkShaderModule& out_module) {
            out_module = VK_NULL_HANDLE;

            std::ifstream file(path, std::ios::ate | std::ios::binary);
            if (!file.is_open()) {
                HN_CORE_ERROR("VulkanClothSim: failed to open SPIR-V file: {0}", path.string());
                return false;
            }

            const std::streamsize size = file.tellg();
            if (size <= 0 || (size % 4) != 0) {
                HN_CORE_ERROR("VulkanClothSim: invalid SPIR-V file size for '{0}'", path.string());
                return false;
            }

            std::vector<uint32_t> code(static_cast<size_t>(size / 4));
            file.seekg(0);
            if (!file.read(reinterpret_cast<char*>(code.data()), size)) {
                HN_CORE_ERROR("VulkanClothSim: failed reading SPIR-V file: {0}", path.string());
                return false;
            }

            VkShaderModuleCreateInfo module_ci{};
            module_ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            module_ci.codeSize = static_cast<size_t>(size);
            module_ci.pCode = code.data();

            VkShaderModule module = VK_NULL_HANDLE;
            VkResult r = vkCreateShaderModule(device, &module_ci, nullptr, &module);
            if (r != VK_SUCCESS) {
                HN_CORE_ERROR("VulkanClothSim: vkCreateShaderModule failed for '{0}' (VkResult={1})",
                              path.string(),
                              static_cast<int>(r));
                return false;
            }

            out_module = module;
            return true;
        }
    }

    VulkanClothSim::~VulkanClothSim() {
        shutdown();
    }

    bool VulkanClothSim::init(VulkanContext* context, const uint32_t width, const uint32_t height) {
        shutdown();

        HN_CORE_ASSERT(context, "VulkanClothSim::init requires a valid VulkanContext");
        HN_CORE_ASSERT(width > 0 && height > 0, "VulkanClothSim::init requires non-zero cloth dimensions");

        m_context = context;
        m_device = reinterpret_cast<VkDevice>(context->get_device());
        m_physical_device = reinterpret_cast<VkPhysicalDevice>(context->get_physical_device());
        m_width = width;
        m_height = height;
        m_particle_count = width * height;
        m_read_index = 0;
        m_write_index = 1;
        m_external_state_buffers[0] = VK_NULL_HANDLE;
        m_external_state_buffers[1] = VK_NULL_HANDLE;
        m_use_external_state_buffers = false;

        if (!m_device || !m_physical_device) {
            HN_CORE_ERROR("VulkanClothSim::init failed: missing Vulkan device handles");
            shutdown();
            return false;
        }

        if (!create_storage_buffers() || !create_descriptor_resources() || !create_pipelines()) {
            HN_CORE_ERROR("VulkanClothSim::init failed to create one or more GPU resources");
            shutdown();
            return false;
        }

        m_initialized = true;
        return true;
    }

    void VulkanClothSim::shutdown() {
        if (!m_device)
            return;

        vkDeviceWaitIdle(m_device);

        destroy_pipelines();
        destroy_descriptors();
        destroy_buffers();

        m_context = nullptr;
        m_device = VK_NULL_HANDLE;
        m_physical_device = VK_NULL_HANDLE;
        m_width = 0;
        m_height = 0;
        m_particle_count = 0;
        m_read_index = 0;
        m_write_index = 1;
        m_external_state_buffers[0] = VK_NULL_HANDLE;
        m_external_state_buffers[1] = VK_NULL_HANDLE;
        m_use_external_state_buffers = false;
        m_initialized = false;
    }

    bool VulkanClothSim::create_storage_buffers() {
        struct ParticleGpu {
            float pos[4]{};
            float vel[4]{};
        };

        const VkDeviceSize buffer_size = static_cast<VkDeviceSize>(m_particle_count) * sizeof(ParticleGpu);
        HN_CORE_ASSERT(buffer_size > 0, "VulkanClothSim::create_storage_buffers computed zero-sized buffer");

        VkBufferCreateInfo buffer_ci{};
        buffer_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_ci.size = buffer_size;
        buffer_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        buffer_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        for (auto& state_buffer : m_state_buffers) {
            VkResult create_res = vkCreateBuffer(m_device, &buffer_ci, nullptr, &state_buffer.buffer);
            HN_CORE_ASSERT(create_res == VK_SUCCESS, "VulkanClothSim failed creating storage buffer");

            VkMemoryRequirements req{};
            vkGetBufferMemoryRequirements(m_device, state_buffer.buffer, &req);

            VkMemoryAllocateInfo alloc{};
            alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            alloc.allocationSize = req.size;
            alloc.memoryTypeIndex = find_memory_type(m_physical_device,
                                                     req.memoryTypeBits,
                                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

            VkDeviceMemory mem = VK_NULL_HANDLE;
            VkResult alloc_res = vkAllocateMemory(m_device, &alloc, nullptr, &mem);
            HN_CORE_ASSERT(alloc_res == VK_SUCCESS, "VulkanClothSim failed allocating storage buffer memory");
            state_buffer.memory = mem;

            VkResult bind_res = vkBindBufferMemory(m_device, state_buffer.buffer, state_buffer.memory, 0);
            HN_CORE_ASSERT(bind_res == VK_SUCCESS, "VulkanClothSim failed binding storage buffer memory");

            void* mapped = nullptr;
            const VkResult map_res = vkMapMemory(m_device, state_buffer.memory, 0, buffer_size, 0, &mapped);
            HN_CORE_ASSERT(map_res == VK_SUCCESS, "VulkanClothSim failed mapping storage buffer for zero init");
            std::memset(mapped, 0, static_cast<size_t>(buffer_size));
            vkUnmapMemory(m_device, state_buffer.memory);
        }

        return true;
    }

    bool VulkanClothSim::create_descriptor_resources() {
        std::array<VkDescriptorSetLayoutBinding, 2> bindings{};

        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo layout_ci{};
        layout_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_ci.bindingCount = static_cast<uint32_t>(bindings.size());
        layout_ci.pBindings = bindings.data();

        const VkResult layout_res = vkCreateDescriptorSetLayout(m_device, &layout_ci, nullptr, &m_descriptor_set_layout);
        HN_CORE_ASSERT(layout_res == VK_SUCCESS, "VulkanClothSim failed to create descriptor set layout");

        VkDescriptorPoolSize pool_size{};
        pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        pool_size.descriptorCount = 4; // 2 sets x 2 bindings

        VkDescriptorPoolCreateInfo pool_ci{};
        pool_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_ci.maxSets = 2;
        pool_ci.poolSizeCount = 1;
        pool_ci.pPoolSizes = &pool_size;

        const VkResult pool_res = vkCreateDescriptorPool(m_device, &pool_ci, nullptr, &m_descriptor_pool);
        HN_CORE_ASSERT(pool_res == VK_SUCCESS, "VulkanClothSim failed to create descriptor pool");

        std::array<VkDescriptorSetLayout, 2> layouts{ m_descriptor_set_layout, m_descriptor_set_layout };
        VkDescriptorSetAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc.descriptorPool = m_descriptor_pool;
        alloc.descriptorSetCount = static_cast<uint32_t>(layouts.size());
        alloc.pSetLayouts = layouts.data();

        const VkResult alloc_res = vkAllocateDescriptorSets(m_device, &alloc, m_descriptor_sets);
        HN_CORE_ASSERT(alloc_res == VK_SUCCESS, "VulkanClothSim failed to allocate descriptor sets");

        update_descriptor_sets_for_state_buffers();

        return true;
    }

    bool VulkanClothSim::create_pipelines() {
        Ref<ShaderCache> shader_cache = Renderer::get_shader_cache();
        if (!shader_cache) {
            HN_CORE_ERROR("VulkanClothSim: shader cache is unavailable");
            return false;
        }

        decltype(shader_cache->get_or_compile_spirv_paths(std::filesystem::path{})) seed_spirv{};
        decltype(shader_cache->get_or_compile_spirv_paths(std::filesystem::path{})) sim_spirv{};
        try {
            seed_spirv = shader_cache->get_or_compile_spirv_paths(std::filesystem::path(ASSET_ROOT) / "shaders" / "ClothSeed.comp.glsl");
            sim_spirv = shader_cache->get_or_compile_spirv_paths(std::filesystem::path(ASSET_ROOT) / "shaders" / "ClothSim.comp.glsl");
        } catch (const std::exception& e) {
            HN_CORE_ERROR("VulkanClothSim: failed compiling/loading cloth shader SPIR-V: {0}", e.what());
            return false;
        }

        if (!seed_spirv.has_compute()) {
            HN_CORE_ERROR("VulkanClothSim: missing compute SPIR-V for ClothSeed.comp.glsl");
            return false;
        }

        if (!sim_spirv.has_compute()) {
            HN_CORE_ERROR("VulkanClothSim: missing compute SPIR-V for ClothSim.comp.glsl");
            return false;
        }

        VkShaderModule seed_module = VK_NULL_HANDLE;
        VkShaderModule sim_module = VK_NULL_HANDLE;

        if (!create_shader_module_from_spv_file(m_device, seed_spirv.compute, seed_module))
            return false;

        if (!create_shader_module_from_spv_file(m_device, sim_spirv.compute, sim_module)) {
            vkDestroyShaderModule(m_device, seed_module, nullptr);
            return false;
        }

        VkPushConstantRange push_range{};
        push_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        push_range.offset = 0;
        push_range.size = sizeof(ComputePushConstants);

        VkPipelineLayoutCreateInfo layout_ci{};
        layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_ci.setLayoutCount = 1;
        layout_ci.pSetLayouts = &m_descriptor_set_layout;
        layout_ci.pushConstantRangeCount = 1;
        layout_ci.pPushConstantRanges = &push_range;

        const VkResult layout_res = vkCreatePipelineLayout(m_device, &layout_ci, nullptr, &m_pipeline_layout);
        if (layout_res != VK_SUCCESS) {
            HN_CORE_ERROR("VulkanClothSim failed to create compute pipeline layout (VkResult={0})",
                          static_cast<int>(layout_res));
            vkDestroyShaderModule(m_device, seed_module, nullptr);
            vkDestroyShaderModule(m_device, sim_module, nullptr);
            return false;
        }

        auto create_compute_pipeline = [&](const VkShaderModule module, VkPipeline& out_pipeline) -> bool {
            VkPipelineShaderStageCreateInfo stage{};
            stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            stage.module = module;
            stage.pName = "main";

            VkComputePipelineCreateInfo pipeline_ci{};
            pipeline_ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            pipeline_ci.layout = m_pipeline_layout;
            pipeline_ci.stage = stage;

            const VkResult pipeline_res = vkCreateComputePipelines(m_device,
                                                                   VK_NULL_HANDLE,
                                                                   1,
                                                                   &pipeline_ci,
                                                                   nullptr,
                                                                   &out_pipeline);
            if (pipeline_res != VK_SUCCESS) {
                HN_CORE_ERROR("VulkanClothSim failed to create compute pipeline (VkResult={0})",
                              static_cast<int>(pipeline_res));
                out_pipeline = VK_NULL_HANDLE;
                return false;
            }

            return true;
        };

        const bool seed_ok = create_compute_pipeline(seed_module, m_seed_pipeline);
        const bool sim_ok = create_compute_pipeline(sim_module, m_sim_pipeline);

        vkDestroyShaderModule(m_device, seed_module, nullptr);
        vkDestroyShaderModule(m_device, sim_module, nullptr);

        if (!seed_ok || !sim_ok) {
            destroy_pipelines();
            return false;
        }

        return true;
    }

    void VulkanClothSim::destroy_pipelines() {
        if (m_seed_pipeline) {
            vkDestroyPipeline(m_device, m_seed_pipeline, nullptr);
            m_seed_pipeline = VK_NULL_HANDLE;
        }
        if (m_sim_pipeline) {
            vkDestroyPipeline(m_device, m_sim_pipeline, nullptr);
            m_sim_pipeline = VK_NULL_HANDLE;
        }
        if (m_pipeline_layout) {
            vkDestroyPipelineLayout(m_device, m_pipeline_layout, nullptr);
            m_pipeline_layout = VK_NULL_HANDLE;
        }
    }

    void VulkanClothSim::destroy_descriptors() {
        if (m_descriptor_pool) {
            vkDestroyDescriptorPool(m_device, m_descriptor_pool, nullptr);
            m_descriptor_pool = VK_NULL_HANDLE;
        }
        if (m_descriptor_set_layout) {
            vkDestroyDescriptorSetLayout(m_device, m_descriptor_set_layout, nullptr);
            m_descriptor_set_layout = VK_NULL_HANDLE;
        }

        m_descriptor_sets[0] = VK_NULL_HANDLE;
        m_descriptor_sets[1] = VK_NULL_HANDLE;
    }

    void VulkanClothSim::destroy_buffers() {
        for (auto& state_buffer : m_state_buffers) {
            if (state_buffer.buffer) {
                vkDestroyBuffer(m_device, state_buffer.buffer, nullptr);
                state_buffer.buffer = VK_NULL_HANDLE;
            }
            if (state_buffer.memory) {
                vkFreeMemory(m_device, state_buffer.memory, nullptr);
                state_buffer.memory = VK_NULL_HANDLE;
            }
        }
    }

    void VulkanClothSim::set_external_state_buffers(const VkBuffer buffer_a, const VkBuffer buffer_b) {
        const bool use_external = (buffer_a != VK_NULL_HANDLE && buffer_b != VK_NULL_HANDLE);

        m_external_state_buffers[0] = buffer_a;
        m_external_state_buffers[1] = buffer_b;
        m_use_external_state_buffers = use_external;

        if ((buffer_a == VK_NULL_HANDLE) != (buffer_b == VK_NULL_HANDLE)) {
            HN_CORE_WARN("VulkanClothSim::set_external_state_buffers received a partial buffer set; reverting to internal buffers");
        }

        if (m_initialized)
            update_descriptor_sets_for_state_buffers();
    }

    VkBuffer VulkanClothSim::state_buffer_handle(const uint32_t index) const {
        HN_CORE_ASSERT(index < 2u, "VulkanClothSim::state_buffer_handle index out of range");
        return m_use_external_state_buffers
            ? m_external_state_buffers[index]
            : m_state_buffers[index].buffer;
    }

    void VulkanClothSim::update_descriptor_sets_for_state_buffers() {
        if (!m_device || !m_descriptor_sets[0] || !m_descriptor_sets[1])
            return;

        const VkDeviceSize buffer_size = static_cast<VkDeviceSize>(m_particle_count) * sizeof(float) * 8;

        // Set 0: read A, write B
        // Set 1: read B, write A
        for (uint32_t set_index = 0; set_index < 2; ++set_index) {
            const uint32_t read = (set_index == 0) ? 0u : 1u;
            const uint32_t write = (set_index == 0) ? 1u : 0u;

            const VkBuffer read_buffer = state_buffer_handle(read);
            const VkBuffer write_buffer = state_buffer_handle(write);

            HN_CORE_ASSERT(read_buffer != VK_NULL_HANDLE && write_buffer != VK_NULL_HANDLE,
                           "VulkanClothSim::update_descriptor_sets_for_state_buffers requires valid state buffers");

            VkDescriptorBufferInfo read_info{};
            read_info.buffer = read_buffer;
            read_info.offset = 0;
            read_info.range = buffer_size;

            VkDescriptorBufferInfo write_info{};
            write_info.buffer = write_buffer;
            write_info.offset = 0;
            write_info.range = buffer_size;

            std::array<VkWriteDescriptorSet, 2> writes{};

            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = m_descriptor_sets[set_index];
            writes[0].dstBinding = 0;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[0].descriptorCount = 1;
            writes[0].pBufferInfo = &read_info;

            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet = m_descriptor_sets[set_index];
            writes[1].dstBinding = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[1].descriptorCount = 1;
            writes[1].pBufferInfo = &write_info;

            vkUpdateDescriptorSets(m_device,
                                   static_cast<uint32_t>(writes.size()),
                                   writes.data(),
                                   0,
                                   nullptr);
        }
    }

    void VulkanClothSim::record_seed(VkCommandBuffer cmd) const {
        HN_CORE_ASSERT(m_initialized, "VulkanClothSim::record_seed called before init");
        HN_CORE_ASSERT(cmd, "VulkanClothSim::record_seed requires a valid command buffer");

        const uint32_t groups_x = (m_width + 15u) / 16u;
        const uint32_t groups_y = (m_height + 15u) / 16u;

        ComputePushConstants pc{};
        pc.dt     = 0.0f;
        pc.width  = m_width;
        pc.height = m_height;
        pc.phase  = 0;

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_seed_pipeline);

        const uint32_t set_index = active_set_index();
        const VkDescriptorSet set = m_descriptor_sets[set_index];
        vkCmdBindDescriptorSets(cmd,
                                VK_PIPELINE_BIND_POINT_COMPUTE,
                                m_pipeline_layout,
                                0,
                                1,
                                &set,
                                0,
                                nullptr);

        vkCmdPushConstants(cmd,
                           m_pipeline_layout,
                           VK_SHADER_STAGE_COMPUTE_BIT,
                           0,
                           sizeof(ComputePushConstants),
                           &pc);

        vkCmdDispatch(cmd, groups_x, groups_y, 1);
    }

    void VulkanClothSim::record_sim(VkCommandBuffer cmd, const float dt, const uint32_t /*frame_index*/, const uint32_t substeps) {
        HN_CORE_ASSERT(m_initialized, "VulkanClothSim::record_sim called before init");
        HN_CORE_ASSERT(cmd, "VulkanClothSim::record_sim requires a valid command buffer");
        HN_CORE_ASSERT(substeps > 0, "VulkanClothSim::record_sim substeps must be at least 1");

        const uint32_t groups_x = (m_width  + 15u) / 16u;
        const uint32_t groups_y = (m_height + 15u) / 16u;
        const float    sub_dt   = dt / static_cast<float>(substeps);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_sim_pipeline);

        // Red-Black Gauss-Seidel: each substep is split into two phases.
        //   Phase 0: update particles where (x+y)%2==0 ("black"), copy the rest.
        //   Phase 1: update particles where (x+y)%2==1 ("red") — using the
        //            now-updated black positions from phase 0 (Gauss-Seidel order).
        // Each phase requires its own barrier+swap so the next phase reads the
        // freshly written buffer.  After the final phase of the final substep,
        // the caller's swap_ping_pong() advances the read buffer for rendering.
        for (uint32_t s = 0; s < substeps; ++s) {
            for (uint32_t phase = 0; phase < 2u; ++phase) {
                const VkDescriptorSet set = m_descriptor_sets[active_set_index()];
                vkCmdBindDescriptorSets(cmd,
                                        VK_PIPELINE_BIND_POINT_COMPUTE,
                                        m_pipeline_layout,
                                        0, 1, &set,
                                        0, nullptr);

                ComputePushConstants pc{};
                pc.dt     = sub_dt;
                pc.width  = m_width;
                pc.height = m_height;
                pc.phase  = phase;
                vkCmdPushConstants(cmd,
                                   m_pipeline_layout,
                                   VK_SHADER_STAGE_COMPUTE_BIT,
                                   0,
                                   sizeof(ComputePushConstants),
                                   &pc);

                vkCmdDispatch(cmd, groups_x, groups_y, 1);

                // Barrier + swap after phase 0 (so phase 1 reads updated black),
                // and after phase 1 of every substep except the last (so the next
                // substep reads the fully-updated buffer).
                const bool need_barrier = (phase == 0u) || (s < substeps - 1u);
                if (need_barrier) {
                    VkBuffer written = state_buffer_handle(m_write_index);
                    std::swap(m_read_index, m_write_index);

                    VkBufferMemoryBarrier barrier{};
                    barrier.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                    barrier.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
                    barrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
                    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.buffer              = written;
                    barrier.offset              = 0;
                    barrier.size                = VK_WHOLE_SIZE;

                    vkCmdPipelineBarrier(cmd,
                                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                         0,
                                         0, nullptr,
                                         1, &barrier,
                                         0, nullptr);
                }
            }
        }
    }

    void VulkanClothSim::reset_ping_pong() {
        m_read_index = 0;
        m_write_index = 1;
    }

    void VulkanClothSim::swap_ping_pong() {
        std::swap(m_read_index, m_write_index);
    }

    VkBuffer VulkanClothSim::current_read_buffer() const {
        return state_buffer_handle(m_read_index);
    }

    VkBuffer VulkanClothSim::current_write_buffer() const {
        return state_buffer_handle(m_write_index);
    }

    uint32_t VulkanClothSim::active_set_index() const {
        if (m_read_index == 0 && m_write_index == 1)
            return 0;
        if (m_read_index == 1 && m_write_index == 0)
            return 1;

        HN_CORE_ASSERT(false, "VulkanClothSim ping-pong state is invalid");
        return 0;
    }

    uint32_t VulkanClothSim::find_memory_type(VkPhysicalDevice phys,
                                              const uint32_t type_filter,
                                              const VkMemoryPropertyFlags properties) {
        VkPhysicalDeviceMemoryProperties mem_props{};
        vkGetPhysicalDeviceMemoryProperties(phys, &mem_props);

        for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
            if ((type_filter & (1u << i)) &&
                ((mem_props.memoryTypes[i].propertyFlags & properties) == properties)) {
                return i;
            }
        }

        HN_CORE_ASSERT(false, "VulkanClothSim failed to find suitable memory type");
        return 0;
    }
}
