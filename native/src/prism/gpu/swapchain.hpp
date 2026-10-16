#pragma once

#include "prism/all_extern.hpp"

namespace gpu {
// Minecraft owns the window's swapchain and presents it. What the renderer keeps here is what it sizes its per-frame
// resources by: how many frames can be in flight at once (the frame slots) and the size of the image the world is
// traced into.
class Swapchain : public SharedObject<Swapchain> {
  public:
    static constexpr uint32_t FRAME_SLOTS = 3;

    Swapchain(uint32_t width, uint32_t height) : extent_{width, height} {}

    void reconstruct(uint32_t width, uint32_t height) {
        extent_ = {width, height};
    }

    VkExtent2D &vkExtent() {
        return extent_;
    }

    uint32_t imageCount() const {
        return FRAME_SLOTS;
    }

  private:
    VkExtent2D extent_;
};
} // namespace gpu
