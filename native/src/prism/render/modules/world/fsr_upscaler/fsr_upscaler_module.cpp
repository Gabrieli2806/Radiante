#include "prism/render/modules/world/fsr_upscaler/fsr_upscaler_module.hpp"

#include "prism/render/buffers.hpp"
#include "prism/render/modules/world/fsr_upscaler/fsr3_upscaler.hpp"
#include "prism/render/pipeline.hpp"
#include "prism/render/render_framework.hpp"
#include "prism/render/renderer.hpp"
#include "prism/util/logging.hpp"

#include <algorithm>
#include <cmath>
#include <ostream>
#include <span>
#include <string_view>
#include <utility>

namespace {

// Where each image is in the module's input and output lists (modules/fsr_upscaler.yaml).
enum Input : uint32_t { kColor, kDepth, kMotionVectors, kFirstHitDepth, kNormalRoughness };
enum Output : uint32_t { kUpscaledColor, kUpscaledFirstHitDepth, kUpscaledMotionVectors, kUpscaledNormalRoughness };

// The bindings of the table the four passes share (shaders/world/upscaler): the depth conversion uses 0 to 3, the
// first-hit depth upscale 0 and 1, the motion upscale 2 and 4, the normal and roughness upscale 5 and 6.
enum Binding : uint32_t {
    kBindingFirstHitDepthIn,
    kBindingFirstHitDepthOut,
    kBindingMotionIn,
    kBindingFsrMotion,
    kBindingMotionOut,
    kBindingNormalRoughnessIn,
    kBindingNormalRoughnessOut,
    kBindingCount,
};

constexpr std::string_view kAttributePrefix = "render_pipeline.module.fsr_upscaler.attribute.";

constexpr std::pair<std::string_view, FSRUpscalerModule::QualityMode> kQualityModes[] = {
    {"render_pipeline.module.fsr_upscaler.attribute.quality_mode.native", FSRUpscalerModule::QualityMode::NativeAA},
    {"render_pipeline.module.fsr_upscaler.attribute.quality_mode.quality", FSRUpscalerModule::QualityMode::Quality},
    {"render_pipeline.module.fsr_upscaler.attribute.quality_mode.balanced", FSRUpscalerModule::QualityMode::Balanced},
    {"render_pipeline.module.fsr_upscaler.attribute.quality_mode.performance",
     FSRUpscalerModule::QualityMode::Performance},
    {"render_pipeline.module.fsr_upscaler.attribute.quality_mode.ultra",
     FSRUpscalerModule::QualityMode::UltraPerformance},
};

// The planes the device depth is made with, and the ones FSR is told about.
constexpr float kCameraNear = 0.1f;
constexpr float kCameraFar = 10000.0f;
// The frame time FSR is given when there is no measurement worth using (milliseconds).
constexpr float kFallbackFrameTime = 16.67f;

struct DepthConversionPushConstants {
    float cameraNear;
    float cameraFar;
    uint32_t width;
    uint32_t height;
    float jitterX;
    float jitterY;
};

// The tables' one push constant range, which holds either pass's constants.
constexpr uint32_t kPushConstantSize = sizeof(float) * 4 + sizeof(uint32_t) * 2;
static_assert(sizeof(DepthConversionPushConstants) == kPushConstantSize);
static_assert(sizeof(MotionUpscalePushConstants) == kPushConstantSize);

// Work groups for a size in pixels (the passes run 16 x 16 invocations each).
uint32_t groupCount(uint32_t pixels) {
    return (pixels + 15) / 16;
}

float upscaleRatio(FSRUpscalerModule::QualityMode mode) {
    switch (mode) {
        case FSRUpscalerModule::QualityMode::NativeAA: return 1.0f;
        case FSRUpscalerModule::QualityMode::Quality: return 1.5f;
        case FSRUpscalerModule::QualityMode::Balanced: return 1.7f;
        case FSRUpscalerModule::QualityMode::Performance: return 2.0f;
        case FSRUpscalerModule::QualityMode::UltraPerformance: return 3.0f;
    }
    return 1.0f;
}

bool isHdrFormat(VkFormat format) {
    return format == VK_FORMAT_R16G16B16A16_SFLOAT || format == VK_FORMAT_R32G32B32A32_SFLOAT ||
           format == VK_FORMAT_B10G11R11_UFLOAT_PACK32;
}

// Keeps the images other modules made (they must already have the size) and makes the missing ones.
bool adoptOrCreate(Framework &framework,
                   std::vector<std::shared_ptr<gpu::DeviceLocalImage>> &images,
                   const std::vector<VkFormat> &formats,
                   uint32_t width,
                   uint32_t height,
                   VkImageUsageFlags usage,
                   std::span<std::shared_ptr<gpu::DeviceLocalImage>> kept) {
    for (size_t i = 0; i < images.size(); i++) {
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

const std::shared_ptr<gpu::DeviceLocalImage> *firstMade(
    const std::vector<std::shared_ptr<gpu::DeviceLocalImage>> &images) {
    auto made = std::ranges::find_if(images, [](const auto &image) { return image != nullptr; });
    return made == images.end() ? nullptr : &*made;
}

std::shared_ptr<gpu::ComputePipeline> buildPipeline(const std::shared_ptr<gpu::Device> &device,
                                                    const std::shared_ptr<gpu::DescriptorTable> &layout,
                                                    const char *spirvPath) {
    auto shader = gpu::Shader::create(device, (Renderer::folderPath / spirvPath).string());
    return gpu::ComputePipelineBuilder{}.defineShader(shader).definePipelineLayout(layout).build(device);
}

} // namespace

FSRUpscalerModule::FSRUpscalerModule() = default;

FSRUpscalerModule::~FSRUpscalerModule() = default;

bool FSRUpscalerModule::isQualityModeAttributeKey(const std::string &key) {
    return key == "render_pipeline.module.fsr_upscaler.attribute.quality_mode";
}

bool FSRUpscalerModule::parseQualityModeValue(const std::string &value, QualityMode &outMode) {
    for (const auto &[name, mode] : kQualityModes) {
        if (value == name) {
            outMode = mode;
            return true;
        }
    }
    return false;
}

void FSRUpscalerModule::getRenderResolution(uint32_t displayWidth,
                                            uint32_t displayHeight,
                                            QualityMode mode,
                                            uint32_t *outRenderWidth,
                                            uint32_t *outRenderHeight) {
    const float ratio = upscaleRatio(mode);
    *outRenderWidth = static_cast<uint32_t>(static_cast<float>(displayWidth) / ratio);
    *outRenderHeight = static_cast<uint32_t>(static_cast<float>(displayHeight) / ratio);
}

void FSRUpscalerModule::init(std::shared_ptr<Framework> framework, std::shared_ptr<WorldPipeline> worldPipeline) {
    WorldModule::init(framework, worldPipeline);
    frames_.resize(framework->swapchain()->imageCount());
}

bool FSRUpscalerModule::setOrCreateInputImages(std::vector<std::shared_ptr<gpu::DeviceLocalImage>> &images,
                                               std::vector<VkFormat> &formats,
                                               uint32_t frameIndex) {
    if (images.size() != inputImageNum) return false;
    auto framework = framework_.lock();
    if (!framework) return false;

    if (displayWidth_ == 0 || displayHeight_ == 0) {
        VkExtent2D extent = framework->swapchain()->vkExtent();
        displayWidth_ = extent.width;
        displayHeight_ = extent.height;
    }
    if (renderWidth_ == 0 || renderHeight_ == 0) {
        // An input another module already made fixes the render size; otherwise the settings choose it.
        if (auto made = firstMade(images)) {
            renderWidth_ = (*made)->width();
            renderHeight_ = (*made)->height();
        }
        if (renderWidth_ == 0 || renderHeight_ == 0) resolveRenderResolution();
    }

    return adoptOrCreate(*framework, images, formats, renderWidth_, renderHeight_,
                         VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                         frames_.at(frameIndex).inputs);
}

bool FSRUpscalerModule::setOrCreateOutputImages(std::vector<std::shared_ptr<gpu::DeviceLocalImage>> &images,
                                                std::vector<VkFormat> &formats,
                                                uint32_t frameIndex) {
    if (images.size() != outputImageNum) return false;
    auto framework = framework_.lock();
    if (!framework) return false;

    if (displayWidth_ == 0 || displayHeight_ == 0) {
        // An output a later module already made fixes the display size; otherwise it is the target's.
        if (auto made = firstMade(images)) {
            displayWidth_ = (*made)->width();
            displayHeight_ = (*made)->height();
        }
        if (displayWidth_ == 0 || displayHeight_ == 0) {
            VkExtent2D extent = framework->swapchain()->vkExtent();
            displayWidth_ = extent.width;
            displayHeight_ = extent.height;
        }
    }

    return adoptOrCreate(*framework, images, formats, displayWidth_, displayHeight_,
                         VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                             VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                         frames_.at(frameIndex).outputs);
}

void FSRUpscalerModule::build() {
    auto framework = framework_.lock();
    auto worldPipeline = worldPipeline_.lock();

    fsr3_.reset();
    if (fsr3Enabled_) {
        prism::FSR3Upscaler::Config config;
        config.device = framework->device()->vkDevice();
        config.physicalDevice = framework->physicalDevice()->vkPhysicalDevice();
        config.maxRenderWidth = renderWidth_;
        config.maxRenderHeight = renderHeight_;
        config.maxDisplayWidth = displayWidth_;
        config.maxDisplayHeight = displayHeight_;
        const auto &color = frames_.empty() ? nullptr : frames_.front().inputs[kColor];
        config.hdr = color != nullptr && isHdrFormat(color->vkFormat());
        // The depth conversion pass writes finite device depth, 0 at the near plane.
        config.depthInverted = false;
        config.depthInfinite = false;
        config.autoExposure = false;

        auto upscaler = std::make_unique<prism::FSR3Upscaler>();
        if (upscaler->initialize(config)) {
            fsr3_ = std::move(upscaler);
        } else {
            radiante::err() << "FSRUpscalerModule: Failed to initialize FSR3" << std::endl;
        }
    }

    createFrameResources();
    createPipelines();

    contexts_.clear();
    contexts_.reserve(frames_.size());
    for (size_t i = 0; i < frames_.size(); i++) {
        contexts_.push_back(std::make_shared<FSRUpscalerModuleContext>(
            framework->contexts()[i], worldPipeline->contexts()[i], shared_from_this(), frames_[i]));
    }
}

void FSRUpscalerModule::createFrameResources() {
    auto framework = framework_.lock();
    auto device = framework->device();

    for (Frame &frame : frames_) {
        gpu::DescriptorTableBuilder builder;
        auto &bindings = builder.beginDescriptorLayoutSet().beginDescriptorLayoutSetBinding();
        for (uint32_t binding = 0; binding < kBindingCount; binding++) {
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
        frame.descriptorTable = builder.build(device);

        frame.deviceDepth =
            gpu::DeviceLocalImage::create(device, framework->vma(), false, renderWidth_, renderHeight_, 1,
                                          VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        frame.fsrMotionVectors =
            gpu::DeviceLocalImage::create(device, framework->vma(), false, renderWidth_, renderHeight_, 1,
                                          VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

        // Written once: the images never change. The set is read as it stands when a frame is submitted, so this is
        // what every pass of every frame has always seen (see recordDepthConversion for what that means for it).
        const std::pair<uint32_t, std::shared_ptr<gpu::DeviceLocalImage>> images[] = {
            {kBindingFirstHitDepthIn, frame.inputs[kFirstHitDepth]},
            {kBindingFirstHitDepthOut, frame.outputs[kUpscaledFirstHitDepth]},
            {kBindingMotionIn, frame.inputs[kMotionVectors]},
            {kBindingFsrMotion, frame.fsrMotionVectors},
            {kBindingMotionOut, frame.outputs[kUpscaledMotionVectors]},
            {kBindingNormalRoughnessIn, frame.inputs[kNormalRoughness]},
            {kBindingNormalRoughnessOut, frame.outputs[kUpscaledNormalRoughness]},
        };
        for (const auto &[binding, image] : images) {
            frame.descriptorTable->bindImage(image, VK_IMAGE_LAYOUT_GENERAL, 0, binding);
        }
    }
}

void FSRUpscalerModule::createPipelines() {
    auto device = framework_.lock()->device();
    // Every frame's table has the same layout: the first one's serves all of them.
    const auto &layout = frames_.front().descriptorTable;
    depthConversionPipeline_ =
        buildPipeline(device, layout, "shaders/world/upscaler/linear_to_device_depth_comp.spv");
    firstHitDepthUpscalePipeline_ =
        buildPipeline(device, layout, "shaders/world/upscaler/upscale_first_hit_depth_comp.spv");
    motionUpscalePipeline_ = buildPipeline(device, layout, "shaders/world/upscaler/upscale_motion_vector_comp.spv");
    normalRoughnessUpscalePipeline_ =
        buildPipeline(device, layout, "shaders/world/upscaler/upscale_normal_roughness_comp.spv");
}

void FSRUpscalerModule::setAttributes(int attributeCount, std::vector<std::string> &attributeKVs) {
    const size_t pairs = std::min(static_cast<size_t>(std::max(attributeCount, 0)), attributeKVs.size() / 2);
    bool resolutionChanged = false;

    for (size_t i = 0; i < pairs; i++) {
        const std::string &key = attributeKVs[2 * i];
        const std::string &value = attributeKVs[2 * i + 1];
        if (isQualityModeAttributeKey(key)) {
            if (parseQualityModeValue(value, qualityMode_)) resolutionChanged = true;
            continue;
        }
        if (!key.starts_with(kAttributePrefix)) continue;

        const std::string_view name = std::string_view(key).substr(kAttributePrefix.size());
        if (name == "enable") {
            fsr3Enabled_ = value == "render_pipeline.true";
        } else if (name == "render_scale") {
            const float percent = std::stof(value);
            renderScale_ = percent > 0.0f ? std::clamp(percent / 100.0f, 0.33f, 1.0f) : 0.0f;
            resolutionChanged = true;
        } else if (name == "sharpness") {
            sharpness_ = std::stof(value);
        } else if (name == "pre_exposure") {
            preExposure_ = std::stof(value);
        }
    }

    if (resolutionChanged) resolveRenderResolution();
}

void FSRUpscalerModule::resolveRenderResolution() {
    if (displayWidth_ == 0 || displayHeight_ == 0) {
        renderWidth_ = 0;
        renderHeight_ = 0;
        return;
    }
    if (renderScale_ > 0.0f) {
        auto scaled = [this](uint32_t size) {
            return std::max(1u, static_cast<uint32_t>(std::lround(static_cast<float>(size) * renderScale_)));
        };
        renderWidth_ = scaled(displayWidth_);
        renderHeight_ = scaled(displayHeight_);
        return;
    }
    getRenderResolution(displayWidth_, displayHeight_, qualityMode_, &renderWidth_, &renderHeight_);
}

bool FSRUpscalerModule::cameraCut(const glm::vec3 &position, const glm::vec3 &direction) {
    // More than a block of movement or 30 degrees of turning (cos 30 = 0.866) between two frames.
    const bool cut = std::exchange(firstFrame_, false) || glm::length(position - lastCameraPos_) > 1.0f ||
                     glm::dot(glm::normalize(direction), glm::normalize(lastCameraDir_)) < 0.866f;
    lastCameraPos_ = position;
    lastCameraDir_ = direction;
    return cut;
}

std::vector<std::shared_ptr<WorldModuleContext>> &FSRUpscalerModule::contexts() {
    return contexts_;
}

void FSRUpscalerModule::bindTexture(std::shared_ptr<gpu::Sampler>, std::shared_ptr<gpu::DeviceLocalImage>, int) {}

void FSRUpscalerModule::preClose() {
    if (auto framework = framework_.lock()) framework->waitRenderQueueIdle();
    if (fsr3_ != nullptr) {
        fsr3_->destroy();
        fsr3_.reset();
    }
}

FSRUpscalerModuleContext::FSRUpscalerModuleContext(std::shared_ptr<FrameworkContext> frameworkContext,
                                                   std::shared_ptr<WorldPipelineContext> worldPipelineContext,
                                                   std::shared_ptr<FSRUpscalerModule> upscalerModule,
                                                   const FSRUpscalerModule::Frame &frame)
    : WorldModuleContext(std::move(frameworkContext), std::move(worldPipelineContext)),
      upscalerModule_(upscalerModule),
      frame_(frame),
      lastFrameTime_(Clock::now()) {}

void FSRUpscalerModuleContext::render() {
    auto module = upscalerModule_.lock();
    auto context = frameworkContext.lock();
    if (module == nullptr || context == nullptr) return;
    gpu::CommandBuffer &commandBuffer = *context->worldCommandBuffer;

    if (!module->fsr3Enabled_) {
        recordBlit(commandBuffer);
        recordAuxiliaryUpscales(*module, commandBuffer);
        return;
    }
    if (module->fsr3_ == nullptr) return;

    auto worldUniformBuffer = Renderer::instance().buffers()->worldUniformBuffer();
    if (worldUniformBuffer == nullptr) return;
    const auto &world = *static_cast<const gpu::Data::WorldUBO *>(worldUniformBuffer->mappedPtr());

    recordDepthConversion(*module, commandBuffer, world.cameraJitter);
    recordFsr(*module, commandBuffer, world);
    recordAuxiliaryUpscales(*module, commandBuffer);
}

void FSRUpscalerModuleContext::recordBlit(gpu::CommandBuffer &commandBuffer) {
    const auto &color = frame_.inputs[kColor];
    const auto &output = frame_.outputs[kUpscaledColor];
    commandBuffer.sync({
        {color, gpu::ImageUsage::TransferSrc},
        {output, gpu::ImageUsage::TransferDst, 0, true},
    });

    VkImageBlit region{};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.srcOffsets[1] = {static_cast<int32_t>(color->width()), static_cast<int32_t>(color->height()), 1};
    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstOffsets[1] = {static_cast<int32_t>(output->width()), static_cast<int32_t>(output->height()), 1};
    vkCmdBlitImage(commandBuffer.vkCommandBuffer(), color->vkImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   output->vkImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_LINEAR);

    // Handed on in GENERAL, as FSR leaves it.
    commandBuffer.sync({{output, gpu::ImageUsage::General}});
}

void FSRUpscalerModuleContext::recordDepthConversion(FSRUpscalerModule &module,
                                                     gpu::CommandBuffer &commandBuffer,
                                                     glm::vec2 jitter) {
    // This pass is meant to turn the linear depth into deviceDepth for FSR, but the table is update-after-bind, so it
    // uses the bindings as they stand at submission, and 0 and 1 hold the first-hit depth pair there (as they always
    // did): it converts the first-hit depth into the upscaled first-hit depth, which is rewritten in full afterwards,
    // and deviceDepth keeps whatever it was made with. Binding the intended pair would change FSR's image, so it is
    // left so, and both pairs are readied.
    commandBuffer.sync({
        {frame_.inputs[kDepth], gpu::ImageUsage::StorageRead},
        {frame_.deviceDepth, gpu::ImageUsage::StorageWrite},
        {frame_.inputs[kFirstHitDepth], gpu::ImageUsage::StorageRead},
        {frame_.outputs[kUpscaledFirstHitDepth], gpu::ImageUsage::StorageWrite, 0, true},
        {frame_.inputs[kMotionVectors], gpu::ImageUsage::StorageRead},
        {frame_.fsrMotionVectors, gpu::ImageUsage::StorageWrite, 0, true},
    });

    const DepthConversionPushConstants constants{
        kCameraNear, kCameraFar, module.renderWidth_, module.renderHeight_, jitter.x, jitter.y,
    };
    commandBuffer.bindDescriptorTable(frame_.descriptorTable, VK_PIPELINE_BIND_POINT_COMPUTE);
    commandBuffer.bindComputePipeline(module.depthConversionPipeline_);
    vkCmdPushConstants(commandBuffer.vkCommandBuffer(), frame_.descriptorTable->vkPipelineLayout(),
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
    vkCmdDispatch(commandBuffer.vkCommandBuffer(), groupCount(module.renderWidth_), groupCount(module.renderHeight_),
                  1);
}

void FSRUpscalerModuleContext::recordFsr(FSRUpscalerModule &module,
                                         gpu::CommandBuffer &commandBuffer,
                                         const gpu::Data::WorldUBO &world) {
    const auto &color = frame_.inputs[kColor];
    const auto &output = frame_.outputs[kUpscaledColor];
    commandBuffer.sync({
        {color, gpu::ImageUsage::Sampled},
        {frame_.deviceDepth, gpu::ImageUsage::Sampled},
        {frame_.fsrMotionVectors, gpu::ImageUsage::Sampled},
        {output, gpu::ImageUsage::General},
    });

    prism::FSR3Upscaler::Frame upscale;
    upscale.commandBuffer = commandBuffer.vkCommandBuffer();
    upscale.color = {color->vkImage(), color->vkFormat()};
    upscale.depth = {frame_.deviceDepth->vkImage(), frame_.deviceDepth->vkFormat()};
    upscale.motionVectors = {frame_.fsrMotionVectors->vkImage(), frame_.fsrMotionVectors->vkFormat()};
    upscale.output = {output->vkImage(), output->vkFormat()};
    upscale.renderWidth = module.renderWidth_;
    upscale.renderHeight = module.renderHeight_;
    upscale.displayWidth = module.displayWidth_;
    upscale.displayHeight = module.displayHeight_;
    // FSR takes the camera's offset, the opposite of the jitter the rays are traced with.
    upscale.jitterX = -world.cameraJitter.x;
    upscale.jitterY = -world.cameraJitter.y;
    // The motion vectors are already in pixels.
    upscale.motionVectorScaleX = 1.0f;
    upscale.motionVectorScaleY = 1.0f;
    upscale.cameraNear = kCameraNear;
    upscale.cameraFar = kCameraFar;
    upscale.cameraFovVertical = 2.0f * std::atan(1.0f / std::abs(world.cameraProjMat[1][1]));
    upscale.frameTimeDelta = smoothedFrameTime();
    upscale.preExposure = module.preExposure_;
    upscale.reset = module.cameraCut(
        glm::vec3(world.cameraPos),
        glm::vec3(world.cameraViewMat[0][2], world.cameraViewMat[1][2], world.cameraViewMat[2][2]));
    upscale.enableSharpening = true;
    upscale.sharpness = module.sharpness_;
    module.fsr3_->dispatch(upscale);

    // FidelityFX moves these through barriers of its own and back: whatever comes next waits on all of it.
    color->imageLayout() = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    frame_.deviceDepth->imageLayout() = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    frame_.fsrMotionVectors->imageLayout() = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    output->imageLayout() = VK_IMAGE_LAYOUT_GENERAL;
}

void FSRUpscalerModuleContext::recordAuxiliaryUpscales(FSRUpscalerModule &module, gpu::CommandBuffer &commandBuffer) {
    // The three passes are independent of each other: one barrier readies all of them.
    commandBuffer.sync({
        {frame_.inputs[kFirstHitDepth], gpu::ImageUsage::StorageRead},
        {frame_.outputs[kUpscaledFirstHitDepth], gpu::ImageUsage::StorageWrite, 0, true},
        {frame_.inputs[kMotionVectors], gpu::ImageUsage::StorageRead},
        {frame_.outputs[kUpscaledMotionVectors], gpu::ImageUsage::StorageWrite, 0, true},
        {frame_.inputs[kNormalRoughness], gpu::ImageUsage::StorageRead},
        {frame_.outputs[kUpscaledNormalRoughness], gpu::ImageUsage::StorageWrite, 0, true},
    });

    const MotionUpscalePushConstants constants{
        static_cast<float>(module.displayWidth_) / static_cast<float>(std::max(1u, module.renderWidth_)),
        static_cast<float>(module.displayHeight_) / static_cast<float>(std::max(1u, module.renderHeight_)),
        module.renderWidth_,
        module.renderHeight_,
        module.displayWidth_,
        module.displayHeight_,
    };
    commandBuffer.bindDescriptorTable(frame_.descriptorTable, VK_PIPELINE_BIND_POINT_COMPUTE);
    for (const auto *pipeline : {&module.firstHitDepthUpscalePipeline_, &module.motionUpscalePipeline_,
                                 &module.normalRoughnessUpscalePipeline_}) {
        commandBuffer.bindComputePipeline(*pipeline);
        vkCmdPushConstants(commandBuffer.vkCommandBuffer(), frame_.descriptorTable->vkPipelineLayout(),
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
        vkCmdDispatch(commandBuffer.vkCommandBuffer(), groupCount(module.displayWidth_),
                      groupCount(module.displayHeight_), 1);
    }
}

float FSRUpscalerModuleContext::smoothedFrameTime() {
    const Clock::time_point now = Clock::now();
    float delta = std::chrono::duration<float, std::milli>(now - lastFrameTime_).count();
    lastFrameTime_ = now;
    if (!std::exchange(timingStarted_, true)) return kFallbackFrameTime;
    if (delta < 1.0f || delta > 100.0f) delta = kFallbackFrameTime;

    frameTimes_[nextFrameTime_] = delta;
    nextFrameTime_ = (nextFrameTime_ + 1) % frameTimeSamples;
    frameTimeCount_ = std::min(frameTimeCount_ + 1, frameTimeSamples);

    // Summed oldest first.
    const uint32_t oldest = frameTimeCount_ < frameTimeSamples ? 0 : nextFrameTime_;
    float sum = 0.0f;
    for (uint32_t i = 0; i < frameTimeCount_; i++) sum += frameTimes_[(oldest + i) % frameTimeSamples];
    return sum / static_cast<float>(frameTimeCount_);
}
