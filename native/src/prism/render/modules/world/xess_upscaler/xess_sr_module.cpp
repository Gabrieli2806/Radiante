#include "prism/render/modules/world/xess_upscaler/xess_sr_module.hpp"

#include "prism/render/buffers.hpp"
#include "prism/render/modules/world/xess_upscaler/xess_wrapper.hpp"
#include "prism/render/pipeline.hpp"
#include "prism/render/render_framework.hpp"
#include "prism/render/renderer.hpp"
#include "prism/util/logging.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {

using QualityMode = XessSrModule::QualityMode;
using Preset = prism::XeSSQualityMode;

constexpr const char *kEnableKey = "render_pipeline.module.xess_sr.attribute.enable";
constexpr const char *kQualityModeKey = "render_pipeline.module.xess_sr.attribute.quality_mode";
constexpr const char *kRenderScaleKey = "render_pipeline.module.xess_sr.attribute.render_scale";
constexpr const char *kPreExposureKey = "render_pipeline.module.xess_sr.attribute.pre_exposure";
constexpr const char *kTrue = "render_pipeline.true";

struct QualityModeValue {
    const char *value;
    QualityMode mode;
};

constexpr QualityModeValue kQualityModeValues[] = {
    {"render_pipeline.module.xess_sr.attribute.quality_mode.native", QualityMode::NativeAA},
    {"render_pipeline.module.xess_sr.attribute.quality_mode.ultra_quality_plus", QualityMode::UltraQualityPlus},
    {"render_pipeline.module.xess_sr.attribute.quality_mode.ultra_quality", QualityMode::UltraQuality},
    {"render_pipeline.module.xess_sr.attribute.quality_mode.quality", QualityMode::Quality},
    {"render_pipeline.module.xess_sr.attribute.quality_mode.balanced", QualityMode::Balanced},
    {"render_pipeline.module.xess_sr.attribute.quality_mode.performance", QualityMode::Performance},
    {"render_pipeline.module.xess_sr.attribute.quality_mode.ultra_performance", QualityMode::UltraPerformance},
};

constexpr bool samePreset(QualityMode mode, Preset preset) {
    return static_cast<int>(mode) == static_cast<int>(preset);
}
static_assert(samePreset(QualityMode::NativeAA, Preset::NativeAA) &&
                  samePreset(QualityMode::UltraQualityPlus, Preset::UltraQualityPlus) &&
                  samePreset(QualityMode::UltraQuality, Preset::UltraQuality) &&
                  samePreset(QualityMode::Quality, Preset::Quality) &&
                  samePreset(QualityMode::Balanced, Preset::Balanced) &&
                  samePreset(QualityMode::Performance, Preset::Performance) &&
                  samePreset(QualityMode::UltraPerformance, Preset::UltraPerformance),
              "quality modes convert to XeSS presets by value");

Preset toPreset(QualityMode mode) {
    return static_cast<Preset>(mode);
}

// Storage image bindings of the helper shaders in shader/world/upscaler: linear_to_device_depth uses 0-3,
// upscale_first_hit_depth 0-1, upscale_motion_vector 2 and 4, upscale_normal_roughness 5-6.
enum Binding : uint32_t {
    DepthIn,
    DepthOut,
    MotionIn,
    XessMotionOut,
    UpscaledMotionOut,
    NormalRoughnessIn,
    UpscaledNormalRoughnessOut,
    BindingCount,
};

// linear_to_device_depth.comp's push constants. The upscale passes take MotionUpscalePushConstants, of the same size,
// so all four pipelines share one layout.
struct DepthConversionPushConstants {
    float nearPlane;
    float farPlane;
    uint32_t width;
    uint32_t height;
    float jitterX;
    float jitterY;
};

constexpr uint32_t kPushConstantSize = sizeof(DepthConversionPushConstants);
static_assert(sizeof(MotionUpscalePushConstants) == kPushConstantSize);

// The camera planes the depth conversion maps linear depth with.
constexpr float kNearPlane = 0.1f;
constexpr float kFarPlane = 10000.0f;

constexpr VkPipelineStageFlags2 kCompute = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

constexpr VkImageUsageFlags kInputUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                          VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                          VK_IMAGE_USAGE_TRANSFER_DST_BIT;
constexpr VkImageUsageFlags kOutputUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                           VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
constexpr VkImageUsageFlags kInternalUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

// Takes the size of the first image another module already made, if there is one.
void adoptSize(const std::vector<std::shared_ptr<gpu::DeviceLocalImage>> &images, uint32_t &width, uint32_t &height) {
    for (const auto &image : images) {
        if (image == nullptr) continue;
        width = image->width();
        height = image->height();
        return;
    }
}

// Makes the images the pipeline left to this module at width x height and keeps every image in `slots`. False for an
// image another module made at a different size.
template <size_t N>
bool takeImages(std::vector<std::shared_ptr<gpu::DeviceLocalImage>> &images,
                const std::vector<VkFormat> &formats,
                uint32_t width,
                uint32_t height,
                VkImageUsageFlags usage,
                Framework &framework,
                std::array<std::shared_ptr<gpu::DeviceLocalImage>, N> &slots) {
    for (size_t i = 0; i < N; i++) {
        if (images[i] == nullptr) {
            images[i] = gpu::DeviceLocalImage::create(framework.device(), framework.vma(), false, width, height, 1,
                                                      formats[i], usage);
        } else if (images[i]->width() != width || images[i]->height() != height) {
            return false;
        }
        slots[i] = images[i];
    }
    return true;
}

// One helper pass over width x height texels, in 16 x 16 groups.
template <typename PushConstants>
void recordPass(gpu::CommandBuffer &commandBuffer,
                const std::shared_ptr<gpu::DescriptorTable> &descriptors,
                const std::shared_ptr<gpu::ComputePipeline> &pipeline,
                const PushConstants &pushConstants,
                uint32_t width,
                uint32_t height) {
    static_assert(sizeof(PushConstants) == kPushConstantSize);
    commandBuffer.bindDescriptorTable(descriptors, VK_PIPELINE_BIND_POINT_COMPUTE);
    commandBuffer.bindComputePipeline(pipeline);
    vkCmdPushConstants(commandBuffer.vkCommandBuffer(), descriptors->vkPipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(PushConstants), &pushConstants);
    vkCmdDispatch(commandBuffer.vkCommandBuffer(), (width + 15) / 16, (height + 15) / 16, 1);
}

prism::XeSSImage xessImage(gpu::DeviceLocalImage &image) {
    prism::XeSSImage described;
    described.image = image.vkImage();
    described.imageView = image.vkImageView();
    described.format = image.vkFormat();
    described.width = image.width();
    described.height = image.height();
    return described;
}

} // namespace

XessSrModule::XessSrModule() = default;

XessSrModule::~XessSrModule() = default;

bool XessSrModule::isQualityModeAttributeKey(const std::string &key) {
    return key == kQualityModeKey;
}

bool XessSrModule::parseQualityModeValue(const std::string &value, QualityMode &outMode) {
    for (const QualityModeValue &entry : kQualityModeValues) {
        if (value == entry.value) {
            outMode = entry.mode;
            return true;
        }
    }
    return false;
}

void XessSrModule::getRenderResolution(uint32_t displayWidth,
                                       uint32_t displayHeight,
                                       QualityMode mode,
                                       uint32_t *outRenderWidth,
                                       uint32_t *outRenderHeight) {
    float ratio = 1.0f;
    switch (mode) {
        case QualityMode::NativeAA: ratio = 1.0f; break;
        case QualityMode::UltraQualityPlus: ratio = 1.3f; break;
        case QualityMode::UltraQuality: ratio = 1.5f; break;
        case QualityMode::Quality: ratio = 1.7f; break;
        case QualityMode::Balanced: ratio = 2.0f; break;
        case QualityMode::Performance: ratio = 2.3f; break;
        case QualityMode::UltraPerformance: ratio = 3.0f; break;
    }
    *outRenderWidth = static_cast<uint32_t>(static_cast<float>(displayWidth) / ratio);
    *outRenderHeight = static_cast<uint32_t>(static_cast<float>(displayHeight) / ratio);
}

void XessSrModule::init(std::shared_ptr<Framework> framework, std::shared_ptr<WorldPipeline> worldPipeline) {
    WorldModule::init(framework, worldPipeline);
    frames_.resize(framework->swapchain()->imageCount());
}

bool XessSrModule::setOrCreateInputImages(std::vector<std::shared_ptr<gpu::DeviceLocalImage>> &images,
                                          std::vector<VkFormat> &formats,
                                          uint32_t frameIndex) {
    if (images.size() != inputImageNum || formats.size() < images.size() || frameIndex >= frames_.size()) {
        return false;
    }
    auto framework = framework_.lock();
    if (framework == nullptr) return false;

    if (displayWidth_ == 0 || displayHeight_ == 0) {
        const VkExtent2D extent = framework->swapchain()->vkExtent();
        displayWidth_ = extent.width;
        displayHeight_ = extent.height;
    }
    // An input another module already made fixes the render size; otherwise the settings choose it.
    if (renderWidth_ == 0 || renderHeight_ == 0) {
        adoptSize(images, renderWidth_, renderHeight_);
        if (renderWidth_ == 0 || renderHeight_ == 0) updateRenderResolution();
    }
    return takeImages(images, formats, renderWidth_, renderHeight_, kInputUsage, *framework,
                      frames_[frameIndex].inputs);
}

bool XessSrModule::setOrCreateOutputImages(std::vector<std::shared_ptr<gpu::DeviceLocalImage>> &images,
                                           std::vector<VkFormat> &formats,
                                           uint32_t frameIndex) {
    if (images.size() != outputImageNum || formats.size() < images.size() || frameIndex >= frames_.size()) {
        return false;
    }
    auto framework = framework_.lock();
    if (framework == nullptr) return false;

    // An output a later module already made fixes the display size; otherwise it is the swapchain's.
    if (displayWidth_ == 0 || displayHeight_ == 0) {
        adoptSize(images, displayWidth_, displayHeight_);
        if (displayWidth_ == 0 || displayHeight_ == 0) {
            const VkExtent2D extent = framework->swapchain()->vkExtent();
            displayWidth_ = extent.width;
            displayHeight_ = extent.height;
        }
    }
    return takeImages(images, formats, displayWidth_, displayHeight_, kOutputUsage, *framework,
                      frames_[frameIndex].outputs);
}

void XessSrModule::setAttributes(int attributeCount, std::vector<std::string> &attributeKVs) {
    const size_t pairs = std::min(static_cast<size_t>(std::max(attributeCount, 0)), attributeKVs.size() / 2);
    bool resolutionChanged = false;

    for (size_t i = 0; i < pairs; i++) {
        const std::string &key = attributeKVs[2 * i];
        const std::string &value = attributeKVs[2 * i + 1];

        if (key == kEnableKey) {
            xessEnabled_ = value == kTrue;
            resolutionChanged = true;
        } else if (isQualityModeAttributeKey(key)) {
            resolutionChanged |= parseQualityModeValue(value, qualityMode_);
        } else if (key == kRenderScaleKey) {
            const float percent = std::stof(value);
            renderScale_ = percent > 0.0f ? std::clamp(percent / 100.0f, 0.33f, 1.0f) : 0.0f;
            resolutionChanged = true;
        } else if (key == kPreExposureKey) {
            preExposure_ = std::stof(value);
        }
    }

    if (resolutionChanged) updateRenderResolution();
}

void XessSrModule::build() {
    auto framework = framework_.lock();
    auto worldPipeline = worldPipeline_.lock();
    if (framework == nullptr || worldPipeline == nullptr) {
        throw std::runtime_error("XessSrModule: built without its framework or world pipeline");
    }
    const auto missing = [](const auto &image) { return image == nullptr; };
    for (const Frame &frame : frames_) {
        if (std::ranges::any_of(frame.inputs, missing) || std::ranges::any_of(frame.outputs, missing)) {
            throw std::runtime_error("XessSrModule: built before its images were set");
        }
    }

    xess_ = xessEnabled_ ? createXeSS(*framework) : nullptr;
    createDescriptorTables(*framework);
    createImages(*framework);
    bindDescriptors();
    createPipelines(*framework);

    contexts_.resize(frames_.size());
    for (size_t i = 0; i < frames_.size(); i++) {
        contexts_[i] = std::make_shared<XessSrModuleContext>(framework->contexts()[i], worldPipeline->contexts()[i],
                                                             shared_from_this());
    }
}

std::vector<std::shared_ptr<WorldModuleContext>> &XessSrModule::contexts() {
    return contexts_;
}

void XessSrModule::bindTexture(std::shared_ptr<gpu::Sampler>, std::shared_ptr<gpu::DeviceLocalImage>, int) {}

void XessSrModule::preClose() {
    // The XeSS context is released now rather than with the module, and frames still in flight may be using it.
    if (auto framework = framework_.lock()) framework->waitRenderQueueIdle();
    if (xess_ != nullptr) {
        xess_->destroy();
        xess_.reset();
    }
}

void XessSrModule::updateRenderResolution() {
    if (displayWidth_ == 0 || displayHeight_ == 0) {
        renderWidth_ = 0;
        renderHeight_ = 0;
        return;
    }

    if (renderScale_ > 0.0f) {
        const auto scaled = [this](uint32_t size) {
            return std::max(1u, static_cast<uint32_t>(std::lround(static_cast<float>(size) * renderScale_)));
        };
        renderWidth_ = scaled(displayWidth_);
        renderHeight_ = scaled(displayHeight_);
        return;
    }

    auto framework = framework_.lock();
    const bool asked = xessEnabled_ && framework != nullptr &&
                       prism::XeSSWrapper::queryOptimalInputResolution(
                           framework->instance()->vkInstance(), framework->physicalDevice()->vkPhysicalDevice(),
                           framework->device()->vkDevice(), displayWidth_, displayHeight_, toPreset(qualityMode_),
                           &renderWidth_, &renderHeight_);
    if (!asked) getRenderResolution(displayWidth_, displayHeight_, qualityMode_, &renderWidth_, &renderHeight_);
}

XessSrModule::QualityMode XessSrModule::effectiveQualityMode() const {
    if (renderScale_ <= 0.0f) return qualityMode_;
    // Native is for an input the size of the output only.
    if (renderScale_ >= 0.995f) return QualityMode::NativeAA;
    const float ratio = 1.0f / renderScale_;
    if (ratio < 1.4f) return QualityMode::UltraQualityPlus;
    if (ratio < 1.6f) return QualityMode::UltraQuality;
    if (ratio < 1.85f) return QualityMode::Quality;
    if (ratio < 2.15f) return QualityMode::Balanced;
    if (ratio < 2.65f) return QualityMode::Performance;
    return QualityMode::UltraPerformance;
}

std::unique_ptr<prism::XeSSWrapper> XessSrModule::createXeSS(Framework &framework) const {
    prism::XeSSConfig config;
    config.instance = framework.instance()->vkInstance();
    config.physicalDevice = framework.physicalDevice()->vkPhysicalDevice();
    config.device = framework.device()->vkDevice();
    config.renderWidth = renderWidth_;
    config.renderHeight = renderHeight_;
    config.displayWidth = displayWidth_;
    config.displayHeight = displayHeight_;
    config.qualityMode = toPreset(effectiveQualityMode());

    auto xess = std::make_unique<prism::XeSSWrapper>();
    if (xess->initialize(config)) return xess;
    radiante::err() << "XessSrModule: failed to initialize XeSS, fallback to blit" << std::endl;
    return nullptr;
}

void XessSrModule::createDescriptorTables(Framework &framework) {
    for (Frame &frame : frames_) {
        gpu::DescriptorTableBuilder builder;
        auto &bindings = builder.beginDescriptorLayoutSet().beginDescriptorLayoutSetBinding();
        for (uint32_t binding = 0; binding < BindingCount; binding++) {
            bindings.defineDescriptorLayoutSetBinding({
                .binding = binding,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            });
        }
        bindings.endDescriptorLayoutSetBinding().endDescriptorLayoutSet().definePushConstant({
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .offset = 0,
            .size = kPushConstantSize,
        });
        frame.descriptors = builder.build(framework.device());
    }
}

void XessSrModule::createImages(Framework &framework) {
    for (Frame &frame : frames_) {
        frame.deviceDepth = gpu::DeviceLocalImage::create(framework.device(), framework.vma(), false, renderWidth_,
                                                          renderHeight_, 1, VK_FORMAT_R32_SFLOAT, kInternalUsage);
        frame.xessMotionVector =
            gpu::DeviceLocalImage::create(framework.device(), framework.vma(), false, renderWidth_, renderHeight_, 1,
                                          VK_FORMAT_R16G16_SFLOAT, kInternalUsage);
    }
}

void XessSrModule::bindDescriptors() {
    // A frame's set is read as it stands when the frame is submitted (its bindings are update-after-bind), and the
    // passes used to rebind it while recording, the first-hit depth upscale last. So the GPU has always run all four
    // passes with these bindings: the depth conversion reads the first-hit depth and writes the upscaled first-hit
    // depth, which the upscale pass then overwrites, and deviceDepth, XeSS's depth input, is never written. Binding
    // what the conversion was written for would change XeSS's output.
    for (Frame &frame : frames_) {
        const auto bind = [&frame](Binding binding, const std::shared_ptr<gpu::DeviceLocalImage> &image) {
            frame.descriptors->bindImage(image, VK_IMAGE_LAYOUT_GENERAL, 0, binding);
        };
        bind(DepthIn, frame.inputs[Input::FirstHitDepth]);
        bind(DepthOut, frame.outputs[Output::UpscaledFirstHitDepth]);
        bind(MotionIn, frame.inputs[Input::MotionVector]);
        bind(XessMotionOut, frame.xessMotionVector);
        bind(UpscaledMotionOut, frame.outputs[Output::UpscaledMotionVector]);
        bind(NormalRoughnessIn, frame.inputs[Input::NormalRoughness]);
        bind(UpscaledNormalRoughnessOut, frame.outputs[Output::UpscaledNormalRoughness]);
    }
}

void XessSrModule::createPipelines(Framework &framework) {
    const auto device = framework.device();
    const auto load = [&device](const char *path) {
        return gpu::Shader::create(device, (Renderer::folderPath / path).string());
    };
    const auto depthConversion = load("shaders/world/upscaler/linear_to_device_depth_comp.spv");
    const auto firstHitDepth = load("shaders/world/upscaler/upscale_first_hit_depth_comp.spv");
    const auto motionVector = load("shaders/world/upscaler/upscale_motion_vector_comp.spv");
    const auto normalRoughness = load("shaders/world/upscaler/upscale_normal_roughness_comp.spv");

    // Every frame's table has the same layout.
    const auto pipeline = [&](const std::shared_ptr<gpu::Shader> &shader) {
        return gpu::ComputePipelineBuilder{}
            .defineShader(shader)
            .definePipelineLayout(frames_.front().descriptors)
            .build(device);
    };
    depthConversionPipeline_ = pipeline(depthConversion);
    firstHitDepthUpscalePipeline_ = pipeline(firstHitDepth);
    motionUpscalePipeline_ = pipeline(motionVector);
    normalRoughnessUpscalePipeline_ = pipeline(normalRoughness);
}

void XessSrModule::record(uint32_t frameIndex, gpu::CommandBuffer &commandBuffer) {
    Frame &frame = frames_[frameIndex];
    const bool upscaled = xess_ != nullptr && upscaleWithXeSS(frame, commandBuffer);
    if (!upscaled) blitColor(frame, commandBuffer);
    upscaleGuides(frame, commandBuffer, !upscaled);
}

bool XessSrModule::upscaleWithXeSS(Frame &frame, gpu::CommandBuffer &commandBuffer) {
    auto worldUniforms = Renderer::instance().buffers()->worldUniformBuffer();
    if (worldUniforms == nullptr) return false;
    const auto &world = *static_cast<const gpu::Data::WorldUBO *>(worldUniforms->mappedPtr());

    // The depth conversion zeroes NaN and Inf motion vectors for XeSS (its images are explained in bindDescriptors).
    commandBuffer.sync({
        {frame.inputs[Input::FirstHitDepth], gpu::ImageUsage::StorageRead, kCompute},
        {frame.outputs[Output::UpscaledFirstHitDepth], gpu::ImageUsage::StorageWrite, kCompute},
        {frame.inputs[Input::MotionVector], gpu::ImageUsage::StorageRead, kCompute},
        {frame.xessMotionVector, gpu::ImageUsage::StorageWrite, kCompute},
    });
    const DepthConversionPushConstants conversion{
        kNearPlane, kFarPlane, renderWidth_, renderHeight_, world.cameraJitter.x, world.cameraJitter.y};
    recordPass(commandBuffer, frame.descriptors, depthConversionPipeline_, conversion, renderWidth_, renderHeight_);

    commandBuffer.sync({
        {frame.inputs[Input::Color], gpu::ImageUsage::Sampled, kCompute},
        {frame.deviceDepth, gpu::ImageUsage::Sampled, kCompute},
        {frame.xessMotionVector, gpu::ImageUsage::Sampled, kCompute},
        {frame.outputs[Output::Upscaled], gpu::ImageUsage::General},
    });

    prism::XeSSInput input;
    input.commandBuffer = commandBuffer.vkCommandBuffer();
    input.colorTexture = xessImage(*frame.inputs[Input::Color]);
    input.velocityTexture = xessImage(*frame.xessMotionVector);
    input.depthTexture = xessImage(*frame.deviceDepth);
    input.outputTexture = xessImage(*frame.outputs[Output::Upscaled]);
    input.jitterOffsetX = -world.cameraJitter.x;
    input.jitterOffsetY = -world.cameraJitter.y;
    input.exposureScale = preExposure_;
    input.resetHistory =
        cameraJumped(glm::vec3(world.cameraPos),
                     glm::vec3(world.cameraViewMat[0][2], world.cameraViewMat[1][2], world.cameraViewMat[2][2]));
    input.resetHistory |= Renderer::instance().buffers()->heldLightHistoryReset();
    input.inputWidth = renderWidth_;
    input.inputHeight = renderHeight_;
    return xess_->dispatch(input);
}

void XessSrModule::blitColor(Frame &frame, gpu::CommandBuffer &commandBuffer) {
    auto &color = frame.inputs[Input::Color];
    auto &output = frame.outputs[Output::Upscaled];
    commandBuffer.sync({{color, gpu::ImageUsage::TransferSrc}, {output, gpu::ImageUsage::TransferDst}});

    VkImageBlit region{};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.srcOffsets[1] = {static_cast<int32_t>(color->width()), static_cast<int32_t>(color->height()), 1};
    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstOffsets[1] = {static_cast<int32_t>(output->width()), static_cast<int32_t>(output->height()), 1};
    vkCmdBlitImage(commandBuffer.vkCommandBuffer(), color->vkImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   output->vkImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_LINEAR);
}

void XessSrModule::upscaleGuides(Frame &frame, gpu::CommandBuffer &commandBuffer, bool blitted) {
    // The three passes touch separate images, so one barrier serves them all. A blitted output goes back to GENERAL
    // in it too, the layout XeSS leaves the output in.
    commandBuffer.sync({
        {blitted ? frame.outputs[Output::Upscaled].get() : nullptr, gpu::ImageUsage::General},
        {frame.inputs[Input::FirstHitDepth], gpu::ImageUsage::StorageRead, kCompute},
        {frame.outputs[Output::UpscaledFirstHitDepth], gpu::ImageUsage::StorageWrite, kCompute},
        {frame.inputs[Input::MotionVector], gpu::ImageUsage::StorageRead, kCompute},
        {frame.outputs[Output::UpscaledMotionVector], gpu::ImageUsage::StorageWrite, kCompute},
        {frame.inputs[Input::NormalRoughness], gpu::ImageUsage::StorageRead, kCompute},
        {frame.outputs[Output::UpscaledNormalRoughness], gpu::ImageUsage::StorageWrite, kCompute},
    });

    const MotionUpscalePushConstants scale{
        static_cast<float>(displayWidth_) / static_cast<float>(std::max(1u, renderWidth_)),
        static_cast<float>(displayHeight_) / static_cast<float>(std::max(1u, renderHeight_)),
        renderWidth_,
        renderHeight_,
        displayWidth_,
        displayHeight_,
    };
    for (const auto *pipeline :
         {&firstHitDepthUpscalePipeline_, &motionUpscalePipeline_, &normalRoughnessUpscalePipeline_}) {
        recordPass(commandBuffer, frame.descriptors, *pipeline, scale, displayWidth_, displayHeight_);
    }
}

// XeSS drops its history on its first frame and whenever the camera jumped since the last frame it ran: moved more
// than a block, or turned by more than 30 degrees.
bool XessSrModule::cameraJumped(const glm::vec3 &position, const glm::vec3 &direction) {
    bool jumped = true;
    if (camera_.seen) {
        const float moved = glm::length(position - camera_.position);
        const float alignment = glm::dot(glm::normalize(direction), glm::normalize(camera_.direction));
        jumped = moved > 1.0f || alignment < 0.866f;
    }
    camera_ = {position, direction, true};
    return jumped;
}

XessSrModuleContext::XessSrModuleContext(std::shared_ptr<FrameworkContext> frameworkContext,
                                         std::shared_ptr<WorldPipelineContext> worldPipelineContext,
                                         std::shared_ptr<XessSrModule> xessModule)
    : WorldModuleContext(frameworkContext, std::move(worldPipelineContext)),
      xessModule_(std::move(xessModule)),
      frameIndex_(frameworkContext->frameIndex) {}

void XessSrModuleContext::render() {
    auto module = xessModule_.lock();
    auto context = frameworkContext.lock();
    if (module == nullptr || context == nullptr) return;
    module->record(frameIndex_, *context->worldCommandBuffer);
}
