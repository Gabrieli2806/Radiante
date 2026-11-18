#include "prism/render/hdr_output.hpp"

#include "prism/render/modules/world/tone_mapping/tone_mapping_module.hpp"
#include "prism/render/pipeline.hpp"
#include "prism/render/render_framework.hpp"
#include "prism/render/renderer.hpp"
#include "prism/util/logging.hpp"

#include <glm/gtc/packing.hpp>

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace {

std::ostream &hdrErr() {
    return radiante::err() << "[HDR] ";
}

std::ostream &hdrOut() {
    return radiante::out() << "[HDR] ";
}

// The push constants of shader/display/hdr_compose.comp.
struct ComposeConstants {
    float paperWhite; // scRGB units: 1.0 is 80 nits
    float peak;       // the display's peak, in paper whites
    int32_t worldValid;
    int32_t debugView;
    int32_t scaled; // the world was upscaled to Minecraft's image from a lower output resolution
    int32_t calibration;
    float testLevel; // scRGB units
    float padding;   // the rectangles are vec4s, aligned to 16 bytes
    glm::vec4 rectA;
    glm::vec4 rectB;
};
static_assert(sizeof(ComposeConstants) == 64);

glm::vec4 inImage(const glm::vec4 &rect, bool flipY) {
    return flipY ? glm::vec4(rect.x, 1.0f - rect.y - rect.w, rect.z, rect.w) : rect;
}

constexpr VkPipelineStageFlags2 ANY_STAGE = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
constexpr VkAccessFlags2 ANY_ACCESS = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
constexpr VkPipelineStageFlags2 COMPUTE = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

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

uint8_t toPreviewByte(float linearInPaperWhites, float peakRatio) {
    const float v = std::max(linearInPaperWhites, 0.0f);
    // Map [0..1] paper white to [0..0.75] and (1..peak] to (0.75..1.0] so SDR and HDR headroom are both visible in an
    // 8-bit inspection PNG without clipping highlights at paper white.
    float mapped;
    if (v <= 1.0f) {
        mapped = 0.75f * std::pow(v, 1.0f / 2.2f);
    } else {
        const float t = std::clamp((v - 1.0f) / std::max(peakRatio - 1.0f, 1e-3f), 0.0f, 1.0f);
        mapped = 0.75f + 0.25f * std::sqrt(t);
    }
    return static_cast<uint8_t>(std::clamp(std::round(mapped * 255.0f), 0.0f, 255.0f));
}

} // namespace

HdrOutput &HdrOutput::instance() {
    static HdrOutput *output = new HdrOutput();
    return *output;
}

void HdrOutput::setTarget(bool active, float paperWhiteNits, float peakNits) {
    if (!active) {
        targetPeakRatio_.store(1.0f, std::memory_order_relaxed);
        return;
    }
    const float pw = std::clamp(paperWhiteNits, 40.0f, 1000.0f);
    const float peak = std::max(peakNits, pw);
    targetPeakRatio_.store(peak / pw, std::memory_order_relaxed);
}

void HdrOutput::noteWorldFrame(const std::shared_ptr<WorldPipelineContext> &worldContext,
                               const std::shared_ptr<gpu::DeviceLocalImage> &upscaled) {
    std::scoped_lock lock(mutex_);
    flushPendingReadback();
    world_ = WorldFrame{};
    worldFresh_ = false;
    if (worldContext == nullptr || worldContext->outputImage == nullptr) return;

    for (const auto &moduleContext : worldContext->worldModuleContexts) {
        auto toneMapping = std::dynamic_pointer_cast<ToneMappingModuleContext>(moduleContext);
        if (toneMapping == nullptr) continue;

        world_.output = worldContext->outputImage;
        world_.fused = upscaled != nullptr ? upscaled : worldContext->outputImage;
        world_.mapped = toneMapping->ldrImage;
        worldFresh_ = world_.mapped != nullptr && world_.output != nullptr && world_.fused != nullptr;
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
                                      // Minecraft's image, the fused world, the tone mapped world, the post-rendered world
                                      .defineDescriptorLayoutSetBinding(
                                          binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER))
                                      .defineDescriptorLayoutSetBinding(
                                          binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER))
                                      .defineDescriptorLayoutSetBinding(
                                          binding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER))
                                      .defineDescriptorLayoutSetBinding(
                                          binding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER))
                                      // the composed scRGB image
                                      .defineDescriptorLayoutSetBinding(binding(4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE))
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

bool HdrOutput::recordComposeDispatch(VkCommandBuffer commands,
                                      VkImage minecraftImage,
                                      VkImageLayout minecraftLayout,
                                      VkFormat minecraftFormat,
                                      uint32_t width,
                                      uint32_t height,
                                      float paperWhiteNits,
                                      float peakNits,
                                      bool flipY,
                                      bool debugView,
                                      bool consumeWorldFrame) {
    if (commands == VK_NULL_HANDLE || width == 0 || height == 0) return false;
    if (!prepare(width, height)) return false;
    const std::shared_ptr<gpu::ExternalImage> minecraft = minecraftView(minecraftImage, minecraftFormat, width, height);
    if (minecraft == nullptr) return false;

    const uint32_t outputWidth = world_.output != nullptr ? world_.output->width() : 0;
    const uint32_t outputHeight = world_.output != nullptr ? world_.output->height() : 0;
    const bool worldValid = worldFresh_ && sameSize(world_.fused, width, height) &&
                            sameSize(world_.mapped, outputWidth, outputHeight) &&
                            sameSize(world_.output, outputWidth, outputHeight);
    const bool scaled = worldValid && (outputWidth != width || outputHeight != height);
    if (consumeWorldFrame) worldFresh_ = false;
    const std::array<gpu::DeviceLocalImage *, 3> worldImages = {world_.fused.get(), world_.mapped.get(),
                                                                world_.output.get()};

    struct Restore {
        VkImage image;
        VkImageLayout layout;
    };
    std::array<Restore, 4> restore{};
    size_t restoreCount = 0;
    std::array<VkImageMemoryBarrier2, 5> before{};
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
    table->bindSamplerImage(sampler_, worldOrMinecraft(world_.output), VK_IMAGE_LAYOUT_GENERAL, 0, 3, 0);
    table->bindImage(composed_, VK_IMAGE_LAYOUT_GENERAL, 0, 4);

    const float clampedPaperWhite = std::clamp(paperWhiteNits, 40.0f, 1000.0f);
    const ComposeConstants constants{
        .paperWhite = clampedPaperWhite / 80.0f,
        .peak = std::max(peakNits, clampedPaperWhite) / std::max(clampedPaperWhite, 1.0f),
        .worldValid = worldValid ? 1 : 0,
        .debugView = debugView ? 1 : 0,
        .scaled = scaled ? 1 : 0,
        .calibration = calibration_.mode,
        .testLevel = peakNits / 80.0f,
        .padding = 0.0f,
        .rectA = inImage(calibration_.rectA, flipY),
        .rectB = inImage(calibration_.rectB, flipY),
    };
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_->vkPipeline());
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, table->vkPipelineLayout(), 0,
                            static_cast<uint32_t>(table->descriptorSet().size()), table->descriptorSet().data(), 0,
                            nullptr);
    vkCmdPushConstants(commands, table->vkPipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants),
                       &constants);
    vkCmdDispatch(commands, (width + 15) / 16, (height + 15) / 16, 1);

    std::array<VkImageMemoryBarrier2, 5> after{};
    size_t afterCount = 0;
    after[afterCount++] =
        layoutChange(composed_->vkImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, COMPUTE,
                     VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    for (size_t i = 0; i < restoreCount; i++) {
        after[afterCount++] = layoutChange(restore[i].image, VK_IMAGE_LAYOUT_GENERAL, restore[i].layout, COMPUTE,
                                           VK_ACCESS_2_SHADER_READ_BIT, ANY_STAGE, ANY_ACCESS);
    }
    recordBarriers(commands, nullptr, after.data(), afterCount);

    composed_->imageLayout() = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    if (worldValid) {
        for (gpu::DeviceLocalImage *image : worldImages) {
            const VkImageLayout layout = image->state().layout;
            image->imageLayout() = layout;
        }
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
    setTarget(true, paperWhiteNits, peakNits);
    std::scoped_lock lock(mutex_);
    if (!recordComposeDispatch(commands, minecraftImage, minecraftLayout, minecraftFormat, width, height,
                               paperWhiteNits, peakNits, flipY, debugView, /*consumeWorldFrame=*/true)) {
        return false;
    }

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
    return true;
}

void HdrOutput::capturePreview(VkCommandBuffer commands,
                               VkImage minecraftImage,
                               VkImageLayout minecraftLayout,
                               VkFormat minecraftFormat,
                               uint32_t width,
                               uint32_t height,
                               float paperWhiteNits,
                               float peakNits,
                               bool flipY,
                               bool debugView,
                               std::string path) {
    std::scoped_lock lock(mutex_);
    if (!recordComposeDispatch(commands, minecraftImage, minecraftLayout, minecraftFormat, width, height,
                               paperWhiteNits, peakNits, flipY, debugView, /*consumeWorldFrame=*/false)) {
        hdrErr() << "could not record HDR preview capture for " << path << std::endl;
        return;
    }

    const size_t bytes = static_cast<size_t>(width) * height * 8;
    auto readback = gpu::HostVisibleBuffer::create(vma_, device_, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {width, height, 1};
    vkCmdCopyImageToBuffer(commands, composed_->vkImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback->vkBuffer(),
                           1, &region);

    VkMemoryBarrier2 hostVisible{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    hostVisible.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    hostVisible.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    hostVisible.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    hostVisible.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    recordBarriers(commands, &hostVisible, nullptr, 0);

    pendingReadbacks_.push_back({
        .buffer = std::move(readback),
        .width = width,
        .height = height,
        .paperWhiteNits = paperWhiteNits,
        .peakNits = peakNits,
        .flipY = flipY,
        .path = std::move(path),
    });
}

void HdrOutput::flushPendingReadback() {
    if (pendingReadbacks_.empty() || device_ == nullptr) return;
    vkDeviceWaitIdle(device_->vkDevice());

    for (const PendingReadback &item : pendingReadbacks_) {
        if (item.buffer == nullptr || item.buffer->mappedPtr() == nullptr) continue;
        const auto *halfData = static_cast<const uint16_t *>(item.buffer->mappedPtr());
        const size_t pixelCount = static_cast<size_t>(item.width) * item.height;
        const float pw = std::clamp(item.paperWhiteNits, 40.0f, 1000.0f);
        const float pwScRgb = pw / 80.0f;
        const float peakRatio = std::max(item.peakNits, pw) / pw;

        float maxChannelScRgb = 0.0f;
        double sumLumaNits = 0.0;
        size_t hdrPixels = 0;
        std::vector<uint8_t> rgba(pixelCount * 4, 255);

        for (uint32_t y = 0; y < item.height; y++) {
            const uint32_t srcY = item.flipY ? (item.height - 1 - y) : y;
            for (uint32_t x = 0; x < item.width; x++) {
                const size_t srcIdx = (static_cast<size_t>(srcY) * item.width + x) * 4;
                const size_t dstIdx = (static_cast<size_t>(y) * item.width + x) * 4;
                const float r = glm::unpackHalf1x16(halfData[srcIdx + 0]);
                const float g = glm::unpackHalf1x16(halfData[srcIdx + 1]);
                const float b = glm::unpackHalf1x16(halfData[srcIdx + 2]);
                const float cleanR = std::isfinite(r) ? std::max(r, 0.0f) : 0.0f;
                const float cleanG = std::isfinite(g) ? std::max(g, 0.0f) : 0.0f;
                const float cleanB = std::isfinite(b) ? std::max(b, 0.0f) : 0.0f;

                const float peakCh = std::max({cleanR, cleanG, cleanB});
                maxChannelScRgb = std::max(maxChannelScRgb, peakCh);
                if (peakCh > pwScRgb * 1.02f) hdrPixels++;
                const float lumaScRgb = 0.2126f * cleanR + 0.7152f * cleanG + 0.0722f * cleanB;
                sumLumaNits += static_cast<double>(lumaScRgb) * 80.0;

                rgba[dstIdx + 0] = toPreviewByte(cleanR / pwScRgb, peakRatio);
                rgba[dstIdx + 1] = toPreviewByte(cleanG / pwScRgb, peakRatio);
                rgba[dstIdx + 2] = toPreviewByte(cleanB / pwScRgb, peakRatio);
                rgba[dstIdx + 3] = 255;
            }
        }

        stbi_write_png(item.path.c_str(), static_cast<int>(item.width), static_cast<int>(item.height), 4, rgba.data(),
                       static_cast<int>(item.width * 4));
        const double hdrPct = pixelCount > 0 ? (100.0 * static_cast<double>(hdrPixels) / pixelCount) : 0.0;
        const double meanNits = pixelCount > 0 ? (sumLumaNits / pixelCount) : 0.0;
        hdrOut() << "captured preview " << item.path << ": peak=" << std::fixed << std::setprecision(1)
                 << (maxChannelScRgb * 80.0f) << " nits, mean=" << meanNits << " nits, above_paper_white="
                 << std::setprecision(2) << hdrPct << "%" << std::endl;
    }
    pendingReadbacks_.clear();
}

void HdrOutput::setCalibration(int mode, const glm::vec4 &rectA, const glm::vec4 &rectB) {
    std::scoped_lock lock(mutex_);
    calibration_ = {mode, rectA, rectB};
}

void HdrOutput::release() {
    targetPeakRatio_.store(1.0f, std::memory_order_relaxed);
    std::scoped_lock lock(mutex_);
    flushPendingReadback();
    if (device_ != nullptr) {
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
    sampler_ = nullptr;
    pipeline_ = nullptr;
    tables_.clear();
    nextTable_ = 0;
    vma_ = nullptr;
    device_ = nullptr;
    ready_ = false;
}
