#include "hnpch.h"
#include "vk_backend.h"

#include <algorithm>
#include <cstring>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "imgui_impl_vulkan.h"

namespace Honey {

    // -----------------------------
    // Local helpers
    // -----------------------------

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
        VkDebugUtilsMessageTypeFlagsEXT /*type*/,
        const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
        void* /*user_data*/
    ) {
        const char* msg = (callback_data && callback_data->pMessage) ? callback_data->pMessage : "<no message>";

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
        if (fn)
            fn(instance, messenger, allocator);
    }

    static bool check_validation_layer_support_local() {
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

    static std::vector<const char*> get_required_instance_extensions_local() {
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

    static bool device_supports_extensions(VkPhysicalDevice device, const std::vector<const char*>& required) {
        uint32_t count = 0;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
        std::vector<VkExtensionProperties> exts(count);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &count, exts.data());

        std::set<std::string> needed;
        for (auto* e : required) needed.insert(e);

        for (const auto& ext : exts) {
            needed.erase(ext.extensionName);
        }

        return needed.empty();
    }

    static std::optional<uint32_t> find_graphics_family_no_surface(VkPhysicalDevice device) {
        uint32_t queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, nullptr);
        std::vector<VkQueueFamilyProperties> families(queue_family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, families.data());

        for (uint32_t i = 0; i < queue_family_count; i++) {
            if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
                return i;
        }
        return std::nullopt;
    }

    // -----------------------------
    // VulkanBackend
    // -----------------------------

    VulkanBackend::~VulkanBackend() {
        shutdown();
    }

    void VulkanBackend::init() {
        if (m_initialized) return;

        HN_CORE_INFO("VulkanBackend::init");
        HN_CORE_ASSERT(glfwVulkanSupported(), "GLFW reports Vulkan is not supported!");

        create_instance();
        setup_debug_messenger();

        // Defer physical device + logical device until first surface (present support needs a surface).
        m_initialized = true;
    }

    void VulkanBackend::shutdown() {
        if (!m_initialized) return;

        HN_CORE_INFO("VulkanBackend::shutdown");

        m_initialized = false;

        VkDevice device = VK_NULL_HANDLE;
        {
            std::scoped_lock lock(m_pool_mutex);
            device = m_device;
        }

        if (device) {
            vkDeviceWaitIdle(device);

            shutdown_imgui_resources();

            if (m_sampler_nearest) {
                vkDestroySampler(device, m_sampler_nearest, nullptr);
                m_sampler_nearest = VK_NULL_HANDLE;
            }
            if (m_sampler_linear) {
                vkDestroySampler(device, m_sampler_linear, nullptr);
                m_sampler_linear = VK_NULL_HANDLE;
            }
            if (m_sampler_aniso) {
                vkDestroySampler(device, m_sampler_aniso, nullptr);
                m_sampler_aniso = VK_NULL_HANDLE;
            }

            // Destroys fence/command pool used by immediate_submit()
            shutdown_upload_context();

            {
                std::scoped_lock lock(m_pool_mutex);
                if (m_device) {
                    vkDestroyDevice(m_device, nullptr);
                    m_device = VK_NULL_HANDLE;
                }

                m_graphics_queues.clear();
                m_present_queues.clear();
                m_free_graphics_indices.clear();
                m_free_present_indices.clear();
            }
        } else {
            std::scoped_lock lock(m_pool_mutex);
            m_graphics_queues.clear();
            m_present_queues.clear();
            m_free_graphics_indices.clear();
            m_free_present_indices.clear();
        }

        m_physical_device = VK_NULL_HANDLE;
        m_families = {};

        destroy_debug_messenger();

        if (m_instance) {
            vkDestroyInstance(m_instance, nullptr);
            m_instance = VK_NULL_HANDLE;
        }

        m_initialized = false;
    }

    VulkanQueueLease VulkanBackend::acquire_queue_lease(VkSurfaceKHR surface) {
        HN_CORE_ASSERT(m_initialized, "VulkanBackend not initialized");
        HN_CORE_ASSERT(surface, "acquire_queue_lease: surface is null");

        std::scoped_lock lock(m_pool_mutex);

        // First surface triggers physical device selection + device creation.
        if (!m_device) {
            pick_physical_device();

            QueueFamilyInfo surfaceFamilies = find_queue_families(m_physical_device, surface);
            HN_CORE_ASSERT(surfaceFamilies.graphicsFamily != UINT32_MAX, "No graphics family found for surface");
            HN_CORE_ASSERT(surfaceFamilies.presentFamily != UINT32_MAX, "No present family found for surface");

            m_families.graphicsFamily = surfaceFamilies.graphicsFamily;
            m_families.presentFamily = surfaceFamilies.presentFamily;

            create_logical_device(m_families, k_desired_queues_per_family);

            // Device now exists; create upload context once.
            init_upload_context();
            init_imgui_resources();
        }

        // For every surface (including first), compute the best present family:
        // prefer graphics family if it can present to this surface; otherwise use any present-capable family.
        QueueFamilyInfo surfaceFamilies = find_queue_families(m_physical_device, surface);
        HN_CORE_ASSERT(surfaceFamilies.graphicsFamily != UINT32_MAX, "Surface has no graphics family");
        HN_CORE_ASSERT(surfaceFamilies.presentFamily != UINT32_MAX, "Surface has no present family");

        HN_CORE_ASSERT(surfaceFamilies.graphicsFamily == m_families.graphicsFamily,
                       "Surface requires different graphics queue family than device was created with");

        VulkanQueueLease lease{};
        lease.graphicsFamily = m_families.graphicsFamily;

        // Present family for this surface may differ from the device's initial present family.
        // For now: we only support present from the family we created queues for.
        // If you need true per-surface present family flexibility, expand backend to keep pools per family.
        HN_CORE_ASSERT(surfaceFamilies.presentFamily == m_families.presentFamily,
                       "Surface requires a different present family than backend was initialized with. "
                       "Next step: support per-family present queue pools.");

        lease.presentFamily = m_families.presentFamily;

        // Try unique graphics
        if (!m_free_graphics_indices.empty()) {
            lease.graphicsQueueIndex = m_free_graphics_indices.back();
            m_free_graphics_indices.pop_back();
            lease.graphicsQueue = m_graphics_queues[lease.graphicsQueueIndex];
            lease.sharedGraphics = false;
            lease.graphicsSubmitMutex = nullptr;
        } else {
            lease.graphicsQueueIndex = 0;
            lease.graphicsQueue = m_graphics_queues.empty() ? VK_NULL_HANDLE : m_graphics_queues[0];
            lease.sharedGraphics = true;
            lease.graphicsSubmitMutex = &m_shared_graphics_mutex;
        }

        // Try unique present
        if (!m_free_present_indices.empty()) {
            lease.presentQueueIndex = m_free_present_indices.back();
            m_free_present_indices.pop_back();
            lease.presentQueue = m_present_queues[lease.presentQueueIndex];
            lease.sharedPresent = false;
            lease.presentSubmitMutex = nullptr;
        } else {
            lease.presentQueueIndex = 0;
            lease.presentQueue = m_present_queues.empty() ? VK_NULL_HANDLE : m_present_queues[0];
            lease.sharedPresent = true;
            lease.presentSubmitMutex = &m_shared_present_mutex;
        }

        HN_CORE_ASSERT(lease.graphicsQueue, "Failed to acquire graphics queue");
        HN_CORE_ASSERT(lease.presentQueue, "Failed to acquire present queue");

        return lease;
    }

    void VulkanBackend::init_upload_context() {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(m_device, "init_upload_context called without device");

        if (m_upload_command_pool || m_upload_fence || m_upload_queue) {
            // Already initialized
            return;
        }

        HN_CORE_ASSERT(!m_graphics_queues.empty() && m_graphics_queues[0], "No graphics queue available for upload context");

        m_upload_queue = m_graphics_queues[0];

        VkCommandPoolCreateInfo pool_ci{};
        pool_ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool_ci.queueFamilyIndex = m_families.graphicsFamily;

        VkResult r = vkCreateCommandPool(m_device, &pool_ci, nullptr, &m_upload_command_pool);
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkCreateCommandPool failed for upload context");

        VkFenceCreateInfo fence_ci{};
        fence_ci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_ci.flags = 0;

        r = vkCreateFence(m_device, &fence_ci, nullptr, &m_upload_fence);
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkCreateFence failed for upload context");
    }

    void VulkanBackend::shutdown_upload_context() {
        if (!m_device)
            return;

        // Serialize against any in-flight immediate_submit()
        std::scoped_lock lk(m_upload_mutex);

        if (m_upload_fence) {
            vkDestroyFence(m_device, m_upload_fence, nullptr);
            m_upload_fence = VK_NULL_HANDLE;
        }
        if (m_upload_command_pool) {
            vkDestroyCommandPool(m_device, m_upload_command_pool, nullptr);
            m_upload_command_pool = VK_NULL_HANDLE;
        }
        m_upload_queue = VK_NULL_HANDLE;
    }

    void VulkanBackend::immediate_submit(const std::function<void(VkCommandBuffer)>& record) {
        HN_CORE_ASSERT(m_device, "immediate_submit requires a valid VkDevice");
        HN_CORE_ASSERT(m_upload_command_pool && m_upload_fence && m_upload_queue,
                       "immediate_submit called before upload context was initialized");
        HN_CORE_ASSERT(record, "immediate_submit record callback is empty");

        std::scoped_lock lk(m_upload_mutex);

        VkCommandBufferAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc.commandPool = m_upload_command_pool;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 1;

        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkResult r = vkAllocateCommandBuffers(m_device, &alloc, &cmd);
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkAllocateCommandBuffers failed (immediate_submit)");

        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        r = vkBeginCommandBuffer(cmd, &begin);
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkBeginCommandBuffer failed (immediate_submit)");

        record(cmd);

        r = vkEndCommandBuffer(cmd);
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkEndCommandBuffer failed (immediate_submit)");

        vkResetFences(m_device, 1, &m_upload_fence);

        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;

        // Protect shared queue usage (queue index 0) with the existing shared graphics mutex
        {
            std::scoped_lock qlk(m_shared_graphics_mutex);
            r = vkQueueSubmit(m_upload_queue, 1, &submit, m_upload_fence);
        }
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkQueueSubmit failed (immediate_submit)");

        r = vkWaitForFences(m_device, 1, &m_upload_fence, VK_TRUE, UINT64_MAX);
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkWaitForFences failed (immediate_submit)");

        vkFreeCommandBuffers(m_device, m_upload_command_pool, 1, &cmd);
    }

    void VulkanBackend::release_queue_lease(const VulkanQueueLease& lease) {
        std::scoped_lock lock(m_pool_mutex);

        if (!lease.sharedGraphics && lease.graphicsQueueIndex != UINT32_MAX) {
            m_free_graphics_indices.push_back(lease.graphicsQueueIndex);
        }
        if (!lease.sharedPresent && lease.presentQueueIndex != UINT32_MAX) {
            m_free_present_indices.push_back(lease.presentQueueIndex);
        }
    }

    VkCommandBuffer VulkanBackend::begin_single_time_commands() {
            HN_CORE_ASSERT(m_device, "begin_single_time_commands requires valid device");
            HN_CORE_ASSERT(m_upload_command_pool, "begin_single_time_commands requires upload command pool");

            VkCommandBufferAllocateInfo alloc{};
            alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            alloc.commandPool = m_upload_command_pool;
            alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            alloc.commandBufferCount = 1;

            VkCommandBuffer cmd = VK_NULL_HANDLE;
            VkResult r = vkAllocateCommandBuffers(m_device, &alloc, &cmd);
            HN_CORE_ASSERT(r == VK_SUCCESS, "vkAllocateCommandBuffers failed (begin_single_time_commands)");

            VkCommandBufferBeginInfo begin{};
            begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

            r = vkBeginCommandBuffer(cmd, &begin);
            HN_CORE_ASSERT(r == VK_SUCCESS, "vkBeginCommandBuffer failed (begin_single_time_commands)");

            return cmd;
        }

        void VulkanBackend::end_single_time_commands(VkCommandBuffer cmd) {
            HN_CORE_ASSERT(m_device, "end_single_time_commands requires valid device");
            HN_CORE_ASSERT(m_upload_fence && m_upload_queue, "end_single_time_commands requires upload context");
            HN_CORE_ASSERT(cmd, "end_single_time_commands called with null command buffer");

            VkResult r = vkEndCommandBuffer(cmd);
            HN_CORE_ASSERT(r == VK_SUCCESS, "vkEndCommandBuffer failed (end_single_time_commands)");

            vkResetFences(m_device, 1, &m_upload_fence);

            VkSubmitInfo submit{};
            submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &cmd;

            {
                std::scoped_lock qlk(m_shared_graphics_mutex);
                r = vkQueueSubmit(m_upload_queue, 1, &submit, m_upload_fence);
            }
            HN_CORE_ASSERT(r == VK_SUCCESS, "vkQueueSubmit failed (end_single_time_commands)");

            r = vkWaitForFences(m_device, 1, &m_upload_fence, VK_TRUE, UINT64_MAX);
            HN_CORE_ASSERT(r == VK_SUCCESS, "vkWaitForFences failed (end_single_time_commands)");

            vkFreeCommandBuffers(m_device, m_upload_command_pool, 1, &cmd);
        }

        void VulkanBackend::init_imgui_resources() {
        HN_PROFILE_FUNCTION();
        if (!m_device)
            return;

        // Make sure Dear ImGui's Vulkan backend knows how to call Vulkan functions.
        // We use vkGetInstanceProcAddr to resolve all required entry points.
        {
            const uint32_t api_version = VK_API_VERSION_1_3; // or ImGui_ImplVulkan_GetDefaultApiVersion()
            bool ok = ImGui_ImplVulkan_LoadFunctions(
                api_version,
                [](const char* name, void* user_data) -> PFN_vkVoidFunction {
                    VkInstance instance = reinterpret_cast<VkInstance>(user_data);
                    return vkGetInstanceProcAddr(instance, name);
                },
                reinterpret_cast<void*>(m_instance)
            );
            HN_CORE_ASSERT(ok, "ImGui_ImplVulkan_LoadFunctions failed");
        }

        // Descriptor pool for ImGui. This is straight from Dear ImGui examples (with minor tweaks).
        {
            VkDescriptorPoolSize pool_sizes[] = {
                { VK_DESCRIPTOR_TYPE_SAMPLER,                1000 },
                { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
                { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          1000 },
                { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1000 },
                { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,   1000 },
                { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,   1000 },
                { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1000 },
                { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         1000 },
                { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
                { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
                { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,       1000 }
            };

            VkDescriptorPoolCreateInfo pool_info{};
            pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
            pool_info.maxSets = 1000 * (uint32_t)(sizeof(pool_sizes) / sizeof(pool_sizes[0]));
            pool_info.poolSizeCount = (uint32_t)(sizeof(pool_sizes) / sizeof(pool_sizes[0]));
            pool_info.pPoolSizes = pool_sizes;

            VkResult r = vkCreateDescriptorPool(m_device, &pool_info, nullptr, &m_imgui_descriptor_pool);
            HN_CORE_ASSERT(r == VK_SUCCESS, "vkCreateDescriptorPool failed for ImGui: {0}", vk_result_to_string(r));
        }

        {
            VkSamplerCreateInfo si{};
            si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            si.magFilter = VK_FILTER_LINEAR;
            si.minFilter = VK_FILTER_LINEAR;
            si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            si.anisotropyEnable = VK_FALSE;
            si.maxAnisotropy = 1.0f;
            si.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
            si.unnormalizedCoordinates = VK_FALSE;
            si.compareEnable = VK_FALSE;
            si.compareOp = VK_COMPARE_OP_ALWAYS;
            si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            si.mipLodBias = 0.0f;
            si.minLod = 0.0f;
            si.maxLod = 0.0f;

            VkResult r = vkCreateSampler(m_device, &si, nullptr, &m_imgui_sampler);
            HN_CORE_ASSERT(r == VK_SUCCESS, "vkCreateSampler failed for ImGui sampler: {0}", vk_result_to_string(r));
        }

        // We no longer create a separate ImGui render pass or per‑frame ImGui command buffer here.
        // The main VulkanContext command buffer and swapchain render pass are used instead.
        m_imgui_initialized = true;
    }

    void VulkanBackend::shutdown_imgui_resources() {
        if (!m_device || !m_imgui_initialized)
            return;

        if (m_imgui_sampler) {
            vkDestroySampler(m_device, m_imgui_sampler, nullptr);
            m_imgui_sampler = VK_NULL_HANDLE;
        }

        if (m_imgui_descriptor_pool) {
            vkDestroyDescriptorPool(m_device, m_imgui_descriptor_pool, nullptr);
            m_imgui_descriptor_pool = VK_NULL_HANDLE;
        }

        m_imgui_initialized = false;
    }

    void VulkanBackend::render_imgui_on_current_swapchain_image(VkCommandBuffer cmd,
                                                                        VkImageView /*target_view*/,
                                                                        VkExtent2D /*extent*/) {
        HN_CORE_ASSERT(m_device, "render_imgui_on_current_swapchain_image: device is null");

        ImDrawData* draw_data = ImGui::GetDrawData();
        if (!draw_data || draw_data->CmdListsCount == 0)
            return;

        // Let Dear ImGui's Vulkan backend record draw calls into our command buffer.
        // We pass VK_NULL_HANDLE as pipeline; the backend will use its internally created pipeline.
        ImGui_ImplVulkan_RenderDrawData(draw_data, cmd, VK_NULL_HANDLE);
    }

    // -----------------------------
    // Core init helpers
    // -----------------------------

    void VulkanBackend::create_instance() {
        HN_PROFILE_FUNCTION();

        HN_CORE_ASSERT(glfwVulkanSupported(), "GLFW reports Vulkan is not supported on this system!");

        if (k_enable_validation) {
            HN_CORE_ASSERT(check_validation_layer_support_local(),
                           "Vulkan validation requested, but VK_LAYER_KHRONOS_validation is not available.");
        }

        std::vector<const char*> extensions = get_required_instance_extensions_local();

        VkApplicationInfo app_info{};
        app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app_info.pApplicationName = "Honey";
        app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
        app_info.pEngineName = "Honey";
        app_info.engineVersion = VK_MAKE_VERSION(0, 1, 0);
        app_info.apiVersion = VK_API_VERSION_1_3;

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

            create_info.pNext = &debug_ci;
        } else {
            create_info.enabledLayerCount = 0;
            create_info.pNext = nullptr;
        }

        VkInstance instance = VK_NULL_HANDLE;
        VkResult res = vkCreateInstance(&create_info, nullptr, &instance);
        HN_CORE_ASSERT(res == VK_SUCCESS, "vkCreateInstance failed: {0}", vk_result_to_string(res));

        m_instance = instance;
        HN_CORE_INFO("Vulkan instance created.");
    }

    void VulkanBackend::setup_debug_messenger() {
        if (!k_enable_validation)
            return;

        HN_CORE_ASSERT(m_instance, "setup_debug_messenger called without instance");

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
        VkResult res = create_debug_utils_messenger_ext(m_instance, &ci, nullptr, &messenger);
        HN_CORE_ASSERT(res == VK_SUCCESS, "Failed to create Vulkan debug messenger: {0}", vk_result_to_string(res));

        m_debug_messenger = messenger;
        HN_CORE_INFO("Vulkan debug messenger created.");
    }

    void VulkanBackend::destroy_debug_messenger() {
        if (!m_debug_messenger || !m_instance)
            return;

        destroy_debug_utils_messenger_ext(m_instance, m_debug_messenger, nullptr);
        m_debug_messenger = VK_NULL_HANDLE;
    }

    void VulkanBackend::pick_physical_device() {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(m_instance, "pick_physical_device called without instance");

        uint32_t device_count = 0;
        vkEnumeratePhysicalDevices(m_instance, &device_count, nullptr);
        HN_CORE_ASSERT(device_count > 0, "No Vulkan physical devices found.");

        std::vector<VkPhysicalDevice> devices(device_count);
        vkEnumeratePhysicalDevices(m_instance, &device_count, devices.data());

        const std::vector<const char*> required_device_exts = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };

        VkPhysicalDevice selected = VK_NULL_HANDLE;

        for (auto dev : devices) {
            if (!device_supports_extensions(dev, required_device_exts))
                continue;

            auto gfx = find_graphics_family_no_surface(dev);
            if (!gfx.has_value())
                continue;

            selected = dev;
            break;
        }

        HN_CORE_ASSERT(selected, "Failed to find a suitable Vulkan physical device (swapchain + graphics queue required).");

        m_physical_device = selected;
        HN_CORE_INFO("Selected Vulkan physical device.");
    }

    VulkanBackend::QueueFamilyInfo VulkanBackend::find_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface) {
        QueueFamilyInfo info{};
        info.graphicsFamily = UINT32_MAX;
        info.presentFamily  = UINT32_MAX;

        uint32_t queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, nullptr);
        std::vector<VkQueueFamilyProperties> families(queue_family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, families.data());

        for (uint32_t i = 0; i < queue_family_count; i++) {
            if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && info.graphicsFamily == UINT32_MAX) {
                info.graphicsFamily = i;
            }

            VkBool32 present_support = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present_support);
            if (present_support && info.presentFamily == UINT32_MAX) {
                info.presentFamily = i;
            }

            if (info.graphicsFamily != UINT32_MAX && info.presentFamily != UINT32_MAX)
                break;
        }

        return info;
    }

    void VulkanBackend::create_logical_device(const QueueFamilyInfo& families, uint32_t desiredQueuesPerFamily) {
        HN_PROFILE_FUNCTION();

        HN_CORE_ASSERT(m_physical_device, "create_logical_device called without physical device");

        uint32_t queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(m_physical_device, &queue_family_count, nullptr);
        std::vector<VkQueueFamilyProperties> props(queue_family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(m_physical_device, &queue_family_count, props.data());

        auto clamp_count = [&](uint32_t family) -> uint32_t {
            HN_CORE_ASSERT(family < props.size(), "Queue family index out of range");
            const uint32_t available = props[family].queueCount;
            return std::max(1u, std::min(desiredQueuesPerFamily, available));
        };

        const bool same_family = (families.graphicsFamily == families.presentFamily);

        uint32_t graphics_count = clamp_count(families.graphicsFamily);
        uint32_t present_count  = same_family ? graphics_count : clamp_count(families.presentFamily);

        const uint32_t max_count = std::max(graphics_count, present_count);
        std::vector<float> priorities(max_count, 1.0f);

        std::vector<VkDeviceQueueCreateInfo> queue_infos;
        queue_infos.reserve(same_family ? 1 : 2);

        VkDeviceQueueCreateInfo q0{};
        q0.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        q0.queueFamilyIndex = families.graphicsFamily;
        q0.queueCount = graphics_count;
        q0.pQueuePriorities = priorities.data();
        queue_infos.push_back(q0);

        if (!same_family) {
            VkDeviceQueueCreateInfo q1{};
            q1.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            q1.queueFamilyIndex = families.presentFamily;
            q1.queueCount = present_count;
            q1.pQueuePriorities = priorities.data();
            queue_infos.push_back(q1);
        }

        VkPhysicalDeviceProperties device_props{};
        vkGetPhysicalDeviceProperties(m_physical_device, &device_props);

        VkPhysicalDeviceFeatures features{};
        features.independentBlend = VK_TRUE;
        features.samplerAnisotropy = VK_TRUE;
        m_max_anisotropy = device_props.limits.maxSamplerAnisotropy;
        HN_CORE_INFO("Max Anisotropy: {0}", m_max_anisotropy);
        features.fillModeNonSolid = VK_TRUE;

        const std::vector<const char*> device_extensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };

        VkDeviceCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        create_info.queueCreateInfoCount = static_cast<uint32_t>(queue_infos.size());
        create_info.pQueueCreateInfos = queue_infos.data();
        create_info.pEnabledFeatures = &features;
        create_info.enabledExtensionCount = static_cast<uint32_t>(device_extensions.size());
        create_info.ppEnabledExtensionNames = device_extensions.data();

        create_info.enabledLayerCount = 0;
        create_info.ppEnabledLayerNames = nullptr;

        VkDevice device = VK_NULL_HANDLE;
        VkResult res = vkCreateDevice(m_physical_device, &create_info, nullptr, &device);
        HN_CORE_ASSERT(res == VK_SUCCESS, "vkCreateDevice failed: {0}", vk_result_to_string(res));

        m_device = device;

        m_graphics_queues.clear();
        m_graphics_queues.resize(graphics_count, VK_NULL_HANDLE);
        for (uint32_t i = 0; i < graphics_count; i++) {
            vkGetDeviceQueue(m_device, families.graphicsFamily, i, &m_graphics_queues[i]);
            HN_CORE_ASSERT(m_graphics_queues[i], "vkGetDeviceQueue returned null graphics queue");
        }

        m_present_queues.clear();
        if (same_family) {
            m_present_queues = m_graphics_queues;
        } else {
            m_present_queues.resize(present_count, VK_NULL_HANDLE);
            for (uint32_t i = 0; i < present_count; i++) {
                vkGetDeviceQueue(m_device, families.presentFamily, i, &m_present_queues[i]);
                HN_CORE_ASSERT(m_present_queues[i], "vkGetDeviceQueue returned null present queue");
            }
        }

        m_free_graphics_indices.clear();
        for (uint32_t i = 1; i < m_graphics_queues.size(); i++) {
            m_free_graphics_indices.push_back(i);
        }

        m_free_present_indices.clear();
        for (uint32_t i = 1; i < m_present_queues.size(); i++) {
            m_free_present_indices.push_back(i);
        }

        HN_CORE_INFO("Vulkan logical device created. Graphics queues: {0}, Present queues: {1}",
                     (uint32_t)m_graphics_queues.size(), (uint32_t)m_present_queues.size());

        {
                VkSamplerCreateInfo si{};
                si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
                si.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                si.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                si.borderColor  = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
                si.unnormalizedCoordinates = VK_FALSE;
                si.compareEnable = VK_FALSE;
                si.compareOp     = VK_COMPARE_OP_ALWAYS;
                si.mipLodBias    = 0.0f;
                si.minLod        = 0.0f;
                si.maxLod        = 0.0f;

                // Nearest
                si.magFilter  = VK_FILTER_NEAREST;
                si.minFilter  = VK_FILTER_NEAREST;
                si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
                si.anisotropyEnable = VK_FALSE;
                si.maxAnisotropy    = 1.0f;

                res = vkCreateSampler(m_device, &si, nullptr, &m_sampler_nearest);
                HN_CORE_ASSERT(res == VK_SUCCESS, "vkCreateSampler failed for m_sampler_nearest: {0}", vk_result_to_string(res));

                // Linear
                si.magFilter  = VK_FILTER_LINEAR;
                si.minFilter  = VK_FILTER_LINEAR;
                si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
                si.anisotropyEnable = VK_FALSE;
                si.maxAnisotropy    = 1.0f;

                res = vkCreateSampler(m_device, &si, nullptr, &m_sampler_linear);
                HN_CORE_ASSERT(res == VK_SUCCESS, "vkCreateSampler failed for m_sampler_linear: {0}", vk_result_to_string(res));

                // Anisotropic (linear filtering + anisotropy)
                si.magFilter  = VK_FILTER_LINEAR;
                si.minFilter  = VK_FILTER_LINEAR;
                si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
                si.anisotropyEnable = VK_TRUE;
                si.maxAnisotropy    = std::max(1.0f, m_max_anisotropy);

                res = vkCreateSampler(m_device, &si, nullptr, &m_sampler_aniso);
                HN_CORE_ASSERT(res == VK_SUCCESS, "vkCreateSampler failed for m_sampler_aniso: {0}", vk_result_to_string(res));
            }
        }

} // namespace Honey