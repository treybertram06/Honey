#pragma once

#include <filesystem>
#include <vector>

typedef struct VkDevice_T* VkDevice;
typedef struct VkPhysicalDevice_T* VkPhysicalDevice;
typedef struct VkPipelineCache_T* VkPipelineCache;

namespace Honey {

    class VulkanPipelineCacheBlob {
    public:
        VulkanPipelineCacheBlob() = default;
        ~VulkanPipelineCacheBlob() = default;

        VulkanPipelineCacheBlob(const VulkanPipelineCacheBlob&) = delete;
        VulkanPipelineCacheBlob& operator=(const VulkanPipelineCacheBlob&) = delete;

        // Creates VkPipelineCache, optionally seeded from disk.
        void init(VkPhysicalDevice physicalDevice, VkDevice device, const std::filesystem::path& cacheDir);

        // Saves the cache blob to disk (best effort) and destroys VkPipelineCache.
        void shutdown();

        VkPipelineCache get() const { return m_cache; }
        bool valid() const { return m_cache != nullptr; }

    private:
        std::filesystem::path make_cache_file_path() const;
        std::vector<std::byte> try_read_file(const std::filesystem::path& p) const;
        void write_file_atomic(const std::filesystem::path& p, const std::vector<std::byte>& bytes) const;

    private:
        VkPhysicalDevice m_physicalDevice = nullptr;
        VkDevice m_device = nullptr;
        VkPipelineCache m_cache = nullptr;
        std::filesystem::path m_cacheDir{};
    };

}