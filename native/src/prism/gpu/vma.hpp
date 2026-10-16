#pragma once

#include "prism/all_extern.hpp"

namespace gpu {
class PhysicalDevice;
class Device;
class Instance;

// The renderer's memory allocator. Allocations the renderer makes are freed through gpu::retire, which keeps the
// allocator alive until the last of them is.
class VMA : public SharedObject<VMA> {
  public:
    VMA(std::shared_ptr<Instance> instance,
        std::shared_ptr<PhysicalDevice> physicalDevice,
        std::shared_ptr<Device> device);
    ~VMA();

    VMA(const VMA &) = delete;
    VMA &operator=(const VMA &) = delete;

    VmaAllocator &allocator();

  private:
    VmaAllocator allocator_ = VK_NULL_HANDLE;
};
} // namespace gpu
