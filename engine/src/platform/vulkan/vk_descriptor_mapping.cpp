#include "hnpch.h"
#include "vk_descriptor_mapping.h"
#include "Honey/renderer/gpu_types.h"

#include <algorithm>
#include <cctype>
#include <cstddef>

namespace Honey {

    std::vector<uint32_t> compute_block_offsets(const ReflectedShader& refl,
                                                const VulkanDescriptorHeap& heap) {
        std::vector<uint32_t> offsets(refl.bindings.size(), 0);
        uint32_t cursor = 0;
        for (size_t i = 0; i < refl.bindings.size(); ++i) {
            const ReflectedBinding& b = refl.bindings[i];
            if (b.set == 0) continue;                          // persistent region, mapped CONSTANT_OFFSET
            if (b.type == VK_DESCRIPTOR_TYPE_SAMPLER) continue; // lives in the sampler heap
            const uint32_t align  = heap.descriptor_alignment(b.type);
            const uint32_t stride = heap.descriptor_stride(b.type);
            cursor = (cursor + align - 1) & ~(align - 1);
            offsets[i] = cursor;
            const uint32_t count = b.count == 0 ? 1u : b.count;
            cursor += count * stride;
        }
        return offsets;
    }

    VulkanDescriptorHeap::StaticSampler pick_static_sampler(const ReflectedBinding& b) {
        using SS = VulkanDescriptorHeap::StaticSampler;
        if (b.is_comparison_sampler) return SS::ShadowCmp;
        std::string n = b.name;
        std::transform(n.begin(), n.end(), n.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (n.find("nearest") != std::string::npos) return SS::Nearest;
        if (n.find("aniso")   != std::string::npos) return SS::Anisotropic;
        return SS::Linear;
    }

    std::vector<VkDescriptorSetAndBindingMappingEXT>
    build_descriptor_mappings(const ReflectedShader& refl, const VulkanDescriptorHeap& heap) {
        std::vector<VkDescriptorSetAndBindingMappingEXT> mappings;
        mappings.reserve(refl.bindings.size());

        const std::vector<uint32_t> block_offsets = compute_block_offsets(refl, heap);

        for (size_t i = 0; i < refl.bindings.size(); ++i) {
            const ReflectedBinding& b = refl.bindings[i];

            VkDescriptorSetAndBindingMappingEXT m{};
            m.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_AND_BINDING_MAPPING_EXT;
            m.descriptorSet = b.set;
            m.firstBinding  = b.binding;
            m.bindingCount  = 1;
            m.resourceMask  = VK_SPIRV_RESOURCE_TYPE_ALL_EXT;

            if (b.type == VK_DESCRIPTOR_TYPE_SAMPLER) {
                // Static sampler baked into the sampler heap (Step 3): CONSTANT_OFFSET, sampler half only.
                const VulkanDescriptorHeap::StaticSampler s = pick_static_sampler(b);
                m.source = VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_CONSTANT_OFFSET_EXT;
                m.sourceData.constantOffset.samplerHeapOffset      = heap.static_sampler_byte_offset(s);
                m.sourceData.constantOffset.samplerHeapArrayStride = heap.sampler_descriptor_stride();
            } else if (b.set == 0 && b.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
                // set-0 engine globals live at a fixed persistent slot: CONSTANT_OFFSET, resource half.
                m.source = VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_CONSTANT_OFFSET_EXT;
                m.sourceData.constantOffset.heapOffset      = heap.global_ubo_offset();
                m.sourceData.constantOffset.heapArrayStride = 0;
            } else {
                // Transient per-pass descriptor: PUSH_INDEX. The pushed resource_heap_base is the block
                // base; heapIndexStride = 1 makes the pushed value a raw byte offset, so the per-binding
                // difference lives entirely in heapOffset.
                m.source = VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_PUSH_INDEX_EXT;
                m.sourceData.pushIndex.heapOffset      = block_offsets[i];
                m.sourceData.pushIndex.pushOffset      = (uint32_t)offsetof(PassPushData, resource_heap_base);
                m.sourceData.pushIndex.heapIndexStride = 1;
                m.sourceData.pushIndex.heapArrayStride = heap.descriptor_stride(b.type);
            }

            mappings.push_back(m);
        }
        return mappings;
    }

} // namespace Honey