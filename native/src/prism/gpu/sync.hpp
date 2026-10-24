#pragma once

#include "prism/all_extern.hpp"

namespace gpu {
class Device;

// A fence, unsignaled unless asked otherwise. Destroyed once no submitted batch can still signal it (gpu::retire).
class Fence : public SharedObject<Fence> {
  public:
    explicit Fence(std::shared_ptr<Device> device);
    Fence(std::shared_ptr<Device> device, bool signaled);
    ~Fence();

    Fence(const Fence &) = delete;
    Fence &operator=(const Fence &) = delete;

    VkFence &vkFence();

  private:
    std::shared_ptr<Device> device_;
    VkFence fence_ = VK_NULL_HANDLE;
};
} // namespace gpu
