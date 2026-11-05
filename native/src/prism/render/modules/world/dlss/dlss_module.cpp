#include "prism/render/modules/world/dlss/dlss_module.hpp"

#include "prism/render/buffers.hpp"
#include "prism/render/pipeline.hpp"
#include "prism/render/render_framework.hpp"
#include "prism/render/renderer.hpp"
#include "prism/util/logging.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iterator>
#include <span>
#include <system_error>

std::shared_ptr<NgxContext> DLSSModule::ngxContext_ = nullptr;
bool DLSSModule::rayReconstructionAvailable_ = false;

namespace {

constexpr std::string_view kRayReconstructionAttribute = "render_pipeline.module.dlss.attribute.ray_reconstruction";
constexpr std::string_view kRenderScaleAttribute = "render_pipeline.module.dlss.attribute.render_scale";
constexpr std::string_view kModeAttribute = "render_pipeline.module.dlss.attribute.mode";
constexpr std::string_view kTrue = "render_pipeline.true";

struct ModeValue {
    std::string_view name;
    NVSDK_NGX_PerfQuality_Value mode;
};

constexpr ModeValue kModes[] = {
    {"render_pipeline.module.dlss.attribute.mode.ultra_performance", NVSDK_NGX_PerfQuality_Value_UltraPerformance},
    {"render_pipeline.module.dlss.attribute.mode.performance", NVSDK_NGX_PerfQuality_Value_MaxPerf},
    {"render_pipeline.module.dlss.attribute.mode.balanced", NVSDK_NGX_PerfQuality_Value_Balanced},
    {"render_pipeline.module.dlss.attribute.mode.quality", NVSDK_NGX_PerfQuality_Value_MaxQuality},
    {"render_pipeline.module.dlss.attribute.mode.dlaa", NVSDK_NGX_PerfQuality_Value_DLAA},
};

// Ray Reconstruction at Ultra Performance hangs the GPU a few frames in when the render resolution it picks is tiny:
// at an 854x480 window (a 285x160 render) the device is lost within seconds, while the same render size at
// Performance, and Ultra Performance from 1280x720 up (427x240), both run. NGX still reports the tiny size as
// supported, so the floor is ours.
constexpr uint32_t kMinRayReconstructionHeight = 240;

// The first frames after a build ask DLSS to drop its history: the images it accumulates from start out as whatever
// memory they were given, and blending with that left pink and green blotches over the world for seconds after a
// settings change or a window resize.
constexpr uint32_t kHistoryResetFrames = 3;

constexpr VkImageUsageFlags kInputUsage =
    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

// The usage of each output the module makes itself. The processed image always comes from the module after it.
constexpr std::array<VkImageUsageFlags, DLSSModule::outputImageNum> kOutputUsage = {
    0,
    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
};

// The passes that bring first-hit depth, motion vectors and normal/roughness to the output size, in the order they
// run. They share one set of storage images: first-hit depth in and out at bindings 0 and 1, motion vectors at 2 and
// 4, normal/roughness at 5 and 6.
constexpr std::array<const char *, 3> kUpscaleShaders = {
    "shaders/world/upscaler/upscale_first_hit_depth_comp.spv",
    "shaders/world/upscaler/upscale_motion_vector_comp.spv",
    "shaders/world/upscaler/upscale_normal_roughness_comp.spv",
};
constexpr std::array<uint32_t, 6> kUpscaleBindings = {0, 1, 2, 4, 5, 6};

std::shared_ptr<gpu::DescriptorTable> makeUpscaleTable(const std::shared_ptr<gpu::Device> &device) {
    gpu::DescriptorTableBuilder builder;
    auto &set = builder.beginDescriptorLayoutSet().beginDescriptorLayoutSetBinding();
    for (uint32_t binding : kUpscaleBindings) {
        set.defineDescriptorLayoutSetBinding({
            .binding = binding,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        });
    }
    set.endDescriptorLayoutSetBinding().endDescriptorLayoutSet().definePushConstant({
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = sizeof(MotionUpscalePushConstants),
    });
    return builder.build(device);
}

// DLSS is told a mode even at a custom render scale: the one whose own render size is nearest suits its tuning best.
NVSDK_NGX_PerfQuality_Value modeForScale(float scale) {
    if (scale >= 0.995f) return NVSDK_NGX_PerfQuality_Value_DLAA;
    if (scale >= 0.62f) return NVSDK_NGX_PerfQuality_Value_MaxQuality;
    if (scale >= 0.54f) return NVSDK_NGX_PerfQuality_Value_Balanced;
    if (scale >= 0.40f) return NVSDK_NGX_PerfQuality_Value_MaxPerf;
    return NVSDK_NGX_PerfQuality_Value_UltraPerformance;
}

} // namespace

bool DLSSModule::initNGXContext() {
    const std::filesystem::path dlssPath = Renderer::folderPath / "dlss";
    std::error_code ec;
    std::filesystem::create_directories(dlssPath, ec);
    if (ec) {
        // NGX would find neither its libraries nor a place for its logs: the module is left out.
        radiante::err() << "Failed to create directory: " << ec.message() << std::endl;
        return false;
    }

    auto framework = Renderer::instance().framework();
    if (framework == nullptr) return false;

    ngxContext_ = NgxContext::create();
    NgxContext::NgxInitInfo initInfo{};
    initInfo.instance = framework->instance();
    initInfo.physicalDevice = framework->physicalDevice();
    initInfo.device = framework->device();
    initInfo.applicationPath = dlssPath;
    if (ngxContext_->init(initInfo) != NVSDK_NGX_Result_Success) {
        ngxContext_ = nullptr;
        return false;
    }

    // Either feature is enough to keep NGX: a GPU may offer plain upscaling without Ray Reconstruction.
    const bool rayReconstruction = ngxContext_->queryDlssRRAvailable() == NVSDK_NGX_Result_Success;
    const bool superResolution = ngxContext_->queryDlssSRAvailable() == NVSDK_NGX_Result_Success;
    if (!rayReconstruction && !superResolution) {
        deinitNGXContext();
        return false;
    }
    rayReconstructionAvailable_ = rayReconstruction;
    return true;
}

void DLSSModule::deinitNGXContext() {
    if (ngxContext_ == nullptr) return;
    ngxContext_->deinit();
    ngxContext_ = nullptr;
}

void DLSSModule::init(std::shared_ptr<Framework> framework, std::shared_ptr<WorldPipeline> worldPipeline) {
    WorldModule::init(framework, worldPipeline);
    frames_.resize(framework->swapchain()->imageCount());
    dlss_ = DlssRR::create();
    dlssSR_ = DlssSR::create();
}

bool DLSSModule::setOrCreateInputImages(std::vector<std::shared_ptr<gpu::DeviceLocalImage>> &images,
                                        std::vector<VkFormat> &formats,
                                        uint32_t frameIndex) {
    auto framework = framework_.lock();
    if (framework == nullptr || ngxContext_ == nullptr) return false;
    if (images.size() != inputImageNum || formats.size() != inputImageNum || frameIndex >= frames_.size()) {
        return false;
    }

    if (!renderSizeChosen_ || renderSizeFor_.width != outputWidth_ || renderSizeFor_.height != outputHeight_) {
        chooseRenderSize();
    }
    if (inputWidth_ == 0 || inputHeight_ == 0) {
        radiante::err() << "[DLSS] no render size for " << outputWidth_ << "x" << outputHeight_ << std::endl;
        return false;
    }

    Frame &frame = frames_[frameIndex];
    for (uint32_t i = 0; i < inputImageNum; i++) {
        if (images[i] == nullptr) {
            images[i] = gpu::DeviceLocalImage::create(framework->device(), framework->vma(), false, inputWidth_,
                                                      inputHeight_, 1, formats[i], kInputUsage);
        } else if (images[i]->width() != inputWidth_ || images[i]->height() != inputHeight_) {
            return false;
        }
        frame.inputs[i] = images[i];
    }
    return true;
}

bool DLSSModule::setOrCreateOutputImages(std::vector<std::shared_ptr<gpu::DeviceLocalImage>> &images,
                                         std::vector<VkFormat> &formats,
                                         uint32_t frameIndex) {
    auto framework = framework_.lock();
    if (framework == nullptr || ngxContext_ == nullptr) return false;
    if (images.size() != outputImageNum || formats.size() != outputImageNum || frameIndex >= frames_.size() ||
        images[Processed] == nullptr) {
        return false;
    }

    outputWidth_ = images[Processed]->width();
    outputHeight_ = images[Processed]->height();

    Frame &frame = frames_[frameIndex];
    for (uint32_t i = 0; i < outputImageNum; i++) {
        if (images[i] == nullptr) {
            images[i] = gpu::DeviceLocalImage::create(framework->device(), framework->vma(), false, outputWidth_,
                                                      outputHeight_, 1, formats[i], kOutputUsage[i]);
        } else if (images[i]->width() != outputWidth_ || images[i]->height() != outputHeight_) {
            return false;
        }
        frame.outputs[i] = images[i];
    }
    return true;
}

void DLSSModule::chooseRenderSize() {
    renderSizeChosen_ = true;
    renderSizeFor_ = {outputWidth_, outputHeight_};

    effectiveMode_ = renderScale_ > 0.0f ? modeForScale(renderScale_) : mode_;
    NgxContext::QuerySizeInfo query{.outputSize = {outputWidth_, outputHeight_}, .quality = effectiveMode_};
    // A failed query leaves the sizes as they were.
    if (rayReconstruction_) {
        ngxContext_->querySupportedDlssInputSizes(query, supportedSizes_);
    } else {
        ngxContext_->querySupportedDlssSRInputSizes(query, supportedSizes_);
    }
    radiante::out() << "DLSS sizes:" << std::endl;
    radiante::out() << "\tminSize: [" << supportedSizes_.minSize.width << ", " << supportedSizes_.minSize.height << "]"
                    << std::endl;
    radiante::out() << "\tmaxSize: [" << supportedSizes_.maxSize.width << ", " << supportedSizes_.maxSize.height << "]"
                    << std::endl;
    radiante::out() << "\toptimalSize: [" << supportedSizes_.optimalSize.width << ", "
                    << supportedSizes_.optimalSize.height << "]" << std::endl;

    if (rayReconstruction_ && effectiveMode_ == NVSDK_NGX_PerfQuality_Value_UltraPerformance &&
        supportedSizes_.optimalSize.height < kMinRayReconstructionHeight) {
        radiante::out() << "[DLSS] Ultra Performance would render at " << supportedSizes_.optimalSize.width << "x"
                        << supportedSizes_.optimalSize.height << "; using Performance at this window size"
                        << std::endl;
        effectiveMode_ = NVSDK_NGX_PerfQuality_Value_MaxPerf;
        query.quality = effectiveMode_;
        ngxContext_->querySupportedDlssInputSizes(query, supportedSizes_);
    }

    inputWidth_ = supportedSizes_.optimalSize.width;
    inputHeight_ = supportedSizes_.optimalSize.height;
    if (renderScale_ > 0.0f && renderScale_ < 0.995f && outputWidth_ > 0 && outputHeight_ > 0) {
        float scale = renderScale_;
        // The same floor: Ray Reconstruction loses the device on very small renders.
        if (rayReconstruction_) {
            scale = std::max(scale,
                             static_cast<float>(kMinRayReconstructionHeight) / static_cast<float>(outputHeight_));
        }
        scale = std::min(scale, 1.0f);
        inputWidth_ = std::max(1u, static_cast<uint32_t>(std::lround(static_cast<float>(outputWidth_) * scale)));
        inputHeight_ = std::max(1u, static_cast<uint32_t>(std::lround(static_cast<float>(outputHeight_) * scale)));
        radiante::out() << "[DLSS] custom render scale " << scale << ": " << inputWidth_ << "x" << inputHeight_
                        << std::endl;
    }
}

void DLSSModule::setAttributes(int attributeCount, std::vector<std::string> &attributeKVs) {
    const size_t count = std::min(static_cast<size_t>(std::max(attributeCount, 0)), attributeKVs.size() / 2);
    for (size_t i = 0; i < count; i++) {
        const std::string &key = attributeKVs[2 * i];
        const std::string &value = attributeKVs[2 * i + 1];
        if (key == kRayReconstructionAttribute) {
            rayReconstruction_ = value == kTrue;
        } else if (key == kRenderScaleAttribute) {
            const float percent = std::stof(value);
            renderScale_ = percent > 0.0f ? std::clamp(percent / 100.0f, 0.20f, 1.0f) : 0.0f;
        } else if (key == kModeAttribute) {
            for (const ModeValue &mode : kModes) {
                if (value == mode.name) {
                    mode_ = mode.mode;
                    break;
                }
            }
        }
    }
}

void DLSSModule::build() {
    framesSinceBuild_ = 0;

    auto framework = framework_.lock();
    auto worldPipeline = worldPipeline_.lock();
    auto device = framework->device();
    const uint32_t frameCount = framework->swapchain()->imageCount();

    radiante::out() << "[DLSS] creating " << (rayReconstruction_ ? "ray reconstruction" : "super resolution")
                    << " at " << inputWidth_ << "x" << inputHeight_ << " -> " << outputWidth_ << "x" << outputHeight_
                    << std::endl;

    // A failed creation is logged by the wrapper; the frames then skip the evaluation.
    const VkExtent2D inputSize{inputWidth_, inputHeight_};
    const VkExtent2D outputSize{outputWidth_, outputHeight_};
    if (rayReconstruction_) {
        ngxContext_->initDlssRR({.inputSize = inputSize, .outputSize = outputSize, .quality = effectiveMode_},
                                framework->mainCommandPool(), dlss_);
    } else {
        ngxContext_->initDlssSR({.inputSize = inputSize, .outputSize = outputSize, .quality = effectiveMode_},
                                framework->mainCommandPool(), dlssSR_);
    }

    upscaleConstants_ = {
        .scaleX = static_cast<float>(outputWidth_) / static_cast<float>(std::max(1u, inputWidth_)),
        .scaleY = static_cast<float>(outputHeight_) / static_cast<float>(std::max(1u, inputHeight_)),
        .srcWidth = inputWidth_,
        .srcHeight = inputHeight_,
        .dstWidth = outputWidth_,
        .dstHeight = outputHeight_,
    };

    std::array<std::shared_ptr<gpu::Shader>, kUpscaleShaders.size()> shaders;
    for (size_t i = 0; i < shaders.size(); i++) {
        shaders[i] = gpu::Shader::create(device, (Renderer::folderPath / kUpscaleShaders[i]).string());
    }

    // A frame's images never change after the build, so its set is written once, here.
    for (uint32_t i = 0; i < frameCount; i++) {
        Frame &frame = frames_[i];
        frame.upscaleTable = makeUpscaleTable(device);
        frame.upscaleTable->bindImage(frame.inputs[FirstHitDepth], VK_IMAGE_LAYOUT_GENERAL, 0, 0)
            ->bindImage(frame.outputs[UpscaledFirstHitDepth], VK_IMAGE_LAYOUT_GENERAL, 0, 1)
            ->bindImage(frame.inputs[MotionVector], VK_IMAGE_LAYOUT_GENERAL, 0, 2)
            ->bindImage(frame.outputs[UpscaledMotionVector], VK_IMAGE_LAYOUT_GENERAL, 0, 4)
            ->bindImage(frame.inputs[NormalRoughness], VK_IMAGE_LAYOUT_GENERAL, 0, 5)
            ->bindImage(frame.outputs[UpscaledNormalRoughness], VK_IMAGE_LAYOUT_GENERAL, 0, 6);
    }
    for (size_t i = 0; i < upscalePipelines_.size(); i++) {
        upscalePipelines_[i] = gpu::ComputePipelineBuilder{}
                                   .defineShader(shaders[i])
                                   .definePipelineLayout(frames_[0].upscaleTable)
                                   .build(device);
    }

    contexts_.resize(frameCount);
    for (uint32_t i = 0; i < frameCount; i++) {
        contexts_[i] =
            DLSSModuleContext::create(framework->contexts()[i], worldPipeline->contexts()[i], shared_from_this());
    }
}

std::vector<std::shared_ptr<WorldModuleContext>> &DLSSModule::contexts() {
    return contexts_;
}

void DLSSModule::bindTexture(std::shared_ptr<gpu::Sampler>, std::shared_ptr<gpu::DeviceLocalImage>, int) {}

void DLSSModule::preClose() {
    if (dlss_ != nullptr) dlss_->deinit();
    if (dlssSR_ != nullptr) dlssSR_->deinit();
}

DLSSModuleContext::DLSSModuleContext(std::shared_ptr<FrameworkContext> frameworkContext,
                                     std::shared_ptr<WorldPipelineContext> worldPipelineContext,
                                     std::shared_ptr<DLSSModule> dlssModule)
    : WorldModuleContext(frameworkContext, worldPipelineContext), dLSSModule(dlssModule) {
    const DLSSModule::Frame &frame = dlssModule->frames_[frameworkContext->frameIndex];
    hdrImage = frame.inputs[DLSSModule::Radiance];
    diffuseAlbedoImage = frame.inputs[DLSSModule::DiffuseAlbedo];
    specularAlbedoImage = frame.inputs[DLSSModule::SpecularAlbedo];
    normalRoughnessImage = frame.inputs[DLSSModule::NormalRoughness];
    motionVectorImage = frame.inputs[DLSSModule::MotionVector];
    linearDepthImage = frame.inputs[DLSSModule::LinearDepth];
    specularHitDepthImage = frame.inputs[DLSSModule::SpecularHitDepth];
    firstHitDepthImage = frame.inputs[DLSSModule::FirstHitDepth];
    processedImage = frame.outputs[DLSSModule::Processed];
    upscaledFirstHitDepthImage = frame.outputs[DLSSModule::UpscaledFirstHitDepth];
    upscaledMotionVectorImage = frame.outputs[DLSSModule::UpscaledMotionVector];
    upscaledNormalRoughnessImage = frame.outputs[DLSSModule::UpscaledNormalRoughness];
    motionDescriptorTable = frame.upscaleTable;
}

void DLSSModuleContext::render() {
    auto context = frameworkContext.lock();
    auto module = dLSSModule.lock();
    if (context == nullptr || module == nullptr) return;
    const std::shared_ptr<gpu::CommandBuffer> &cmd = context->worldCommandBuffer;

    // NGX records work of its own on these: the General use waits for whatever wrote them and makes the next use
    // wait for all of NGX's work. The specular hit distance, last, is a guide only Ray Reconstruction reads.
    const gpu::ImageUse ngxImages[] = {
        {hdrImage, gpu::ImageUsage::General},
        {diffuseAlbedoImage, gpu::ImageUsage::General},
        {specularAlbedoImage, gpu::ImageUsage::General},
        {normalRoughnessImage, gpu::ImageUsage::General},
        {motionVectorImage, gpu::ImageUsage::General},
        {linearDepthImage, gpu::ImageUsage::General},
        {processedImage, gpu::ImageUsage::General},
        {specularHitDepthImage, gpu::ImageUsage::General},
    };
    const size_t ngxImageCount = module->rayReconstruction_ ? std::size(ngxImages) : std::size(ngxImages) - 1;
    cmd->sync(std::span(ngxImages).first(ngxImageCount));

    // The camera of this frame, as the CPU wrote it for the shaders.
    const auto worldUniforms = Renderer::instance().buffers()->worldUniformBuffer();
    const auto *ubo =
        worldUniforms == nullptr ? nullptr : static_cast<const gpu::Data::WorldUBO *>(worldUniforms->mappedPtr());
    if (ubo != nullptr) {
        const bool starting = module->framesSinceBuild_ < kHistoryResetFrames;
        if (starting) module->framesSinceBuild_++;
        const bool reset = starting || Renderer::instance().buffers()->heldLightHistoryReset();
        const glm::uvec2 renderSize{module->inputWidth_, module->inputHeight_};

        if (module->rayReconstruction_) {
            DlssRR &dlss = *module->dlss_;
            if (dlss.ready()) {
                dlss.setResource(DlssRR::RESOURCE_COLOR_IN, hdrImage);
                dlss.setResource(DlssRR::RESOURCE_COLOR_OUT, processedImage);
                dlss.setResource(DlssRR::RESOURCE_DIFFUSE_ALBEDO, diffuseAlbedoImage);
                dlss.setResource(DlssRR::RESOURCE_SPECULAR_ALBEDO, specularAlbedoImage);
                dlss.setResource(DlssRR::RESOURCE_NORMALROUGHNESS, normalRoughnessImage);
                dlss.setResource(DlssRR::RESOURCE_MOTIONVECTOR, motionVectorImage);
                dlss.setResource(DlssRR::RESOURCE_LINEARDEPTH, linearDepthImage);
                dlss.setResource(DlssRR::RESOURCE_SPECULAR_HITDISTANCE, specularHitDepthImage);
                dlss.denoise(cmd, renderSize, ubo->cameraJitter, ubo->cameraViewMat, ubo->cameraProjMat, reset);
            }
        } else {
            // Plain upscaling: the colour was already denoised by NRD, and the G-buffer is none of its inputs.
            DlssSR &dlss = *module->dlssSR_;
            if (dlss.ready()) {
                dlss.setResource(DlssSR::RESOURCE_COLOR_IN, hdrImage);
                dlss.setResource(DlssSR::RESOURCE_COLOR_OUT, processedImage);
                dlss.setResource(DlssSR::RESOURCE_MOTIONVECTOR, motionVectorImage);
                dlss.setResource(DlssSR::RESOURCE_DEPTH, linearDepthImage);
                dlss.upscale(cmd, renderSize, ubo->cameraJitter, reset);
            }
        }
    }

    // The three upscale passes are independent of each other, so one barrier serves them all.
    constexpr VkPipelineStageFlags2 compute = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    cmd->sync({
        {firstHitDepthImage, gpu::ImageUsage::StorageRead, compute},
        {upscaledFirstHitDepthImage, gpu::ImageUsage::StorageWrite, compute},
        {motionVectorImage, gpu::ImageUsage::StorageRead, compute},
        {upscaledMotionVectorImage, gpu::ImageUsage::StorageWrite, compute},
        {normalRoughnessImage, gpu::ImageUsage::StorageRead, compute},
        {upscaledNormalRoughnessImage, gpu::ImageUsage::StorageWrite, compute},
    });
    // Their pipelines share one layout, so the set and the push constants are bound once for all three.
    cmd->bindDescriptorTable(motionDescriptorTable, VK_PIPELINE_BIND_POINT_COMPUTE);
    vkCmdPushConstants(cmd->vkCommandBuffer(), motionDescriptorTable->vkPipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(MotionUpscalePushConstants), &module->upscaleConstants_);
    const uint32_t groupsX = (module->outputWidth_ + 15) / 16;
    const uint32_t groupsY = (module->outputHeight_ + 15) / 16;
    for (const auto &pipeline : module->upscalePipelines_) {
        cmd->bindComputePipeline(pipeline);
        vkCmdDispatch(cmd->vkCommandBuffer(), groupsX, groupsY, 1);
    }
}
