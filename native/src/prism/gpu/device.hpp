#pragma once

#include "prism/all_extern.hpp"

#include <filesystem>
#include <mutex>

namespace gpu {
class Instance;
class PhysicalDevice;

// The VkDevice Minecraft's Vulkan backend creates and destroys, with the two queues the renderer submits to. Minecraft
// creates it through createMerged(), which adds what ray tracing, the upscalers, frame generation and Reflex need to
// Minecraft's own request; the wrapper made for that device reports which of them could be enabled.
class Device : public SharedObject<Device> {
  public:
    Device(std::shared_ptr<Instance> instance,
           std::shared_ptr<PhysicalDevice> physicalDevice,
           VkDevice device,
           VkQueue mainQueue,
           VkQueue secondaryQueue);
    ~Device();

    VkDevice &vkDevice();
    VkQueue &mainVkQueue();
    VkQueue &secondaryQueue();

    bool hasExtendedDynamicState2LogicOp() const;
    bool isDlssDeviceExtensionsCompatible() const;
    bool isXessDeviceExtensionsCompatible() const;
    bool isDlssFrameGenerationDeviceExtensionsCompatible() const;
    bool isFsrFrameGenerationDeviceExtensionsCompatible() const;
    bool isShaderExecutionReorderingEnabled() const;

    // One VkPipelineCache for every pipeline the renderer builds, kept on disk between runs: a pipeline whose shaders
    // were compiled before is rebuilt from the driver's saved code. Made on first use from the file set with
    // setPipelineCacheFile (data written for another GPU or driver is left out); VK_NULL_HANDLE if it cannot be made.
    VkPipelineCache pipelineCache();
    // Writes the cache to its file; called when a pipeline rebuild ends and when the device goes.
    void savePipelineCache();
    static void setPipelineCacheFile(const std::filesystem::path &file);

    // vkCreateDevice for Minecraft's backend, with the renderer's extensions and features merged into its request.
    static VkResult createMerged(VkPhysicalDevice physicalDevice,
                                 const VkDeviceCreateInfo *baseInfo,
                                 const VkAllocationCallbacks *allocator,
                                 VkDevice *outDevice);

  private:
    std::shared_ptr<Instance> instance_;
    std::shared_ptr<PhysicalDevice> physicalDevice_;

    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue mainQueue_ = VK_NULL_HANDLE;
    VkQueue secondaryQueue_ = VK_NULL_HANDLE;

    // Guards the cache handle, which pipelines built on several threads at once ask for. Made once: when that fails,
    // pipelines are built without a cache.
    std::mutex pipelineCacheMutex_;
    VkPipelineCache pipelineCache_ = VK_NULL_HANDLE;
    bool pipelineCacheAttempted_ = false;
    // One save at a time: saves write the same temporary file.
    std::mutex pipelineCacheSaveMutex_;
};
} // namespace gpu
