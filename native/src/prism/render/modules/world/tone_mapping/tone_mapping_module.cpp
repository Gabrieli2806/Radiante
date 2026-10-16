#include "prism/render/modules/world/tone_mapping/tone_mapping_module.hpp"

#include "prism/render/buffers.hpp"
#include "prism/render/pipeline.hpp"
#include "prism/render/render_framework.hpp"
#include "prism/render/renderer.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <optional>
#include <span>

namespace {

// The layout the tone mapped image is drawn in and handed on in: its render pass takes it in and leaves it there.
#ifdef USE_AMD
constexpr VkImageLayout kOutputLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
#else
constexpr VkImageLayout kOutputLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
#endif

constexpr VkImageUsageFlags kImageUsage =
    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
constexpr VkFormat kBloomFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr uint32_t kHistogramBins = 256;
// hist.comp and bedrock_hist.comp give a group 16 x 16 pixels.
constexpr uint32_t kGroupSize = 16;

constexpr VkPipelineStageFlags2 kCompute = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
constexpr VkPipelineStageFlags2 kFragment = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
constexpr VkPipelineStageFlags2 kTransfer = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
constexpr VkPipelineStageFlags2 kRayTracing = VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
constexpr VkAccessFlags2 kWrites = VK_ACCESS_2_MEMORY_WRITE_BIT;
constexpr VkAccessFlags2 kReadsAndWrites = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
constexpr VkShaderStageFlags kComputeAndFragment = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

// ---- settings

constexpr std::string_view kAttributePrefix = "render_pipeline.module.tone_mapping.attribute.";
constexpr float kUnbounded = std::numeric_limits<float>::infinity();

// A number: its name after kAttributePrefix, where it is kept, and the range it is held to.
struct NumberSetting {
    std::string_view name;
    float *value;
    float min;
    float max;
};

// On or off, given as "render_pipeline.true" or "render_pipeline.false".
struct SwitchSetting {
    std::string_view name;
    bool *value;
};

struct Choice {
    std::string_view name;
    int value;
};

// One of a list, given as the setting's full key, a dot and the choice's name.
struct ChoiceSetting {
    std::string_view name;
    int *value;
    std::span<const Choice> choices;
};

constexpr Choice kMethods[] = {
    {"pbr_neutral", TONE_MAPPING_METHOD_PBR_NEUTRAL},
    {"reinhard", TONE_MAPPING_METHOD_REINHARD},
    {"reinhard_white_point", TONE_MAPPING_METHOD_REINHARD_WHITE_POINT},
    {"aces", TONE_MAPPING_METHOD_ACES_FITTED},
    {"aces_white_point", TONE_MAPPING_METHOD_ACES_FITTED_WHITE_POINT},
    {"uncharted2", TONE_MAPPING_METHOD_UNCHARTED2},
    {"bedrock_provisional", TONE_MAPPING_METHOD_BEDROCK_PROVISIONAL},
};

constexpr Choice kMeteringModes[] = {
    {"global", TONE_MAPPING_EXPOSURE_METERING_MODE_GLOBAL},
    {"center", TONE_MAPPING_EXPOSURE_METERING_MODE_CENTER},
    {"light_meter", TONE_MAPPING_EXPOSURE_METERING_MODE_LIGHT_METER},
    {"incident", TONE_MAPPING_EXPOSURE_METERING_MODE_INCIDENT},
    {"bedrock_original", TONE_MAPPING_EXPOSURE_METERING_MODE_BEDROCK_ORIGINAL},
};

// The number text starts with, as std::stof reads it; none when it starts with no number or one out of float range.
std::optional<float> parseNumber(const std::string &text) {
    try {
        return std::stof(text);
    } catch (const std::exception &) { return std::nullopt; }
}

// ---- Vulkan objects

uint32_t groupsFor(uint32_t pixels) {
    return (pixels + kGroupSize - 1) / kGroupSize;
}

VkDescriptorSetLayoutBinding layoutBinding(uint32_t binding, VkDescriptorType type, VkShaderStageFlags stages) {
    return {.binding = binding, .descriptorType = type, .descriptorCount = 1, .stageFlags = stages};
}

// A table of one descriptor set, with one push constant range.
std::shared_ptr<gpu::DescriptorTable> makeDescriptorTable(const std::shared_ptr<gpu::Device> &device,
                                                          std::initializer_list<VkDescriptorSetLayoutBinding> bindings,
                                                          VkPushConstantRange pushConstants) {
    gpu::DescriptorTableBuilder builder;
    auto &set = builder.beginDescriptorLayoutSet().beginDescriptorLayoutSetBinding();
    for (const VkDescriptorSetLayoutBinding &binding : bindings) set.defineDescriptorLayoutSetBinding(binding);
    return set.endDescriptorLayoutSetBinding().endDescriptorLayoutSet().definePushConstant(pushConstants).build(device);
}

// A render pass of one colour target, loaded with loadOp, kept, and in `layout` before and after the pass.
std::shared_ptr<gpu::RenderPass> makeRenderPass(const std::shared_ptr<gpu::Device> &device,
                                                VkFormat format,
                                                VkAttachmentLoadOp loadOp,
                                                VkImageLayout layout) {
    return gpu::RenderPassBuilder{}
        .beginAttachmentDescription()
        .defineAttachmentDescription({
            .format = format,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = loadOp,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = layout,
            .finalLayout = layout,
        })
        .endAttachmentDescription()
        .beginAttachmentReference()
        .defineAttachmentReference({.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL})
        .endAttachmentReference()
        .beginSubpassDescription()
        .defineSubpassDescription({
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentIndices = {0},
        })
        .endSubpassDescription()
        .build(device);
}

std::shared_ptr<gpu::Framebuffer> makeFramebuffer(const std::shared_ptr<gpu::Device> &device,
                                                  const std::shared_ptr<gpu::RenderPass> &renderPass,
                                                  const std::shared_ptr<gpu::DeviceLocalImage> &target) {
    gpu::FramebufferBuilder builder;
    return builder.beginAttachment().defineAttachment(target).endAttachment().build(device, renderPass);
}

// One triangle over the whole of a target `extent` in size: no vertex input, no depth, the target written as is.
std::shared_ptr<gpu::GraphicsPipeline> makeFullScreenPipeline(const std::shared_ptr<gpu::Device> &device,
                                                              const std::shared_ptr<gpu::RenderPass> &renderPass,
                                                              const std::shared_ptr<gpu::Shader> &vertexShader,
                                                              const std::shared_ptr<gpu::Shader> &fragmentShader,
                                                              const std::shared_ptr<gpu::DescriptorTable> &layout,
                                                              VkExtent2D extent) {
    return gpu::GraphicsPipelineBuilder{}
        .defineRenderPass(renderPass, 0)
        .beginShaderStage()
        .defineShaderStage(vertexShader, VK_SHADER_STAGE_VERTEX_BIT)
        .defineShaderStage(fragmentShader, VK_SHADER_STAGE_FRAGMENT_BIT)
        .endShaderStage()
        .defineVertexInputState<void>()
        .defineViewportScissorState({
            .viewport =
                {
                    .x = 0.0f,
                    .y = 0.0f,
                    .width = static_cast<float>(extent.width),
                    .height = static_cast<float>(extent.height),
                    .minDepth = 0.0f,
                    .maxDepth = 1.0f,
                },
            .scissor = {.offset = {0, 0}, .extent = extent},
        })
        .defineDepthStencilState({
            .depthTestEnable = VK_FALSE,
            .depthWriteEnable = VK_FALSE,
            .depthCompareOp = VK_COMPARE_OP_ALWAYS,
            .depthBoundsTestEnable = VK_FALSE,
            .stencilTestEnable = VK_FALSE,
        })
        .beginColorBlendAttachmentState()
        .defineDefaultColorBlendAttachmentState()
        .endColorBlendAttachmentState()
        .definePipelineLayout(layout)
        .build(device);
}

} // namespace

ToneMappingModule::ToneMappingModule() {}

void ToneMappingModule::init(std::shared_ptr<Framework> framework, std::shared_ptr<WorldPipeline> worldPipeline) {
    WorldModule::init(framework, worldPipeline);
    frames_.resize(framework->swapchain()->imageCount());
}

bool ToneMappingModule::setOrCreateInputImages(std::vector<std::shared_ptr<gpu::DeviceLocalImage>> &images,
                                               std::vector<VkFormat> &formats,
                                               uint32_t frameIndex) {
    if (images.empty()) return false;

    if (images[0] == nullptr) {
        auto framework = framework_.lock();
        images[0] = gpu::DeviceLocalImage::create(framework->device(), framework->vma(), false, width_, height_, 1,
                                                  formats[0], kImageUsage);
    } else if (images[0]->width() != width_ || images[0]->height() != height_) {
        return false;
    }
    frames_[frameIndex].hdr = images[0];
    return true;
}

bool ToneMappingModule::setOrCreateOutputImages(std::vector<std::shared_ptr<gpu::DeviceLocalImage>> &images,
                                                std::vector<VkFormat> &,
                                                uint32_t frameIndex) {
    if (images.empty() || images[0] == nullptr) return false;

    width_ = images[0]->width();
    height_ = images[0]->height();
    frames_[frameIndex].ldr = images[0];
    return true;
}

void ToneMappingModule::setAttributes(int attributeCount, std::vector<std::string> &attributeKVs) {
    for (int i = 0; i < attributeCount; i++) applyAttribute(attributeKVs[2 * i], attributeKVs[2 * i + 1]);
}

bool ToneMappingModule::applyAttribute(std::string_view key, const std::string &value) {
    if (!key.starts_with(kAttributePrefix)) return false;
    const std::string_view name = key.substr(kAttributePrefix.size());
    Settings &s = settings_;
    // A value that does not parse leaves the setting as it was.
    const NumberSetting numbers[] = {
        {"middle_grey", &s.middleGrey, 1e-4f, kUnbounded},
        {"exposure_up_speed", &s.speedUp, 0.0f, kUnbounded},
        {"exposure_down_speed", &s.speedDown, 0.0f, kUnbounded},
        {"log2_luminance_min", &s.log2Min, -kUnbounded, kUnbounded},
        {"log2_luminance_max", &s.log2Max, -kUnbounded, kUnbounded},
        {"histogram_epsilon", &s.epsilon, 1e-8f, kUnbounded},
        {"low_percent", &s.lowPercent, -kUnbounded, kUnbounded},
        {"high_percent", &s.highPercent, -kUnbounded, kUnbounded},
        {"min_exposure", &s.minExposure, 1e-6f, kUnbounded},
        {"max_exposure", &s.maxExposure, 1e-6f, kUnbounded},
        {"manual_exposure", &s.manualExposure, 1e-6f, kUnbounded},
        {"exposure_bias", &s.exposureBias, -kUnbounded, kUnbounded},
        {"white_point", &s.whitePoint, 1e-3f, kUnbounded},
        {"saturation", &s.saturation, 0.0f, kUnbounded},
        {"center_metering_percent", &s.centerMeteringPercent, 1.0f, 100.0f},
        {"exposure_adaptation", &s.adaptation, 0.0f, 1.0f},
        {"bloom_intensity", &s.bloomIntensity, 0.0f, 4.0f},
        {"bloom_scatter", &s.bloomScatter, 0.0f, 1.0f},
        {"bloom_threshold", &s.bloomThreshold, 0.0f, 64.0f},
        {"bloom_weight_1", &s.bloomWeights[0], 0.0f, 4.0f},
        {"bloom_weight_2", &s.bloomWeights[1], 0.0f, 4.0f},
        {"bloom_weight_3", &s.bloomWeights[2], 0.0f, 4.0f},
        {"bloom_weight_4", &s.bloomWeights[3], 0.0f, 4.0f},
        {"contrast", &s.contrast, 0.25f, 4.0f},
        {"shadow_contrast", &s.shadowContrast, -2.0f, 2.0f},
        {"shadow_contrast_end", &s.shadowContrastEnd, -8.0f, 4.0f},
        {"gamma", &s.gamma, 0.5f, 3.0f},
        {"curve_dynamic_range", &s.curveDynamicRange, 2.6f, 20.0f},
        {"curve_shift", &s.curveShift, -4.0f, 4.0f},
        {"curve_max_exposure_increase", &s.curveMaxExposureIncrease, 0.0f, 16.0f},
        {"curve_shadow_min_slope", &s.curveShadowMinSlope, 0.0f, 1.0f},
        {"filmic_saturation", &s.filmicSaturation, 0.0f, 2.0f},
        {"color_balance_r", &s.colorBalance[0], 0.0f, 4.0f},
        {"color_balance_g", &s.colorBalance[1], 0.0f, 4.0f},
        {"color_balance_b", &s.colorBalance[2], 0.0f, 4.0f},
    };
    const SwitchSetting switches[] = {
        {"enable_auto_exposure", &s.autoExposure},
        {"clamp_output", &s.clampOutput},
        {"bloom_enable", &s.bloomEnabled},
    };
    const ChoiceSetting choices[] = {
        {"method", &s.method, kMethods},
        {"exposure_metering_mode", &s.meteringMode, kMeteringModes},
    };

    for (const NumberSetting &setting : numbers) {
        if (setting.name != name) continue;
        std::optional<float> number = parseNumber(value);
        if (!number) return false;
        *setting.value = std::clamp(*number, setting.min, setting.max);
        return true;
    }
    for (const SwitchSetting &setting : switches) {
        if (setting.name != name) continue;
        if (value == "render_pipeline.true") *setting.value = true;
        else if (value == "render_pipeline.false") *setting.value = false;
        else return false;
        return true;
    }
    for (const ChoiceSetting &setting : choices) {
        if (setting.name != name) continue;
        std::string_view chosen = value;
        if (!chosen.starts_with(key) || chosen.size() <= key.size() || chosen[key.size()] != '.') return false;
        chosen.remove_prefix(key.size() + 1);
        for (const Choice &choice : setting.choices) {
            if (choice.name != chosen) continue;
            *setting.value = choice.value;
            return true;
        }
        return false;
    }
    return false;
}

bool ToneMappingModule::setRuntimeAttribute(const std::string &key, const std::string &value) {
    // Whether bloom runs decides which images and pipelines the build makes.
    if (key.ends_with(".bloom_enable")) return false;
    if (!applyAttribute(key, value)) return false;

    const bool wasBedrockCurve = pushConstants_.toneMappingMethod == TONE_MAPPING_METHOD_BEDROCK_PROVISIONAL;
    pushConstants_ = makePushConstants();
    curvePushConstants_ = makeCurvePushConstants();
    // The curve buffer still holds what the Bedrock-style curve left when it last ran, maybe long ago: it is built
    // afresh rather than eased from that.
    if (!wasBedrockCurve && pushConstants_.toneMappingMethod == TONE_MAPPING_METHOD_BEDROCK_PROVISIONAL) {
        bedrockCurveNeedsReset_ = true;
    }
    if (settings_.bloomEnabled) {
        for (Frame &frame : frames_) {
            for (uint32_t index = 0; index < BLOOM_PASSES; index++) {
                setBloomPassSettings(frame.bloomPasses[index].constants, index);
            }
        }
    }
    return true;
}

void ToneMappingModule::build() {
    auto framework = framework_.lock();
    auto worldPipeline = worldPipeline_.lock();

    pushConstants_ = makePushConstants();
    curvePushConstants_ = makeCurvePushConstants();

    createBuffers(*framework);
    createDescriptorTables(*framework);
    // The full-screen triangle of the tone mapping pass serves the bloom passes too.
    auto vertexShader = gpu::Shader::create(
        framework->device(), (Renderer::folderPath / "shaders" / "world/tone_mapping/tone_mapping_vert.spv").string());
    createPipelines(*framework, vertexShader);
    createBloom(*framework, vertexShader);

    contexts_.resize(frames_.size());
    for (uint32_t i = 0; i < frames_.size(); i++) {
        contexts_[i] = ToneMappingModuleContext::create(framework->contexts()[i], worldPipeline->contexts()[i],
                                                        shared_from_this());
    }

    lastFrameTime_ = std::chrono::steady_clock::now();
}

std::vector<std::shared_ptr<WorldModuleContext>> &ToneMappingModule::contexts() {
    return contexts_;
}

void ToneMappingModule::bindTexture(std::shared_ptr<gpu::Sampler>, std::shared_ptr<gpu::DeviceLocalImage>, int) {}

void ToneMappingModule::preClose() {}

ToneMappingModulePushConstant ToneMappingModule::makePushConstants() const {
    const Settings &s = settings_;
    ToneMappingModulePushConstant constants{};
    // What the shaders rely on: a luminance range and a percentile window that are not empty, and an exposure range
    // whose top is not below its bottom.
    constants.log2Min = std::min(s.log2Min, s.log2Max - 1e-3f);
    constants.log2Max = std::max(s.log2Max, constants.log2Min + 1e-3f);
    constants.epsilon = std::max(s.epsilon, 1e-8f);
    constants.lowPercent = std::clamp(s.lowPercent, 0.0f, 0.9999f);
    constants.highPercent = std::clamp(s.highPercent, constants.lowPercent + 1e-4f, 1.0f);
    constants.middleGrey = std::max(s.middleGrey, 1e-4f);
    constants.speedUp = std::max(s.speedUp, 0.0f);
    constants.speedDown = std::max(s.speedDown, 0.0f);
    constants.minExposure = std::max(s.minExposure, 1e-6f);
    constants.maxExposure = std::max(s.maxExposure, constants.minExposure);
    constants.manualExposure = std::max(s.manualExposure, 1e-6f);
    constants.exposureBias = s.exposureBias;
    constants.whitePoint = std::max(s.whitePoint, 1e-3f);
    constants.saturation = std::max(s.saturation, 0.0f);
    constants.toneMappingMethod = std::clamp(s.method, static_cast<int>(TONE_MAPPING_METHOD_PBR_NEUTRAL),
                                             static_cast<int>(TONE_MAPPING_METHOD_BEDROCK_PROVISIONAL));
    constants.autoExposure = s.autoExposure ? 1 : 0;
    constants.clampOutput = s.clampOutput ? 1 : 0;
    constants.exposureMeteringMode =
        std::clamp(s.meteringMode, static_cast<int>(TONE_MAPPING_EXPOSURE_METERING_MODE_GLOBAL),
                   static_cast<int>(TONE_MAPPING_EXPOSURE_METERING_MODE_BEDROCK_ORIGINAL));
    constants.centerMeteringPercent = std::clamp(s.centerMeteringPercent, 1.0f, 100.0f) / 100.0f;
    constants.adaptation = std::clamp(s.adaptation, 0.0f, 1.0f);
    constants.bloomIntensity = s.bloomEnabled ? s.bloomIntensity : 0.0f;
    constants.contrast = s.contrast;
    constants.shadowContrast = s.shadowContrast;
    constants.shadowContrastEnd = s.shadowContrastEnd;
    constants.gamma = s.gamma;
    constants.balanceR = s.colorBalance[0];
    constants.balanceG = s.colorBalance[1];
    constants.balanceB = s.colorBalance[2];
    constants.filmicSaturation = s.filmicSaturation;
    return constants;
}

ToneMappingBedrockCurvePushConstant ToneMappingModule::makeCurvePushConstants() const {
    const Settings &s = settings_;
    return {
        .dynamicRange = s.curveDynamicRange,
        .curveShift = s.curveShift,
        .maxExposureIncrease = s.curveMaxExposureIncrease,
        .shadowMinSlope = s.curveShadowMinSlope,
        .shadowContrast = s.shadowContrast,
        .shadowContrastEnd = s.shadowContrastEnd,
        .needsReset = 0,
        .autoExposure = s.autoExposure ? 1 : 0,
        .manualExposure = std::max(s.manualExposure, 1e-6f),
        .exposureBias = s.exposureBias,
    };
}

void ToneMappingModule::createBuffers(Framework &framework) {
    auto device = framework.device();
    auto vma = framework.vma();

    // Only the GPU touches these, so they keep no staging copy. The transfer uses (the clears) are always added.
    exposure_ = gpu::DeviceLocalBuffer::create(vma, device, false, sizeof(ToneMappingModuleExposureData),
                                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    bedrockCurve_ = gpu::DeviceLocalBuffer::create(vma, device, false, 2 * kHistogramBins * sizeof(uint32_t),
                                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    for (Frame &frame : frames_) {
        frame.histogram = gpu::DeviceLocalBuffer::create(vma, device, false, kHistogramBins * sizeof(uint32_t),
                                                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    }
    exposureNeedsReset_ = true;
    bedrockCurveNeedsReset_ = true;
}

void ToneMappingModule::createDescriptorTables(Framework &framework) {
    auto device = framework.device();
    sampler_ =
        gpu::Sampler::create(device, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT);
    auto lightMeter = Renderer::instance().buffers()->lightMeterBuffer();

    for (Frame &frame : frames_) {
        frame.descriptors = makeDescriptorTable(
            device,
            {
                layoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kComputeAndFragment), // the radiance
                layoutBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kComputeAndFragment),         // the histogram
                layoutBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kComputeAndFragment),         // the exposure
                // The bloom pyramid's result.
                layoutBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
                // The Bedrock-style histogram and tone curve.
                layoutBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kComputeAndFragment),
                // The incident light meter the ray tracing writes.
                layoutBinding(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT),
            },
            {.stageFlags = kComputeAndFragment, .offset = 0, .size = sizeof(ToneMappingModulePushConstant)});

        frame.descriptors->bindSamplerImageForShader(sampler_, frame.hdr, 0, 0);
        frame.descriptors->bindBuffer(frame.histogram, 0, 1);
        frame.descriptors->bindBuffer(exposure_, 0, 2);
        frame.descriptors->bindBuffer(bedrockCurve_, 0, 4);
        frame.descriptors->bindBuffer(lightMeter, 0, 5);
    }
}

void ToneMappingModule::createPipelines(Framework &framework, const std::shared_ptr<gpu::Shader> &vertexShader) {
    auto device = framework.device();
    std::filesystem::path shaderPath = Renderer::folderPath / "shaders";
    const std::shared_ptr<gpu::DescriptorTable> &layout = frames_[0].descriptors;

    auto computePipeline = [&](const char *spirv) {
        auto shader = gpu::Shader::create(device, (shaderPath / spirv).string());
        return gpu::ComputePipelineBuilder{}.defineShader(shader).definePipelineLayout(layout).build(device);
    };
    histogramPipeline_ = computePipeline("world/tone_mapping/hist_comp.spv");
    exposurePipeline_ = computePipeline("world/tone_mapping/exposure_comp.spv");
    bedrockHistogramPipeline_ = computePipeline("world/tone_mapping/bedrock_hist_comp.spv");
    bedrockCurvePipeline_ = computePipeline("world/tone_mapping/bedrock_curve_comp.spv");

    // The pass clears the output as it begins.
    renderPass_ = makeRenderPass(device, frames_[0].ldr->vkFormat(), VK_ATTACHMENT_LOAD_OP_CLEAR, kOutputLayout);
    for (Frame &frame : frames_) frame.framebuffer = makeFramebuffer(device, renderPass_, frame.ldr);
    auto fragmentShader =
        gpu::Shader::create(device, (shaderPath / "world/tone_mapping/tone_mapping_frag.spv").string());
    pipeline_ = makeFullScreenPipeline(device, renderPass_, vertexShader, fragmentShader, layout, {width_, height_});
}

void ToneMappingModule::createBloom(Framework &framework, const std::shared_ptr<gpu::Shader> &vertexShader) {
    if (!settings_.bloomEnabled) {
        // Nothing is drawn and the tone mapping pass adds none of it, but the binding still has to name an image.
        for (Frame &frame : frames_) frame.descriptors->bindSamplerImageForShader(sampler_, frame.hdr, 0, 3);
        return;
    }

    auto device = framework.device();
    auto vma = framework.vma();
    bloomSampler_ = gpu::Sampler::create(device, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST,
                                         VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    // Each pass draws over the whole of its target, so none is loaded.
    bloomRenderPass_ =
        makeRenderPass(device, kBloomFormat, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    std::filesystem::path shaderPath = Renderer::folderPath / "shaders";
    auto downShader = gpu::Shader::create(device, (shaderPath / "world/tone_mapping/bloom_down_frag.spv").string());
    auto upShader = gpu::Shader::create(device, (shaderPath / "world/tone_mapping/bloom_up_frag.spv").string());

    // Level i is 1 / 2^(i + 1) of the picture's size.
    auto makeLevel = [&](uint32_t level) {
        return gpu::DeviceLocalImage::create(device, vma, false, std::max(1u, width_ >> (level + 1)),
                                             std::max(1u, height_ >> (level + 1)), 1, kBloomFormat,
                                             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    };

    for (size_t frameIndex = 0; frameIndex < frames_.size(); frameIndex++) {
        Frame &frame = frames_[frameIndex];
        std::array<std::shared_ptr<gpu::DeviceLocalImage>, BLOOM_DOWN_LEVELS> down;
        std::array<std::shared_ptr<gpu::DeviceLocalImage>, BLOOM_UP_LEVELS> up;
        for (uint32_t level = 0; level < BLOOM_DOWN_LEVELS; level++) down[level] = makeLevel(level);
        for (uint32_t level = 0; level < BLOOM_UP_LEVELS; level++) up[level] = makeLevel(level);

        for (uint32_t index = 0; index < BLOOM_PASSES; index++) {
            BloomPass &pass = frame.bloomPasses[index];
            // A downscale reads one image, the picture and then each level, through both bindings. An upscale, from
            // the coarsest level on, spreads the level below it (the coarsest downscaled one at first, then the last
            // combined one) over its own level as that was downscaled, and keeps `weight` of the latter.
            std::shared_ptr<gpu::DeviceLocalImage> coarser;
            std::shared_ptr<gpu::DeviceLocalImage> own;
            if (index < BLOOM_DOWN_LEVELS) {
                coarser = own = index == 0 ? frame.hdr : down[index - 1];
                pass.target = down[index];
            } else {
                uint32_t level = BLOOM_PASSES - 1 - index; // 3, 2, 1, 0
                coarser = level == BLOOM_UP_LEVELS - 1 ? down[BLOOM_DOWN_LEVELS - 1] : up[level + 1];
                own = down[level];
                pass.target = up[level];
            }

            pass.descriptors = makeDescriptorTable(
                device,
                {
                    layoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
                    layoutBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
                    layoutBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT),
                },
                {.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                 .offset = 0,
                 .size = sizeof(ToneMappingBloomPushConstant)});
            pass.descriptors->bindSamplerImageForShader(bloomSampler_, coarser, 0, 0);
            pass.descriptors->bindSamplerImageForShader(bloomSampler_, own, 0, 1);
            pass.descriptors->bindBuffer(exposure_, 0, 2);
            pass.framebuffer = makeFramebuffer(device, bloomRenderPass_, pass.target);
            pass.constants = {
                .srcTexelX = 1.0f / static_cast<float>(coarser->width()),
                .srcTexelY = 1.0f / static_cast<float>(coarser->height()),
                .firstPass = index == 0 ? 1 : 0,
            };
            setBloomPassSettings(pass.constants, index);

            // The pipelines are the same for every frame; their layout is the first frame's, which all match.
            if (frameIndex == 0) {
                bloomPipelines_[index] = makeFullScreenPipeline(
                    device, bloomRenderPass_, vertexShader, index < BLOOM_DOWN_LEVELS ? downShader : upShader,
                    pass.descriptors, {pass.target->width(), pass.target->height()});
            }
        }

        // The tone mapping pass adds the finest combined level.
        frame.descriptors->bindSamplerImageForShader(bloomSampler_, up[0], 0, 3);
    }
}

void ToneMappingModule::setBloomPassSettings(ToneMappingBloomPushConstant &constants, uint32_t index) const {
    constants.autoExposure = settings_.autoExposure ? 1 : 0;
    constants.manualExposure = std::max(settings_.manualExposure, 1e-6f);
    constants.exposureBias = settings_.exposureBias;
    constants.threshold = settings_.bloomThreshold;
    constants.scatter = settings_.bloomScatter;
    // An upscale keeps its level's weight of the level as it was downscaled; a downscale has no weight.
    constants.weight = index < BLOOM_DOWN_LEVELS ? 1.0f : settings_.bloomWeights[BLOOM_PASSES - 1 - index];
    constants.padding0 = 0.0f;
}

void ToneMappingModule::record(uint32_t frameIndex, gpu::CommandBuffer &commandBuffer) {
    Frame &frame = frames_[frameIndex];
    VkCommandBuffer cmd = commandBuffer.vkCommandBuffer();

    // Every pass below samples the radiance.
    commandBuffer.sync({{frame.hdr, gpu::ImageUsage::Sampled, kCompute | kFragment}});
    // This frame's histogram is about to be cleared. The output goes into the layout its render pass takes it in,
    // without what it held: the pass clears it.
    const gpu::ImageState &lastUse = frame.ldr->state();
    commandBuffer.barriersBufferImage(
        {{
            .srcStageMask = kCompute | kTransfer,
            .srcAccessMask = kWrites,
            .dstStageMask = kFragment | kCompute | kTransfer,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = frame.histogram,
        }},
        {{
            .srcStageMask =
                lastUse.stages == VK_PIPELINE_STAGE_2_NONE ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT : lastUse.stages,
            .srcAccessMask = lastUse.access,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | kTransfer,
            .dstAccessMask =
                VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | kReadsAndWrites,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = kOutputLayout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = frame.ldr,
            .subresourceRange = gpu::wholeColorSubresourceRange,
        }});
    vkCmdFillBuffer(cmd, frame.histogram->vkBuffer(), 0, VK_WHOLE_SIZE, 0);
    commandBuffer.memoryBarrier(kCompute | kTransfer, kWrites, kFragment | kCompute | kTransfer, kReadsAndWrites);

    // Seconds since the last frame, for the exposure's adaptation: at most 1 (after a stall), 1/60 should the clock
    // give something that is not a number.
    auto now = std::chrono::steady_clock::now();
    float dt = static_cast<float>(std::chrono::duration<double>(now - lastFrameTime_).count());
    lastFrameTime_ = now;
    if (!std::isfinite(dt)) dt = 1.0f / 60.0f;

    ToneMappingModulePushConstant constants = pushConstants_;
    constants.dt = std::clamp(dt, 0.0f, 1.0f);
    auto pushToneMappingConstants = [&] {
        vkCmdPushConstants(cmd, frame.descriptors->vkPipelineLayout(), kComputeAndFragment, 0, sizeof(constants),
                           &constants);
    };
    pushToneMappingConstants();

    commandBuffer.bindDescriptorTable(frame.descriptors, VK_PIPELINE_BIND_POINT_COMPUTE)
        ->bindComputePipeline(histogramPipeline_);
    vkCmdDispatch(cmd, groupsFor(width_), groupsFor(height_), 1);

    if (exposureNeedsReset_) {
        // A new buffer holds no exposure yet. Zero makes exposure.comp start from the manual exposure instead of
        // taking whatever the memory held for one.
        vkCmdFillBuffer(cmd, exposure_->vkBuffer(), 0, VK_WHOLE_SIZE, 0);
        exposureNeedsReset_ = false;
    }
    // The exposure pass reads this frame's histogram and the light meter the ray tracing wrote, and writes the
    // exposure that earlier frames' passes read, and the meter.
    commandBuffer.memoryBarrier(kCompute | kTransfer | kRayTracing | kFragment, kWrites,
                                kFragment | kCompute | kTransfer, kReadsAndWrites);
    commandBuffer.bindComputePipeline(exposurePipeline_);
    vkCmdDispatch(cmd, 1, 1, 1);
    // For every pass that applies the exposure.
    commandBuffer.memoryBarrier(kCompute | kTransfer, kWrites, kFragment | kCompute | kTransfer, kReadsAndWrites);

    bool bedrockCurve = pushConstants_.toneMappingMethod == TONE_MAPPING_METHOD_BEDROCK_PROVISIONAL;
    if (bedrockCurve) recordBedrockCurve(frame, commandBuffer);
    if (settings_.bloomEnabled) recordBloom(frame, commandBuffer);
    // The curve passes pushed constants of their own over these, and the bloom passes bound other layouts.
    if (bedrockCurve || settings_.bloomEnabled) pushToneMappingConstants();

    commandBuffer.beginRenderPass({
        .renderPass = renderPass_,
        .framebuffer = frame.framebuffer,
        .renderAreaExtent = {frame.ldr->width(), frame.ldr->height()},
        .clearValues = {{.color = {0.1f, 0.1f, 0.1f, 1.0f}}},
    });
    commandBuffer.bindGraphicsPipeline(pipeline_)
        ->bindDescriptorTable(frame.descriptors, VK_PIPELINE_BIND_POINT_GRAPHICS)
        ->draw(3, 1)
        ->endRenderPass();
    // The render pass moves the image through layouts of its own: whatever uses it next waits on all earlier work.
    frame.ldr->imageLayout() = kOutputLayout;
}

void ToneMappingModule::recordBedrockCurve(Frame &frame, gpu::CommandBuffer &commandBuffer) {
    VkCommandBuffer cmd = commandBuffer.vkCommandBuffer();

    ToneMappingBedrockCurvePushConstant constants = curvePushConstants_;
    constants.needsReset = bedrockCurveNeedsReset_ ? 1 : 0;
    if (bedrockCurveNeedsReset_) {
        // A new buffer: the histogram starts empty, and the curve is built afresh instead of eased from what it held.
        vkCmdFillBuffer(cmd, bedrockCurve_->vkBuffer(), 0, VK_WHOLE_SIZE, 0);
        bedrockCurveNeedsReset_ = false;
    }
    // The histogram and the curve are written over what the last frame's tone mapping pass read.
    commandBuffer.memoryBarrier(kCompute | kTransfer | kFragment, kWrites, kFragment | kCompute | kTransfer,
                                kReadsAndWrites);
    vkCmdPushConstants(cmd, frame.descriptors->vkPipelineLayout(), kComputeAndFragment, 0, sizeof(constants),
                       &constants);

    commandBuffer.bindComputePipeline(bedrockHistogramPipeline_);
    vkCmdDispatch(cmd, groupsFor(width_), groupsFor(height_), 1);
    commandBuffer.memoryBarrier(kCompute | kTransfer, kWrites, kFragment | kCompute | kTransfer, kReadsAndWrites);
    commandBuffer.bindComputePipeline(bedrockCurvePipeline_);
    vkCmdDispatch(cmd, 1, 1, 1);
    commandBuffer.memoryBarrier(kCompute | kTransfer, kWrites, kFragment | kCompute | kTransfer, kReadsAndWrites);
}

void ToneMappingModule::recordBloom(Frame &frame, gpu::CommandBuffer &commandBuffer) {
    for (uint32_t index = 0; index < BLOOM_PASSES; index++) {
        BloomPass &pass = frame.bloomPasses[index];
        // The level the last pass drew is read from now on. This pass draws over the whole of its target.
        commandBuffer.sync({
            {index > 0 ? frame.bloomPasses[index - 1].target.get() : nullptr, gpu::ImageUsage::Sampled, kFragment},
            {pass.target, gpu::ImageUsage::ColorAttachment, 0, true},
        });
        vkCmdPushConstants(commandBuffer.vkCommandBuffer(), pass.descriptors->vkPipelineLayout(),
                           VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pass.constants), &pass.constants);
        commandBuffer.beginRenderPass({
            .renderPass = bloomRenderPass_,
            .framebuffer = pass.framebuffer,
            .renderAreaExtent = {pass.target->width(), pass.target->height()},
        });
        commandBuffer.bindGraphicsPipeline(bloomPipelines_[index])
            ->bindDescriptorTable(pass.descriptors, VK_PIPELINE_BIND_POINT_GRAPHICS)
            ->draw(3, 1)
            ->endRenderPass();
    }
    // The tone mapping pass reads the last level drawn, the finest combined one.
    commandBuffer.sync({{frame.bloomPasses[BLOOM_PASSES - 1].target, gpu::ImageUsage::Sampled, kFragment}});
}

ToneMappingModuleContext::ToneMappingModuleContext(std::shared_ptr<FrameworkContext> frameworkContext,
                                                   std::shared_ptr<WorldPipelineContext> worldPipelineContext,
                                                   std::shared_ptr<ToneMappingModule> toneMappingModule)
    : WorldModuleContext(frameworkContext, worldPipelineContext),
      toneMappingModule(toneMappingModule),
      hdrImage(toneMappingModule->frames_[frameworkContext->frameIndex].hdr),
      ldrImage(toneMappingModule->frames_[frameworkContext->frameIndex].ldr),
      frameIndex_(frameworkContext->frameIndex) {}

void ToneMappingModuleContext::render() {
    auto context = frameworkContext.lock();
    toneMappingModule.lock()->record(frameIndex_, *context->worldCommandBuffer);
}
