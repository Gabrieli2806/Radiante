#pragma once

#include "prism/render/modules/world/world_module.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace prism {
class XeSSWrapper;
} // namespace prism

class XessSrModuleContext;

// Intel XeSS super resolution. Takes the denoised radiance at the render size and gives the later modules the radiance
// at the display size, with the first-hit depth, motion vectors and normal/roughness scaled up to match. When XeSS is
// turned off or cannot run, the radiance is stretched with a linear blit instead.
class XessSrModule : public WorldModule, public SharedObject<XessSrModule> {
    friend XessSrModuleContext;

  public:
    enum class QualityMode {
        NativeAA = 0,
        UltraQualityPlus = 1,
        UltraQuality = 2,
        Quality = 3,
        Balanced = 4,
        Performance = 5,
        UltraPerformance = 6,
    };

    static constexpr const char *NAME = "render_pipeline.module.xess_sr.name";
    static constexpr uint32_t inputImageNum = 5;
    static constexpr uint32_t outputImageNum = 4;

    static bool isQualityModeAttributeKey(const std::string &key);
    // False, leaving outMode as it was, for a value that names no mode.
    static bool parseQualityModeValue(const std::string &value, QualityMode &outMode);
    // The render size from a mode's nominal ratio, for when XeSS itself cannot be asked.
    static void getRenderResolution(uint32_t displayWidth,
                                    uint32_t displayHeight,
                                    QualityMode mode,
                                    uint32_t *outRenderWidth,
                                    uint32_t *outRenderHeight);

    XessSrModule();
    ~XessSrModule();

    void init(std::shared_ptr<Framework> framework, std::shared_ptr<WorldPipeline> worldPipeline);

    bool setOrCreateInputImages(std::vector<std::shared_ptr<gpu::DeviceLocalImage>> &images,
                                std::vector<VkFormat> &formats,
                                uint32_t frameIndex) override;
    bool setOrCreateOutputImages(std::vector<std::shared_ptr<gpu::DeviceLocalImage>> &images,
                                 std::vector<VkFormat> &formats,
                                 uint32_t frameIndex) override;
    void setAttributes(int attributeCount, std::vector<std::string> &attributeKVs) override;
    void build() override;
    std::vector<std::shared_ptr<WorldModuleContext>> &contexts() override;
    void bindTexture(std::shared_ptr<gpu::Sampler> sampler,
                     std::shared_ptr<gpu::DeviceLocalImage> image,
                     int index) override;
    void preClose() override;

  private:
    // Slots in the module's image lists, in the order modules/xess_sr.yaml declares them.
    enum Input : uint32_t { Color, Depth, MotionVector, FirstHitDepth, NormalRoughness };
    enum Output : uint32_t { Upscaled, UpscaledFirstHitDepth, UpscaledMotionVector, UpscaledNormalRoughness };

    // What one frame in flight records with.
    struct Frame {
        std::array<std::shared_ptr<gpu::DeviceLocalImage>, inputImageNum> inputs;
        std::array<std::shared_ptr<gpu::DeviceLocalImage>, outputImageNum> outputs;
        std::shared_ptr<gpu::DeviceLocalImage> deviceDepth;      // XeSS's depth input
        std::shared_ptr<gpu::DeviceLocalImage> xessMotionVector; // the motion vectors with NaN and Inf zeroed
        std::shared_ptr<gpu::DescriptorTable> descriptors;       // one set for all four helper passes
    };

    // The camera when XeSS last ran, to tell when its history no longer fits the view.
    struct CameraHistory {
        glm::vec3 position = glm::vec3(0.0f);
        glm::vec3 direction = glm::vec3(0.0f, 0.0f, -1.0f);
        bool seen = false;
    };

    void updateRenderResolution();
    // The preset XeSS is set up with: the chosen one, or the one whose ratio is nearest a custom render scale.
    QualityMode effectiveQualityMode() const;

    // A ready XeSS context for the current sizes, or null (logged) when XeSS could not be set up.
    std::unique_ptr<prism::XeSSWrapper> createXeSS(Framework &framework) const;
    void createDescriptorTables(Framework &framework);
    void createImages(Framework &framework);
    void bindDescriptors();
    void createPipelines(Framework &framework);

    void record(uint32_t frameIndex, gpu::CommandBuffer &commandBuffer);
    bool upscaleWithXeSS(Frame &frame, gpu::CommandBuffer &commandBuffer);
    void blitColor(Frame &frame, gpu::CommandBuffer &commandBuffer);
    void upscaleGuides(Frame &frame, gpu::CommandBuffer &commandBuffer, bool blitted);
    bool cameraJumped(const glm::vec3 &position, const glm::vec3 &direction);

    std::vector<Frame> frames_;
    std::vector<std::shared_ptr<WorldModuleContext>> contexts_;

    uint32_t renderWidth_ = 0;
    uint32_t renderHeight_ = 0;
    uint32_t displayWidth_ = 0;
    uint32_t displayHeight_ = 0;
    QualityMode qualityMode_ = QualityMode::Quality;
    // Share of the display size to render at (0.33-1); 0 leaves the size to the quality mode.
    float renderScale_ = 0.0f;
    float preExposure_ = 1.0f;
    bool xessEnabled_ = true;

    // Set while XeSS runs; without it the module blits.
    std::unique_ptr<prism::XeSSWrapper> xess_;

    std::shared_ptr<gpu::ComputePipeline> depthConversionPipeline_;
    std::shared_ptr<gpu::ComputePipeline> firstHitDepthUpscalePipeline_;
    std::shared_ptr<gpu::ComputePipeline> motionUpscalePipeline_;
    std::shared_ptr<gpu::ComputePipeline> normalRoughnessUpscalePipeline_;

    CameraHistory camera_;
};

class XessSrModuleContext : public WorldModuleContext {
  public:
    XessSrModuleContext(std::shared_ptr<FrameworkContext> frameworkContext,
                        std::shared_ptr<WorldPipelineContext> worldPipelineContext,
                        std::shared_ptr<XessSrModule> xessModule);

    void render() override;

  private:
    std::weak_ptr<XessSrModule> xessModule_;
    uint32_t frameIndex_ = 0;
};
