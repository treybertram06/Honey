#pragma once

#include <cstdint>
#include <vector>

#include <vulkan/vulkan_core.h>

#include "frame_graph.h"

namespace Honey {

    // One transient descriptor to write for a heap-mode pass. Built once from the bound
    // pipeline's reflection cross-referenced against the pass's declared read bindings, then
    // cached on FGCompiledPass::descriptor_plan and re-walked each frame to do the writes.
    struct PassDescriptorPlanEntry {
        uint32_t         set          = 0;
        uint32_t         binding      = 0;
        VkDescriptorType type         = VK_DESCRIPTOR_TYPE_MAX_ENUM;
        uint32_t         count        = 1;
        uint32_t         block_offset = 0;                 // byte offset within the pass block (compute_block_offsets)
        FGResourceHandle resource     = k_invalid_resource; // resolved FG resource (framebuffer / buffer)
        FGViewKind       view_kind    = FGViewKind::Color2D;
        uint32_t         attachment   = 0;                 // color attachment index (Color2D)
        bool             is_buffer    = false;             // reserved for buffer resources (deferred)
    };

    struct PassDescriptorPlan {
        bool built               = false;
        bool diagnostics_emitted = false;
        std::vector<PassDescriptorPlanEntry> entries;       // non-set0, non-SAMPLER bindings only
        uint32_t resource_block_total_bytes = 0;            // size to allocate for the transient block
    };

} // namespace Honey