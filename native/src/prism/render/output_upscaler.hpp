#pragma once

#include "prism/all_extern.hpp"
#include "prism/gpu/gpu.hpp"

#include <cstdint>
#include <memory>
#include <vector>

class Framework;

// The final upscale of a lower output resolution: the world pipeline renders, denoises, upscales and post processes
// at the output size Java picked, and this brings its picture to the window's size with AMD FidelityFX Super
// Resolution 1 (EASU, then RCAS sharpening), before it is fused into Minecraft's target. Minecraft then draws its HUD
// and menus over it at the window's resolution.
//
// At the native output size nothing here runs: the framework blits the picture into Minecraft's target as it always
// did.
class OutputUpscaler {
  public:
    // How much RCAS sharpens, in stops below its strongest (FidelityFX's own default).
    static constexpr float SHARPNESS_STOPS = 0.2f;

    // Records the upscale of `source` (the world pipeline's picture, left in `restingLayout`, to which it is put back)
    // into an image of width x height, on commandBuffer. Returns that image, left in GENERAL, or null when the upscale
    // could not be set up (the caller then blits as usual).
    std::shared_ptr<gpu::DeviceLocalImage> record(Framework &framework,
                                                  gpu::CommandBuffer &commandBuffer,
                                                  const std::shared_ptr<gpu::DeviceLocalImage> &source,
                                                  VkImageLayout restingLayout,
                                                  uint32_t width,
                                                  uint32_t height,
                                                  uint32_t frameIndex);

    // Drops every resource (a rebuild, the renderer closing); frames in flight keep what they use until they end.
    void reset();

  private:
    bool prepare(Framework &framework, uint32_t width, uint32_t height, uint32_t frameCount);

    bool failed_ = false;
    std::shared_ptr<gpu::ComputePipeline> easu_;
    std::shared_ptr<gpu::ComputePipeline> rcas_;
    std::shared_ptr<gpu::Sampler> linear_;
    // One pair per frame slot: a slot's tables are rewritten when it records, after the frame it held last has ended.
    std::vector<std::shared_ptr<gpu::DescriptorTable>> easuTables_;
    std::vector<std::shared_ptr<gpu::DescriptorTable>> rcasTables_;
    // EASU's result, in half floats so RCAS sharpens it without another rounding to 8 bits; then the sharpened picture.
    std::shared_ptr<gpu::DeviceLocalImage> upscaled_;
    std::shared_ptr<gpu::DeviceLocalImage> sharpened_;
};
