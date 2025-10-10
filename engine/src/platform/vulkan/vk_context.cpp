#include "hnpch.h"
#include "vk_context.h"
#include "vk_swapchain.h"

#include <set>

#include "GLFW/glfw3.h"

namespace Honey {

    VulkanContext* VulkanContext::s_instance = nullptr;

    VulkanContext::VulkanContext(GLFWwindow *window_handle)
        : m_window_handle(window_handle) {
        HN_CORE_ASSERT(m_window_handle, "Window handle is null!");
        s_instance = this;
    }

    VulkanContext::~VulkanContext() {
        cleanup();
    }

    void VulkanContext::init() {
        HN_PROFILE_FUNCTION();

        create_instance();
        if (m_enable_validation_layers) setup_debug_messenger();
        create_surface();
        pick_physical_device();
        create_logical_device();
        create_command_pool();

        // Create swapchain
        m_swapchain = std::make_unique<VulkanSwapchain>(m_physical_device, m_device, m_surface, m_window_handle);
        m_swapchain->create();

        // Create command buffers and synchronization objects
        create_command_buffers();
        create_sync_objects();

        // Create swapchain render pass and framebuffers
        create_swapchain_framebuffers();

        HN_CORE_INFO("Vulkan context initialized!");
    }

    VkFramebuffer VulkanContext::get_current_swapchain_framebuffer() const {
        return m_swapchain_framebuffers[m_current_image_index];
    }

    VkRenderPass VulkanContext::get_swapchain_render_pass() const {
        return m_swapchain_render_pass;
    }

    void VulkanContext::begin_frame() {
        HN_PROFILE_FUNCTION();

        // Wait for the fence of the current frame
        vkWaitForFences(m_device, 1, &m_in_flight_fences[m_current_frame], VK_TRUE, UINT64_MAX);

        // Acquire next image from swapchain
        VkResult result = m_swapchain->acquire_next_image(&m_current_image_index, m_image_available_semaphores[m_current_frame]);

        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            m_swapchain->recreate();
            return;
        } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            HN_CORE_ASSERT(false, "Failed to acquire swap chain image!");
        }

        // Reset fence only if we're submitting work
        vkResetFences(m_device, 1, &m_in_flight_fences[m_current_frame]);

        // Reset command buffer
        vkResetCommandBuffer(m_command_buffers[m_current_frame], 0);

        // Begin command buffer recording
        VkCommandBufferBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = 0;
        begin_info.pInheritanceInfo = nullptr;

        VkResult cmd_result = vkBeginCommandBuffer(m_command_buffers[m_current_frame], &begin_info);
        HN_CORE_ASSERT(cmd_result == VK_SUCCESS, "Failed to begin recording command buffer!");
    }

    void VulkanContext::end_frame() {
        HN_PROFILE_FUNCTION();

        // End command buffer recording
        VkResult result = vkEndCommandBuffer(m_command_buffers[m_current_frame]);
        HN_CORE_ASSERT(result == VK_SUCCESS, "Failed to record command buffer!");

        // Submit command buffer
        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkSemaphore wait_semaphores[] = { m_image_available_semaphores[m_current_frame] };
        VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = wait_semaphores;
        submit_info.pWaitDstStageMask = wait_stages;

        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &m_command_buffers[m_current_frame];

        VkSemaphore signal_semaphores[] = { m_render_finished_semaphores[m_current_frame] };
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = signal_semaphores;

        result = vkQueueSubmit(m_graphics_queue, 1, &submit_info, m_in_flight_fences[m_current_frame]);
        HN_CORE_ASSERT(result == VK_SUCCESS, "Failed to submit draw command buffer!");

        // Present
        result = m_swapchain->present(m_current_image_index, m_render_finished_semaphores[m_current_frame], m_present_queue);

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            m_swapchain->recreate();
        } else if (result != VK_SUCCESS) {
            HN_CORE_ASSERT(false, "Failed to present swap chain image!");
        }

        m_current_frame = (m_current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    void VulkanContext::swap_buffers() {
        HN_PROFILE_FUNCTION();
        // Swap buffers is now handled by begin_frame/end_frame
        // This is kept for compatibility with the GraphicsContext interface
    }


    void VulkanContext::create_instance() {
        HN_PROFILE_FUNCTION();

        VkApplicationInfo app_info{};
        app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app_info.pApplicationName = "Honey Applicaton";
        app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        app_info.pEngineName = "Honey Engine";
        app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        app_info.apiVersion = VK_API_VERSION_1_0;

        VkInstanceCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        create_info.pApplicationInfo = &app_info;

        uint32_t glfw_extension_count = 0;
        const char** glfw_extensions;
        glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);

        std::vector<const char*> extensions(glfw_extensions, glfw_extensions + glfw_extension_count);

        if (m_enable_validation_layers) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        create_info.ppEnabledExtensionNames = extensions.data();

        // Set up validation layers
        if (m_enable_validation_layers) {
            HN_CORE_ASSERT(check_validation_layer_support(), "Validation layers requested but not available!");
            create_info.enabledLayerCount = static_cast<uint32_t>(m_validation_layers.size());
            create_info.ppEnabledLayerNames = m_validation_layers.data();
        } else {
            create_info.enabledLayerCount = 0;
        }

        VkResult result = vkCreateInstance(&create_info, nullptr, &m_instance);
        HN_CORE_ASSERT(result == VK_SUCCESS, "Failed to create instance!");
    }


    void VulkanContext::setup_debug_messenger() {
        // will do this later
    }

    void VulkanContext::create_surface() {
        VkResult result = glfwCreateWindowSurface(m_instance, m_window_handle, nullptr, &m_surface);
        HN_CORE_ASSERT(result == VK_SUCCESS, "Failed to create window surface!");
    }

    void VulkanContext::pick_physical_device() {
        uint32_t device_count = 0;
        vkEnumeratePhysicalDevices(m_instance, &device_count, nullptr);

        HN_CORE_ASSERT(device_count > 0, "Failed to find GPUs with Vulkan support!");

        std::vector<VkPhysicalDevice> devices(device_count);
        vkEnumeratePhysicalDevices(m_instance, &device_count, devices.data());

        int best_score = -1;
        for (const auto& device : devices) {
            int score = rate_device_suitability(device);
            if (score > best_score) {
                best_score = score;
                m_physical_device = device;
            }
        }

        HN_CORE_ASSERT(m_physical_device != VK_NULL_HANDLE, "Failed to find a suitable GPU!");


        VkPhysicalDeviceProperties device_properties;
        vkGetPhysicalDeviceProperties(m_physical_device, &device_properties);

        HN_CORE_INFO("Selected GPU: {0}", device_properties.deviceName);
    }

    int VulkanContext::rate_device_suitability(VkPhysicalDevice device) {
        VkPhysicalDeviceProperties device_properties;
        VkPhysicalDeviceFeatures device_features;
        vkGetPhysicalDeviceProperties(device, &device_properties);
        vkGetPhysicalDeviceFeatures(device, &device_features);

        int score = 0;

        // Discrete GPUs have a significant performance advantage
        if (device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            score += 1000;
        }

        // Maximum possible size of textures affects graphics quality
        score += device_properties.limits.maxImageDimension2D;

        // Application can't function without required features
        if (!device_features.geometryShader) {
            return 0; // Disqualify this device
        }

        // Check if device supports required queue families
        QueueFamilyIndices indices = find_queue_families(device);
        if (!indices.is_complete()) {
            return 0; // Disqualify this device
        }

        // Check for swapchain support
        bool extensions_supported = check_device_extension_support(device);
        if (!extensions_supported) {
            return 0; // Disqualify this device
        }

        // Check swapchain adequacy
        bool swapchain_adequate = false;
        if (extensions_supported) {
            SwapchainSupportDetails swapchain_support = query_swapchain_support(device);
            swapchain_adequate = !swapchain_support.formats.empty() &&
                               !swapchain_support.present_modes.empty();
        }

        if (!swapchain_adequate) {
            return 0; // Disqualify this device
        }

        return score;
    }

    void VulkanContext::create_logical_device() {
        QueueFamilyIndices indices = find_queue_families(m_physical_device);

        std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
        std::set<uint32_t> unique_queue_families = {
            indices.graphics_family.value(),
            indices.present_family.value()
        };

        float queue_priority = 1.0f;
        for (uint32_t queue_family : unique_queue_families) {
            VkDeviceQueueCreateInfo queue_create_info{};
            queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queue_create_info.queueFamilyIndex = queue_family;
            queue_create_info.queueCount = 1;
            queue_create_info.pQueuePriorities = &queue_priority;
            queue_create_infos.push_back(queue_create_info);
        }

        VkPhysicalDeviceFeatures device_features{};
        // Enable features you need here

        VkDeviceCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        create_info.queueCreateInfoCount = static_cast<uint32_t>(queue_create_infos.size());
        create_info.pQueueCreateInfos = queue_create_infos.data();
        create_info.pEnabledFeatures = &device_features;

        create_info.enabledExtensionCount = static_cast<uint32_t>(m_device_extensions.size());
        create_info.ppEnabledExtensionNames = m_device_extensions.data();

        if (m_enable_validation_layers) {
            create_info.enabledLayerCount = static_cast<uint32_t>(m_validation_layers.size());
            create_info.ppEnabledLayerNames = m_validation_layers.data();
        } else {
            create_info.enabledLayerCount = 0;
        }

        VkResult result = vkCreateDevice(m_physical_device, &create_info, nullptr, &m_device);
        HN_CORE_ASSERT(result == VK_SUCCESS, "Failed to create logical device!");

        vkGetDeviceQueue(m_device, indices.graphics_family.value(), 0, &m_graphics_queue);
        vkGetDeviceQueue(m_device, indices.present_family.value(), 0, &m_present_queue);
    }

    void VulkanContext::create_command_pool() {
        HN_PROFILE_FUNCTION();

        QueueFamilyIndices queue_family_indices = find_queue_families(m_physical_device);

        VkCommandPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool_info.queueFamilyIndex = queue_family_indices.graphics_family.value();

        VkResult result = vkCreateCommandPool(m_device, &pool_info, nullptr, &m_command_pool);
        HN_CORE_ASSERT(result == VK_SUCCESS, "Failed to create command pool!");
    }

    void VulkanContext::create_command_buffers() {
        HN_PROFILE_FUNCTION();

        m_command_buffers.resize(MAX_FRAMES_IN_FLIGHT);

        VkCommandBufferAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.commandPool = m_command_pool;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandBufferCount = static_cast<uint32_t>(m_command_buffers.size());

        VkResult result = vkAllocateCommandBuffers(m_device, &alloc_info, m_command_buffers.data());
        HN_CORE_ASSERT(result == VK_SUCCESS, "Failed to allocate command buffers!");
    }

    void VulkanContext::create_sync_objects() {
        HN_PROFILE_FUNCTION();

        m_image_available_semaphores.resize(MAX_FRAMES_IN_FLIGHT);
        m_render_finished_semaphores.resize(MAX_FRAMES_IN_FLIGHT);
        m_in_flight_fences.resize(MAX_FRAMES_IN_FLIGHT);

        VkSemaphoreCreateInfo semaphore_info{};
        semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fence_info{};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            VkResult sem1_result = vkCreateSemaphore(m_device, &semaphore_info, nullptr, &m_image_available_semaphores[i]);
            VkResult sem2_result = vkCreateSemaphore(m_device, &semaphore_info, nullptr, &m_render_finished_semaphores[i]);
            VkResult fence_result = vkCreateFence(m_device, &fence_info, nullptr, &m_in_flight_fences[i]);

            HN_CORE_ASSERT(sem1_result == VK_SUCCESS && sem2_result == VK_SUCCESS && fence_result == VK_SUCCESS,
                          "Failed to create synchronization objects for a frame!");
        }
    }

    void VulkanContext::create_swapchain_framebuffers() {
        HN_PROFILE_FUNCTION();

        const auto& swapchain_image_views = m_swapchain->get_image_views();
        VkExtent2D extent = m_swapchain->get_extent();

        // Create simple render pass for swapchain
        VkAttachmentDescription color_attachment{};
        color_attachment.format = m_swapchain->get_image_format();
        color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference color_attachment_ref{};
        color_attachment_ref.attachment = 0;
        color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_attachment_ref;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo render_pass_info{};
        render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        render_pass_info.attachmentCount = 1;
        render_pass_info.pAttachments = &color_attachment;
        render_pass_info.subpassCount = 1;
        render_pass_info.pSubpasses = &subpass;
        render_pass_info.dependencyCount = 1;
        render_pass_info.pDependencies = &dependency;

        VkResult result = vkCreateRenderPass(m_device, &render_pass_info, nullptr, &m_swapchain_render_pass);
        HN_CORE_ASSERT(result == VK_SUCCESS, "Failed to create swapchain render pass!");

        // Create framebuffers for each swapchain image
        m_swapchain_framebuffers.resize(swapchain_image_views.size());

        for (size_t i = 0; i < swapchain_image_views.size(); i++) {
            VkImageView attachments[] = { swapchain_image_views[i] };

            VkFramebufferCreateInfo framebuffer_info{};
            framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebuffer_info.renderPass = m_swapchain_render_pass;
            framebuffer_info.attachmentCount = 1;
            framebuffer_info.pAttachments = attachments;
            framebuffer_info.width = extent.width;
            framebuffer_info.height = extent.height;
            framebuffer_info.layers = 1;

            result = vkCreateFramebuffer(m_device, &framebuffer_info, nullptr, &m_swapchain_framebuffers[i]);
            HN_CORE_ASSERT(result == VK_SUCCESS, "Failed to create swapchain framebuffer!");
        }
    }

    void VulkanContext::cleanup() {
        if (m_device) {
            vkDeviceWaitIdle(m_device);
        }

        // Cleanup synchronization objects
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            if (m_render_finished_semaphores.size() > i) {
                vkDestroySemaphore(m_device, m_render_finished_semaphores[i], nullptr);
            }
            if (m_image_available_semaphores.size() > i) {
                vkDestroySemaphore(m_device, m_image_available_semaphores[i], nullptr);
            }
            if (m_in_flight_fences.size() > i) {
                vkDestroyFence(m_device, m_in_flight_fences[i], nullptr);
            }
        }

        // Cleanup swapchain framebuffers
        for (auto framebuffer : m_swapchain_framebuffers) {
            vkDestroyFramebuffer(m_device, framebuffer, nullptr);
        }
        m_swapchain_framebuffers.clear();

        if (m_swapchain_render_pass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(m_device, m_swapchain_render_pass, nullptr);
        }

        m_swapchain.reset();

        if (m_command_pool) {
            vkDestroyCommandPool(m_device, m_command_pool, nullptr);
        }

        if (m_device) {
            vkDestroyDevice(m_device, nullptr);
        }

        if (m_surface) {
            vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        }

        if (m_enable_validation_layers && m_debug_messenger) {
            // Will need to implement vkDestroyDebugUtilsMessengerEXT
        }

        if (m_instance) {
            vkDestroyInstance(m_instance, nullptr);
        }
    }

    VulkanContext::QueueFamilyIndices VulkanContext::find_queue_families(VkPhysicalDevice device) {
        QueueFamilyIndices indices;

        uint32_t queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, nullptr);

        std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, queue_families.data());

        int i = 0;
        for (const auto& queue_family : queue_families) {
            if (queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                indices.graphics_family = i;
            }

            VkBool32 present_support = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &present_support);

            if (present_support) {
                indices.present_family = i;
            }

            if (indices.is_complete()) {
                break;
            }

            i++;
        }

        return indices;
    }

    bool VulkanContext::check_device_extension_support(VkPhysicalDevice device) {
        uint32_t extension_count;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, nullptr);

        std::vector<VkExtensionProperties> available_extensions(extension_count);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, available_extensions.data());

        std::set<std::string> required_extensions(m_device_extensions.begin(), m_device_extensions.end());

        for (const auto& extension : available_extensions) {
            required_extensions.erase(extension.extensionName);
        }

        return required_extensions.empty();
    }

    VulkanContext::SwapchainSupportDetails VulkanContext::query_swapchain_support(VkPhysicalDevice device) {
        SwapchainSupportDetails details;

        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_surface, &details.capabilities);

        uint32_t format_count;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &format_count, nullptr);

        if (format_count != 0) {
            details.formats.resize(format_count);
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &format_count, details.formats.data());
        }

        uint32_t present_mode_count;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &present_mode_count, nullptr);

        if (present_mode_count != 0) {
            details.present_modes.resize(present_mode_count);
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &present_mode_count, details.present_modes.data());
        }

        return details;
    }

    bool VulkanContext::check_validation_layer_support() {
        uint32_t layer_count;
        vkEnumerateInstanceLayerProperties(&layer_count, nullptr);

        std::vector<VkLayerProperties> available_layers(layer_count);
        vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());

        for (const char* layer_name : m_validation_layers) {
            bool layer_found = false;
            for (const auto& layer_properties : available_layers) {
                if (strcmp(layer_name, layer_properties.layerName) == 0) {
                    layer_found = true;
                    break;
                }
            }
            if (!layer_found) {
                HN_CORE_WARN("Validation layer {} not available!", layer_name);
                return false;
            }
        }
        return true;
    }

}
