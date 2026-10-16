#include "prism/gpu/sync.hpp"

#include "prism/gpu/device.hpp"
#include "prism/gpu/retire.hpp"

#include <stdexcept>

namespace gpu {

// ---- Semaphore

Semaphore::Semaphore(std::shared_ptr<Device> device) : device_(std::move(device)) {
    VkSemaphoreCreateInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    if (vkCreateSemaphore(device_->vkDevice(), &info, nullptr, &semaphore_) != VK_SUCCESS) {
        throw std::runtime_error("semaphore could not be made");
    }
}

Semaphore::~Semaphore() {
    retire::later(
        [device = device_, semaphore = semaphore_] { vkDestroySemaphore(device->vkDevice(), semaphore, nullptr); });
}

VkSemaphore &Semaphore::vkSemaphore() {
    return semaphore_;
}

// ---- Fence

Fence::Fence(std::shared_ptr<Device> device) : Fence(std::move(device), false) {}

Fence::Fence(std::shared_ptr<Device> device, bool signaled) : device_(std::move(device)) {
    VkFenceCreateInfo info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (signaled) info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateFence(device_->vkDevice(), &info, nullptr, &fence_) != VK_SUCCESS) {
        throw std::runtime_error("fence could not be made");
    }
}

Fence::~Fence() {
    retire::later([device = device_, fence = fence_] { vkDestroyFence(device->vkDevice(), fence, nullptr); });
}

VkFence &Fence::vkFence() {
    return fence_;
}

} // namespace gpu
