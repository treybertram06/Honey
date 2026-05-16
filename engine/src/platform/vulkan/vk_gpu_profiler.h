#pragma once
#include "Honey/core/base.h"
#include <vulkan/vulkan.h>
#include <atomic>
#include <vector>

namespace Honey {

class GpuProfiler {
public:
    static constexpr uint32_t k_max_zones    = 64;
    static constexpr uint32_t k_invalid_slot = UINT32_MAX;

    void init(VkDevice device, VkPhysicalDevice phys_device, uint32_t image_count);
    void destroy(VkDevice device);

    void readback(VkDevice device, uint32_t image_index);
    void set_current_image_index(uint32_t idx) { m_current_image_index = idx; }
    void reset_frame(VkCommandBuffer cmd);

    // Called by the RAII scope - do not call directly.
    void write_begin(VkCommandBuffer cmd, uint32_t slot, VkPipelineStageFlags flags);
    void write_end  (VkCommandBuffer cmd, uint32_t slot, VkPipelineStageFlags flags);

    // Static interface used by alloc_slot and GpuProfileScope.
    static uint32_t alloc_slot(const char* name); // thread-safe, first-call-wins
    static void     set_active(GpuProfiler* p);   // called by VulkanContext on init/destroy

    // Results - safe to call from render or main thread after readback.
    uint32_t    get_slot_count()               const { return m_slot_count.load(); }
    const char* get_slot_name(uint32_t slot)   const;
    double      get_slot_time_ms(uint32_t slot) const;

private:
    VkQueryPool  m_query_pool          = VK_NULL_HANDLE;
    double       m_timestamp_period_ns = 1.0;
    uint32_t     m_image_count         = 0;
    uint32_t     m_current_image_index = 0;

    std::atomic<uint32_t> m_slot_count{0};
    const char*  m_slot_names[k_max_zones]{};

    // Flat layout: index = image_index * k_max_zones + slot
    std::vector<bool>   m_written;   // set in write_end; cleared in reset_frame
    std::vector<bool>   m_valid;     // set in readback when query succeeded
    std::vector<double> m_times_ms;  // set in readback

protected:
    static GpuProfiler* s_active;

    friend class GpuProfileScope;
};

// RAII scope — writes begin timestamp on construction, end on destruction.
class GpuProfileScope {
public:
    GpuProfileScope(VkCommandBuffer cmd, uint32_t slot,
                    VkPipelineStageFlags begin_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    ~GpuProfileScope();

    GpuProfileScope(const GpuProfileScope&) = delete;
    GpuProfileScope& operator=(const GpuProfileScope&) = delete;

private:
    VkCommandBuffer m_cmd;
    uint32_t        m_slot;
};

}

// HN_GPU_SCOPE(cmd, "Zone Name")
// Allocates a static slot on the first call (zero cost on subsequent calls),
// then writes begin/end timestamps around the enclosing scope.
// Works correctly inside lambdas — the static local is per-callsite, not per-invocation.
#define HN_GPU_SCOPE(cmd, name)                                                \
    static const uint32_t HN_CONCAT(s_gpu_slot_, __LINE__) =                  \
        ::Honey::GpuProfiler::alloc_slot(name);                                \
    ::Honey::GpuProfileScope HN_CONCAT(gpu_scope_, __LINE__)(                  \
        cmd, HN_CONCAT(s_gpu_slot_, __LINE__))