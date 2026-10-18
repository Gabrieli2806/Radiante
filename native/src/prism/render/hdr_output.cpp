#include "prism/render/hdr_output.hpp"

#include "prism/render/modules/world/tone_mapping/tone_mapping_module.hpp"
#include "prism/render/pipeline.hpp"
#include "prism/render/render_framework.hpp"
#include "prism/render/renderer.hpp"
#include "prism/util/logging.hpp"

#include <algorithm>
#include <array>

namespace {

std::ostream &hdrErr() {
    return radiante::err() << "[HDR] ";
}

// The push constants of shader/display/hdr_compose.comp.
struct ComposeConstants {
    float paperWhite;     // scRGB units: 1.0 is 80 nits
    float peak;           // the display's peak, in paper whites
    float exposureBias;   // stops, as the tone mapping module applies them
    float saturation;
    float manualExposure;
    int32_t autoExposure;
    int32_t worldValid;
    int32_t debugView;
    int32_t scaled; // the world was upscaled to Minecraft's image from a lower output resolution
};
static_assert(sizeof(ComposeConstants) == 36);

constexpr VkPipelineStageFlags2 ANY_STAGE = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
constexpr VkAccessFlags2 ANY_ACCESS = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
constexpr VkPipelineStageFlags2 COMPUTE = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

// The pass records into Minecraft's command buffer and works on Minecraft's images: its barriers are written out.
VkImageMemoryBarrier2 layoutChange(VkImage image,
                                   VkImageLayout from,
                                   VkImageLayout to,
                                   VkPipelineStageFlags2 srcStages,
                                   VkAccessFlags2 srcAccess,
                                   VkPipelineStageFlags2 dstStages,
                                   VkAccessFlags2 dstAccess) {
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = srcStages;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = dstStages;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    return barrier;
}

void recordBarriers(VkCommandBuffer commands,
                    const VkMemoryBarrier2 *memory,
                    const VkImageMemoryBarrier2 *images,
                    size_t imageCount) {
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.memoryBarrierCount = memory != nullptr ? 1 : 0;
    dependency.pMemoryBarriers = memory;
    dependency.imageMemoryBarrierCount = static_cast<uint32_t>(imageCount);
    dependency.pImageMemoryBarriers = images;
    vkCmdPipelineBarrier2(commands, &dependency);
}

bool sameSize(const std::shared_ptr<gpu::DeviceLocalImage> &image, uint32_t width, uint32_t height) {
    return image != nullptr && image->width() == width && image->height() == height;
}

} // namespace

HdrOutput &HdrOutput::instance() {
    // Never destroyed: it holds Vulkan objects, and a static destructor would free them as the process exits, after the
    // device may already be gone (the game can end without the renderer closing), and crash in the driver on quit.
    // release() frees them properly when the renderer closes.
    static HdrOutput *output = new HdrOutput();
    return *output;
}

void HdrOutput::noteWorldFrame(const std::shared_ptr<WorldPipelineContext> &worldContext,
                               const std::shared_ptr<gpu::DeviceLocalImage> &upscaled) {
    std::scoped_lock lock(mutex_);
    world_ = WorldFrame{};
    worldFresh_ = false;
    if (worldContext == nullptr || worldContext->outputImage == nullptr) return;

    for (const auto &moduleContext : worldContext->worldModuleContexts) {
        auto toneMapping = std::dynamic_pointer_cast<ToneMappingModuleContext>(moduleContext);
        if (toneMapping == nullptr) continue;
        auto module = toneMapping->toneMappingModule.lock();
        if (module == nullptr) return;

        world_.output = worldContext->outputImage;
        world_.fused = upscaled != nullptr ? upscaled : worldContext->outputImage;
        world_.mapped = toneMapping->ldrImage;
        world_.radiance = toneMapping->hdrImage;
        world_.exposure = module->exposureBuffer();
        world_.exposureBias = module->exposureBias();
        world_.saturation = module->saturation();
        world_.manualExposure = module->manualExposure();
        world_.autoExposure = module->isAutoExposureEnabled();
        worldFresh_ = world_.mapped != nullptr && world_.radiance != nullptr && world_.exposure != nullptr;
        return;
    }
}

std::shared_ptr<gpu::ExternalImage>
HdrOutput::minecraftView(VkImage image, VkFormat format, uint32_t width, uint32_t height) {
    const VkDevice device = device_->vkDevice();
    for (auto retired = retiredViews_.begin(); retired != retiredViews_.end();) {
        if (++retired->age >= VIEW_RETIREMENT) {
            vkDestroyImageView(device, retired->view, nullptr);
            retired = retiredViews_.erase(retired);
        } else {
            ++retired;
        }
    }

    if (view_ != nullptr && viewedImage_ == image && view_->width() == width && view_->height() == height &&
        view_->vkFormat() == format) {
        return view_;
    }
    if (view_ != nullptr && view_->vkImageView(0) != VK_NULL_HANDLE) {
        retiredViews_.push_back({view_->vkImageView(0), 0});
    }
    view_ = nullptr;
    viewedImage_ = VK_NULL_HANDLE;

    VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    info.image = image;
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format = format;
    info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageView view = VK_NULL_HANDLE;
    if (vkCreateImageView(device, &info, nullptr, &view) != VK_SUCCESS) return nullptr;
    view_ = gpu::ExternalImage::create(image, width, height, format);
    view_->vkImageView(0) = view;
    viewedImage_ = image;
    return view_;
}

bool HdrOutput::prepare(uint32_t width, uint32_t height) {
    if (failed_) return false;
    if (!ready_) {
        if (!Renderer::is_initialized()) return false;
        auto framework = Renderer::instance().framework();
        if (framework == nullptr) return false;
        try {
            device_ = framework->device();
            vma_ = framework->vma();
            const auto binding = [](uint32_t index, VkDescriptorType type) {
                return VkDescriptorSetLayoutBinding{
                    .binding = index,
                    .descriptorType = type,
                    .descriptorCount = 1,
                    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                };
            };
            tables_.clear();
            for (uint32_t i = 0; i < TABLE_RING; i++) {
                tables_.push_back(gpu::DescriptorTableBuilder{}
                                      .beginDescriptorLayoutSet()
                                      .beginDescriptorLayoutSetBinding()
                                      // Minecraft's image, the fused world, the tone mapped world, its radiance
                                      .defineDescriptorLayoutSetBinding(
                                          binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER))
                                      .defineDescriptorLayoutSetBinding(
                                          binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER))
                                      .defineDescriptorLayoutSetBinding(
                                          binding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER))
                                      .defineDescriptorLayoutSetBinding(
                                          binding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER))
                                      // the exposure, the composed image
                                      .defineDescriptorLayoutSetBinding(binding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER))
                                      .defineDescriptorLayoutSetBinding(binding(5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE))
                                      // the world pipeline's picture, before an upscale to a lower output resolution
                                      .defineDescriptorLayoutSetBinding(
                                          binding(6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER))
                                      .endDescriptorLayoutSetBinding()
                                      .endDescriptorLayoutSet()
                                      .definePushConstant({
                                          .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                                          .offset = 0,
                                          .size = sizeof(ComposeConstants),
                                      })
                                      .build(device_));
            }
            auto shader =
                gpu::Shader::create(device_, (Renderer::folderPath / "shaders/display/hdr_compose_comp.spv").string());
            pipeline_ =
                gpu::ComputePipelineBuilder{}.defineShader(shader).definePipelineLayout(tables_[0]).build(device_);
            sampler_ = gpu::Sampler::create(device_, VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST,
                                            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
            noExposure_ = gpu::DeviceLocalBuffer::create(vma_, device_, 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            ready_ = true;
        } catch (const std::exception &e) {
            hdrErr() << "could not set up the HDR output, falling back to SDR: " << e.what() << std::endl;
            failed_ = true;
            return false;
        }
    }

    if (composed_ == nullptr || composed_->width() != width || composed_->height() != height) {
        gpu::ImageDesc desc;
        desc.width = width;
        desc.height = height;
        desc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        desc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        composed_ = gpu::DeviceLocalImage::create(device_, vma_, desc);
    }
    return true;
}

bool HdrOutput::compose(VkCommandBuffer commands,
                        VkImage minecraftImage,
                        VkImageLayout minecraftLayout,
                        VkFormat minecraftFormat,
                        uint32_t width,
                        uint32_t height,
                        VkImage swapchainImage,
                        uint32_t swapchainWidth,
                        uint32_t swapchainHeight,
                        float paperWhiteNits,
                        float peakNits,
                        bool flipY,
                        bool debugView) {
    std::scoped_lock lock(mutex_);
    if (commands == VK_NULL_HANDLE || width == 0 || height == 0) return false;
    if (!prepare(width, height)) return false;
    const std::shared_ptr<gpu::ExternalImage> minecraft = minecraftView(minecraftImage, minecraftFormat, width, height);
    if (minecraft == nullptr) return false;

    // With a lower output resolution the fused picture is at Minecraft's size and the others at the output's.
    const uint32_t outputWidth = world_.output != nullptr ? world_.output->width() : 0;
    const uint32_t outputHeight = world_.output != nullptr ? world_.output->height() : 0;
    const bool worldValid = worldFresh_ && sameSize(world_.fused, width, height) &&
                            sameSize(world_.mapped, outputWidth, outputHeight) &&
                            sameSize(world_.radiance, outputWidth, outputHeight);
    const bool scaled = worldValid && (outputWidth != width || outputHeight != height);
    // A world frame is shown once: when Minecraft presents again without tracing one (a menu over a paused game that
    // stopped rendering the level), what it shows is its own image.
    worldFresh_ = false;
    const std::array<gpu::DeviceLocalImage *, 4> worldImages = {world_.fused.get(), world_.mapped.get(),
                                                                world_.radiance.get(), world_.output.get()};

    // Every image the pass samples is read in GENERAL and put back afterwards as it was, so neither Minecraft nor the
    // world pipeline finds a layout it did not leave.
    struct Restore {
        VkImage image;
        VkImageLayout layout;
    };
    std::array<Restore, 5> restore{};
    size_t restoreCount = 0;
    std::array<VkImageMemoryBarrier2, 6> before{};
    size_t beforeCount = 0;
    const auto readInGeneral = [&](VkImage image, VkImageLayout layout) {
        if (layout == VK_IMAGE_LAYOUT_GENERAL) return;
        for (size_t i = 0; i < restoreCount; i++) {
            if (restore[i].image == image) return;
        }
        before[beforeCount++] = layoutChange(image, layout, VK_IMAGE_LAYOUT_GENERAL, ANY_STAGE, ANY_ACCESS, COMPUTE,
                                             VK_ACCESS_2_SHADER_READ_BIT);
        restore[restoreCount++] = {image, layout};
    };
    readInGeneral(minecraftImage, minecraftLayout);
    if (worldValid) {
        for (gpu::DeviceLocalImage *image : worldImages) readInGeneral(image->vkImage(), image->state().layout);
    }
    before[beforeCount++] = layoutChange(composed_->vkImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                                         ANY_STAGE, ANY_ACCESS, COMPUTE, VK_ACCESS_2_SHADER_WRITE_BIT);
    // Everything written before, by anyone, is visible to the pass.
    VkMemoryBarrier2 written{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    written.srcStageMask = ANY_STAGE;
    written.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    written.dstStageMask = COMPUTE;
    written.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
    recordBarriers(commands, &written, before.data(), beforeCount);

    const std::shared_ptr<gpu::DescriptorTable> &table = tables_[nextTable_];
    nextTable_ = (nextTable_ + 1) % TABLE_RING;
    const auto worldOrMinecraft = [&](const std::shared_ptr<gpu::DeviceLocalImage> &image) {
        return worldValid ? std::shared_ptr<gpu::Image>(image) : std::shared_ptr<gpu::Image>(minecraft);
    };
    table->bindSamplerImage(sampler_, minecraft, VK_IMAGE_LAYOUT_GENERAL, 0, 0, 0);
    table->bindSamplerImage(sampler_, worldOrMinecraft(world_.fused), VK_IMAGE_LAYOUT_GENERAL, 0, 1, 0);
    table->bindSamplerImage(sampler_, worldOrMinecraft(world_.mapped), VK_IMAGE_LAYOUT_GENERAL, 0, 2, 0);
    table->bindSamplerImage(sampler_, worldOrMinecraft(world_.radiance), VK_IMAGE_LAYOUT_GENERAL, 0, 3, 0);
    table->bindBuffer(worldValid ? world_.exposure : noExposure_, 0, 4);
    table->bindImage(composed_, VK_IMAGE_LAYOUT_GENERAL, 0, 5);
    table->bindSamplerImage(sampler_, worldOrMinecraft(world_.output), VK_IMAGE_LAYOUT_GENERAL, 0, 6, 0);

    const ComposeConstants constants{
        .paperWhite = std::clamp(paperWhiteNits, 40.0f, 1000.0f) / 80.0f,
        .peak = std::max(peakNits, paperWhiteNits) / std::max(paperWhiteNits, 1.0f),
        .exposureBias = world_.exposureBias,
        .saturation = world_.saturation,
        .manualExposure = world_.manualExposure,
        .autoExposure = world_.autoExposure ? 1 : 0,
        .worldValid = worldValid ? 1 : 0,
        .debugView = debugView ? 1 : 0,
        .scaled = scaled ? 1 : 0,
    };
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_->vkPipeline());
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, table->vkPipelineLayout(), 0,
                            static_cast<uint32_t>(table->descriptorSet().size()), table->descriptorSet().data(), 0,
                            nullptr);
    vkCmdPushConstants(commands, table->vkPipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants),
                       &constants);
    vkCmdDispatch(commands, (width + 15) / 16, (height + 15) / 16, 1);

    std::array<VkImageMemoryBarrier2, 6> after{};
    size_t afterCount = 0;
    after[afterCount++] =
        layoutChange(composed_->vkImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, COMPUTE,
                     VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    for (size_t i = 0; i < restoreCount; i++) {
        after[afterCount++] = layoutChange(restore[i].image, VK_IMAGE_LAYOUT_GENERAL, restore[i].layout, COMPUTE,
                                           VK_ACCESS_2_SHADER_READ_BIT, ANY_STAGE, ANY_ACCESS);
    }
    recordBarriers(commands, nullptr, after.data(), afterCount);

    // Only what both images cover; flipped, the rows are read bottom up.
    const int32_t blitWidth = static_cast<int32_t>(std::min(width, swapchainWidth));
    const int32_t blitHeight = static_cast<int32_t>(std::min(height, swapchainHeight));
    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[0] = {0, flipY ? blitHeight : 0, 0};
    blit.srcOffsets[1] = {blitWidth, flipY ? 0 : blitHeight, 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstOffsets[1] = {blitWidth, blitHeight, 1};
    vkCmdBlitImage(commands, composed_->vkImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapchainImage,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);

    // Used outside the layout tracking, each left in the layout it was found in (the composed image in TRANSFER_SRC):
    // their next tracked use waits on everything before it.
    composed_->imageLayout() = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    if (worldValid) {
        for (gpu::DeviceLocalImage *image : worldImages) {
            const VkImageLayout layout = image->state().layout;
            image->imageLayout() = layout;
        }
    }
    return true;
}

void HdrOutput::release() {
    std::scoped_lock lock(mutex_);
    if (device_ != nullptr) {
        // Nothing retires the views on their own.
        vkDeviceWaitIdle(device_->vkDevice());
        for (const RetiredView &retired : retiredViews_) vkDestroyImageView(device_->vkDevice(), retired.view, nullptr);
        if (view_ != nullptr && view_->vkImageView(0) != VK_NULL_HANDLE) {
            vkDestroyImageView(device_->vkDevice(), view_->vkImageView(0), nullptr);
        }
    }
    retiredViews_.clear();
    view_ = nullptr;
    viewedImage_ = VK_NULL_HANDLE;
    world_ = WorldFrame{};
    worldFresh_ = false;
    composed_ = nullptr;
    noExposure_ = nullptr;
    sampler_ = nullptr;
    pipeline_ = nullptr;
    tables_.clear();
    nextTable_ = 0;
    vma_ = nullptr;
    device_ = nullptr;
    ready_ = false;
}
