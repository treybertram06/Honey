#include "hnpch.h"
#include "vk_gpu_profiler.h"

#include "vk_backend.h"

namespace Honey {

GpuProfiler* GpuProfiler::s_active = nullptr;

// --- Static interface ---

void GpuProfiler::set_active(GpuProfiler* p) {
    s_active = p;
}

uint32_t GpuProfiler::alloc_slot(const char* name) {
    if (!s_active) {
        HN_CORE_WARN("GpuProfiler::alloc_slot called before profiler is active (zone: {})", name);
        return k_invalid_slot;
    }
    uint32_t slot = s_active->m_slot_count.fetch_add(1, std::memory_order_relaxed);
    if (slot >= k_max_zones) {
        HN_CORE_WARN("GpuProfiler: exceeded k_max_zones ({}), zone '{}' not profiled", k_max_zones, name);
        s_active->m_slot_count.store(k_max_zones, std::memory_order_relaxed); // clamp
        return k_invalid_slot;
    }
    s_active->m_slot_names[slot] = name;
    return slot;
}

// --- Lifecycle ---

void GpuProfiler::init(VkDevice device, VkPhysicalDevice phys_device, uint32_t image_count) {
    m_image_count = image_count;

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(phys_device, &props);
    m_timestamp_period_ns = static_cast<double>(props.limits.timestampPeriod);

    const uint32_t query_count = image_count * k_max_zones * 2; // 2 per zone (begin + end)
    VkQueryPoolCreateInfo qp_ci{};
    qp_ci.sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    qp_ci.queryType  = VK_QUERY_TYPE_TIMESTAMP;
    qp_ci.queryCount = query_count;

    VkResult r = vkCreateQueryPool(device, &qp_ci, nullptr, &m_query_pool);
    HN_CORE_ASSERT(r == VK_SUCCESS, "GpuProfiler: vkCreateQueryPool failed");

    const size_t flat_size = image_count * k_max_zones;
    m_written.assign(flat_size, false);
    m_valid.assign(flat_size, false);
    m_times_ms.assign(flat_size, 0.0);
}

void GpuProfiler::destroy(VkDevice device) {
    if (m_query_pool) {
        vkDestroyQueryPool(device, m_query_pool, nullptr);
        m_query_pool = VK_NULL_HANDLE;
    }
}

// --- Per-frame hooks ---

void GpuProfiler::reset_frame(VkCommandBuffer cmd) {
    if (!m_query_pool) return;
    const uint32_t base  = m_current_image_index * k_max_zones * 2;
    const uint32_t count = k_max_zones * 2;
    vkCmdResetQueryPool(cmd, m_query_pool, base, count);

    // Clear written flags for this image so stale zones don't get read back next frame.
    const size_t img_base = m_current_image_index * k_max_zones;
    std::fill(m_written.begin() + img_base,
              m_written.begin() + img_base + k_max_zones, false);
}

void GpuProfiler::readback(VkDevice device, uint32_t image_index) {
    if (!m_query_pool) return;

    const uint32_t slot_count = m_slot_count.load(std::memory_order_relaxed);
    const size_t   img_base   = image_index * k_max_zones;

    for (uint32_t slot = 0; slot < slot_count; ++slot) {
        if (!m_written[img_base + slot]) continue; // not written this frame — skip

        const uint32_t query_base = image_index * k_max_zones * 2 + slot * 2;
        uint64_t ts[2] = {};

        VkResult qr = vkGetQueryPoolResults(
            device, m_query_pool,
            query_base, 2,
            sizeof(ts), ts, sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT
        );

        if (qr == VK_SUCCESS) {
            const double delta_ms = (static_cast<double>(ts[1]) - static_cast<double>(ts[0]))
                                    * m_timestamp_period_ns / 1.0e6;
            if (delta_ms >= 0.0) {
                m_times_ms[img_base + slot] = delta_ms;
                m_valid   [img_base + slot] = true;
            }
        } else {
            m_valid[img_base + slot] = false;
            if (qr != VK_NOT_READY)
                HN_CORE_WARN("GpuProfiler readback failed for slot '{0}': {1}", m_slot_names[slot], VulkanBackend::vk_result_to_string(qr));
        }
    }
}

// --- Timestamp writes ---

void GpuProfiler::write_begin(VkCommandBuffer cmd, uint32_t slot, VkPipelineStageFlags flags) {
    if (!m_query_pool || slot >= k_max_zones) return;
    const uint32_t query_idx = m_current_image_index * k_max_zones * 2 + slot * 2;
    vkCmdWriteTimestamp(cmd, static_cast<VkPipelineStageFlagBits>(flags), m_query_pool, query_idx);
}

void GpuProfiler::write_end(VkCommandBuffer cmd, uint32_t slot, VkPipelineStageFlags flags) {
    if (!m_query_pool || slot >= k_max_zones) return;
    const uint32_t query_idx = m_current_image_index * k_max_zones * 2 + slot * 2 + 1;
    vkCmdWriteTimestamp(cmd, static_cast<VkPipelineStageFlagBits>(flags), m_query_pool, query_idx);
    m_written[m_current_image_index * k_max_zones + slot] = true;
}

// --- Results ---

const char* GpuProfiler::get_slot_name(uint32_t slot) const {
    if (slot >= m_slot_count.load()) return "<unknown>";
    return m_slot_names[slot] ? m_slot_names[slot] : "<unnamed>";
}

double GpuProfiler::get_slot_time_ms(uint32_t slot) const {
    // Average across all swapchain images that have a valid result for this slot.
    // This smooths the one-frame-behind readback latency.
    double   sum = 0.0;
    uint32_t cnt = 0;
    for (uint32_t img = 0; img < m_image_count; ++img) {
        const size_t idx = img * k_max_zones + slot;
        if (idx < m_valid.size() && m_valid[idx]) {
            sum += m_times_ms[idx];
            ++cnt;
        }
    }
    return (cnt > 0) ? (sum / cnt) : 0.0;
}

// --- GpuProfileScope ---

GpuProfileScope::GpuProfileScope(VkCommandBuffer cmd, uint32_t slot, VkPipelineStageFlags begin_stage)
    : m_cmd(cmd), m_slot(slot)
{
    if (GpuProfiler::s_active && slot != GpuProfiler::k_invalid_slot)
        GpuProfiler::s_active->write_begin(cmd, slot, begin_stage);
}

GpuProfileScope::~GpuProfileScope() {
    if (GpuProfiler::s_active && m_slot != GpuProfiler::k_invalid_slot)
        GpuProfiler::s_active->write_end(m_cmd, m_slot, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
}

} // namespace Honey