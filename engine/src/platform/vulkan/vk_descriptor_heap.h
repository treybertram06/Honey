#pragma once
#include <vulkan/vulkan_core.h>

namespace Honey {

    class VulkanDescriptorHeap {
    public:
        VulkanDescriptorHeap(VkInstance instance, VkPhysicalDevice phys, VkDevice device);
        ~VulkanDescriptorHeap();



    private:
        VkInstance m_instance = nullptr;
        VkPhysicalDevice m_phys = nullptr;
        VkDevice m_device = nullptr;

        // EXT function pointers
        PFN_vkWriteSamplerDescriptorsEXT    m_fnWriteSamplerDescriptors = nullptr;
        PFN_vkWriteResourceDescriptorsEXT   m_fnWriteResourceDescriptors = nullptr;
        PFN_vkCmdBindResourceHeapEXT        m_fnBindResourceHeap = nullptr;
        PFN_vkCmdBindSamplerHeapEXT         m_fnBindSamplerHeap = nullptr;
        PFN_vkCmdPushDataEXT                m_fnPushData = nullptr;
        PFN_vkGetPhysicalDeviceDescriptorSizeEXT m_fnGetDescriptorSize = nullptr;
    };

}
