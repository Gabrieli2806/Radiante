#pragma once

#include "prism/all_extern.hpp"

#include <string>
#include <unordered_set>

namespace gpu {
class Instance;

// The GPU Minecraft picked: the queue families its main and secondary queues come from, and the properties and limits
// the renderer sizes its work by (read once).
class PhysicalDevice : public SharedObject<PhysicalDevice> {
  public:
    PhysicalDevice(std::shared_ptr<Instance> instance,
                   VkPhysicalDevice physicalDevice,
                   uint32_t mainQueueFamily,
                   uint32_t secondaryQueueFamily);

    VkPhysicalDevice &vkPhysicalDevice();
    uint32_t mainQueueIndex() const;
    uint32_t secondaryQueueIndex() const;

    const VkPhysicalDeviceProperties &properties() const;
    const VkPhysicalDeviceRayTracingPipelinePropertiesKHR &rayTracingProperties() const;
    const VkPhysicalDeviceAccelerationStructurePropertiesKHR &accelerationStructProperties() const;

    // Whether a GPU can run the renderer: ray tracing pipelines, acceleration structures, synchronization2 and buffer
    // device addresses. Minecraft passes over GPUs that cannot.
    static bool isRayTracingCapable(VkPhysicalDevice device);
    // The device extensions a GPU offers.
    static std::unordered_set<std::string> extensionNames(VkPhysicalDevice device);

  private:
    std::shared_ptr<Instance> instance_;

    VkPhysicalDevice physicalDevice_;
    uint32_t mainQueueIndex_;
    uint32_t secondaryQueueIndex_;

    VkPhysicalDeviceProperties properties_{};
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingProperties_{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
    VkPhysicalDeviceAccelerationStructurePropertiesKHR accelerationStructProperties_{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
};
} // namespace gpu
