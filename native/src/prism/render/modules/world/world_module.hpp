#pragma once

#include "common/shared.hpp"
#include "common/singleton.hpp"
#include "prism/all_extern.hpp"
#include "prism/gpu/gpu.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

class Framework;
class FrameworkContext;
class WorldPipeline;
struct WorldPipelineContext;

struct WorldModuleContext;

// Push constants of the upscalers' passes that bring motion vectors, depth and normals to the output size
// (native/src/shader/world/upscaler/upscale_*.comp).
struct MotionUpscalePushConstants {
    float scaleX;
    float scaleY;
    uint32_t srcWidth;
    uint32_t srcHeight;
    uint32_t dstWidth;
    uint32_t dstHeight;
};
static_assert(sizeof(MotionUpscalePushConstants) == 24, "must match the push constant block of upscale_*.comp");

// One stage of the world pipeline (ray tracing, denoising, upscaling, accumulation, tone mapping, post passes).
//
// WorldPipeline::init makes the modules last to first, gives each its attributes and the images it shares with the
// stages around it, then builds it. A module records a frame's work through its context for that frame slot.
class WorldModule {
  public:
    WorldModule();
    virtual ~WorldModule() = default;

    void init(std::shared_ptr<Framework> framework, std::shared_ptr<WorldPipeline> worldPipeline);

    // The images the module reads, or writes, for one frame slot, in the order the module declares them. An entry the
    // stages after it did not provide is null and the module makes it. False when the images do not suit the module.
    virtual bool setOrCreateInputImages(std::vector<std::shared_ptr<gpu::DeviceLocalImage>> &images,
                                        std::vector<VkFormat> &formats,
                                        uint32_t frameIndex) = 0;
    virtual bool setOrCreateOutputImages(std::vector<std::shared_ptr<gpu::DeviceLocalImage>> &images,
                                         std::vector<VkFormat> &formats,
                                         uint32_t frameIndex) = 0;

    // The module's attributes as names and values, one after the other: attributeCount pairs.
    virtual void setAttributes(int attributeCount, std::vector<std::string> &attributeKVs) = 0;

    // Applies one attribute (its full name, and a value as setAttributes takes it) to the built module from the next
    // frame on, so a settings change skips the rebuild. False when the module reads it only while it builds (the
    // default) or the value does not read; the caller then rebuilds, which applies it either way.
    virtual bool setRuntimeAttribute(const std::string &, const std::string &) {
        return false;
    }

    virtual void build() = 0;
    virtual std::vector<std::shared_ptr<WorldModuleContext>> &contexts() = 0;

    virtual void
    bindTexture(std::shared_ptr<gpu::Sampler> sampler, std::shared_ptr<gpu::DeviceLocalImage> image, int index) = 0;

    // Releases what has to go before the module is destroyed (NGX features, upscaler contexts). The device is idle.
    virtual void preClose() = 0;

  protected:
    std::weak_ptr<Framework> framework_;
    std::weak_ptr<WorldPipeline> worldPipeline_;
};

// A module's work for one frame slot.
struct WorldModuleContext {
    std::weak_ptr<FrameworkContext> frameworkContext;
    std::weak_ptr<WorldPipelineContext> worldPipelineContext;

    WorldModuleContext(std::shared_ptr<FrameworkContext> frameworkContext,
                       std::shared_ptr<WorldPipelineContext> worldPipelineContext);
    virtual ~WorldModuleContext() = default;

    // Records the module's commands for the frame into the frame's world command buffer.
    virtual void render() = 0;
};
