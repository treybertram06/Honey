#include "hnpch.h"
#include "vk_context.h"
#include "vk_framebuffer.h"

#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <vector>

#define GLM_ENABLE_EXPERIMENTAL
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include "imgui.h"
#include "vk_buffer.h"
#include "vk_texture.h"
#include "glm/gtx/string_cast.hpp"
#include "Honey/core/settings.h"
#include "Honey/renderer/shader_cache.h"
#include "Honey/renderer/texture_cache.h"
#include "platform/vulkan/vk_renderer_api.h"

namespace Honey {
    static const char* vk_result_to_string(VkResult res) {
        switch (res) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_EVENT_SET: return "VK_EVENT_SET";
        case VK_EVENT_RESET: return "VK_EVENT_RESET";
        case VK_INCOMPLETE: return "VK_INCOMPLETE";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
        case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
        default: return "VK_ERROR_<unknown>";
        }
    }

    struct QueueFamilyIndices {
        std::optional<uint32_t> graphics;
        std::optional<uint32_t> present;

        bool complete() const { return graphics.has_value() && present.has_value(); }
    };

    struct SwapchainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities{};
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> present_modes;
    };

    static QueueFamilyIndices find_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface) {
        QueueFamilyIndices indices;

        uint32_t queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, nullptr);
        std::vector<VkQueueFamilyProperties> families(queue_family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, families.data());

        for (uint32_t i = 0; i < queue_family_count; i++) {
            if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                indices.graphics = i;
            }

            VkBool32 present_support = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present_support);
            if (present_support) {
                indices.present = i;
            }

            if (indices.complete())
                break;
        }

        return indices;
    }

    static SwapchainSupportDetails query_swapchain_support(VkPhysicalDevice device, VkSurfaceKHR surface) {
        SwapchainSupportDetails details;

        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

        uint32_t format_count = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, nullptr);
        if (format_count > 0) {
            details.formats.resize(format_count);
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, details.formats.data());
        }

        uint32_t present_mode_count = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &present_mode_count, nullptr);
        if (present_mode_count > 0) {
            details.present_modes.resize(present_mode_count);
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &present_mode_count, details.present_modes.data());
        }

        return details;
    }

    VkSurfaceFormatKHR choose_surface_format(const std::vector<VkSurfaceFormatKHR>& formats) {
        // Prefer a linear UNORM format to avoid double-gamma / washed-out colors.
        for (const auto& f : formats) {
            if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
                f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return f;
                }
        }

        for (const auto& f : formats) {
            if (f.format == VK_FORMAT_R8G8B8A8_UNORM &&
                f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return f;
                }
        }

        // Fallback: just return the first available format.
        return formats[0];
    }

    static VkPresentModeKHR choose_present_mode(const std::vector<VkPresentModeKHR>& modes) {
        const auto& renderer = Settings::get().renderer;

        // If vsync is ON: we want FIFO (always supported, tear-free).
        if (renderer.vsync) {
            for (auto m : modes) {
                if (m == VK_PRESENT_MODE_FIFO_KHR)
                    return m;
            }
            // FIFO is guaranteed by the spec, but keep a fallback just in case.
            return VK_PRESENT_MODE_FIFO_KHR;
        }

        // If vsync is OFF: try MAILBOX (low-latency, tear-friendly) then IMMEDIATE.
        for (auto m : modes) {
            if (m == VK_PRESENT_MODE_MAILBOX_KHR)
                return m;
        }
        for (auto m : modes) {
            if (m == VK_PRESENT_MODE_IMMEDIATE_KHR)
                return m;
        }

        // As a last resort, fall back to FIFO (vsync on).
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    static VkExtent2D choose_extent(GLFWwindow* window, const VkSurfaceCapabilitiesKHR& caps) {
        if (caps.currentExtent.width != UINT32_MAX) {
            return caps.currentExtent;
        }

        int w = 0, h = 0;
        glfwGetFramebufferSize(window, &w, &h);

        VkExtent2D extent{};
        extent.width = static_cast<uint32_t>(w);
        extent.height = static_cast<uint32_t>(h);

        extent.width = std::clamp(extent.width, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp(extent.height, caps.minImageExtent.height, caps.maxImageExtent.height);
        return extent;
    }

    VulkanContext::VulkanContext(GLFWwindow* window_handle, VulkanBackend* backend)
            : m_window_handle(window_handle), m_backend(backend) {
        HN_CORE_ASSERT(m_window_handle, "VulkanContext: window handle is null!");
        HN_CORE_ASSERT(m_backend, "VulkanContext: backend is null!");
    }

    VulkanContext::~VulkanContext() {
        destroy();
    }

    void VulkanContext::notify_framebuffer_resized() {
        m_framebuffer_resized = true;
    }

    bool VulkanContext::wait_for_nonzero_framebuffer_size() const {
        int w = 0, h = 0;
        glfwGetFramebufferSize(m_window_handle, &w, &h);
        while (w == 0 || h == 0) {
            glfwWaitEvents();
            glfwGetFramebufferSize(m_window_handle, &w, &h);
        }
        return true;
    }

    void VulkanContext::recreate_swapchain_if_needed() {
        if (!m_framebuffer_resized)
            return;

        // Minimized windows report 0x0 framebuffer; wait until restored.
        wait_for_nonzero_framebuffer_size();

        vkDeviceWaitIdle(reinterpret_cast<VkDevice>(m_device));

        cleanup_swapchain();
        create_swapchain();
        create_image_views();
        create_render_pass();
        create_graphics_pipeline();
        create_framebuffers();
        create_command_buffers();

        m_framebuffer_resized = false;
    }

    static uint32_t find_memory_type_local(VkPhysicalDevice phys, uint32_t type_filter, VkMemoryPropertyFlags props) {
        VkPhysicalDeviceMemoryProperties mem_props{};
        vkGetPhysicalDeviceMemoryProperties(phys, &mem_props);

        for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
            if ((type_filter & (1u << i)) && ((mem_props.memoryTypes[i].propertyFlags & props) == props))
                return i;
        }

        HN_CORE_ASSERT(false, "Failed to find suitable Vulkan memory type (vk_context)");
        return 0;
    }

        void VulkanContext::create_global_descriptor_resources() {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(m_device && m_physical_device, "create_global_descriptor_resources called without device");

        // Descriptor set layout: set=0
        // binding 0 => camera UBO (vertex)
        VkDescriptorSetLayoutBinding ubo_binding{};
        ubo_binding.binding = 0;
        ubo_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ubo_binding.descriptorCount = 1;
        ubo_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        // binding 1 => sampler (fragment)
        VkDescriptorSetLayoutBinding sampler_binding{};
        sampler_binding.binding = 1;
        sampler_binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        sampler_binding.descriptorCount = 1;
        sampler_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        // binding 2 => texture array (sampled images, fragment)
        VkDescriptorSetLayoutBinding tex_binding{};
        tex_binding.binding = 2;
        tex_binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        tex_binding.descriptorCount = VulkanRendererAPI::k_max_texture_slots;
        tex_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding bindings[] = {
            ubo_binding,
            sampler_binding,
            tex_binding
        };

        VkDescriptorSetLayoutCreateInfo layout_ci{};
        layout_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_ci.bindingCount = 3;
        layout_ci.pBindings = bindings;

        VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
        VkResult r = vkCreateDescriptorSetLayout(reinterpret_cast<VkDevice>(m_device), &layout_ci, nullptr, &set_layout);
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkCreateDescriptorSetLayout failed");
        m_global_set_layout = reinterpret_cast<VkDescriptorSetLayout>(set_layout);

        // Descriptor pool sized for frames-in-flight:
        VkDescriptorPoolSize pool_sizes[3]{};

        // UBOs (binding 0)
        pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_sizes[0].descriptorCount = k_max_frames_in_flight;

        // Samplers (binding 1)
        pool_sizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLER;
        pool_sizes[1].descriptorCount = k_max_frames_in_flight;

        // Sampled images (binding 2)
        pool_sizes[2].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        pool_sizes[2].descriptorCount = k_max_frames_in_flight * VulkanRendererAPI::k_max_texture_slots;

        VkDescriptorPoolCreateInfo pool_ci{};
        pool_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_ci.maxSets = k_max_frames_in_flight;
        pool_ci.poolSizeCount = 3;
        pool_ci.pPoolSizes = pool_sizes;

        VkDescriptorPool pool = VK_NULL_HANDLE;
        r = vkCreateDescriptorPool(reinterpret_cast<VkDevice>(m_device), &pool_ci, nullptr, &pool);
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkCreateDescriptorPool failed");
        m_descriptor_pool = reinterpret_cast<VkDescriptorPool>(pool);

        // Create per-frame UBO + allocate per-frame descriptor sets
        m_camera_ubo_size = sizeof(glm::mat4);

        VkDescriptorSetLayout layouts[k_max_frames_in_flight]{};
        for (uint32_t i = 0; i < k_max_frames_in_flight; ++i)
            layouts[i] = reinterpret_cast<VkDescriptorSetLayout>(m_global_set_layout);

        VkDescriptorSetAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc.descriptorPool = reinterpret_cast<VkDescriptorPool>(m_descriptor_pool);
        alloc.descriptorSetCount = k_max_frames_in_flight;
        alloc.pSetLayouts = layouts;

        VkDescriptorSet sets[k_max_frames_in_flight]{};
        r = vkAllocateDescriptorSets(reinterpret_cast<VkDevice>(m_device), &alloc, sets);
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkAllocateDescriptorSets failed");

        for (uint32_t frame = 0; frame < k_max_frames_in_flight; ++frame) {
            m_global_descriptor_sets[frame] = reinterpret_cast<VkDescriptorSet>(sets[frame]);

            // Create camera UBO for this frame
            VkBufferCreateInfo bi{};
            bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bi.size = m_camera_ubo_size;
            bi.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VkBuffer ubo = VK_NULL_HANDLE;
            r = vkCreateBuffer(reinterpret_cast<VkDevice>(m_device), &bi, nullptr, &ubo);
            HN_CORE_ASSERT(r == VK_SUCCESS, "vkCreateBuffer (camera ubo) failed");

            VkMemoryRequirements req{};
            vkGetBufferMemoryRequirements(reinterpret_cast<VkDevice>(m_device), ubo, &req);

            VkMemoryAllocateInfo ai{};
            ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            ai.allocationSize = req.size;
            ai.memoryTypeIndex = find_memory_type_local(
                reinterpret_cast<VkPhysicalDevice>(m_physical_device),
                req.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );

            VkDeviceMemory mem = VK_NULL_HANDLE;
            r = vkAllocateMemory(reinterpret_cast<VkDevice>(m_device), &ai, nullptr, &mem);
            HN_CORE_ASSERT(r == VK_SUCCESS, "vkAllocateMemory (camera ubo) failed");

            r = vkBindBufferMemory(reinterpret_cast<VkDevice>(m_device), ubo, mem, 0);
            HN_CORE_ASSERT(r == VK_SUCCESS, "vkBindBufferMemory (camera ubo) failed");

            m_camera_ubos[frame] = reinterpret_cast<void*>(ubo);
            m_camera_ubo_memories[frame] = reinterpret_cast<void*>(mem);

            // Write binding 0 (UBO) now; bindings 1 & 2 will be updated later.
            VkDescriptorBufferInfo dbi{};
            dbi.buffer = ubo;
            dbi.offset = 0;
            dbi.range = m_camera_ubo_size;

            VkWriteDescriptorSet write_ubo{};
            write_ubo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write_ubo.dstSet = reinterpret_cast<VkDescriptorSet>(m_global_descriptor_sets[frame]);
            write_ubo.dstBinding = 0;
            write_ubo.dstArrayElement = 0;
            write_ubo.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            write_ubo.descriptorCount = 1;
            write_ubo.pBufferInfo = &dbi;

            vkUpdateDescriptorSets(reinterpret_cast<VkDevice>(m_device), 1, &write_ubo, 0, nullptr);
        }
    }

    void VulkanContext::cleanup_global_descriptor_resources() {
        if (!m_device) return;

        for (uint32_t frame = 0; frame < k_max_frames_in_flight; ++frame) {
            if (m_camera_ubos[frame]) {
                vkDestroyBuffer(reinterpret_cast<VkDevice>(m_device), reinterpret_cast<VkBuffer>(m_camera_ubos[frame]), nullptr);
                m_camera_ubos[frame] = nullptr;
            }
            if (m_camera_ubo_memories[frame]) {
                vkFreeMemory(reinterpret_cast<VkDevice>(m_device), reinterpret_cast<VkDeviceMemory>(m_camera_ubo_memories[frame]), nullptr);
                m_camera_ubo_memories[frame] = nullptr;
            }
            m_global_descriptor_sets[frame] = nullptr;
        }

        m_camera_ubo_size = 0;

        if (m_descriptor_pool) {
            vkDestroyDescriptorPool(reinterpret_cast<VkDevice>(m_device), reinterpret_cast<VkDescriptorPool>(m_descriptor_pool), nullptr);
            m_descriptor_pool = nullptr;
        }
        if (m_global_set_layout) {
            vkDestroyDescriptorSetLayout(reinterpret_cast<VkDevice>(m_device), reinterpret_cast<VkDescriptorSetLayout>(m_global_set_layout), nullptr);
            m_global_set_layout = nullptr;
        }
    }

    void VulkanContext::init() {
        HN_PROFILE_FUNCTION();

        if (m_initialized)
            return;

        HN_CORE_INFO("VulkanContext::init");

        HN_CORE_ASSERT(m_backend && m_backend->initialized(), "VulkanContext::init requires initialized VulkanBackend");

        // Shared handles from backend
        m_instance = m_backend->instance();
        m_device = m_backend->device();
        m_physical_device = m_backend->physical_device();

        // Create per-window surface first, then lease queues (backend may create device on first surface).
        create_surface();

        m_queue_lease = m_backend->acquire_queue_lease(reinterpret_cast<VkSurfaceKHR>(m_surface));

        // Refresh cached device handles after leasing (backend may have created the device).
        m_instance = m_backend->instance();
        m_device = m_backend->device();
        m_physical_device = m_backend->physical_device();

        m_graphics_queue_family = m_queue_lease.graphicsFamily;
        m_present_queue_family = m_queue_lease.presentFamily;
        m_graphics_queue = m_queue_lease.graphicsQueue;
        m_present_queue = m_queue_lease.presentQueue;

        create_global_descriptor_resources();

        create_swapchain();
        create_image_views();
        create_render_pass();
        create_graphics_pipeline();
        create_framebuffers();

        create_command_pool();
        create_command_buffers();

        create_sync_objects();

        m_initialized = true;
    }

    void VulkanContext::swap_buffers() {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(m_initialized, "VulkanContext::swap_buffers called before init");

        VulkanRendererAPI::set_recording_context(this);

        recreate_swapchain_if_needed();

        if (m_pipeline_dirty) {
            vkDeviceWaitIdle(reinterpret_cast<VkDevice>(m_device));
            cleanup_pipeline();
            create_graphics_pipeline();
        }

        VkFence in_flight = reinterpret_cast<VkFence>(m_in_flight_fences[m_current_frame]);
        vkWaitForFences(reinterpret_cast<VkDevice>(m_device), 1, &in_flight, VK_TRUE, UINT64_MAX);

        uint32_t image_index = 0;
        VkResult acquire_res = vkAcquireNextImageKHR(
            reinterpret_cast<VkDevice>(m_device),
            reinterpret_cast<VkSwapchainKHR>(m_swapchain),
            UINT64_MAX,
            reinterpret_cast<VkSemaphore>(m_image_available_semaphores[m_current_frame]),
            VK_NULL_HANDLE,
            &image_index
        );

        if (acquire_res == VK_ERROR_OUT_OF_DATE_KHR) {
            m_framebuffer_resized = true;
            recreate_swapchain_if_needed();
            return;
        }
        HN_CORE_ASSERT(acquire_res == VK_SUCCESS || acquire_res == VK_SUBOPTIMAL_KHR,
                       "vkAcquireNextImageKHR failed: {0}", vk_result_to_string(acquire_res));

        HN_CORE_ASSERT(image_index < m_images_in_flight.size(), "Acquired image index out of bounds.");
        VkFence image_fence = reinterpret_cast<VkFence>(m_images_in_flight[image_index]);
        if (image_fence != VK_NULL_HANDLE) {
            vkWaitForFences(reinterpret_cast<VkDevice>(m_device), 1, &image_fence, VK_TRUE, UINT64_MAX);
        }

        m_images_in_flight[image_index] = reinterpret_cast<VkFence>(m_in_flight_fences[m_current_frame]);

        vkResetFences(reinterpret_cast<VkDevice>(m_device), 1, &in_flight);

        record_command_buffer(image_index);

        VkSemaphore wait_semaphores[] = { reinterpret_cast<VkSemaphore>(m_image_available_semaphores[m_current_frame]) };
        VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

        HN_CORE_ASSERT(image_index < m_render_finished_semaphores.size(), "renderFinished semaphore index out of bounds.");
        VkSemaphore signal_semaphores[] = { reinterpret_cast<VkSemaphore>(m_render_finished_semaphores[image_index]) };

        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = wait_semaphores;
        submit_info.pWaitDstStageMask = wait_stages;
        submit_info.commandBufferCount = 1;
        VkCommandBuffer cmd = reinterpret_cast<VkCommandBuffer>(m_command_buffers[image_index]);
        submit_info.pCommandBuffers = &cmd;
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = signal_semaphores;

        VkResult submit_res = VK_SUCCESS;
        if (m_queue_lease.sharedGraphics && m_queue_lease.graphicsSubmitMutex) {
            std::scoped_lock lk(*m_queue_lease.graphicsSubmitMutex);
            submit_res = vkQueueSubmit(reinterpret_cast<VkQueue>(m_graphics_queue), 1, &submit_info, in_flight);
        } else {
            submit_res = vkQueueSubmit(reinterpret_cast<VkQueue>(m_graphics_queue), 1, &submit_info, in_flight);
        }
        HN_CORE_ASSERT(submit_res == VK_SUCCESS, "vkQueueSubmit failed: {0}", vk_result_to_string(submit_res));

        VkSwapchainKHR swapchains[] = { reinterpret_cast<VkSwapchainKHR>(m_swapchain) };

        VkPresentInfoKHR present_info{};
        present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores = signal_semaphores;
        present_info.swapchainCount = 1;
        present_info.pSwapchains = swapchains;
        present_info.pImageIndices = &image_index;

        VkResult present_res = VK_SUCCESS;
        if (m_queue_lease.sharedPresent && m_queue_lease.presentSubmitMutex) {
            std::scoped_lock lk(*m_queue_lease.presentSubmitMutex);
            present_res = vkQueuePresentKHR(reinterpret_cast<VkQueue>(m_present_queue), &present_info);
        } else {
            present_res = vkQueuePresentKHR(reinterpret_cast<VkQueue>(m_present_queue), &present_info);
        }

        if (present_res == VK_ERROR_OUT_OF_DATE_KHR || present_res == VK_SUBOPTIMAL_KHR) {
            m_framebuffer_resized = true;
            recreate_swapchain_if_needed();
        } else {
            HN_CORE_ASSERT(present_res == VK_SUCCESS, "vkQueuePresentKHR failed: {0}", vk_result_to_string(present_res));
        }

        m_current_frame = (m_current_frame + 1) % k_max_frames_in_flight;
        frame_packet().frame_begun = false;
    }

    void VulkanContext::wait_idle() {
        if (!m_device)
            return;
        vkDeviceWaitIdle(reinterpret_cast<VkDevice>(m_device));
    }

#if defined(BUILD_DEBUG)
    static constexpr bool k_enable_validation = true;
#else
    static constexpr bool k_enable_validation = false;
#endif

    static const std::vector<const char*> k_validation_layers = {
        "VK_LAYER_KHRONOS_validation"
    };

    static VKAPI_ATTR VkBool32 VKAPI_CALL vk_debug_callback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT type,
        const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
        void* /*user_data*/
    ) {
        const char* msg = callback_data && callback_data->pMessage ? callback_data->pMessage : "<no message>";

        if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
            HN_CORE_ERROR("[Vulkan] {0}", msg);
        } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
            HN_CORE_WARN("[Vulkan] {0}", msg);
        } else {
            HN_CORE_INFO("[Vulkan] {0}", msg);
        }

        return VK_FALSE;
    }

    static VkResult create_debug_utils_messenger_ext(
        VkInstance instance,
        const VkDebugUtilsMessengerCreateInfoEXT* create_info,
        const VkAllocationCallbacks* allocator,
        VkDebugUtilsMessengerEXT* messenger
    ) {
        auto fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT")
        );
        if (!fn)
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        return fn(instance, create_info, allocator, messenger);
    }

    static void destroy_debug_utils_messenger_ext(
        VkInstance instance,
        VkDebugUtilsMessengerEXT messenger,
        const VkAllocationCallbacks* allocator
    ) {
        auto fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT")
        );
        if (fn) {
            fn(instance, messenger, allocator);
        }
    }

    void VulkanContext::create_surface() {
        HN_PROFILE_FUNCTION();

        HN_CORE_ASSERT(m_backend && m_backend->instance(), "VulkanContext::create_surface called without initialized backend instance");

        VkSurfaceKHR surface = VK_NULL_HANDLE;
        VkResult res = glfwCreateWindowSurface(
            m_backend->instance(),
            m_window_handle,
            nullptr,
            &surface
        );
        HN_CORE_ASSERT(res == VK_SUCCESS, "glfwCreateWindowSurface failed: {0}", vk_result_to_string(res));

        m_surface = reinterpret_cast<VkSurfaceKHR>(surface);
        HN_CORE_INFO("Vulkan surface created.");
    }

    void VulkanContext::create_swapchain() {
        HN_PROFILE_FUNCTION();

        SwapchainSupportDetails sc = query_swapchain_support(
            reinterpret_cast<VkPhysicalDevice>(m_physical_device),
            reinterpret_cast<VkSurfaceKHR>(m_surface)
        );

        VkSurfaceFormatKHR surface_format = choose_surface_format(sc.formats);
        VkPresentModeKHR present_mode = choose_present_mode(sc.present_modes);
        VkExtent2D extent = choose_extent(m_window_handle, sc.capabilities);

        uint32_t image_count = sc.capabilities.minImageCount + 1;
        if (sc.capabilities.maxImageCount > 0 && image_count > sc.capabilities.maxImageCount) {
            image_count = sc.capabilities.maxImageCount;
        }

        VkSwapchainKHR old_swapchain = reinterpret_cast<VkSwapchainKHR>(m_swapchain);

        VkSwapchainCreateInfoKHR create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        create_info.surface = reinterpret_cast<VkSurfaceKHR>(m_surface);
        create_info.minImageCount = image_count;
        create_info.imageFormat = surface_format.format;
        create_info.imageColorSpace = surface_format.colorSpace;
        create_info.imageExtent = extent;
        create_info.imageArrayLayers = 1;
        create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        uint32_t queue_family_indices[] = { m_graphics_queue_family, m_present_queue_family };
        if (m_graphics_queue_family != m_present_queue_family) {
            create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            create_info.queueFamilyIndexCount = 2;
            create_info.pQueueFamilyIndices = queue_family_indices;
        } else {
            create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        create_info.preTransform = sc.capabilities.currentTransform;
        create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        create_info.presentMode = present_mode;
        create_info.clipped = VK_TRUE;
        create_info.oldSwapchain = old_swapchain;

        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        VkResult res = vkCreateSwapchainKHR(reinterpret_cast<VkDevice>(m_device), &create_info, nullptr, &swapchain);
        HN_CORE_ASSERT(res == VK_SUCCESS, "vkCreateSwapchainKHR failed: {0}", vk_result_to_string(res));

        m_swapchain = reinterpret_cast<VkSwapchainKHR>(swapchain);

        // It's now safe to destroy the old swapchain handle if it existed.
        if (old_swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(reinterpret_cast<VkDevice>(m_device), old_swapchain, nullptr);
        }

        uint32_t actual_count = 0;
        vkGetSwapchainImagesKHR(reinterpret_cast<VkDevice>(m_device), reinterpret_cast<VkSwapchainKHR>(m_swapchain), &actual_count, nullptr);
        m_swapchain_images.resize(actual_count);
        vkGetSwapchainImagesKHR(reinterpret_cast<VkDevice>(m_device), reinterpret_cast<VkSwapchainKHR>(m_swapchain), &actual_count,
                                reinterpret_cast<VkImage*>(m_swapchain_images.data()));

        m_swapchain_image_format = static_cast<uint32_t>(surface_format.format);
        m_swapchain_extent_width = extent.width;
        m_swapchain_extent_height = extent.height;

        m_images_in_flight.assign(m_swapchain_images.size(), VK_NULL_HANDLE);
        m_current_frame = 0;

        // If per-image semaphores were cleared (swapchain recreation), recreate them here.
        if (m_render_finished_semaphores.empty()) {
            VkSemaphoreCreateInfo sem{};
            sem.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

            m_render_finished_semaphores.resize(m_swapchain_images.size());
            for (size_t i = 0; i < m_render_finished_semaphores.size(); i++) {
                VkSemaphore render_done = VK_NULL_HANDLE;
                VkResult r = vkCreateSemaphore(reinterpret_cast<VkDevice>(m_device), &sem, nullptr, &render_done);
                HN_CORE_ASSERT(r == VK_SUCCESS, "Failed to create Vulkan per-image render-finished semaphore.");
                m_render_finished_semaphores[i] = reinterpret_cast<VkSemaphore>(render_done);
            }
        }

        HN_CORE_INFO("Swapchain created: {0} images, extent {1}x{2}", actual_count, extent.width, extent.height);
    }

    void VulkanContext::create_image_views() {
        HN_PROFILE_FUNCTION();

        m_swapchain_image_views.resize(m_swapchain_images.size());

        for (size_t i = 0; i < m_swapchain_images.size(); i++) {
            VkImageViewCreateInfo view{};
            view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view.image = reinterpret_cast<VkImage>(m_swapchain_images[i]);
            view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view.format = static_cast<VkFormat>(m_swapchain_image_format);
            view.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            view.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            view.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            view.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            view.subresourceRange.baseMipLevel = 0;
            view.subresourceRange.levelCount = 1;
            view.subresourceRange.baseArrayLayer = 0;
            view.subresourceRange.layerCount = 1;

            VkImageView image_view = VK_NULL_HANDLE;
            VkResult res = vkCreateImageView(reinterpret_cast<VkDevice>(m_device), &view, nullptr, &image_view);
            HN_CORE_ASSERT(res == VK_SUCCESS, "vkCreateImageView failed: {0}", vk_result_to_string(res));

            m_swapchain_image_views[i] = reinterpret_cast<VkImageView>(image_view);

            // Debug names
            {
                char imgName[64];
                std::snprintf(imgName, sizeof(imgName), "SwapchainImage_%zu", i);
                set_debug_name(reinterpret_cast<VkDevice>(m_device),
                               VK_OBJECT_TYPE_IMAGE,
                               reinterpret_cast<uint64_t>(m_swapchain_images[i]),
                               imgName);

                char viewName[64];
                std::snprintf(viewName, sizeof(viewName), "SwapchainImageView_%zu", i);
                set_debug_name(reinterpret_cast<VkDevice>(m_device),
                               VK_OBJECT_TYPE_IMAGE_VIEW,
                               reinterpret_cast<uint64_t>(image_view),
                               viewName);
            }
        }
    }

    void VulkanContext::create_render_pass() {
        HN_PROFILE_FUNCTION();

        VkAttachmentDescription color{};
        color.format = static_cast<VkFormat>(m_swapchain_image_format);
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference color_ref{};
        color_ref.attachment = 0;
        color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_ref;

        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.srcAccessMask = 0;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rp.attachmentCount = 1;
        rp.pAttachments = &color;
        rp.subpassCount = 1;
        rp.pSubpasses = &subpass;
        rp.dependencyCount = 1;
        rp.pDependencies = &dep;

        VkRenderPass render_pass = VK_NULL_HANDLE;
        VkResult res = vkCreateRenderPass(reinterpret_cast<VkDevice>(m_device), &rp, nullptr, &render_pass);
        HN_CORE_ASSERT(res == VK_SUCCESS, "vkCreateRenderPass failed: {0}", vk_result_to_string(res));

        m_render_pass = reinterpret_cast<VkRenderPass>(render_pass);

        set_debug_name(reinterpret_cast<VkDevice>(m_device),
                           VK_OBJECT_TYPE_RENDER_PASS,
                           reinterpret_cast<uint64_t>(render_pass),
                           "MainSwapchain_RenderPass");
    }

    void VulkanContext::create_framebuffers() {
        HN_PROFILE_FUNCTION();

        m_swapchain_framebuffers.resize(m_swapchain_image_views.size());

        for (size_t i = 0; i < m_swapchain_image_views.size(); i++) {
            VkImageView attachments[] = { reinterpret_cast<VkImageView>(m_swapchain_image_views[i]) };

            VkFramebufferCreateInfo fb{};
            fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fb.renderPass = reinterpret_cast<VkRenderPass>(m_render_pass);
            fb.attachmentCount = 1;
            fb.pAttachments = attachments;
            fb.width = m_swapchain_extent_width;
            fb.height = m_swapchain_extent_height;
            fb.layers = 1;

            VkFramebuffer framebuffer = VK_NULL_HANDLE;
            VkResult res = vkCreateFramebuffer(reinterpret_cast<VkDevice>(m_device), &fb, nullptr, &framebuffer);
            HN_CORE_ASSERT(res == VK_SUCCESS, "vkCreateFramebuffer failed: {0}", vk_result_to_string(res));

            m_swapchain_framebuffers[i] = reinterpret_cast<VkFramebuffer>(framebuffer);

            char fbName[64];
            std::snprintf(fbName, sizeof(fbName), "SwapchainFramebuffer_%zu", i);
            set_debug_name(reinterpret_cast<VkDevice>(m_device),
                           VK_OBJECT_TYPE_FRAMEBUFFER,
                           reinterpret_cast<uint64_t>(framebuffer),
                           fbName);
        }
    }

    void VulkanContext::create_command_pool() {
        HN_PROFILE_FUNCTION();

        VkCommandPoolCreateInfo pool{};
        pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool.queueFamilyIndex = m_graphics_queue_family;

        VkCommandPool command_pool = VK_NULL_HANDLE;
        VkResult res = vkCreateCommandPool(reinterpret_cast<VkDevice>(m_device), &pool, nullptr, &command_pool);
        HN_CORE_ASSERT(res == VK_SUCCESS, "vkCreateCommandPool failed: {0}", vk_result_to_string(res));

        m_command_pool = reinterpret_cast<VkCommandPool>(command_pool);
    }

    void VulkanContext::create_command_buffers() {
        HN_PROFILE_FUNCTION();

        // If we recreate swapchain, command buffers must match swapchain image count.
        if (!m_command_buffers.empty()) {
            vkFreeCommandBuffers(
                reinterpret_cast<VkDevice>(m_device),
                reinterpret_cast<VkCommandPool>(m_command_pool),
                static_cast<uint32_t>(m_command_buffers.size()),
                reinterpret_cast<VkCommandBuffer*>(m_command_buffers.data())
            );
            m_command_buffers.clear();
        }

        m_command_buffers.resize(m_swapchain_images.size());

        VkCommandBufferAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc.commandPool = reinterpret_cast<VkCommandPool>(m_command_pool);
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = static_cast<uint32_t>(m_command_buffers.size());

        VkResult res = vkAllocateCommandBuffers(
            reinterpret_cast<VkDevice>(m_device),
            &alloc,
            reinterpret_cast<VkCommandBuffer*>(m_command_buffers.data())
        );
        HN_CORE_ASSERT(res == VK_SUCCESS, "vkAllocateCommandBuffers failed: {0}", vk_result_to_string(res));
    }

    void VulkanContext::create_sync_objects() {
        HN_PROFILE_FUNCTION();

        // Per-frame sync (frames in flight)
        m_image_available_semaphores.resize(k_max_frames_in_flight);
        m_in_flight_fences.resize(k_max_frames_in_flight);

        // Per-swapchain-image sync (solves swapchain semaphore reuse warnings)
        HN_CORE_ASSERT(!m_swapchain_images.empty(), "create_sync_objects called before swapchain images exist.");
        m_render_finished_semaphores.resize(m_swapchain_images.size());

        VkSemaphoreCreateInfo sem{};
        sem.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fence{};
        fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (uint32_t i = 0; i < k_max_frames_in_flight; i++) {
            VkSemaphore image_avail = VK_NULL_HANDLE;
            VkFence in_flight = VK_NULL_HANDLE;

            VkResult r1 = vkCreateSemaphore(reinterpret_cast<VkDevice>(m_device), &sem, nullptr, &image_avail);
            VkResult r2 = vkCreateFence(reinterpret_cast<VkDevice>(m_device), &fence, nullptr, &in_flight);

            HN_CORE_ASSERT(r1 == VK_SUCCESS && r2 == VK_SUCCESS, "Failed to create Vulkan per-frame sync objects.");

            m_image_available_semaphores[i] = reinterpret_cast<VkSemaphore>(image_avail);
            m_in_flight_fences[i] = reinterpret_cast<VkFence>(in_flight);
        }

        for (size_t i = 0; i < m_render_finished_semaphores.size(); i++) {
            VkSemaphore render_done = VK_NULL_HANDLE;
            VkResult r = vkCreateSemaphore(reinterpret_cast<VkDevice>(m_device), &sem, nullptr, &render_done);
            HN_CORE_ASSERT(r == VK_SUCCESS, "Failed to create Vulkan per-image render-finished semaphore.");

            m_render_finished_semaphores[i] = reinterpret_cast<VkSemaphore>(render_done);
        }
    }

    void VulkanContext::record_command_buffer(uint32_t image_index) {
        HN_PROFILE_FUNCTION();

        VulkanRendererAPI::set_recording_context(this);

        VkCommandBuffer cmd = reinterpret_cast<VkCommandBuffer>(m_command_buffers[image_index]);
        vkResetCommandBuffer(cmd, 0);

        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        VkResult res = vkBeginCommandBuffer(cmd, &begin);
        HN_CORE_ASSERT(res == VK_SUCCESS, "vkBeginCommandBuffer failed: {0}", vk_result_to_string(res));

        //create_graphics_pipeline(); // Called every frame to ensure it updates when settings are changed

        auto& p = frame_packet();

        const bool use_cmds = !p.cmds.empty();
        bool render_pass_open = false;
        bool in_swapchain_pass = false;

        auto bind_pipeline_and_dynamic = [&](VkPipeline pipeline, uint32_t width, uint32_t height) {
            HN_CORE_ASSERT(pipeline != VK_NULL_HANDLE, "Vulkan pipeline is null");
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

            VkViewport viewport{};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width  = static_cast<float>(width);
            viewport.height = static_cast<float>(height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &viewport);

            VkRect2D scissor{};
            scissor.offset = { 0, 0 };
            scissor.extent = { width, height };
            vkCmdSetScissor(cmd, 0, 1, &scissor);
        };

           auto apply_globals = [&](const FramePacket::CmdBindGlobals& g) {
               const uint32_t frame = m_current_frame;
               VkDescriptorSet ds = reinterpret_cast<VkDescriptorSet>(m_global_descriptor_sets[frame]);

               // Camera UBO
               if (g.hasCamera && m_camera_ubo_memories[frame] && m_camera_ubo_size == sizeof(glm::mat4)) {
                   // g.viewProjection is in EngineClip (GL-style).
                   glm::mat4 vp_engine = g.viewProjection;

                   // Convert EngineClip -> VulkanClip (Y down, z in [0,1]).
                   glm::mat4 correction(1.0f);
                   correction[1][1] = -1.0f; // flip Y
                   correction[2][2] = 0.5f;
                   correction[3][2] = 0.5f;  // z' = 0.5*z + 0.5*w

                   glm::mat4 vp_vulkan = correction * vp_engine;
                   //HN_CORE_INFO("Vulkan VP:\n{}", glm::to_string(vp_vulkan));

                   void* mapped = nullptr;
                   VkResult mr = vkMapMemory(reinterpret_cast<VkDevice>(m_device),
                                             reinterpret_cast<VkDeviceMemory>(m_camera_ubo_memories[frame]),
                                             0, m_camera_ubo_size, 0, &mapped);
                   HN_CORE_ASSERT(mr == VK_SUCCESS, "vkMapMemory camera ubo failed");
                   std::memcpy(mapped, &vp_vulkan, sizeof(glm::mat4));
                   vkUnmapMemory(reinterpret_cast<VkDevice>(m_device),
                                 reinterpret_cast<VkDeviceMemory>(m_camera_ubo_memories[frame]));
               }

               // Sampler + texture descriptors
               if (g.hasTextures) {
                   HN_CORE_ASSERT(g.textureCount > 0 && g.textures[0],
                                  "Vulkan: expected texture slot 0 to be present");

                   auto* white_base = reinterpret_cast<Texture2D*>(g.textures[0]);
                   auto* white_vk = dynamic_cast<VulkanTexture2D*>(white_base);
                   HN_CORE_ASSERT(white_vk, "Vulkan: slot 0 texture is not a VulkanTexture2D");

                   // --- Binding 1: sampler (single) ---
                   VkSampler sampler_handle = VK_NULL_HANDLE;

                   // Choose sampler based on renderer settings
                   auto& rs = Settings::get().renderer;
                   switch (rs.texture_filter) {
                   case RendererSettings::TextureFilter::nearest:
                       // For now, just use a single global sampler. You can expand this
                       // to keep 2–3 precreated samplers in the backend and switch here.
                       sampler_handle = m_backend->get_sampler_nearest();
                       break;
                   case RendererSettings::TextureFilter::linear:
                       sampler_handle = m_backend->get_sampler_linear();
                       break;
                   case RendererSettings::TextureFilter::anisotropic:
                       sampler_handle = m_backend->get_sampler_anisotropic();
                       break;
                   }

                   if (!sampler_handle) {
                       sampler_handle = m_backend->get_sampler_linear();
                   }

                   VkDescriptorImageInfo sampler_info{};
                   sampler_info.sampler = sampler_handle;
                   sampler_info.imageView = VK_NULL_HANDLE;
                   sampler_info.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

                   VkWriteDescriptorSet write_sampler{};
                   write_sampler.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                   write_sampler.dstSet = ds;
                   write_sampler.dstBinding = 1; // sampler binding
                   write_sampler.dstArrayElement = 0;
                   write_sampler.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
                   write_sampler.descriptorCount = 1;
                   write_sampler.pImageInfo = &sampler_info;

                   // --- Binding 2: sampled image array ---
                   VkDescriptorImageInfo infos[VulkanRendererAPI::k_max_texture_slots]{};

                   for (uint32_t i = 0; i < VulkanRendererAPI::k_max_texture_slots; ++i) {
                       void* raw = (i < g.textureCount) ? g.textures[i] : g.textures[0];
                       auto* base = reinterpret_cast<Texture2D*>(raw);
                       auto* vktex = dynamic_cast<VulkanTexture2D*>(base);
                       if (!vktex) vktex = white_vk;

                       infos[i].sampler = VK_NULL_HANDLE; // sampler is bound separately
                       infos[i].imageView = reinterpret_cast<VkImageView>(vktex->get_vk_image_view());
                       infos[i].imageLayout = static_cast<VkImageLayout>(vktex->get_vk_image_layout());
                   }

                   VkWriteDescriptorSet write_images{};
                   write_images.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                   write_images.dstSet = ds;
                   write_images.dstBinding = 2; // texture array binding
                   write_images.dstArrayElement = 0;
                   write_images.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                   write_images.descriptorCount = VulkanRendererAPI::k_max_texture_slots;
                   write_images.pImageInfo = infos;

                   VkWriteDescriptorSet writes[] = { write_sampler, write_images };
                   vkUpdateDescriptorSets(reinterpret_cast<VkDevice>(m_device),
                                          static_cast<uint32_t>(std::size(writes)),
                                          writes,
                                          0,
                                          nullptr);
               }

               vkCmdBindDescriptorSets(cmd,
                                       VK_PIPELINE_BIND_POINT_GRAPHICS,
                                       reinterpret_cast<VkPipelineLayout>(m_pipeline_quad.layout()),
                                       0,
                                       1,
                                       &ds,
                                       0,
                                       nullptr);
           };

        for (const auto& c : p.cmds) {
            switch (c.type) {
            case FramePacket::CmdType::BeginSwapchainPass: {
                    VkClearValue clear_value{};
                    clear_value.color = { { c.begin.clearColor.r, c.begin.clearColor.g, c.begin.clearColor.b, c.begin.clearColor.a } };

                    VkRenderPassBeginInfo rp_begin{};
                    rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                    rp_begin.renderPass = reinterpret_cast<VkRenderPass>(m_render_pass);
                    rp_begin.framebuffer = reinterpret_cast<VkFramebuffer>(m_swapchain_framebuffers[image_index]);
                    rp_begin.renderArea.offset = { 0, 0 };
                    rp_begin.renderArea.extent = { m_swapchain_extent_width, m_swapchain_extent_height };
                    rp_begin.clearValueCount = 1;
                    rp_begin.pClearValues = &clear_value;

                    vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
                    render_pass_open = true;
                    in_swapchain_pass = true;

                    bind_pipeline_and_dynamic(
                        reinterpret_cast<VkPipeline>(m_pipeline_quad.pipeline()),
                        m_swapchain_extent_width,
                        m_swapchain_extent_height);
                    break;
            }
            case FramePacket::CmdType::BeginOffscreenPass: {
                    auto* vk_fb = c.offscreen.framebuffer;
                    HN_CORE_ASSERT(vk_fb, "BeginOffscreenPass: framebuffer is null");

                    VkRenderPass rp = reinterpret_cast<VkRenderPass>(vk_fb->get_render_pass());
                    VkFramebuffer fb = reinterpret_cast<VkFramebuffer>(vk_fb->get_framebuffer());
                    auto extent = vk_fb->get_extent();

                    // Lazily create offscreen quad pipeline compatible with this FB's render pass
                    if (!m_pipeline_quad_fb.valid()) {
                        static ShaderCache cache;
                        const auto glsl = std::filesystem::path("../assets/shaders/Renderer2D_Quad.glsl");
                        const auto spirv = cache.get_or_compile_spirv_paths(glsl);

                        auto& rs = Settings::get().renderer; // <- Hip, new and cool

                        PipelineSpec offspec;
                        offspec.shaderGLSLPath = glsl;
                        offspec.topology = PrimitiveTopology::Triangles;
                        offspec.cullMode = CullMode::None;
                        offspec.frontFace = FrontFaceWinding::CounterClockwise;
                        offspec.depthStencil.depthTest  = rs.depth_test;
                        offspec.depthStencil.depthWrite = rs.depth_write;
                        offspec.passType = RenderPassType::Offscreen;
                        offspec.wireframe = rs.wireframe;

                        // Blend states per color attachment of vk_fb
                        const uint32_t colorCount = vk_fb->get_color_attachment_count();
                        offspec.perColorAttachmentBlend.clear();
                        offspec.perColorAttachmentBlend.resize(colorCount);

                        for (uint32_t i = 0; i < colorCount; ++i) {
                            auto fmt = vk_fb->get_color_attachment_format(i);
                            AttachmentBlendState ab{};
                            switch (fmt) {
                            case FramebufferTextureFormat::RGBA8:
                                ab.enabled = rs.blending;
                                break;
                            case FramebufferTextureFormat::RED_INTEGER:
                                ab.enabled = false; // must not blend integer attachment
                                break;
                            default:
                                ab.enabled = false;
                                break;
                            }
                            offspec.perColorAttachmentBlend[i] = ab;
                        }

                        // Same vertex bindings as swapchain pipeline
                        VertexInputBindingSpec static_binding;
                        static_binding.layout = BufferLayout{
                                { ShaderDataType::Float2, "a_local_pos" },  // loc 0
                                { ShaderDataType::Float2, "a_local_tex" }   // loc 1
                        };

                        VertexInputBindingSpec instance_binding;
                        instance_binding.layout = BufferLayout{
                                { ShaderDataType::Float3, "i_center",       false, true }, // loc 2
                                { ShaderDataType::Float2, "i_half_size",    false, true }, // loc 3
                                { ShaderDataType::Float , "i_rotation",     false, true }, // loc 4
                                { ShaderDataType::Float4, "i_color",        false, true }, // loc 5
                                { ShaderDataType::Int,   "i_tex_index",     false, true }, // loc 6
                                { ShaderDataType::Float, "i_tiling",        false, true }, // loc 7
                                { ShaderDataType::Float2,"i_tex_coord_min", false, true }, // loc 8
                                { ShaderDataType::Float2,"i_tex_coord_max", false, true }, // loc 9
                                { ShaderDataType::Int,   "i_entity_id",     false, true }  // loc 10
                        };

                        offspec.vertexBindings.push_back(static_binding);
                        offspec.vertexBindings.push_back(instance_binding);

                        m_pipeline_quad_fb.create(
                            reinterpret_cast<VkDevice>(m_device),
                            rp,
                            reinterpret_cast<VkDescriptorSetLayout>(m_global_set_layout),
                            spirv.vertex.string(),
                            spirv.fragment.string(),
                            offspec
                        );
                    }

                    // Query attachment layout from the framebuffer itself
                    const uint32_t colorCount = vk_fb->get_color_attachment_count();
                    const bool hasDepth = vk_fb->has_depth_attachment();
                    const uint32_t totalAttachments = colorCount + (hasDepth ? 1u : 0u);

                    std::vector<VkClearValue> clear_values(totalAttachments);

                    // Color attachments
                    for (uint32_t i = 0; i < colorCount; ++i) {
                        const auto fmt = vk_fb->get_color_attachment_format(i);
                        VkClearValue& cv = clear_values[i];

                        if (i == 0) {
                            // First color: scene color, use clearColor from the pass
                            cv.color = { {
                                c.offscreen.clearColor.r,
                                c.offscreen.clearColor.g,
                                c.offscreen.clearColor.b,
                                c.offscreen.clearColor.a
                            } };
                        } else {
                            // Other colors: format‑dependent. For RED_INTEGER, clear to -1 for picking.
                            switch (fmt) {
                            case FramebufferTextureFormat::RED_INTEGER:
                                cv.color.int32[0] = -1;
                                cv.color.int32[1] = -1;
                                cv.color.int32[2] = -1;
                                cv.color.int32[3] = -1;
                                break;
                            case FramebufferTextureFormat::RGBA8:
                            default:
                                cv.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
                                break;
                            }
                        }
                    }

                    // Depth attachment, if present, is always after all colors
                    if (hasDepth) {
                        VkClearValue& dv = clear_values[colorCount];
                        dv.depthStencil.depth = 1.0f;
                        dv.depthStencil.stencil = 0;
                    }

                    VkRenderPassBeginInfo rp_begin{};
                    rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                    rp_begin.renderPass = rp;
                    rp_begin.framebuffer = fb;
                    rp_begin.renderArea.offset = { 0, 0 };
                    rp_begin.renderArea.extent = { extent.width, extent.height };
                    rp_begin.clearValueCount = totalAttachments;
                    rp_begin.pClearValues = clear_values.data();

                    vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
                    render_pass_open = true;
                    in_swapchain_pass = false;

                    bind_pipeline_and_dynamic(
                        reinterpret_cast<VkPipeline>(m_pipeline_quad_fb.pipeline()),
                        extent.width,
                        extent.height
                    );
                    break;
            }
            case FramePacket::CmdType::BindPipelineQuad2D: {
                    if (render_pass_open) {
                        VkPipeline pipeline =
                            in_swapchain_pass
                                ? reinterpret_cast<VkPipeline>(m_pipeline_quad.pipeline())
                                : reinterpret_cast<VkPipeline>(m_pipeline_quad_fb.pipeline());

                        uint32_t w = in_swapchain_pass ? m_swapchain_extent_width : 0;
                        uint32_t h = in_swapchain_pass ? m_swapchain_extent_height : 0;
                        if (w && h)
                            bind_pipeline_and_dynamic(pipeline, w, h);
                    }
                    break;
            }
            case FramePacket::CmdType::BindGlobals: {
                    HN_CORE_ASSERT(render_pass_open, "BindGlobals must occur inside a render pass");
                    //HN_CORE_INFO("BindGlobals: textureCount = {}", c.globals.textureCount);
                    apply_globals(c.globals);
                    break;
            }
            case FramePacket::CmdType::DrawIndexed: {
                    HN_CORE_ASSERT(render_pass_open, "DrawIndexed must occur inside a render pass");
                    HN_CORE_ASSERT(c.draw.va, "DrawIndexed: VertexArray is null");

                    const auto& vbs = c.draw.va->get_vertex_buffers();
                    const auto& ib = c.draw.va->get_index_buffer();

                    HN_CORE_ASSERT(vbs.size() >= 2, "Vulkan draw: expected 2 vertex buffers (static + instance)");
                    HN_CORE_ASSERT(ib, "Vulkan draw: VertexArray has no index buffer");

                    auto vk_vb0 = std::dynamic_pointer_cast<VulkanVertexBuffer>(vbs[0]);
                    auto vk_vb1 = std::dynamic_pointer_cast<VulkanVertexBuffer>(vbs[1]);
                    auto vk_ib = std::dynamic_pointer_cast<VulkanIndexBuffer>(ib);

                    HN_CORE_ASSERT(vk_vb0 && vk_vb1, "Vulkan draw: expected VulkanVertexBuffer(s) in VertexArray");
                    HN_CORE_ASSERT(vk_ib, "Vulkan draw: expected VulkanIndexBuffer in VertexArray");

                    VkBuffer vbufs[] = {
                        reinterpret_cast<VkBuffer>(vk_vb0->get_vk_buffer()),
                        reinterpret_cast<VkBuffer>(vk_vb1->get_vk_buffer())
                    };
                    VkDeviceSize offsets[] = { 0, 0 };
                    vkCmdBindVertexBuffers(cmd, 0, 2, vbufs, offsets);

                    VkBuffer ibuf = reinterpret_cast<VkBuffer>(vk_ib->get_vk_buffer());
                    vkCmdBindIndexBuffer(cmd, ibuf, 0, VK_INDEX_TYPE_UINT32);

                    const uint32_t index_count = (c.draw.indexCount != 0) ? c.draw.indexCount : vk_ib->get_count();
                    const uint32_t instance_count = (c.draw.instanceCount != 0) ? c.draw.instanceCount : 1;
                    vkCmdDrawIndexed(cmd, index_count, instance_count, 0, 0, 0);
                    break;
            }
            case FramePacket::CmdType::EndPass: {
                    if (render_pass_open) {
                        if (in_swapchain_pass) {
                            // Before ending the main swapchain pass, draw Dear ImGui on top.
                            if (ImGui::GetDrawData() && ImGui::GetDrawData()->CmdListsCount > 0 && m_backend) {
                                VkExtent2D imgui_extent{
                                    m_swapchain_extent_width,
                                    m_swapchain_extent_height
                                };
                                VkImageView imgui_target_view =
                                    reinterpret_cast<VkImageView>(m_swapchain_image_views[image_index]);

                                m_backend->render_imgui_on_current_swapchain_image(cmd,
                                                                                   imgui_target_view,
                                                                                   imgui_extent);
                            }
                        }

                        vkCmdEndRenderPass(cmd);
                        render_pass_open = false;
                        in_swapchain_pass = false;
                    }
                    break;
            }
            }
        }

        if (render_pass_open) {
            // Safety: if EndPass was never seen, close the pass.
            if (in_swapchain_pass) {
                if (ImGui::GetDrawData() && ImGui::GetDrawData()->CmdListsCount > 0 && m_backend) {
                    VkExtent2D imgui_extent{
                        m_swapchain_extent_width,
                        m_swapchain_extent_height
                    };
                    VkImageView imgui_target_view =
                        reinterpret_cast<VkImageView>(m_swapchain_image_views[image_index]);

                    m_backend->render_imgui_on_current_swapchain_image(cmd,
                                                                       imgui_target_view,
                                                                       imgui_extent);
                }
            }

            vkCmdEndRenderPass(cmd);
            render_pass_open = false;
            in_swapchain_pass = false;
        }

        res = vkEndCommandBuffer(cmd);
        HN_CORE_ASSERT(res == VK_SUCCESS, "vkEndCommandBuffer failed: {0}", vk_result_to_string(res));
    }

    void VulkanContext::cleanup_swapchain() {
        if (!m_device || !m_swapchain)
            return;

        vkDeviceWaitIdle(reinterpret_cast<VkDevice>(m_device));

        cleanup_pipeline();

        m_images_in_flight.clear();

        // Destroy per-swapchain-image semaphores (they are swapchain-size dependent)
        for (auto sem : m_render_finished_semaphores) {
            if (sem) {
                vkDestroySemaphore(reinterpret_cast<VkDevice>(m_device), reinterpret_cast<VkSemaphore>(sem), nullptr);
            }
        }
        m_render_finished_semaphores.clear();

        if (!m_command_buffers.empty() && m_command_pool) {
            vkFreeCommandBuffers(
                reinterpret_cast<VkDevice>(m_device),
                reinterpret_cast<VkCommandPool>(m_command_pool),
                static_cast<uint32_t>(m_command_buffers.size()),
                reinterpret_cast<VkCommandBuffer*>(m_command_buffers.data())
            );
            m_command_buffers.clear();
        }

        for (auto fb : m_swapchain_framebuffers) {
            if (fb) {
                vkDestroyFramebuffer(reinterpret_cast<VkDevice>(m_device), reinterpret_cast<VkFramebuffer>(fb), nullptr);
            }
        }
        m_swapchain_framebuffers.clear();

        if (m_render_pass) {
            vkDestroyRenderPass(reinterpret_cast<VkDevice>(m_device), reinterpret_cast<VkRenderPass>(m_render_pass), nullptr);
            m_render_pass = nullptr;
        }

        for (auto iv : m_swapchain_image_views) {
            if (iv) {
                vkDestroyImageView(reinterpret_cast<VkDevice>(m_device), reinterpret_cast<VkImageView>(iv), nullptr);
            }
        }
        m_swapchain_image_views.clear();

        vkDestroySwapchainKHR(reinterpret_cast<VkDevice>(m_device), reinterpret_cast<VkSwapchainKHR>(m_swapchain), nullptr);
        m_swapchain = nullptr;

        m_swapchain_images.clear();
    }

    void VulkanContext::destroy() {
        HN_PROFILE_FUNCTION();

        HN_CORE_INFO("VulkanContext::destroy");

        if (m_device) {
            vkDeviceWaitIdle(reinterpret_cast<VkDevice>(m_device));

            for (uint32_t i = 0; i < m_image_available_semaphores.size(); i++) {
                if (m_image_available_semaphores[i]) {
                    vkDestroySemaphore(reinterpret_cast<VkDevice>(m_device), reinterpret_cast<VkSemaphore>(m_image_available_semaphores[i]), nullptr);
                }
                if (m_in_flight_fences[i]) {
                    vkDestroyFence(reinterpret_cast<VkDevice>(m_device), reinterpret_cast<VkFence>(m_in_flight_fences[i]), nullptr);
                }
            }

            for (uint32_t i = 0; i < m_render_finished_semaphores.size(); i++) {
                if (m_render_finished_semaphores[i]) {
                    vkDestroySemaphore(reinterpret_cast<VkDevice>(m_device), reinterpret_cast<VkSemaphore>(m_render_finished_semaphores[i]), nullptr);
                }
            }

            m_image_available_semaphores.clear();
            m_render_finished_semaphores.clear();
            m_in_flight_fences.clear();

            cleanup_pipeline();
            cleanup_swapchain();
            cleanup_global_descriptor_resources();

            if (m_command_pool) {
                vkDestroyCommandPool(reinterpret_cast<VkDevice>(m_device), reinterpret_cast<VkCommandPool>(m_command_pool), nullptr);
                m_command_pool = nullptr;
            }
        }

        if (m_surface && m_instance) {
            vkDestroySurfaceKHR(reinterpret_cast<VkInstance>(m_instance), reinterpret_cast<VkSurfaceKHR>(m_surface), nullptr);
            m_surface = nullptr;
        }

        if (m_backend) {
            m_backend->release_queue_lease(m_queue_lease);
        }
        m_queue_lease = {};

        // DO NOT destroy VkDevice/VkInstance here: backend owns them.
        m_device = nullptr;
        m_physical_device = nullptr;
        m_instance = nullptr;

        m_initialized = false;
    }

    std::string VulkanContext::shader_path(const char* filename) const {
        // User-specified runtime location:
        // assets folder can be found at ../../../assets/shaders
        return (std::string("../../../assets/shaders/") + filename);
    }

    void VulkanContext::cleanup_pipeline() {
        if (!m_device)
            return;

        m_pipeline_quad.destroy(reinterpret_cast<VkDevice>(m_device));
        m_pipeline_quad_fb.destroy(reinterpret_cast<VkDevice>(m_device));
    }

    void VulkanContext::refresh_all_texture_samplers() {
        // no-op now
    }

    void VulkanContext::create_graphics_pipeline() {
        HN_PROFILE_FUNCTION();

        HN_CORE_ASSERT(m_device, "create_graphics_pipeline called without device");
        HN_CORE_ASSERT(m_render_pass, "create_graphics_pipeline called without render pass");
        HN_CORE_ASSERT(m_global_set_layout, "create_graphics_pipeline called without global descriptor set layout");

        static ShaderCache cache; // uses default cache directory
        const auto glsl = std::filesystem::path("../assets/shaders/Renderer2D_Quad.glsl");
        const auto spirv = cache.get_or_compile_spirv_paths(glsl);

        auto& rs = Settings::get().renderer;

        PipelineSpec spec;
        spec.shaderGLSLPath = glsl;
        spec.topology = PrimitiveTopology::Triangles;
        spec.cullMode = CullMode::None;
        spec.frontFace = FrontFaceWinding::CounterClockwise;
        spec.depthStencil.depthTest  = rs.depth_test;
        spec.depthStencil.depthWrite = rs.depth_write;
        spec.passType = RenderPassType::Swapchain;
        spec.wireframe = rs.wireframe;

        // Swapchain subpass has exactly 1 color attachment
        spec.perColorAttachmentBlend.clear();
        AttachmentBlendState colorBlend{};
        colorBlend.enabled = rs.blending;
        spec.perColorAttachmentBlend.push_back(colorBlend);

        // Vertex bindings must match Renderer2D VA layout and shader locations
        VertexInputBindingSpec static_binding;
        static_binding.layout = BufferLayout{
                    { ShaderDataType::Float2, "a_local_pos" },  // loc 0
                    { ShaderDataType::Float2, "a_local_tex" }   // loc 1
        };

        VertexInputBindingSpec instance_binding;
        instance_binding.layout = BufferLayout{
                    { ShaderDataType::Float3, "i_center",       false, true }, // loc 2
                    { ShaderDataType::Float2, "i_half_size",    false, true }, // loc 3
                    { ShaderDataType::Float , "i_rotation",     false, true }, // loc 4
                    { ShaderDataType::Float4, "i_color",        false, true }, // loc 5
                    { ShaderDataType::Int,   "i_tex_index",     false, true }, // loc 6
                    { ShaderDataType::Float, "i_tiling",        false, true }, // loc 7
                    { ShaderDataType::Float2,"i_tex_coord_min", false, true }, // loc 8
                    { ShaderDataType::Float2,"i_tex_coord_max", false, true }, // loc 9
                    { ShaderDataType::Int,   "i_entity_id",     false, true }  // loc 10
        };

        spec.vertexBindings.push_back(static_binding);
        spec.vertexBindings.push_back(instance_binding);

        // Swapchain pipeline
        m_pipeline_quad.create(
            reinterpret_cast<VkDevice>(m_device),
            reinterpret_cast<VkRenderPass>(m_render_pass),
            reinterpret_cast<VkDescriptorSetLayout>(m_global_set_layout),
            spirv.vertex.string(),
            spirv.fragment.string(),
            spec
        );

        m_last_pipeline_spec = spec;
        m_pipeline_dirty = false;

    }
} // namespace Honey