#pragma once

#include "common/shared.hpp"
#include "common/singleton.hpp"
#include "prism/all_extern.hpp"
#include "prism/gpu/gpu.hpp"

#include "prism/render/modules/world/dlss/dlss_wrapper.hpp"
#include "prism/render/modules/world/world_module.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class Framework;
class FrameworkContext;
class WorldPipeline;
struct WorldModuleContext;

struct DLSSModuleContext;

/**
 * The NVIDIA module of the world pipeline. With ray reconstruction on, DLSS_RR denoises and upscales in one step and
 * the G-buffer inputs matter; with it off, plain DLSS only upscales a picture NRD has already denoised, and wants
 * nothing but colour, depth and motion vectors. Either way first-hit depth, motion vectors and normal/roughness are
 * brought to the output size for the modules after it.
 */
class DLSSModule : public WorldModule, public SharedObject<DLSSModule> {
    friend DLSSModuleContext;

  public:
    constexpr static std::string_view NAME = "render_pipeline.module.dlss.name";
    constexpr static uint32_t inputImageNum = 8;
    constexpr static uint32_t outputImageNum = 4;

    // The place of each image in the module's input and output lists (modules/dlss.yaml).
    enum Input : uint32_t {
        Radiance,
        DiffuseAlbedo,
        SpecularAlbedo,
        NormalRoughness,
        MotionVector,
        LinearDepth,
        SpecularHitDepth,
        FirstHitDepth,
    };
    enum Output : uint32_t {
        Processed,
        UpscaledFirstHitDepth,
        UpscaledMotionVector,
        UpscaledNormalRoughness,
    };

    // NGX is one per process: set up when the modules are collected, shared with frame generation, shut down when the
    // pipeline closes.
    static bool initNGXContext();
    static void deinitNGXContext();
    static std::shared_ptr<NgxContext> ngxContext() {
        return ngxContext_;
    }
    // Whether NGX offers Ray Reconstruction here, as initNGXContext found; NGX may offer super resolution alone.
    static bool rayReconstructionAvailable() {
        return ngxContext_ != nullptr && rayReconstructionAvailable_;
    }

    DLSSModule() = default;

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
    struct Frame {
        std::array<std::shared_ptr<gpu::DeviceLocalImage>, inputImageNum> inputs;
        std::array<std::shared_ptr<gpu::DeviceLocalImage>, outputImageNum> outputs;
        // The storage images of the three upscale passes.
        std::shared_ptr<gpu::DescriptorTable> upscaleTable;
    };

    // Picks the mode DLSS runs and the size the world is rendered at, for the current output size.
    void chooseRenderSize();

    static std::shared_ptr<NgxContext> ngxContext_;
    static bool rayReconstructionAvailable_;

    bool rayReconstruction_ = true;
    // Share of the output size to render at (0-1); 0 leaves it to the mode.
    float renderScale_ = 0.0f;
    NVSDK_NGX_PerfQuality_Value mode_ = NVSDK_NGX_PerfQuality_Value_Balanced;
    // The mode actually run at the current size; differs from mode_ where a size floor applies.
    NVSDK_NGX_PerfQuality_Value effectiveMode_ = NVSDK_NGX_PerfQuality_Value_Balanced;

    NgxContext::SupportedSizes supportedSizes_{};
    // The output size the render size was chosen for; every frame of a build has the same.
    bool renderSizeChosen_ = false;
    VkExtent2D renderSizeFor_{};
    uint32_t inputWidth_ = 0;
    uint32_t inputHeight_ = 0;
    uint32_t outputWidth_ = 0;
    uint32_t outputHeight_ = 0;

    std::vector<Frame> frames_;

    std::shared_ptr<DlssRR> dlss_;
    std::shared_ptr<DlssSR> dlssSR_;
    // Frames since the pipeline was built, counted up to the few that drop DLSS's history.
    uint32_t framesSinceBuild_ = 0;

    // First-hit depth, motion vectors, normal/roughness: the order the passes run in.
    std::array<std::shared_ptr<gpu::ComputePipeline>, 3> upscalePipelines_;
    MotionUpscalePushConstants upscaleConstants_{};

    std::vector<std::shared_ptr<WorldModuleContext>> contexts_;
};

struct DLSSModuleContext : public WorldModuleContext, SharedObject<DLSSModuleContext> {
    std::weak_ptr<DLSSModule> dLSSModule;

    // input
    std::shared_ptr<gpu::DeviceLocalImage> hdrImage;
    std::shared_ptr<gpu::DeviceLocalImage> diffuseAlbedoImage;
    std::shared_ptr<gpu::DeviceLocalImage> specularAlbedoImage;
    std::shared_ptr<gpu::DeviceLocalImage> normalRoughnessImage;
    std::shared_ptr<gpu::DeviceLocalImage> motionVectorImage;
    std::shared_ptr<gpu::DeviceLocalImage> linearDepthImage;
    std::shared_ptr<gpu::DeviceLocalImage> specularHitDepthImage;
    std::shared_ptr<gpu::DeviceLocalImage> firstHitDepthImage;

    // output
    std::shared_ptr<gpu::DeviceLocalImage> processedImage;
    std::shared_ptr<gpu::DeviceLocalImage> upscaledFirstHitDepthImage;
    std::shared_ptr<gpu::DeviceLocalImage> upscaledMotionVectorImage;
    std::shared_ptr<gpu::DeviceLocalImage> upscaledNormalRoughnessImage;
    std::shared_ptr<gpu::DescriptorTable> motionDescriptorTable;

    DLSSModuleContext(std::shared_ptr<FrameworkContext> frameworkContext,
                      std::shared_ptr<WorldPipelineContext> worldPipelineContext,
                      std::shared_ptr<DLSSModule> dLSSModule);

    void render() override;
};
