#include "prism/render/output_upscaler.hpp"

#include "prism/render/render_framework.hpp"
#include "prism/render/renderer.hpp"
#include "prism/util/logging.hpp"

#include <exception>

namespace {

// The push constants of shader/display/fsr1_easu.comp.
struct EasuConstants {
    uint32_t inputWidth;
    uint32_t inputHeight;
    uint32_t outputWidth;
    uint32_t outputHeight;
};
static_assert(sizeof(EasuConstants) == 16);

// The push constants of shader/display/fsr1_rcas.comp.
struct RcasConstants {
    uint32_t width;
    uint32_t height;
    float sharpness;
};
static_assert(sizeof(RcasConstants) == 12);

// Both passes have one input and one output; EASU samples its input, RCAS loads it.
std::shared_ptr<gpu::DescriptorTable>
makeTable(const std::shared_ptr<gpu::Device> &device, VkDescriptorType inputType, uint32_t pushConstantSize) {
    const auto binding = [](uint32_t index, VkDescriptorType type) {
        return VkDescriptorSetLayoutBinding{
            .binding = index,
            .descriptorType = type,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        };
    };
    return gpu::DescriptorTableBuilder{}
        .beginDescriptorLayoutSet()
        .beginDescriptorLayoutSetBinding()
        .defineDescriptorLayoutSetBinding(binding(0, inputType))
        .defineDescriptorLayoutSetBinding(binding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE))
        .endDescriptorLayoutSetBinding()
        .endDescriptorLayoutSet()
        .definePushConstant({
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .offset = 0,
            .size = pushConstantSize,
        })
        .build(device);
}

std::shared_ptr<gpu::ComputePipeline> makePipeline(const std::shared_ptr<gpu::Device> &device,
                                                   const char *shader,
                                                   const std::shared_ptr<gpu::DescriptorTable> &layout) {
    auto module = gpu::Shader::create(device, (Renderer::folderPath / "shaders/display" / shader).string());
    return gpu::ComputePipelineBuilder{}.defineShader(module).definePipelineLayout(layout).build(device);
}

// Each 64 thread group covers a 16x16 tile (fsr1_*.comp).
uint32_t tiles(uint32_t pixels) {
    return (pixels + 15) / 16;
}

} // namespace

bool OutputUpscaler::prepare(Framework &framework, uint32_t width, uint32_t height, uint32_t frameCount) {
    if (failed_) return false;
    const auto device = framework.device();
    try {
        if (easu_ == nullptr || easuTables_.size() != frameCount) {
            easuTables_.clear();
            rcasTables_.clear();
            for (uint32_t i = 0; i < frameCount; i++) {
                easuTables_.push_back(
                    makeTable(device, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, sizeof(EasuConstants)));
                rcasTables_.push_back(makeTable(device, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, sizeof(RcasConstants)));
            }
        }
        if (easu_ == nullptr) {
            easu_ = makePipeline(device, "fsr1_easu_comp.spv", easuTables_[0]);
            rcas_ = makePipeline(device, "fsr1_rcas_comp.spv", rcasTables_[0]);
            linear_ = gpu::Sampler::create(device, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST,
                                           VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        }
        if (sharpened_ == nullptr || sharpened_->width() != width || sharpened_->height() != height) {
            gpu::ImageDesc desc;
            desc.width = width;
            desc.height = height;
            desc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
            desc.usage = VK_IMAGE_USAGE_STORAGE_BIT;
            upscaled_ = gpu::DeviceLocalImage::create(device, framework.vma(), desc);
            // Sampled too: HDR output and frame generation read it as the world shown in Minecraft's target.
            desc.format = VK_FORMAT_R8G8B8A8_UNORM;
            desc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            sharpened_ = gpu::DeviceLocalImage::create(device, framework.vma(), desc);
            radiante::out() << "[Output resolution] FSR 1 upscale to " << width << "x" << height << std::endl;
        }
        return true;
    } catch (const std::exception &e) {
        radiante::err() << "[Output resolution] could not set up the FSR 1 upscale, blitting instead: " << e.what()
                        << std::endl;
        failed_ = true;
        return false;
    }
}

std::shared_ptr<gpu::DeviceLocalImage> OutputUpscaler::record(Framework &framework,
                                                              gpu::CommandBuffer &commandBuffer,
                                                              const std::shared_ptr<gpu::DeviceLocalImage> &source,
                                                              VkImageLayout restingLayout,
                                                              uint32_t width,
                                                              uint32_t height,
                                                              uint32_t frameIndex) {
    const uint32_t frameCount = framework.swapchain()->imageCount();
    if (source == nullptr || frameIndex >= frameCount || !prepare(framework, width, height, frameCount)) return nullptr;
    const VkCommandBuffer commands = commandBuffer.vkCommandBuffer();

    // EASU: the world's picture, sampled, into the half float image at the window's size.
    commandBuffer.sync({
        {source, gpu::ImageUsage::Sampled, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT},
        {upscaled_, gpu::ImageUsage::StorageWrite, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, /*discard=*/true},
    });
    const auto &easuTable = easuTables_[frameIndex];
    easuTable->bindSamplerImage(linear_, source, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, 0, 0);
    easuTable->bindImage(upscaled_, VK_IMAGE_LAYOUT_GENERAL, 0, 1);
    const EasuConstants easu{source->width(), source->height(), width, height};
    commandBuffer.bindDescriptorTable(easuTable, VK_PIPELINE_BIND_POINT_COMPUTE)->bindComputePipeline(easu_);
    vkCmdPushConstants(commands, easuTable->vkPipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(easu), &easu);
    vkCmdDispatch(commands, tiles(width), tiles(height), 1);

    // RCAS: sharpened into the 8-bit picture that is shown.
    commandBuffer.sync({
        {upscaled_, gpu::ImageUsage::StorageRead, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT},
        {sharpened_, gpu::ImageUsage::StorageWrite, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, /*discard=*/true},
    });
    const auto &rcasTable = rcasTables_[frameIndex];
    rcasTable->bindImage(upscaled_, VK_IMAGE_LAYOUT_GENERAL, 0, 0);
    rcasTable->bindImage(sharpened_, VK_IMAGE_LAYOUT_GENERAL, 0, 1);
    const RcasConstants rcas{width, height, SHARPNESS_STOPS};
    commandBuffer.bindDescriptorTable(rcasTable, VK_PIPELINE_BIND_POINT_COMPUTE)->bindComputePipeline(rcas_);
    vkCmdPushConstants(commands, rcasTable->vkPipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(rcas), &rcas);
    vkCmdDispatch(commands, tiles(width), tiles(height), 1);

    // The world's picture back in the layout it rests in between frames, which the passes of the next frame and the
    // HDR output expect; the sharpened picture in GENERAL, ready for the blit into Minecraft's target.
    commandBuffer.barriersBufferImage({}, {{
                                              .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                              .srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
                                              .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                              .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT |
                                                               VK_ACCESS_2_MEMORY_WRITE_BIT,
                                              .oldLayout = source->state().layout,
                                              .newLayout = restingLayout,
                                              .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                              .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                              .image = source,
                                              .subresourceRange = gpu::wholeColorSubresourceRange,
                                          }});
    source->imageLayout() = restingLayout;
    commandBuffer.sync({{sharpened_, gpu::ImageUsage::General}});
    return sharpened_;
}

void OutputUpscaler::reset() {
    easuTables_.clear();
    rcasTables_.clear();
    easu_ = nullptr;
    rcas_ = nullptr;
    linear_ = nullptr;
    upscaled_ = nullptr;
    sharpened_ = nullptr;
}
