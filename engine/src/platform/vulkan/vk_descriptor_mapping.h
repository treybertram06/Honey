#pragma once

#include <vector>
#include <cstdint>

#include <vulkan/vulkan_core.h>

#include "vk_descriptor_heap.h"
#include "Honey/renderer/pipeline_spec.h"

namespace Honey {

    // Per-binding byte offset within a pass's transient descriptor block.
    // THE single source of truth used by both heap-mode pipeline creation
    // (vk_pipeline.cpp) and the runtime descriptor writer (frame_graph.cpp).
    // Skips set==0 (CONSTANT_OFFSET persistent region) and VK_DESCRIPTOR_TYPE_SAMPLER
    // (lives in the sampler heap); those entries stay 0.
    std::vector<uint32_t> compute_block_offsets(const ReflectedShader& refl,
                                                const VulkanDescriptorHeap& heap);

    // Maps a reflected sampler binding to a baked static sampler by name/comparison hint.
    VulkanDescriptorHeap::StaticSampler pick_static_sampler(const ReflectedBinding& b);

    // Build one mapping entry per reflected binding for a heap-mode pipeline.
    // Result must outlive the vkCreate*Pipelines call.
    std::vector<VkDescriptorSetAndBindingMappingEXT>
    build_descriptor_mappings(const ReflectedShader& refl, const VulkanDescriptorHeap& heap);

} // namespace Honey