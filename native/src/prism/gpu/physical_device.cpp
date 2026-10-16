#include "prism/gpu/physical_device.hpp"

#include "prism/gpu/instance.hpp"
#include "prism/util/logging.hpp"

#include <vector>

namespace gpu {

PhysicalDevice::PhysicalDevice(std::shared_ptr<Instance> instance,
                               VkPhysicalDevice physicalDevice,
                               uint32_t mainQueueFamily,
                               uint32_t secondaryQueueFamily)
    : instance_(std::move(instance)),
      physicalDevice_(physicalDevice),
      mainQueueIndex_(mainQueueFamily),
      secondaryQueueIndex_(secondaryQueueFamily) {
    VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &rayTracingProperties_};
    rayTracingProperties_.pNext = &accelerationStructProperties_;
    vkGetPhysicalDeviceProperties2(physicalDevice_, &properties);
    // Each is read on its own; none stays chained to another.
    rayTracingProperties_.pNext = nullptr;
    properties_ = properties.properties;

    radiante::out() << "[PhysicalDevice] using " << properties_.deviceName << " (main queue family " << mainQueueIndex_
                    << ", secondary queue family " << secondaryQueueIndex_ << ")" << std::endl;
}

VkPhysicalDevice &PhysicalDevice::vkPhysicalDevice() {
    return physicalDevice_;
}

uint32_t PhysicalDevice::mainQueueIndex() const {
    return mainQueueIndex_;
}

uint32_t PhysicalDevice::secondaryQueueIndex() const {
    return secondaryQueueIndex_;
}

const VkPhysicalDeviceProperties &PhysicalDevice::properties() const {
    return properties_;
}

const VkPhysicalDeviceRayTracingPipelinePropertiesKHR &PhysicalDevice::rayTracingProperties() const {
    return rayTracingProperties_;
}

const VkPhysicalDeviceAccelerationStructurePropertiesKHR &PhysicalDevice::accelerationStructProperties() const {
    return accelerationStructProperties_;
}

bool PhysicalDevice::isRayTracingCapable(VkPhysicalDevice device) {
    const std::unordered_set<std::string> extensions = extensionNames(device);
    if (!extensions.contains(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) ||
        !extensions.contains(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)) {
        return false;
    }

    VkPhysicalDeviceVulkan12Features vulkan12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan13Features vulkan13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, &vulkan12};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructure{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &vulkan13};
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipeline{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR, &accelerationStructure};
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &rayTracingPipeline};
    vkGetPhysicalDeviceFeatures2(device, &features);
    return rayTracingPipeline.rayTracingPipeline && accelerationStructure.accelerationStructure &&
           vulkan13.synchronization2 && vulkan12.bufferDeviceAddress;
}

std::unordered_set<std::string> PhysicalDevice::extensionNames(VkPhysicalDevice device) {
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data());
    extensions.resize(count);

    std::unordered_set<std::string> names;
    names.reserve(extensions.size());
    for (const VkExtensionProperties &extension : extensions) names.emplace(extension.extensionName);
    return names;
}

} // namespace gpu
