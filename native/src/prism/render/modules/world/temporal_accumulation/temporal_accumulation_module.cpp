#include "prism/render/modules/world/temporal_accumulation/temporal_accumulation_module.hpp"

#include "prism/render/buffers.hpp"
#include "prism/render/pipeline.hpp"
#include "prism/render/render_framework.hpp"
#include "prism/render/renderer.hpp"

#include <filesystem>

namespace {

// How much of this frame goes into the result where the history is kept, and how closely this frame's normal has to
// match the history's (the cosine between them) for the history to be kept.
constexpr TemporalAccumulationPushConstant kBlend = {.alpha = 0.12f, .threshold = 0.9f};

constexpr VkImageUsageFlags kImageUsage =
    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

constexpr VkPipelineStageFlags2 kFragment = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;

// A colour target of the render pass: cleared as the pass begins, kept, and in COLOR_ATTACHMENT_OPTIMAL before and
// after it.
VkAttachmentDescription clearedTarget(VkFormat format) {
    return {
        .format = format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
}

// Copies the whole of one image over the whole of another of the same size. A blit rather than a copy because the
// formats may differ: the history keeps the input's format, the output has the one the pipeline gives it.
void blitWhole(gpu::CommandBuffer &commandBuffer, gpu::DeviceLocalImage &source, gpu::DeviceLocalImage &target) {
    VkImageBlit region{};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.srcOffsets[1] = {static_cast<int32_t>(source.width()), static_cast<int32_t>(source.height()), 1};
    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstOffsets[1] = {static_cast<int32_t>(target.width()), static_cast<int32_t>(target.height()), 1};
    vkCmdBlitImage(commandBuffer.vkCommandBuffer(), source.vkImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   target.vkImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_LINEAR);
}

} // namespace

TemporalAccumulationModule::TemporalAccumulationModule() {}

void TemporalAccumulationModule::init(std::shared_ptr<Framework> framework,
                                      std::shared_ptr<WorldPipeline> worldPipeline) {
    WorldModule::init(framework, worldPipeline);
    frames_.resize(framework->swapchain()->imageCount());
}

bool TemporalAccumulationModule::setOrCreateInputImages(std::vector<std::shared_ptr<gpu::DeviceLocalImage>> &images,
                                                        std::vector<VkFormat> &formats,
                                                        uint32_t frameIndex) {
    if (images.size() != inputImageNum) return false;

    auto framework = framework_.lock();
    for (uint32_t i = 0; i < inputImageNum; i++) {
        if (images[i] == nullptr) {
            images[i] = gpu::DeviceLocalImage::create(framework->device(), framework->vma(), false, width_, height_, 1,
                                                      formats[i], kImageUsage);
        } else if (images[i]->width() != width_ || images[i]->height() != height_) {
            return false;
        }
        frames_[frameIndex].inputs[i] = images[i];
    }
    return true;
}

bool TemporalAccumulationModule::setOrCreateOutputImages(std::vector<std::shared_ptr<gpu::DeviceLocalImage>> &images,
                                                         std::vector<VkFormat> &,
                                                         uint32_t frameIndex) {
    if (images.size() != outputImageNum || images[0] == nullptr) return false;

    width_ = images[0]->width();
    height_ = images[0]->height();
    frames_[frameIndex].radiance = images[0];
    return true;
}

// The module has no settings.
void TemporalAccumulationModule::setAttributes(int, std::vector<std::string> &) {}

void TemporalAccumulationModule::build() {
    auto framework = framework_.lock();
    auto worldPipeline = worldPipeline_.lock();

    createImages(*framework);
    createDescriptorTables(*framework);
    createPipeline(*framework);

    contexts_.resize(frames_.size());
    for (uint32_t i = 0; i < frames_.size(); i++) {
        contexts_[i] = TemporalAccumulationModuleContext::create(framework->contexts()[i], worldPipeline->contexts()[i],
                                                                 shared_from_this());
    }
}

std::vector<std::shared_ptr<WorldModuleContext>> &TemporalAccumulationModule::contexts() {
    return contexts_;
}

void TemporalAccumulationModule::bindTexture(std::shared_ptr<gpu::Sampler>,
                                             std::shared_ptr<gpu::DeviceLocalImage>,
                                             int) {}

void TemporalAccumulationModule::preClose() {}

void TemporalAccumulationModule::createImages(Framework &framework) {
    auto device = framework.device();
    auto vma = framework.vma();

    // The history keeps the formats of what it follows: the input radiance and the input normals.
    VkFormat radianceFormat = frames_[0].inputs[Radiance]->vkFormat();
    VkFormat normalFormat = frames_[0].inputs[NormalRoughness]->vkFormat();
    historyRadiance_ =
        gpu::DeviceLocalImage::create(device, vma, false, width_, height_, 1, radianceFormat, kImageUsage);
    historyNormal_ = gpu::DeviceLocalImage::create(device, vma, false, width_, height_, 1, normalFormat, kImageUsage);
    for (Frame &frame : frames_) {
        frame.normal = gpu::DeviceLocalImage::create(device, vma, false, width_, height_, 1, normalFormat, kImageUsage);
    }
}

void TemporalAccumulationModule::createDescriptorTables(Framework &framework) {
    auto device = framework.device();
    sampler_ =
        gpu::Sampler::create(device, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT);

    for (Frame &frame : frames_) {
        // Five sampled images: this frame's radiance, the history's, the motion vectors, this frame's normals and the
        // history's.
        gpu::DescriptorTableBuilder builder;
        auto &bindings = builder.beginDescriptorLayoutSet().beginDescriptorLayoutSetBinding();
        for (uint32_t binding = 0; binding < 5; binding++) {
            bindings.defineDescriptorLayoutSetBinding({
                .binding = binding,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
            });
        }
        frame.descriptors = bindings.endDescriptorLayoutSetBinding()
                                .endDescriptorLayoutSet()
                                .definePushConstant({
                                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                    .offset = 0,
                                    .size = sizeof(TemporalAccumulationPushConstant),
                                })
                                .build(device);

        frame.descriptors->bindSamplerImageForShader(sampler_, frame.inputs[Radiance], 0, 0);
        frame.descriptors->bindSamplerImageForShader(sampler_, historyRadiance_, 0, 1);
        frame.descriptors->bindSamplerImageForShader(sampler_, frame.inputs[MotionVector], 0, 2);
        frame.descriptors->bindSamplerImageForShader(sampler_, frame.inputs[NormalRoughness], 0, 3);
        frame.descriptors->bindSamplerImageForShader(sampler_, historyNormal_, 0, 4);
    }
}

void TemporalAccumulationModule::createPipeline(Framework &framework) {
    auto device = framework.device();

    renderPass_ = gpu::RenderPassBuilder{}
                      .beginAttachmentDescription()
                      .defineAttachmentDescription(clearedTarget(frames_[0].radiance->vkFormat()))
                      .defineAttachmentDescription(clearedTarget(frames_[0].normal->vkFormat()))
                      .endAttachmentDescription()
                      .beginAttachmentReference()
                      .defineAttachmentReference({.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL})
                      .defineAttachmentReference({.attachment = 1, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL})
                      .endAttachmentReference()
                      .beginSubpassDescription()
                      .defineSubpassDescription({
                          .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                          .colorAttachmentIndices = {0, 1},
                      })
                      .endSubpassDescription()
                      .build(device);

    for (Frame &frame : frames_) {
        frame.framebuffer = gpu::FramebufferBuilder{}
                                .beginAttachment()
                                .defineAttachment(frame.radiance)
                                .defineAttachment(frame.normal)
                                .endAttachment()
                                .build(device, renderPass_);
    }

    std::filesystem::path shaderPath = Renderer::folderPath / "shaders";
    auto vertexShader =
        gpu::Shader::create(device, (shaderPath / "world/temporal_accumulation/tmp_acc_vert.spv").string());
    auto fragmentShader =
        gpu::Shader::create(device, (shaderPath / "world/temporal_accumulation/tmp_acc_frag.spv").string());

    // One triangle over the whole of both targets: no vertex input, no depth, each target written as is.
    VkExtent2D extent = {width_, height_};
    pipeline_ = gpu::GraphicsPipelineBuilder{}
                    .defineRenderPass(renderPass_, 0)
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
                    .defineDefaultColorBlendAttachmentState() // radiance
                    .defineDefaultColorBlendAttachmentState() // normals
                    .endColorBlendAttachmentState()
                    .definePipelineLayout(frames_[0].descriptors)
                    .build(device);
}

void TemporalAccumulationModule::record(uint32_t frameIndex, gpu::CommandBuffer &commandBuffer) {
    Frame &frame = frames_[frameIndex];

    commandBuffer.sync({
        {frame.inputs[Radiance], gpu::ImageUsage::Sampled, kFragment},
        {frame.inputs[MotionVector], gpu::ImageUsage::Sampled, kFragment},
        {frame.inputs[NormalRoughness], gpu::ImageUsage::Sampled, kFragment},
        {historyRadiance_, gpu::ImageUsage::Sampled, kFragment},
        {historyNormal_, gpu::ImageUsage::Sampled, kFragment},
        // The render pass clears both targets as it begins.
        {frame.radiance, gpu::ImageUsage::ColorAttachment, 0, true},
        {frame.normal, gpu::ImageUsage::ColorAttachment, 0, true},
    });

    auto blend = kBlend;
    if (Renderer::instance().buffers()->heldLightHistoryReset()) blend.alpha = 1.0f;
    vkCmdPushConstants(commandBuffer.vkCommandBuffer(), frame.descriptors->vkPipelineLayout(),
                       VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(blend), &blend);
    commandBuffer.beginRenderPass({
        .renderPass = renderPass_,
        .framebuffer = frame.framebuffer,
        .renderAreaExtent = {frame.radiance->width(), frame.radiance->height()},
        .clearValues = {{.color = {0.0f, 0.0f, 0.0f, 1.0f}}, {.color = {0.0f, 0.0f, 0.0f, 1.0f}}},
    });
    commandBuffer.bindGraphicsPipeline(pipeline_)
        ->bindDescriptorTable(frame.descriptors, VK_PIPELINE_BIND_POINT_GRAPHICS)
        ->draw(3, 1)
        ->endRenderPass();

    // This frame's result becomes the history the next frame reads. The copy replaces all of it.
    commandBuffer.sync({
        {frame.radiance, gpu::ImageUsage::TransferSrc},
        {frame.normal, gpu::ImageUsage::TransferSrc},
        {historyRadiance_, gpu::ImageUsage::TransferDst, 0, true},
        {historyNormal_, gpu::ImageUsage::TransferDst, 0, true},
    });
    blitWhole(commandBuffer, *frame.radiance, *historyRadiance_);
    blitWhole(commandBuffer, *frame.normal, *historyNormal_);
}

TemporalAccumulationModuleContext::TemporalAccumulationModuleContext(
    std::shared_ptr<FrameworkContext> frameworkContext,
    std::shared_ptr<WorldPipelineContext> worldPipelineContext,
    std::shared_ptr<TemporalAccumulationModule> temporalAccumulationModule)
    : WorldModuleContext(frameworkContext, worldPipelineContext),
      module_(temporalAccumulationModule),
      frameIndex_(frameworkContext->frameIndex) {}

void TemporalAccumulationModuleContext::render() {
    auto context = frameworkContext.lock();
    module_.lock()->record(frameIndex_, *context->worldCommandBuffer);
}
