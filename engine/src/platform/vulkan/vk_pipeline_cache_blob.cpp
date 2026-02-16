#include "hnpch.h"
#include "vk_pipeline_cache_blob.h"

#include "Honey/core/log.h"

#include <fstream>
#include <sstream>
#include <system_error>

#define GLFW_INCLUDE_VULKAN
#include <vulkan/vulkan.h>

namespace {
    constexpr uint32_t kPipelineCacheVersion = 1;

    static void ensure_dir(const std::filesystem::path& p) {
        std::error_code ec;
        std::filesystem::create_directories(p, ec);
        if (ec) {
            HN_CORE_WARN("Failed to create pipeline cache directory: {}", ec.message());
        }
    }
}

namespace Honey {
    void VulkanPipelineCacheBlob::init(VkPhysicalDevice physicalDevice, VkDevice device, const std::filesystem::path& cacheDir) {
        m_physicalDevice = physicalDevice;
        m_device = device;
        m_cacheDir = cacheDir;

        if (!m_physicalDevice || !m_device) {
            HN_CORE_WARN("VulkanPipelineCacheBlob::init called with null device/physicalDevice");
            return;
        }

        ensure_dir(m_cacheDir);

        const auto cachePath = make_cache_file_path();
        const auto initialData = try_read_file(cachePath);

        VkPipelineCacheCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        ci.initialDataSize = initialData.size();
        ci.pInitialData = initialData.empty() ? nullptr : initialData.data();

        VkResult res = vkCreatePipelineCache(m_device, &ci, nullptr, &m_cache);
        if (res != VK_SUCCESS) {
            HN_CORE_WARN("vkCreatePipelineCache failed (res={}); continuing without pipeline cache", (int)res);
            m_cache = nullptr;
            return;
        }

        if (!initialData.empty()) {
            HN_CORE_INFO("Vulkan pipeline cache loaded: {}", cachePath.string());
        } else {
            HN_CORE_INFO("Vulkan pipeline cache created (no initial data): {}", cachePath.string());
        }
    }

    void VulkanPipelineCacheBlob::shutdown() {
        if (!m_device || !m_cache)
            return;

        const auto cachePath = make_cache_file_path();

        size_t size = 0;
        VkResult r0 = vkGetPipelineCacheData(m_device, m_cache, &size, nullptr);
        if (r0 == VK_SUCCESS && size > 0) {
            std::vector<std::byte> data(size);
            VkResult r1 = vkGetPipelineCacheData(m_device, m_cache, &size, data.data());
            if (r1 == VK_SUCCESS && size > 0) {
                data.resize(size);
                try {
                    write_file_atomic(cachePath, data);
                    HN_CORE_INFO("Vulkan pipeline cache saved: {}", cachePath.string());
                } catch (const std::exception& e) {
                    HN_CORE_WARN("Failed to save Vulkan pipeline cache: {}", e.what());
                }
            } else {
                HN_CORE_WARN("vkGetPipelineCacheData failed while reading bytes (res={})", (int)r1);
            }
        } else {
            // Not unusual early on; some drivers return 0 until they've cached anything.
        }

        vkDestroyPipelineCache(m_device, m_cache, nullptr);
        m_cache = nullptr;
    }

    std::filesystem::path VulkanPipelineCacheBlob::make_cache_file_path() const {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(m_physicalDevice, &props);

        std::ostringstream oss;
        oss << "vk_pipe_cache"
            << ".v" << kPipelineCacheVersion
            << ".ven" << props.vendorID
            << ".dev" << props.deviceID
            << ".drv" << props.driverVersion
            << ".uuid";

        // pipelineCacheUUID is 16 bytes; encode as hex.
        for (uint8_t b : props.pipelineCacheUUID) {
            static const char* hex = "0123456789abcdef";
            oss << hex[(b >> 4) & 0xF] << hex[b & 0xF];
        }

        oss << ".bin";

        return m_cacheDir / oss.str();
    }

    std::vector<std::byte> VulkanPipelineCacheBlob::try_read_file(const std::filesystem::path& p) const {
        std::ifstream ifs(p, std::ios::binary | std::ios::ate);
        if (!ifs)
            return {};

        const std::streamsize size = ifs.tellg();
        if (size <= 0)
            return {};

        std::vector<std::byte> bytes((size_t)size);
        ifs.seekg(0);
        ifs.read(reinterpret_cast<char*>(bytes.data()), size);
        if (!ifs)
            return {};

        return bytes;
    }

    void VulkanPipelineCacheBlob::write_file_atomic(const std::filesystem::path& p, const std::vector<std::byte>& bytes) const {
        ensure_dir(p.parent_path());

        const std::filesystem::path tmp = p.string() + ".tmp";

        {
            std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
            if (!ofs)
                throw std::runtime_error("open temp file failed");

            ofs.write(reinterpret_cast<const char*>(bytes.data()), (std::streamsize)bytes.size());
            ofs.flush();

            if (!ofs)
                throw std::runtime_error("write temp file failed");
        }

        std::error_code ec;
        std::filesystem::rename(tmp, p, ec);
        if (ec) {
            // If rename fails because target exists, remove and retry.
            std::filesystem::remove(p, ec);
            ec.clear();
            std::filesystem::rename(tmp, p, ec);
            if (ec) {
                // Cleanup best-effort
                std::filesystem::remove(tmp, ec);
                throw std::runtime_error("rename temp->final failed");
            }
        }
    }
}