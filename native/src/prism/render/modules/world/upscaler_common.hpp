#pragma once

#include "prism/render/modules/world/world_module.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

// What the upscaler modules do the same way (FSR, XeSS). Each used to carry its own copy, so a fix made in one had to
// be remembered for the other.
namespace upscaler {

// Tells an upscaler when to drop its history: on its first frame, and whenever the camera jumped since the last frame
// it ran, by more than a block of movement or 30 degrees of turning (cos 30 = 0.866).
struct CameraCut {
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 direction = glm::vec3(0.0f, 0.0f, -1.0f);
    bool seen = false;

    // Takes this frame's camera and says whether the history no longer fits it.
    bool update(const glm::vec3 &newPosition, const glm::vec3 &newDirection) {
        bool cut = true;
        if (seen) {
            const float moved = glm::length(newPosition - position);
            const float alignment = glm::dot(glm::normalize(newDirection), glm::normalize(direction));
            cut = moved > 1.0f || alignment < 0.866f;
        }
        position = newPosition;
        direction = newDirection;
        seen = true;
        return cut;
    }
};

// Keeps the images other modules made and makes the ones the pipeline left to this module, at width x height, and
// puts every one of them in `kept`. False for an image another module made at a different size. A template only so
// that this header needs no more of the framework than its device() and vma().
template <typename FrameworkType>
bool adoptOrCreateImages(FrameworkType &framework,
                         std::vector<std::shared_ptr<gpu::DeviceLocalImage>> &images,
                         const std::vector<VkFormat> &formats,
                         uint32_t width,
                         uint32_t height,
                         VkImageUsageFlags usage,
                         std::span<std::shared_ptr<gpu::DeviceLocalImage>> kept) {
    for (size_t i = 0; i < kept.size(); i++) {
        if (images[i] == nullptr) {
            images[i] = gpu::DeviceLocalImage::create(framework.device(), framework.vma(), false, width, height, 1,
                                                      formats[i], usage);
        } else if (images[i]->width() != width || images[i]->height() != height) {
            return false;
        }
        kept[i] = images[i];
    }
    return true;
}

} // namespace upscaler
