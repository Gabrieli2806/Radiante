#pragma once

#include "prism/render/modules/world/world_module.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class Framework;
class FrameworkContext;
class WorldPipeline;
struct WorldModuleContext;

struct ToneMappingModuleContext;

// The exposure carried from frame to frame: exposure.comp writes it; the tone mapping, bloom and Bedrock curve passes
// and the HDR display output read it.
struct ToneMappingModuleExposureData {
    float exposure;
    float avgLogLum;
    float padding0;
    float padding1;
};
static_assert(sizeof(ToneMappingModuleExposureData) == 16, "the layout the shaders read");

enum ToneMappingMethod : int32_t {
    TONE_MAPPING_METHOD_PBR_NEUTRAL = 0,
    TONE_MAPPING_METHOD_REINHARD = 1,
    TONE_MAPPING_METHOD_REINHARD_WHITE_POINT = 2,
    TONE_MAPPING_METHOD_ACES_FITTED = 3,
    TONE_MAPPING_METHOD_ACES_FITTED_WHITE_POINT = 4,
    TONE_MAPPING_METHOD_UNCHARTED2 = 5,
    // The Bedrock-style mapper: a tone curve built from the picture's histogram (bedrock_hist.comp,
    // bedrock_curve.comp), then Bedrock's grade.
    TONE_MAPPING_METHOD_BEDROCK_PROVISIONAL = 6,
};

enum ToneMappingExposureMeteringMode : int32_t {
    // The histogram of the whole picture, or of a centred part of it (hist.comp).
    TONE_MAPPING_EXPOSURE_METERING_MODE_GLOBAL = 0,
    TONE_MAPPING_EXPOSURE_METERING_MODE_CENTER = 1,
    // A Bedrock-style light meter over the picture: the power mean of a 16 x 16 grid (exposure.comp).
    TONE_MAPPING_EXPOSURE_METERING_MODE_LIGHT_METER = 2,
    // The incident light, as Bedrock RTX meters it: the light reaching the surfaces in view, sampled by the ray
    // tracer (Buffers::lightMeterBuffer), without their colour and without the sky.
    TONE_MAPPING_EXPOSURE_METERING_MODE_INCIDENT = 3,
    // Bedrock RTX's own exposure rule, limits and rate, on the same incident light.
    TONE_MAPPING_EXPOSURE_METERING_MODE_BEDROCK_ORIGINAL = 4,
};

// The push constants of the histogram, exposure and tone mapping passes: hist.comp and exposure.comp read the leading
// members, tone_mapping.frag all of them.
struct ToneMappingModulePushConstant {
    float log2Min;
    float log2Max;
    float epsilon;
    float lowPercent;
    float highPercent;
    float middleGrey;
    float dt;
    float speedUp;
    float speedDown;
    float minExposure;
    float maxExposure;
    float manualExposure;
    float exposureBias;
    float whitePoint;
    float saturation;
    int toneMappingMethod;
    int autoExposure;
    int clampOutput;
    int exposureMeteringMode;
    float centerMeteringPercent;
    float adaptation;
    // Bedrock-style grade and bloom; their neutral values change nothing.
    float bloomIntensity;
    float contrast;
    float shadowContrast;
    float shadowContrastEnd;
    float gamma;
    float balanceR;
    float balanceG;
    float balanceB;
    float filmicSaturation;
};
static_assert(sizeof(ToneMappingModulePushConstant) == 30 * 4,
              "the layout tone_mapping.frag reads, within the 128 bytes of push constants every device has");

// The push constants of the Bedrock-style tone curve passes (bedrock_hist.comp, bedrock_curve.comp). Pushed over the
// same range as ToneMappingModulePushConstant, which is pushed again before the tone mapping draw.
struct ToneMappingBedrockCurvePushConstant {
    float dynamicRange;
    float curveShift;
    float maxExposureIncrease;
    float shadowMinSlope;
    float shadowContrast;
    float shadowContrastEnd;
    int32_t needsReset;
    int32_t autoExposure;
    float manualExposure;
    float exposureBias;
};
static_assert(sizeof(ToneMappingBedrockCurvePushConstant) == 10 * 4, "the layout the curve passes read");
static_assert(sizeof(ToneMappingBedrockCurvePushConstant) <= sizeof(ToneMappingModulePushConstant),
              "the curve passes push within the tone mapping range");

// The push constants of one bloom pass (bloom_down.frag, bloom_up.frag).
struct ToneMappingBloomPushConstant {
    float srcTexelX;
    float srcTexelY;
    int32_t firstPass;
    int32_t autoExposure;
    float manualExposure;
    float exposureBias;
    float threshold;
    float scatter;
    float weight;
    float padding0;
};
static_assert(sizeof(ToneMappingBloomPushConstant) == 10 * 4, "the layout the bloom passes read");

// Tone mapping: the exposure follows the scene (hist.comp, exposure.comp), the radiance is exposed, given bloom if
// that is on, and mapped to the displayable image by one of the classic curves or the Bedrock-style curve and grade
// (tone_mapping.frag).
class ToneMappingModule : public WorldModule, public SharedObject<ToneMappingModule> {
    friend ToneMappingModuleContext;

  public:
    constexpr static std::string_view NAME = "render_pipeline.module.tone_mapping.name";
    constexpr static uint32_t inputImageNum = 1;  // the radiance
    constexpr static uint32_t outputImageNum = 1; // the tone mapped image

    // Bedrock's bloom runs one uniform downscale, four Gaussian downscales and four upscales. The levels are the
    // downscaled images (1/2, 1/4, 1/8, 1/16 and 1/32 of the picture) and the combined upscaled ones (1/16 .. 1/2).
    static constexpr uint32_t BLOOM_DOWN_LEVELS = 5;
    static constexpr uint32_t BLOOM_UP_LEVELS = 4;
    static constexpr uint32_t BLOOM_PASSES = BLOOM_DOWN_LEVELS + BLOOM_UP_LEVELS;

    ToneMappingModule();

    void init(std::shared_ptr<Framework> framework, std::shared_ptr<WorldPipeline> worldPipeline);

    bool setOrCreateInputImages(std::vector<std::shared_ptr<gpu::DeviceLocalImage>> &images,
                                std::vector<VkFormat> &formats,
                                uint32_t frameIndex) override;
    bool setOrCreateOutputImages(std::vector<std::shared_ptr<gpu::DeviceLocalImage>> &images,
                                 std::vector<VkFormat> &formats,
                                 uint32_t frameIndex) override;

    void setAttributes(int attributeCount, std::vector<std::string> &attributeKVs) override;
    // Every attribute but bloom_enable: the settings reach the passes as push constants, rebuilt here.
    bool setRuntimeAttribute(const std::string &key, const std::string &value) override;

    void build() override;

    std::vector<std::shared_ptr<WorldModuleContext>> &contexts() override;

    void bindTexture(std::shared_ptr<gpu::Sampler> sampler,
                     std::shared_ptr<gpu::DeviceLocalImage> image,
                     int index) override;

    void preClose() override;

    // What the HDR display output (HdrOutput) needs to tone map the same radiance the same way, brighter.
    std::shared_ptr<gpu::DeviceLocalBuffer> exposureBuffer() const {
        return exposure_;
    }
    float exposureBias() const {
        return settings_.exposureBias;
    }
    float saturation() const {
        return settings_.saturation;
    }
    float manualExposure() const {
        return settings_.manualExposure;
    }
    bool isAutoExposureEnabled() const {
        return settings_.autoExposure;
    }

  private:
    // The module's attributes (modules/tone_mapping.yaml), at the values used when a pipeline does not set them.
    struct Settings {
        // Exposure
        bool autoExposure = true;
        int meteringMode = TONE_MAPPING_EXPOSURE_METERING_MODE_GLOBAL;
        float centerMeteringPercent = 20.0f;
        // How far the exposure follows the scene: 1 all the way, 0 not at all (held at manualExposure).
        float adaptation = 1.0f;
        float middleGrey = 0.18f;
        float speedUp = 3.0f;
        float speedDown = 3.0f;
        float log2Min = -12.0f;
        float log2Max = 4.0f;
        float epsilon = 1e-6f;
        float lowPercent = 0.005f;
        float highPercent = 0.99f;
        float minExposure = 1e-4f;
        float maxExposure = 1.2f;
        float manualExposure = 1.0f;
        float exposureBias = 0.0f;
        // Mapping
        int method = TONE_MAPPING_METHOD_ACES_FITTED;
        float whitePoint = 11.2f;
        float saturation = 1.0f;
        bool clampOutput = true;
        // Bedrock-style grade, neutral by default.
        float contrast = 1.0f;
        float shadowContrast = 0.0f;
        float shadowContrastEnd = 0.1f;
        float gamma = 2.2f;
        std::array<float, 3> colorBalance = {1.0f, 1.0f, 1.0f};
        float filmicSaturation = 0.0f;
        // Bedrock-style tone curve.
        float curveDynamicRange = 8.0f;
        float curveShift = 0.0f;
        float curveMaxExposureIncrease = 2.0f;
        float curveShadowMinSlope = 0.0f;
        // Bloom, off by default (the Bedrock RTX profile turns it on). The filter weights are stand-ins.
        bool bloomEnabled = false;
        float bloomIntensity = 0.04f;
        float bloomScatter = 0.5f;
        float bloomThreshold = 0.0f;
        std::array<float, BLOOM_UP_LEVELS> bloomWeights = {1.0f, 1.0f, 1.0f, 1.0f};
    };

    // One bloom pass of one frame: it draws `target` from the images its descriptors name.
    struct BloomPass {
        std::shared_ptr<gpu::DeviceLocalImage> target;
        std::shared_ptr<gpu::DescriptorTable> descriptors;
        std::shared_ptr<gpu::Framebuffer> framebuffer;
        ToneMappingBloomPushConstant constants;
    };

    // What one frame in flight records with.
    struct Frame {
        std::shared_ptr<gpu::DeviceLocalImage> hdr; // the input radiance
        std::shared_ptr<gpu::DeviceLocalImage> ldr; // the output
        std::shared_ptr<gpu::DescriptorTable> descriptors;
        std::shared_ptr<gpu::DeviceLocalBuffer> histogram; // hist.comp's bins, cleared each frame
        std::shared_ptr<gpu::Framebuffer> framebuffer;
        // In the order they run: the downscales, then the upscales from the coarsest level.
        std::array<BloomPass, BLOOM_PASSES> bloomPasses;
    };

    // Sets the attribute `key` (its full name) from `value`; false for a name the module does not know or a value
    // that does not read, which leaves the setting as it was.
    bool applyAttribute(std::string_view key, const std::string &value);
    // A bloom pass's constants that come from the settings, `index` being the pass's place in bloomPasses.
    void setBloomPassSettings(ToneMappingBloomPushConstant &constants, uint32_t index) const;
    ToneMappingModulePushConstant makePushConstants() const;
    ToneMappingBedrockCurvePushConstant makeCurvePushConstants() const;
    void createBuffers(Framework &framework);
    void createDescriptorTables(Framework &framework);
    void createPipelines(Framework &framework, const std::shared_ptr<gpu::Shader> &vertexShader);
    void createBloom(Framework &framework, const std::shared_ptr<gpu::Shader> &vertexShader);
    void record(uint32_t frameIndex, gpu::CommandBuffer &commandBuffer);
    void recordBedrockCurve(Frame &frame, gpu::CommandBuffer &commandBuffer);
    void recordBloom(Frame &frame, gpu::CommandBuffer &commandBuffer);

    Settings settings_;
    std::vector<Frame> frames_;
    std::vector<std::shared_ptr<WorldModuleContext>> contexts_;
    uint32_t width_ = 0;
    uint32_t height_ = 0;

    std::shared_ptr<gpu::Sampler> sampler_;
    // One exposure for all frames, carried from each to the next.
    std::shared_ptr<gpu::DeviceLocalBuffer> exposure_;
    // The Bedrock-style histogram (uint[256]) and tone curve (float[256]), one for all frames; the curve is eased from
    // frame to frame.
    std::shared_ptr<gpu::DeviceLocalBuffer> bedrockCurve_;
    std::shared_ptr<gpu::ComputePipeline> histogramPipeline_;
    std::shared_ptr<gpu::ComputePipeline> exposurePipeline_;
    std::shared_ptr<gpu::ComputePipeline> bedrockHistogramPipeline_;
    std::shared_ptr<gpu::ComputePipeline> bedrockCurvePipeline_;
    std::shared_ptr<gpu::RenderPass> renderPass_;
    std::shared_ptr<gpu::GraphicsPipeline> pipeline_;

    std::shared_ptr<gpu::Sampler> bloomSampler_;
    std::shared_ptr<gpu::RenderPass> bloomRenderPass_;
    // One per pass: a pipeline bakes its viewport, and every pass draws a different size.
    std::array<std::shared_ptr<gpu::GraphicsPipeline>, BLOOM_PASSES> bloomPipelines_;

    // Made at a build and again when a setting changes live; dt is filled in every frame.
    ToneMappingModulePushConstant pushConstants_{};
    ToneMappingBedrockCurvePushConstant curvePushConstants_{};
    // Set when the buffers are made: the first frame starts the exposure and the curve over.
    bool exposureNeedsReset_ = true;
    bool bedrockCurveNeedsReset_ = true;
    std::chrono::steady_clock::time_point lastFrameTime_;
};

struct ToneMappingModuleContext : public WorldModuleContext, SharedObject<ToneMappingModuleContext> {
    std::weak_ptr<ToneMappingModule> toneMappingModule;

    // This frame's radiance and tone mapped image, which the HDR display output reads too.
    std::shared_ptr<gpu::DeviceLocalImage> hdrImage;
    std::shared_ptr<gpu::DeviceLocalImage> ldrImage;

    ToneMappingModuleContext(std::shared_ptr<FrameworkContext> frameworkContext,
                             std::shared_ptr<WorldPipelineContext> worldPipelineContext,
                             std::shared_ptr<ToneMappingModule> toneMappingModule);

    void render() override;

  private:
    uint32_t frameIndex_;
};
