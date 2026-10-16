#pragma once

#include "prism/render/modules/world/world_module.hpp"

#include <array>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

class FSRUpscalerModuleContext;

namespace prism {
class FSR3Upscaler;
}

// AMD FSR 3: the world image goes from the render resolution to the display resolution. The first-hit depth, the
// motion vectors and the normals with roughness are scaled up alongside it, for the display-resolution passes after.
// With FSR turned off the image is only stretched (a linear blit).
class FSRUpscalerModule : public WorldModule, public SharedObject<FSRUpscalerModule> {
    friend FSRUpscalerModuleContext;

  public:
    enum class QualityMode {
        NativeAA = 0,        // 1.0x (no upscaling)
        Quality = 1,         // 1.5x
        Balanced = 2,        // 1.7x
        Performance = 3,     // 2.0x
        UltraPerformance = 4 // 3.0x
    };

    static constexpr const char *NAME = "render_pipeline.module.fsr_upscaler.name";
    // color, depth, motion vectors, first-hit depth, normal and roughness (render resolution)
    static constexpr uint32_t inputImageNum = 5;
    // upscaled color, first-hit depth, motion vectors, normal and roughness (display resolution)
    static constexpr uint32_t outputImageNum = 4;

    static bool isQualityModeAttributeKey(const std::string &key);
    static bool parseQualityModeValue(const std::string &value, QualityMode &outMode);

    // The display size divided by the mode's ratio, rounded down.
    static void getRenderResolution(uint32_t displayWidth,
                                    uint32_t displayHeight,
                                    QualityMode mode,
                                    uint32_t *outRenderWidth,
                                    uint32_t *outRenderHeight);

    FSRUpscalerModule();
    ~FSRUpscalerModule();

    void init(std::shared_ptr<Framework> framework, std::shared_ptr<WorldPipeline> worldPipeline);

    bool setOrCreateInputImages(std::vector<std::shared_ptr<gpu::DeviceLocalImage>> &images,
                                std::vector<VkFormat> &formats,
                                uint32_t frameIndex) override;

    bool setOrCreateOutputImages(std::vector<std::shared_ptr<gpu::DeviceLocalImage>> &images,
                                 std::vector<VkFormat> &formats,
                                 uint32_t frameIndex) override;

    void build() override;

    void setAttributes(int attributeCount, std::vector<std::string> &attributeKVs) override;

    std::vector<std::shared_ptr<WorldModuleContext>> &contexts() override;

    void
    bindTexture(std::shared_ptr<gpu::Sampler> sampler, std::shared_ptr<gpu::DeviceLocalImage> image, int index) override;

    void preClose() override;

  private:
    // What one frame slot renders with.
    struct Frame {
        std::array<std::shared_ptr<gpu::DeviceLocalImage>, inputImageNum> inputs;
        std::array<std::shared_ptr<gpu::DeviceLocalImage>, outputImageNum> outputs;
        std::shared_ptr<gpu::DeviceLocalImage> deviceDepth;      // FSR's depth input
        std::shared_ptr<gpu::DeviceLocalImage> fsrMotionVectors; // the motion vectors with NaN and Inf zeroed
        std::shared_ptr<gpu::DescriptorTable> descriptorTable;   // shared by the four compute passes
    };

    // The render size for the current display size: the chosen share of it, or the quality mode's.
    void resolveRenderResolution();
    // True when the camera moved or turned too far since the last frame for FSR's history to still fit.
    bool cameraCut(const glm::vec3 &position, const glm::vec3 &direction);

    void createFrameResources();
    void createPipelines();

    std::vector<Frame> frames_;
    std::vector<std::shared_ptr<WorldModuleContext>> contexts_;

    uint32_t renderWidth_ = 0;
    uint32_t renderHeight_ = 0;
    uint32_t displayWidth_ = 0;
    uint32_t displayHeight_ = 0;
    QualityMode qualityMode_ = QualityMode::NativeAA;
    // Share of the display size to render at (0-1); 0 leaves it to the quality mode.
    float renderScale_ = 0.0f;
    float sharpness_ = 0.7f;
    float preExposure_ = 1.0f;
    bool fsr3Enabled_ = true;

    // Made by build() when FSR is on and its context could be created; dropped by preClose().
    std::unique_ptr<prism::FSR3Upscaler> fsr3_;

    std::shared_ptr<gpu::ComputePipeline> depthConversionPipeline_;
    std::shared_ptr<gpu::ComputePipeline> firstHitDepthUpscalePipeline_;
    std::shared_ptr<gpu::ComputePipeline> motionUpscalePipeline_;
    std::shared_ptr<gpu::ComputePipeline> normalRoughnessUpscalePipeline_;

    glm::vec3 lastCameraPos_ = glm::vec3(0.0f);
    glm::vec3 lastCameraDir_ = glm::vec3(0.0f, 0.0f, -1.0f);
    bool firstFrame_ = true;
};

class FSRUpscalerModuleContext : public WorldModuleContext {
  public:
    FSRUpscalerModuleContext(std::shared_ptr<FrameworkContext> frameworkContext,
                             std::shared_ptr<WorldPipelineContext> worldPipelineContext,
                             std::shared_ptr<FSRUpscalerModule> upscalerModule,
                             const FSRUpscalerModule::Frame &frame);

    void render() override;

  private:
    using Clock = std::chrono::steady_clock;
    static constexpr uint32_t frameTimeSamples = 10;

    void recordBlit(gpu::CommandBuffer &commandBuffer);
    void recordDepthConversion(FSRUpscalerModule &module, gpu::CommandBuffer &commandBuffer, glm::vec2 jitter);
    void recordFsr(FSRUpscalerModule &module, gpu::CommandBuffer &commandBuffer, const gpu::Data::WorldUBO &world);
    void recordAuxiliaryUpscales(FSRUpscalerModule &module, gpu::CommandBuffer &commandBuffer);
    // The time since this frame slot last ran, averaged over its last few runs (milliseconds).
    float smoothedFrameTime();

    std::weak_ptr<FSRUpscalerModule> upscalerModule_;
    FSRUpscalerModule::Frame frame_;

    Clock::time_point lastFrameTime_;
    std::array<float, frameTimeSamples> frameTimes_{};
    uint32_t frameTimeCount_ = 0;
    uint32_t nextFrameTime_ = 0;
    bool timingStarted_ = false;
};
