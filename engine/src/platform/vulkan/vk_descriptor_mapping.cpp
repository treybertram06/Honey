#include "hnpch.h"
#include "vk_descriptor_mapping.h"
#include "Honey/renderer/gpu_types.h"
#include "Honey/core/settings.h"

#include <algorithm>
#include <cctype>
#include <cstddef>

namespace Honey {

    std::vector<uint32_t> compute_block_offsets(const ReflectedShader& refl,
                                                const VulkanDescriptorHeap& heap) {
        std::vector<uint32_t> offsets(refl.bindings.size(), 0);
        uint32_t cursor = 0;
        uint32_t next_binding = 0;
        bool have_set = false;
        uint32_t cur_set = 0;
        for (size_t i = 0; i < refl.bindings.size(); ++i) {
            const ReflectedBinding& b = refl.bindings[i];
            if (b.set == 0) continue;                          // persistent region, mapped CONSTANT_OFFSET
            if (b.type == VK_DESCRIPTOR_TYPE_SAMPLER) continue; // lives in the sampler heap

            if (!have_set || b.set != cur_set) {
                // New set: reset the cursor and the binding-gap tracker. C++-side writers index
                // persistent blocks by raw SPIR-V binding number (see allocate_meshlet_heap_blocks),
                // so any binding number not referenced by this shader program (e.g. a buffer only
                // read by other stages/pipelines sharing the same block layout) still occupies a
                // slot in the block and must be accounted for here, not silently compacted away.
                cursor = 0;
                next_binding = 0;
                cur_set = b.set;
                have_set = true;
            }

            const uint32_t align  = heap.descriptor_alignment(b.type);
            const uint32_t stride = heap.descriptor_stride(b.type);

            // Reserve slots for any skipped binding numbers, assuming they share this binding's
            // stride (holds for the homogeneous-type persistent blocks this path is used for).
            while (next_binding < b.binding) {
                cursor = (cursor + align - 1) & ~(align - 1);
                cursor += stride;
                ++next_binding;
            }

            cursor = (cursor + align - 1) & ~(align - 1);
            offsets[i] = cursor;
            const uint32_t count = b.count == 0 ? 1u : b.count;
            cursor += count * stride;
            next_binding = b.binding + 1;
        }
        return offsets;
    }

    VulkanDescriptorHeap::StaticSampler pick_static_sampler(const ReflectedBinding& b) {
        using SS = VulkanDescriptorHeap::StaticSampler;
        if (b.is_comparison_sampler) return SS::ShadowCmp;
        std::string n = b.name;
        std::transform(n.begin(), n.end(), n.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (n.find("shadow") != std::string::npos || n.find("cmp") != std::string::npos) return SS::ShadowCmp;
        if (n.find("nearest") != std::string::npos) return SS::Nearest;
        if (n.find("aniso")   != std::string::npos) return SS::Anisotropic;

        // General-purpose samplers (e.g. u_Sampler in the material shaders) aren't named after a
        // specific filter mode — honor the user's texture-filter setting instead of silently
        // defaulting to Linear, which previously dropped anisotropic filtering entirely for every
        // bindless-sampled material texture regardless of the Renderer Settings panel's selection.
        using TextureFilter = Honey::RendererSettings::TextureFilter;
        switch (Settings::get().renderer.texture_filter) {
            case TextureFilter::nearest:     return SS::Nearest;
            case TextureFilter::anisotropic: return SS::Anisotropic;
            case TextureFilter::linear:
            default:                         return SS::Linear;
        }
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
                const VulkanDescriptorHeap::StaticSampler s = pick_static_sampler(b);
                // Static samplers use an EMBEDDED (immutable) sampler baked into the pipeline rather
                // than the samplerHeapOffset path: on this driver a separate sampler's heap offset
                // does not take effect (every sampler resolved to the driver-reserved range, so
                // REPEAT addressing was silently lost and tiling textures squished into a band --
                // the same failure that forced the manual-PCF shadow workaround in commit 3b58d67).
                // pEmbeddedSampler is the extension's purpose-built path for static samplers and
                // sidesteps the heap entirely. It is only legal when the mapped resource is NOT an
                // array (Vulkan VUID), so scalar samplers (count <= 1) take the embedded path while
                // the rare array sampler falls back to the samplerHeapOffset path.
                m.source = VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_CONSTANT_OFFSET_EXT;
                if (b.count <= 1) {
                    m.sourceData.constantOffset.pEmbeddedSampler = heap.static_sampler_ci(s);
                } else {
                    m.sourceData.constantOffset.samplerHeapOffset      = heap.static_sampler_byte_offset(s);
                    m.sourceData.constantOffset.samplerHeapArrayStride = heap.sampler_descriptor_stride();
                }
            } else if (b.set == 0 && (b.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
                b.type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)) {
                HN_CORE_ASSERT(heap.has_global_binding(b.binding),
                    "Shader declares set-0 binding {0} but no global slot was registered for it. "
                    "Add a row to k_global_bindings.", b.binding);
                // set-0 engine globals live at a fixed persistent slot: CONSTANT_OFFSET, resource half.
                m.source = VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_CONSTANT_OFFSET_EXT;
                m.sourceData.constantOffset.heapOffset      = heap.global_binding_offset(b.binding);
                m.sourceData.constantOffset.heapArrayStride = 0;
            } else if (b.set == 0 && b.type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE && b.count == 0) {
                m.source = VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_CONSTANT_OFFSET_EXT;
                m.sourceData.constantOffset.heapOffset      = heap.bindless_table_byte_offset();
                m.sourceData.constantOffset.heapArrayStride = heap.descriptor_stride(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
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