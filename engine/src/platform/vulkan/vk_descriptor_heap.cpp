#include "hnpch.h"
#include "vk_descriptor_heap.h"

namespace Honey {

    VulkanDescriptorHeap::VulkanDescriptorHeap(VkInstance instance, VkPhysicalDevice phys, VkDevice device)
        : m_device(device), m_phys(phys), m_instance(instance) {

        // Populate fn pointers
        m_fnWriteSamplerDescriptors = reinterpret_cast<PFN_vkWriteSamplerDescriptorsEXT>
        (vkGetDeviceProcAddr(device, "vkWriteSamplerDescriptorsEXT"));
        m_fnWriteResourceDescriptors = reinterpret_cast<PFN_vkWriteResourceDescriptorsEXT>
        (vkGetDeviceProcAddr(device, "vkWriteResourceDescriptorsEXT"));
        m_fnBindResourceHeap = reinterpret_cast<PFN_vkCmdBindResourceHeapEXT>
        (vkGetDeviceProcAddr(device, "vkCmdBindResourceHeapEXT"));
        m_fnBindSamplerHeap = reinterpret_cast<PFN_vkCmdBindSamplerHeapEXT>
        (vkGetDeviceProcAddr(device, "vkCmdBindSamplerHeapEXT"));
        m_fnPushData = reinterpret_cast<PFN_vkCmdPushDataEXT>
        (vkGetDeviceProcAddr(device, "vkCmdPushDataEXT"));

        m_fnGetDescriptorSize = reinterpret_cast<PFN_vkGetPhysicalDeviceDescriptorSizeEXT>
        (vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceDescriptorSizeEXT"));

        HN_CORE_ASSERT(m_fnWriteSamplerDescriptors && m_fnWriteResourceDescriptors
            && m_fnBindResourceHeap && m_fnBindSamplerHeap && m_fnPushData && m_fnGetDescriptorSize,
            "VulkanDescriptorHeap: missing function pointers");


    }

    VulkanDescriptorHeap::~VulkanDescriptorHeap() {
    }

}
