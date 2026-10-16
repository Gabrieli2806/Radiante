#pragma once

#include "prism/all_extern.hpp"

namespace gpu {
class Device;

// A binary semaphore. Destroyed once no frame in flight can still wait on it or signal it (gpu::retire).
class Semaphore : public SharedObject<Semaphore> {
  public:
    explicit Semaphore(std::shared_ptr<Device> device);
    ~Semaphore();

    Semaphore(const Semaphore &) = delete;
    Semaphore &operator=(const Semaphore &) = delete;

    VkSemaphore &vkSemaphore();

  private:
    std::shared_ptr<Device> device_;
    VkSemaphore semaphore_ = VK_NULL_HANDLE;
};

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
