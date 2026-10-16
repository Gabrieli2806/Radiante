#define VMA_IMPLEMENTATION
#include "prism/gpu/vma.hpp"

#include "prism/gpu/device.hpp"
#include "prism/gpu/instance.hpp"
#include "prism/gpu/physical_device.hpp"
#include "prism/util/logging.hpp"

#include <stdexcept>

namespace gpu {

VMA::VMA(std::shared_ptr<Instance> instance,
         std::shared_ptr<PhysicalDevice> physicalDevice,
         std::shared_ptr<Device> device) {
    VmaAllocatorCreateInfo info{};
    info.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    info.physicalDevice = physicalDevice->vkPhysicalDevice();
    info.device = device->vkDevice();
    info.instance = instance->vkInstance();
    info.vulkanApiVersion = instance->apiVersion();

    // VMA calls Vulkan through volk's function pointers, like the rest of the renderer; it keeps its own copy.
    VmaVulkanFunctions functions{};
    VkResult result = vmaImportVulkanFunctionsFromVolk(&info, &functions);
    if (result != VK_SUCCESS) {
        radiante::err() << "[VMA] failed to import the Vulkan functions from volk (" << result << ")" << std::endl;
        throw std::runtime_error("VMA could not import the Vulkan functions");
    }
    info.pVulkanFunctions = &functions;

    result = vmaCreateAllocator(&info, &allocator_);
    if (result != VK_SUCCESS) {
        radiante::err() << "[VMA] failed to create the allocator (" << result << ")" << std::endl;
        throw std::runtime_error("VMA allocator could not be made");
    }
}

VMA::~VMA() {
    vmaDestroyAllocator(allocator_);
}

VmaAllocator &VMA::allocator() {
    return allocator_;
}

} // namespace gpu
