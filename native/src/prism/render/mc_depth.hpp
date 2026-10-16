#pragma once

#include "prism/all_extern.hpp"
#include "prism/gpu/gpu.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

class Framework;

// Writes the traced world's depth into Minecraft's depth texture, in Minecraft's own depth convention. Minecraft's
// level passes run after the tracer for the sake of other mods' overlays (schematics, selection boxes, markers): they
// depth test against that texture, and without the traced depth in it they would draw through walls.
class McDepthWriter {
  public:
    struct Target {
        VkImage image = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        // zScale, zOffset, wScale, wOffset of Minecraft's projection; see mc_depth.comp.
        float projection[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    };

    // Records the write into commandBuffer. linearDepth is the tracer's first hit depth, at any resolution; width x
    // height is the target's size. A target it cannot write (none, or another depth format) is left alone.
    void write(const std::shared_ptr<Framework> &framework,
               const std::shared_ptr<gpu::CommandBuffer> &commandBuffer,
               const std::shared_ptr<gpu::DeviceLocalImage> &linearDepth,
               const Target &target,
               uint32_t width,
               uint32_t height,
               uint32_t frameIndex);

    // Drops every resource (a rebuild, the renderer closing). What frames in flight still use retires once they end.
    void reset();

  private:
    // What one target size needs. Some mods draw extra views at a size of their own between the main ones every few
    // frames (Vivecraft's handheld camera): a few sizes are kept, rather than making these again on every switch.
    struct SizedResources {
        uint32_t width = 0;
        uint32_t height = 0;
        // The converted depth, as a colour image the shader can write.
        std::shared_ptr<gpu::DeviceLocalImage> depth;
        // A colour image cannot be copied into a depth image, but a buffer can: the values go through this one.
        std::shared_ptr<gpu::DeviceLocalBuffer> transfer;
        // One per frame slot.
        std::vector<std::shared_ptr<gpu::DescriptorTable>> tables;
    };
    static constexpr size_t MAX_SIZES = 4;

    SizedResources &resourcesFor(Framework &framework, uint32_t width, uint32_t height, uint32_t frameCount);

    // Oldest first.
    std::vector<SizedResources> sizes_;
    std::shared_ptr<gpu::ComputePipeline> pipeline_;
};
