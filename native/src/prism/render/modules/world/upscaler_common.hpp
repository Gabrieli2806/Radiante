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

// Takes the size of the first image another module already made, if there is one.
inline void adoptSize(const std::vector<std::shared_ptr<gpu::DeviceLocalImage>> &images,
                      uint32_t &width,
                      uint32_t &height) {
    for (const auto &image : images) {
        if (image == nullptr) continue;
        width = image->width();
        height = image->height();
        return;
    }
}

// An upscaler's inputs. The display size is the swapchain's unless it is already known; the render size is that of an
// input another module already made, and otherwise what chooseRenderSize() works out from the module's settings.
template <typename FrameworkType, typename ChooseRenderSize>
bool takeInputImages(FrameworkType &framework,
                     std::vector<std::shared_ptr<gpu::DeviceLocalImage>> &images,
                     const std::vector<VkFormat> &formats,
                     uint32_t &renderWidth,
                     uint32_t &renderHeight,
                     uint32_t &displayWidth,
                     uint32_t &displayHeight,
                     VkImageUsageFlags usage,
                     std::span<std::shared_ptr<gpu::DeviceLocalImage>> kept,
                     ChooseRenderSize &&chooseRenderSize) {
    if (displayWidth == 0 || displayHeight == 0) {
        const VkExtent2D extent = framework.swapchain()->vkExtent();
        displayWidth = extent.width;
        displayHeight = extent.height;
    }
    if (renderWidth == 0 || renderHeight == 0) {
        adoptSize(images, renderWidth, renderHeight);
        if (renderWidth == 0 || renderHeight == 0) chooseRenderSize();
    }
    return adoptOrCreateImages(framework, images, formats, renderWidth, renderHeight, usage, kept);
}

// An upscaler's outputs, at the display size: that of an output a later module already made, and otherwise the
// swapchain's.
template <typename FrameworkType>
bool takeOutputImages(FrameworkType &framework,
                      std::vector<std::shared_ptr<gpu::DeviceLocalImage>> &images,
                      const std::vector<VkFormat> &formats,
                      uint32_t &displayWidth,
                      uint32_t &displayHeight,
                      VkImageUsageFlags usage,
                      std::span<std::shared_ptr<gpu::DeviceLocalImage>> kept) {
    if (displayWidth == 0 || displayHeight == 0) {
        adoptSize(images, displayWidth, displayHeight);
        if (displayWidth == 0 || displayHeight == 0) {
            const VkExtent2D extent = framework.swapchain()->vkExtent();
            displayWidth = extent.width;
            displayHeight = extent.height;
        }
    }
    return adoptOrCreateImages(framework, images, formats, displayWidth, displayHeight, usage, kept);
}

// One helper pass of an upscaler module over width x height texels, in 16 x 16 groups. The module's helper passes
// (the depth conversion, the three guide upscales) share one pipeline layout, so their push constants are one size.
template <typename PushConstants>
void recordPass(gpu::CommandBuffer &commandBuffer,
                const std::shared_ptr<gpu::DescriptorTable> &descriptors,
                const std::shared_ptr<gpu::ComputePipeline> &pipeline,
                const PushConstants &pushConstants,
                uint32_t width,
                uint32_t height) {
    static_assert(sizeof(PushConstants) == sizeof(MotionUpscalePushConstants));
    commandBuffer.bindDescriptorTable(descriptors, VK_PIPELINE_BIND_POINT_COMPUTE);
    commandBuffer.bindComputePipeline(pipeline);
    vkCmdPushConstants(commandBuffer.vkCommandBuffer(), descriptors->vkPipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(PushConstants), &pushConstants);
    vkCmdDispatch(commandBuffer.vkCommandBuffer(), (width + 15) / 16, (height + 15) / 16, 1);
}

// The guides the modules after an upscaler read at the display size (the first hit's depth, the motion vectors, the
// normal and roughness), each by its own pipeline. The caller has readied their images: the three passes touch
// separate ones.
inline void recordGuideUpscales(gpu::CommandBuffer &commandBuffer,
                                const std::shared_ptr<gpu::DescriptorTable> &descriptors,
                                const std::shared_ptr<gpu::ComputePipeline> &firstHitDepth,
                                const std::shared_ptr<gpu::ComputePipeline> &motionVectors,
                                const std::shared_ptr<gpu::ComputePipeline> &normalRoughness,
                                uint32_t renderWidth,
                                uint32_t renderHeight,
                                uint32_t displayWidth,
                                uint32_t displayHeight) {
    const MotionUpscalePushConstants scale{
        static_cast<float>(displayWidth) / static_cast<float>(renderWidth > 0 ? renderWidth : 1u),
        static_cast<float>(displayHeight) / static_cast<float>(renderHeight > 0 ? renderHeight : 1u),
        renderWidth,
        renderHeight,
        displayWidth,
        displayHeight,
    };
    for (const auto *pipeline : {&firstHitDepth, &motionVectors, &normalRoughness}) {
        recordPass(commandBuffer, descriptors, *pipeline, scale, displayWidth, displayHeight);
    }
}

// Stretches the colour as it is over the output, for a frame the upscaler itself cannot do. The caller has readied
// the two images for the transfer.
inline void blitScaled(gpu::CommandBuffer &commandBuffer, gpu::DeviceLocalImage &color, gpu::DeviceLocalImage &output) {
    VkImageBlit region{};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.srcOffsets[1] = {static_cast<int32_t>(color.width()), static_cast<int32_t>(color.height()), 1};
    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstOffsets[1] = {static_cast<int32_t>(output.width()), static_cast<int32_t>(output.height()), 1};
    vkCmdBlitImage(commandBuffer.vkCommandBuffer(), color.vkImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   output.vkImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_LINEAR);
}

} // namespace upscaler
