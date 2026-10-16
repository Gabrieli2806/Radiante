#pragma once

#include "prism/all_extern.hpp"

namespace gpu {
// The VkInstance Minecraft's Vulkan backend creates and destroys. Minecraft creates it through createMerged(), which
// adds the renderer's extensions to Minecraft's request; the wrapper made for that instance reports which upscalers it
// can serve.
class Instance : public SharedObject<Instance> {
  public:
    explicit Instance(VkInstance instance);

    VkInstance &vkInstance();
    // The API version the instance was created with: Minecraft's, raised to what the renderer and XeSS need.
    uint32_t apiVersion() const;
    bool isDlssInstanceExtensionsCompatible() const;
    bool isXessInstanceExtensionsCompatible() const;
    bool isDlssFrameGenerationInstanceExtensionsCompatible() const;

    // vkCreateInstance for Minecraft's backend, with the renderer's extensions merged into its request.
    static VkResult createMerged(const VkInstanceCreateInfo *baseInfo,
                                 const VkAllocationCallbacks *allocator,
                                 VkInstance *outInstance);
    static VkInstance lastCreatedInstance();

  private:
    VkInstance instance_;
};
} // namespace gpu
