#include "hnpch.h"
#include "vk_context.h"

#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <vector>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include "vk_buffer.h"
#include "vk_texture.h"
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

    static VkSurfaceFormatKHR choose_surface_format(const std::vector<VkSurfaceFormatKHR>& formats) {
        for (const auto& f : formats) {
            if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                return f;
        }
        // Fallback
        return formats.front();
    }

    static VkPresentModeKHR choose_present_mode(const std::vector<VkPresentModeKHR>& modes) {
        // MAILBOX is triple-buffer-like, great if available. FIFO is guaranteed.
        for (auto m : modes) {
            if (m == VK_PRESENT_MODE_MAILBOX_KHR)
                return m;
        }
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

    VulkanContext::VulkanContext(GLFWwindow* window_handle)
        : m_window_handle(window_handle) {
        HN_CORE_ASSERT(m_window_handle, "VulkanContext: window handle is null!");
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

        // binding 1 => texture sampler array (fragment)
        VkDescriptorSetLayoutBinding tex_binding{};
        tex_binding.binding = 1;
        tex_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        tex_binding.descriptorCount = VulkanRendererAPI::k_max_texture_slots;
        tex_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding bindings[] = { ubo_binding, tex_binding };

        VkDescriptorSetLayoutCreateInfo layout_ci{};
        layout_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_ci.bindingCount = 2;
        layout_ci.pBindings = bindings;

        VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
        VkResult r = vkCreateDescriptorSetLayout(reinterpret_cast<VkDevice>(m_device), &layout_ci, nullptr, &set_layout);
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkCreateDescriptorSetLayout failed");
        m_global_set_layout = reinterpret_cast<VkDescriptorSetLayout>(set_layout);

        // Descriptor pool sized for frames-in-flight:
        VkDescriptorPoolSize pool_sizes[2]{};
        pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_sizes[0].descriptorCount = k_max_frames_in_flight;

        pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_sizes[1].descriptorCount = k_max_frames_in_flight * VulkanRendererAPI::k_max_texture_slots;

        VkDescriptorPoolCreateInfo pool_ci{};
        pool_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_ci.maxSets = k_max_frames_in_flight;
        pool_ci.poolSizeCount = 2;
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
            ai.memoryTypeIndex = find_memory_type_local(reinterpret_cast<VkPhysicalDevice>(m_physical_device),
                                                        req.memoryTypeBits,
                                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

            VkDeviceMemory mem = VK_NULL_HANDLE;
            r = vkAllocateMemory(reinterpret_cast<VkDevice>(m_device), &ai, nullptr, &mem);
            HN_CORE_ASSERT(r == VK_SUCCESS, "vkAllocateMemory (camera ubo) failed");

            r = vkBindBufferMemory(reinterpret_cast<VkDevice>(m_device), ubo, mem, 0);
            HN_CORE_ASSERT(r == VK_SUCCESS, "vkBindBufferMemory (camera ubo) failed");

            m_camera_ubos[frame] = reinterpret_cast<void*>(ubo);
            m_camera_ubo_memories[frame] = reinterpret_cast<void*>(mem);

            // Write binding 0 (UBO) now; binding 1 (textures) will be updated per frame when Renderer2D submits them.
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

        create_instance();
        create_surface();

        pick_physical_device();
        create_logical_device();

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

        recreate_swapchain_if_needed();

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


        // Wait for the specific swapchain image to become available (if it’s still in flight)
        HN_CORE_ASSERT(image_index < m_images_in_flight.size(), "Acquired image index out of bounds.");
        VkFence image_fence = reinterpret_cast<VkFence>(m_images_in_flight[image_index]);
        if (image_fence != VK_NULL_HANDLE) {
            vkWaitForFences(reinterpret_cast<VkDevice>(m_device), 1, &image_fence, VK_TRUE, UINT64_MAX);
        }

        // Mark this image as now being in flight with the current frame fence
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

        VkResult submit_res = vkQueueSubmit(
            reinterpret_cast<VkQueue>(m_graphics_queue),
            1,
            &submit_info,
            in_flight
        );
        HN_CORE_ASSERT(submit_res == VK_SUCCESS, "vkQueueSubmit failed: {0}", vk_result_to_string(submit_res));

        VkSwapchainKHR swapchains[] = { reinterpret_cast<VkSwapchainKHR>(m_swapchain) };

        VkPresentInfoKHR present_info{};
        present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores = signal_semaphores;
        present_info.swapchainCount = 1;
        present_info.pSwapchains = swapchains;
        present_info.pImageIndices = &image_index;

        VkResult present_res = vkQueuePresentKHR(reinterpret_cast<VkQueue>(m_present_queue), &present_info);
        if (present_res == VK_ERROR_OUT_OF_DATE_KHR || present_res == VK_SUBOPTIMAL_KHR) {
            m_framebuffer_resized = true;
            recreate_swapchain_if_needed();
        } else {
            HN_CORE_ASSERT(present_res == VK_SUCCESS, "vkQueuePresentKHR failed: {0}", vk_result_to_string(present_res));
        }

        m_current_frame = (m_current_frame + 1) % k_max_frames_in_flight;
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

    bool VulkanContext::check_validation_layer_support() const {
        uint32_t layer_count = 0;
        vkEnumerateInstanceLayerProperties(&layer_count, nullptr);

        std::vector<VkLayerProperties> available(layer_count);
        vkEnumerateInstanceLayerProperties(&layer_count, available.data());

        for (const char* requested : k_validation_layers) {
            bool found = false;
            for (const auto& layer : available) {
                if (std::strcmp(requested, layer.layerName) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found)
                return false;
        }
        return true;
    }

    std::vector<const char*> VulkanContext::get_required_instance_extensions() const {
        uint32_t glfw_extension_count = 0;
        const char** glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);
        HN_CORE_ASSERT(glfw_extensions && glfw_extension_count > 0,
                       "Failed to get required Vulkan instance extensions from GLFW");

        std::vector<const char*> extensions(glfw_extensions, glfw_extensions + glfw_extension_count);

        if (k_enable_validation) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        return extensions;
    }

    void VulkanContext::setup_debug_messenger() {
        if (!k_enable_validation)
            return;

        VkDebugUtilsMessengerCreateInfoEXT ci{};
        ci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        ci.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        ci.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        ci.pfnUserCallback = vk_debug_callback;

        VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
        VkResult res = create_debug_utils_messenger_ext(
            reinterpret_cast<VkInstance>(m_instance),
            &ci,
            nullptr,
            &messenger
        );
        HN_CORE_ASSERT(res == VK_SUCCESS, "Failed to create Vulkan debug messenger: {0}", vk_result_to_string(res));

        m_debug_messenger = reinterpret_cast<VkDebugUtilsMessengerEXT>(messenger);
        HN_CORE_INFO("Vulkan debug messenger created.");
    }

    void VulkanContext::destroy_debug_messenger() {
        if (!m_debug_messenger || !m_instance)
            return;

        destroy_debug_utils_messenger_ext(
            reinterpret_cast<VkInstance>(m_instance),
            reinterpret_cast<VkDebugUtilsMessengerEXT>(m_debug_messenger),
            nullptr
        );
        m_debug_messenger = nullptr;
    }

    void VulkanContext::create_instance() {
        HN_PROFILE_FUNCTION();

        HN_CORE_ASSERT(glfwVulkanSupported(), "GLFW reports Vulkan is not supported on this system!");

        if (k_enable_validation) {
            HN_CORE_ASSERT(check_validation_layer_support(),
                           "Vulkan validation requested, but VK_LAYER_KHRONOS_validation is not available.");
        }

        std::vector<const char*> extensions = get_required_instance_extensions();

        VkApplicationInfo app_info{};
        app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app_info.pApplicationName = "Honey_Editor";
        app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
        app_info.pEngineName = "Honey";
        app_info.engineVersion = VK_MAKE_VERSION(0, 1, 0);
        app_info.apiVersion = VK_API_VERSION_1_0;

        VkInstanceCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        create_info.pApplicationInfo = &app_info;
        create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        create_info.ppEnabledExtensionNames = extensions.data();

        VkDebugUtilsMessengerCreateInfoEXT debug_ci{};
        if (k_enable_validation) {
            create_info.enabledLayerCount = static_cast<uint32_t>(k_validation_layers.size());
            create_info.ppEnabledLayerNames = k_validation_layers.data();

            debug_ci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            debug_ci.messageSeverity =
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            debug_ci.messageType =
                VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            debug_ci.pfnUserCallback = vk_debug_callback;

            // This allows validation messages during vkCreateInstance/vkDestroyInstance too.
            create_info.pNext = &debug_ci;
        } else {
            create_info.enabledLayerCount = 0;
            create_info.pNext = nullptr;
        }

        VkInstance instance = VK_NULL_HANDLE;
        VkResult res = vkCreateInstance(&create_info, nullptr, &instance);
        HN_CORE_ASSERT(res == VK_SUCCESS, "vkCreateInstance failed: {0}", vk_result_to_string(res));

        m_instance = reinterpret_cast<VkInstance>(instance);
        HN_CORE_INFO("Vulkan instance created.");

        setup_debug_messenger();
    }

    void VulkanContext::create_surface() {
        HN_PROFILE_FUNCTION();

        VkSurfaceKHR surface = VK_NULL_HANDLE;
        VkResult res = glfwCreateWindowSurface(
            reinterpret_cast<VkInstance>(m_instance),
            m_window_handle,
            nullptr,
            &surface
        );
        HN_CORE_ASSERT(res == VK_SUCCESS, "glfwCreateWindowSurface failed: {0}", vk_result_to_string(res));

        m_surface = reinterpret_cast<VkSurfaceKHR>(surface);
        HN_CORE_INFO("Vulkan surface created.");
    }

    void VulkanContext::pick_physical_device() {
        HN_PROFILE_FUNCTION();

        uint32_t device_count = 0;
        vkEnumeratePhysicalDevices(reinterpret_cast<VkInstance>(m_instance), &device_count, nullptr);
        HN_CORE_ASSERT(device_count > 0, "No Vulkan physical devices found.");

        std::vector<VkPhysicalDevice> devices(device_count);
        vkEnumeratePhysicalDevices(reinterpret_cast<VkInstance>(m_instance), &device_count, devices.data());

        for (auto dev : devices) {
            QueueFamilyIndices q = find_queue_families(dev, reinterpret_cast<VkSurfaceKHR>(m_surface));
            if (!q.complete())
                continue;

            SwapchainSupportDetails sc = query_swapchain_support(dev, reinterpret_cast<VkSurfaceKHR>(m_surface));
            if (sc.formats.empty() || sc.present_modes.empty())
                continue;

            m_physical_device = reinterpret_cast<VkPhysicalDevice>(dev);
            m_graphics_queue_family = q.graphics.value();
            m_present_queue_family = q.present.value();
            break;
        }

        HN_CORE_ASSERT(m_physical_device, "Failed to find a suitable Vulkan physical device.");
        HN_CORE_INFO("Selected Vulkan physical device.");
    }

    void VulkanContext::create_logical_device() {
        HN_PROFILE_FUNCTION();

        std::set<uint32_t> unique_families = { m_graphics_queue_family, m_present_queue_family };

        float priority = 1.0f;
        std::vector<VkDeviceQueueCreateInfo> queue_infos;
        queue_infos.reserve(unique_families.size());
        for (uint32_t family : unique_families) {
            VkDeviceQueueCreateInfo q{};
            q.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            q.queueFamilyIndex = family;
            q.queueCount = 1;
            q.pQueuePriorities = &priority;
            queue_infos.push_back(q);
        }

        VkPhysicalDeviceFeatures features{};

        const char* device_extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

        VkDeviceCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        create_info.queueCreateInfoCount = static_cast<uint32_t>(queue_infos.size());
        create_info.pQueueCreateInfos = queue_infos.data();
        create_info.pEnabledFeatures = &features;
        create_info.enabledExtensionCount = 1;
        create_info.ppEnabledExtensionNames = device_extensions;
        create_info.enabledLayerCount = 0;

        VkDevice device = VK_NULL_HANDLE;
        VkResult res = vkCreateDevice(reinterpret_cast<VkPhysicalDevice>(m_physical_device), &create_info, nullptr, &device);
        HN_CORE_ASSERT(res == VK_SUCCESS, "vkCreateDevice failed: {0}", vk_result_to_string(res));

        m_device = reinterpret_cast<VkDevice>(device);

        VkQueue graphics_queue = VK_NULL_HANDLE;
        VkQueue present_queue = VK_NULL_HANDLE;
        vkGetDeviceQueue(reinterpret_cast<VkDevice>(m_device), m_graphics_queue_family, 0, &graphics_queue);
        vkGetDeviceQueue(reinterpret_cast<VkDevice>(m_device), m_present_queue_family, 0, &present_queue);

        m_graphics_queue = reinterpret_cast<VkQueue>(graphics_queue);
        m_present_queue = reinterpret_cast<VkQueue>(present_queue);

        HN_CORE_INFO("Vulkan logical device created.");
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

        VkCommandBuffer cmd = reinterpret_cast<VkCommandBuffer>(m_command_buffers[image_index]);

        vkResetCommandBuffer(cmd, 0);

        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        VkResult res = vkBeginCommandBuffer(cmd, &begin);
        HN_CORE_ASSERT(res == VK_SUCCESS, "vkBeginCommandBuffer failed: {0}", vk_result_to_string(res));

        glm::vec4 clear = VulkanRendererAPI::consume_clear_color();
        VkClearValue clear_value{};
        clear_value.color = { { clear.r, clear.g, clear.b, clear.a } };

        VkRenderPassBeginInfo rp_begin{};
        rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp_begin.renderPass = reinterpret_cast<VkRenderPass>(m_render_pass);
        rp_begin.framebuffer = reinterpret_cast<VkFramebuffer>(m_swapchain_framebuffers[image_index]);
        rp_begin.renderArea.offset = { 0, 0 };
        rp_begin.renderArea.extent = { m_swapchain_extent_width, m_swapchain_extent_height };
        rp_begin.clearValueCount = 1;
        rp_begin.pClearValues = &clear_value;

        vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

        HN_CORE_ASSERT(m_pipeline, "Vulkan pipeline is null (did create_graphics_pipeline run?)");
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, reinterpret_cast<VkPipeline>(m_pipeline));

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(m_swapchain_extent_width);
        viewport.height = static_cast<float>(m_swapchain_extent_height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = { 0, 0 };
        scissor.extent = { m_swapchain_extent_width, m_swapchain_extent_height };
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        const uint32_t frame = m_current_frame;
        VkDescriptorSet ds = reinterpret_cast<VkDescriptorSet>(m_global_descriptor_sets[frame]);

        // Update camera UBO from per-frame state (submitted by Renderer2D)
        if (m_camera_ubo_memories[frame] && m_camera_ubo_size == sizeof(glm::mat4)) {
            glm::mat4 vp{1.0f};
            if (VulkanRendererAPI::consume_camera_view_projection(vp)) {
                void* mapped = nullptr;
                VkResult mr = vkMapMemory(reinterpret_cast<VkDevice>(m_device),
                                          reinterpret_cast<VkDeviceMemory>(m_camera_ubo_memories[frame]),
                                          0, m_camera_ubo_size, 0, &mapped);
                HN_CORE_ASSERT(mr == VK_SUCCESS, "vkMapMemory camera ubo failed");
                std::memcpy(mapped, &vp, sizeof(glm::mat4));
                vkUnmapMemory(reinterpret_cast<VkDevice>(m_device),
                              reinterpret_cast<VkDeviceMemory>(m_camera_ubo_memories[frame]));
            }
        }

        // Update texture descriptors for this frame (binding 1) if Renderer2D submitted a list.
        std::array<void*, VulkanRendererAPI::k_max_texture_slots> submitted{};
        uint32_t submitted_count = 0;
        if (VulkanRendererAPI::consume_bound_textures(submitted, submitted_count)) {
            HN_CORE_ASSERT(submitted_count > 0 && submitted[0], "Vulkan: expected texture slot 0 (white texture) to be present");

            // Fill all 32 slots; unused slots fallback to slot 0
            VkDescriptorImageInfo infos[VulkanRendererAPI::k_max_texture_slots]{};

            auto* white_base = reinterpret_cast<Texture2D*>(submitted[0]);
            auto* white_vk = dynamic_cast<VulkanTexture2D*>(white_base);
            HN_CORE_ASSERT(white_vk, "Vulkan: slot 0 texture is not a VulkanTexture2D");

            for (uint32_t i = 0; i < VulkanRendererAPI::k_max_texture_slots; ++i) {
                void* raw = (i < submitted_count) ? submitted[i] : submitted[0];
                auto* base = reinterpret_cast<Texture2D*>(raw);
                auto* vktex = dynamic_cast<VulkanTexture2D*>(base);
                if (!vktex) {
                    vktex = white_vk;
                }

                infos[i].sampler = reinterpret_cast<VkSampler>(vktex->get_vk_sampler());
                infos[i].imageView = reinterpret_cast<VkImageView>(vktex->get_vk_image_view());
                infos[i].imageLayout = static_cast<VkImageLayout>(vktex->get_vk_image_layout());
            }

            VkWriteDescriptorSet write_tex{};
            write_tex.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write_tex.dstSet = ds;
            write_tex.dstBinding = 1;
            write_tex.dstArrayElement = 0;
            write_tex.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write_tex.descriptorCount = VulkanRendererAPI::k_max_texture_slots;
            write_tex.pImageInfo = infos;

            vkUpdateDescriptorSets(reinterpret_cast<VkDevice>(m_device), 1, &write_tex, 0, nullptr);
        }

        vkCmdBindDescriptorSets(cmd,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                reinterpret_cast<VkPipelineLayout>(m_pipeline_layout),
                                0,
                                1,
                                &ds,
                                0,
                                nullptr);


        Ref<VertexArray> va;
        uint32_t requested_index_count = 0;
        uint32_t requested_instance_count = 0;
        if (VulkanRendererAPI::consume_draw_request(va, requested_index_count, requested_instance_count) && va) {

            const auto& vbs = va->get_vertex_buffers();
            const auto& ib = va->get_index_buffer();

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

            const uint32_t index_count = (requested_index_count != 0) ? requested_index_count : vk_ib->get_count();
            const uint32_t instance_count = (requested_instance_count != 0) ? requested_instance_count : 1;
            vkCmdDrawIndexed(cmd, index_count, instance_count, 0, 0, 0);
        }

        vkCmdEndRenderPass(cmd);

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
        if (!m_instance)
            return;

        HN_PROFILE_FUNCTION();

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

            vkDestroyDevice(reinterpret_cast<VkDevice>(m_device), nullptr);
            m_device = nullptr;
        }

        if (m_surface) {
            vkDestroySurfaceKHR(reinterpret_cast<VkInstance>(m_instance), reinterpret_cast<VkSurfaceKHR>(m_surface), nullptr);
            m_surface = nullptr;
        }

        destroy_debug_messenger();

        vkDestroyInstance(reinterpret_cast<VkInstance>(m_instance), nullptr);
        m_instance = nullptr;

        m_initialized = false;
    }

    std::string VulkanContext::shader_path(const char* filename) const {
        // User-specified runtime location:
        // assets folder can be found at ../../../assets/shaders
        return (std::string("../../../assets/shaders/") + filename);
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
        HN_CORE_INFO("Loaded SPIR-V: {0} ({1} bytes), magic=0x{2:X}",
                     path, static_cast<uint64_t>(size), data[0]);

        // SPIR-V magic number is 0x07230203
        HN_CORE_ASSERT(data[0] == 0x07230203u, "Invalid SPIR-V magic for file: {0}", path);

        return data;
    }

    VkShaderModule VulkanContext::create_shader_module_from_file(const std::string& path) {
        auto code = read_spirv_u32_file(path);

        VkShaderModuleCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ci.codeSize = code.size() * sizeof(uint32_t);
        ci.pCode = code.data();

        VkShaderModule module = VK_NULL_HANDLE;
        VkResult res = vkCreateShaderModule(reinterpret_cast<VkDevice>(m_device), &ci, nullptr, &module);
        HN_CORE_ASSERT(res == VK_SUCCESS, "vkCreateShaderModule failed for {0}", path);
        return module;
    }

    void VulkanContext::cleanup_pipeline() {
        if (!m_device)
            return;

        if (m_pipeline) {
            vkDestroyPipeline(reinterpret_cast<VkDevice>(m_device), reinterpret_cast<VkPipeline>(m_pipeline), nullptr);
            m_pipeline = nullptr;
        }
        if (m_pipeline_layout) {
            vkDestroyPipelineLayout(reinterpret_cast<VkDevice>(m_device), reinterpret_cast<VkPipelineLayout>(m_pipeline_layout), nullptr);
            m_pipeline_layout = nullptr;
        }
        if (m_vert_module) {
            vkDestroyShaderModule(reinterpret_cast<VkDevice>(m_device), reinterpret_cast<VkShaderModule>(m_vert_module), nullptr);
            m_vert_module = nullptr;
        }
        if (m_frag_module) {
            vkDestroyShaderModule(reinterpret_cast<VkDevice>(m_device), reinterpret_cast<VkShaderModule>(m_frag_module), nullptr);
            m_frag_module = nullptr;
        }
    }

    void VulkanContext::create_graphics_pipeline() {
        HN_PROFILE_FUNCTION();

        HN_CORE_ASSERT(m_device, "create_graphics_pipeline called without device");
        HN_CORE_ASSERT(m_render_pass, "create_graphics_pipeline called without render pass");

        cleanup_pipeline();

        const std::string vert = shader_path("quad.vert.spv");
        const std::string frag = shader_path("quad.frag.spv");

        m_vert_module = create_shader_module_from_file(vert);
        m_frag_module = create_shader_module_from_file(frag);

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

        // binding 0: static quad vertices
        VkVertexInputBindingDescription binding0{};
        binding0.binding = 0;
        binding0.stride = sizeof(float) * 4; // vec2 pos + vec2 uv
        binding0.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        // binding 1: per-instance QuadInstance layout (MUST match Renderer2D::QuadInstance packing)
        VkVertexInputBindingDescription binding1{};
        binding1.binding = 1;
        binding1.stride =
            sizeof(float) * (3 + 2 + 1 + 4) + // center + half + rot + color
            sizeof(int)   * (1 + 1) +         // tex_index + entity_id
            sizeof(float) * (1) +             // tiling
            sizeof(float) * (2 + 2);          // tex_coord_min/max
        binding1.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

        VkVertexInputBindingDescription bindings[] = { binding0, binding1 };

        // locations:
        // 0: a_local_pos (vec2)
        // 1: a_local_tex (vec2)
        // 2: i_center (vec3)
        // 3: i_half_size (vec2)
        // 4: i_rotation (float)
        // 5: i_color (vec4)
        // 6: i_tex_index (int)
        // 7: i_tiling (float)
        // 8: i_tex_coord_min (vec2)
        // 9: i_tex_coord_max (vec2)
        // 10: i_entity_id (int)

        VkVertexInputAttributeDescription attrs[11]{};

        attrs[0].location = 0;
        attrs[0].binding = 0;
        attrs[0].format = VK_FORMAT_R32G32_SFLOAT;
        attrs[0].offset = 0;

        attrs[1].location = 1;
        attrs[1].binding = 0;
        attrs[1].format = VK_FORMAT_R32G32_SFLOAT;
        attrs[1].offset = sizeof(float) * 2;

        uint32_t off = 0;

        attrs[2].location = 2;
        attrs[2].binding = 1;
        attrs[2].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[2].offset = off;
        off += sizeof(float) * 3;

        attrs[3].location = 3;
        attrs[3].binding = 1;
        attrs[3].format = VK_FORMAT_R32G32_SFLOAT;
        attrs[3].offset = off;
        off += sizeof(float) * 2;

        attrs[4].location = 4;
        attrs[4].binding = 1;
        attrs[4].format = VK_FORMAT_R32_SFLOAT;
        attrs[4].offset = off;
        off += sizeof(float) * 1;

        attrs[5].location = 5;
        attrs[5].binding = 1;
        attrs[5].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attrs[5].offset = off;
        off += sizeof(float) * 4;

        attrs[6].location = 6;
        attrs[6].binding = 1;
        attrs[6].format = VK_FORMAT_R32_SINT;
        attrs[6].offset = off;
        off += sizeof(int) * 1;

        attrs[7].location = 7;
        attrs[7].binding = 1;
        attrs[7].format = VK_FORMAT_R32_SFLOAT;
        attrs[7].offset = off;
        off += sizeof(float) * 1;

        attrs[8].location = 8;
        attrs[8].binding = 1;
        attrs[8].format = VK_FORMAT_R32G32_SFLOAT;
        attrs[8].offset = off;
        off += sizeof(float) * 2;

        attrs[9].location = 9;
        attrs[9].binding = 1;
        attrs[9].format = VK_FORMAT_R32G32_SFLOAT;
        attrs[9].offset = off;
        off += sizeof(float) * 2;

        attrs[10].location = 10;
        attrs[10].binding = 1;
        attrs[10].format = VK_FORMAT_R32_SINT;
        attrs[10].offset = off;

        VkPipelineVertexInputStateCreateInfo vertex_input{};
        vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertex_input.vertexBindingDescriptionCount = 2;
        vertex_input.pVertexBindingDescriptions = bindings;
        vertex_input.vertexAttributeDescriptionCount = 11;
        vertex_input.pVertexAttributeDescriptions = attrs;

        VkPipelineInputAssemblyStateCreateInfo input_assembly{};
        input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
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
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.lineWidth = 1.0f;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.depthBiasEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo msaa{};
        msaa.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        msaa.sampleShadingEnable = VK_FALSE;

        VkPipelineColorBlendAttachmentState blend_att{};
        blend_att.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blend_att.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo blend{};
        blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend.logicOpEnable = VK_FALSE;
        blend.attachmentCount = 1;
        blend.pAttachments = &blend_att;

        VkPipelineLayoutCreateInfo layout_ci{};
        layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

        VkDescriptorSetLayout set_layouts[] = { reinterpret_cast<VkDescriptorSetLayout>(m_global_set_layout) };
        layout_ci.setLayoutCount = 1;
        layout_ci.pSetLayouts = set_layouts;

        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkResult layout_res = vkCreatePipelineLayout(reinterpret_cast<VkDevice>(m_device), &layout_ci, nullptr, &layout);
        HN_CORE_ASSERT(layout_res == VK_SUCCESS, "vkCreatePipelineLayout failed");
        m_pipeline_layout = reinterpret_cast<VkPipelineLayout>(layout);

        VkGraphicsPipelineCreateInfo pipe{};
        pipe.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipe.stageCount = 2;
        pipe.pStages = stages;
        pipe.pVertexInputState = &vertex_input;
        pipe.pInputAssemblyState = &input_assembly;
        pipe.pViewportState = &viewport_state;
        pipe.pRasterizationState = &raster;
        pipe.pMultisampleState = &msaa;
        pipe.pDepthStencilState = nullptr;
        pipe.pColorBlendState = &blend;
        pipe.pDynamicState = &dynamic_state;
        pipe.layout = reinterpret_cast<VkPipelineLayout>(m_pipeline_layout);
        pipe.renderPass = reinterpret_cast<VkRenderPass>(m_render_pass);
        pipe.subpass = 0;

        VkPipeline pipeline = VK_NULL_HANDLE;
        VkResult pr = vkCreateGraphicsPipelines(reinterpret_cast<VkDevice>(m_device), VK_NULL_HANDLE, 1, &pipe, nullptr, &pipeline);
        HN_CORE_ASSERT(pr == VK_SUCCESS, "vkCreateGraphicsPipelines failed");
        m_pipeline = reinterpret_cast<VkPipeline>(pipeline);

        HN_CORE_INFO("Vulkan pipeline created (quad-min shaders: {0}, {1})", vert, frag);
    }



} // namespace Honey