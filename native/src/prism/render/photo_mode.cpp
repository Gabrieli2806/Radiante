#include "prism/render/photo_mode.hpp"

#include "prism/render/buffers.hpp"
#include "prism/render/exr_writer.hpp"
#include "prism/render/pipeline.hpp"
#include "prism/render/render_framework.hpp"
#include "prism/render/renderer.hpp"
#include "prism/util/logging.hpp"

#include <glm/gtc/packing.hpp>

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace {

bool matricesDiffer(const glm::mat4 &a, const glm::mat4 &b, float eps = 1e-5f) {
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            if (std::fabs(a[col][row] - b[col][row]) > eps) return true;
        }
    }
    return false;
}

uint8_t floatToByte(float value) {
    if (!std::isfinite(value)) return 0;
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    return static_cast<uint8_t>(std::lround(clamped * 255.0f));
}

bool writePngAtomic(const std::filesystem::path &path, uint32_t width, uint32_t height, const uint8_t *rgba) {
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    std::filesystem::path tempPath = path;
    tempPath += ".tmp";
    if (stbi_write_png(tempPath.string().c_str(), static_cast<int>(width), static_cast<int>(height), 4, rgba,
                       static_cast<int>(width * 4)) == 0) {
        std::filesystem::remove(tempPath, ec);
        return false;
    }
    std::filesystem::rename(tempPath, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(tempPath, path, ec);
        if (ec) {
            std::filesystem::remove(tempPath, ec);
            return false;
        }
    }
    return true;
}

} // namespace

PhotoMode &PhotoMode::instance() {
    static PhotoMode photo;
    return photo;
}

void PhotoMode::setActive(bool active) {
    const bool previous = active_.exchange(active, std::memory_order_acq_rel);
    if (previous == active) return;

    {
        std::scoped_lock lock(mutex_);
        sampleCount_.store(0, std::memory_order_relaxed);
        resetRequested_.store(true, std::memory_order_relaxed);
        hasPose_ = false;
        convergedSlotFrames_ = 0;
        if (!active) {
            lens_ = glm::vec4(0.0f, 12.0f, 0.0f, 0.0f);
            hasExposureOverride_ = false;
            exposureBias_ = 0.0f;
            captureQueued_ = false;
        }
    }

    if (auto buffers = Renderer::instance().buffers(); buffers != nullptr) {
        buffers->setPhotoJitter(active);
        if (!active) buffers->setCameraLens(glm::vec4(0.0f));
    }
    Renderer::options.needRecreate = true;
}

void PhotoMode::setExposureOverride(bool enabled, float exposureBias) {
    std::scoped_lock lock(mutex_);
    hasExposureOverride_ = enabled;
    exposureBias_ = exposureBias;
}

bool PhotoMode::exposureOverride(float &outBias) const {
    std::scoped_lock lock(mutex_);
    if (!hasExposureOverride_) return false;
    outBias = exposureBias_;
    return true;
}

void PhotoMode::setLens(float apertureRadius, float focusDistance, int blades, float bladeRotation) {
    const glm::vec4 next(std::max(0.0f, apertureRadius), std::max(0.05f, focusDistance),
                         blades >= 3 ? static_cast<float>(blades) : 0.0f, bladeRotation);
    bool changed = false;
    {
        std::scoped_lock lock(mutex_);
        if (glm::length(next - lens_) > 1e-6f) {
            lens_ = next;
            changed = true;
        }
    }
    if (changed) {
        if (auto buffers = Renderer::instance().buffers(); buffers != nullptr) {
            buffers->setCameraLens(active() ? next : glm::vec4(0.0f));
        }
        resetAccumulation();
    }
}

void PhotoMode::setTargetSamples(uint32_t targetSamples) {
    const uint32_t previous = targetSamples_.exchange(targetSamples, std::memory_order_relaxed);
    if (targetSamples > previous) {
        std::scoped_lock lock(mutex_);
        convergedSlotFrames_ = 0;
    }
}

void PhotoMode::resetAccumulation() {
    sampleCount_.store(0, std::memory_order_relaxed);
    resetRequested_.store(true, std::memory_order_relaxed);
    std::scoped_lock lock(mutex_);
    convergedSlotFrames_ = 0;
}

void PhotoMode::requestCapture(std::string pngPath, std::string exrPath, bool waitForTarget) {
    std::scoped_lock lock(mutex_);
    queuedPngPath_ = std::move(pngPath);
    queuedExrPath_ = std::move(exrPath);
    captureWaitForTarget_ = waitForTarget;
    captureQueued_ = !queuedPngPath_.empty() || !queuedExrPath_.empty();
    if (captureQueued_) {
        captureStatus_ = 1;
        captureMessage_.clear();
    }
}

int PhotoMode::pollCaptureStatus(std::string &outMessage) {
    flushPendingCapture(false);
    std::scoped_lock lock(mutex_);
    const int status = captureStatus_;
    outMessage = captureMessage_;
    if (status == 2 || status < 0) {
        captureStatus_ = 0;
        captureMessage_.clear();
    }
    return status;
}

bool PhotoMode::checkPoseChangedAndUpdate() {
    auto buffers = Renderer::instance().buffers();
    if (buffers == nullptr) return false;
    const gpu::Data::WorldUBO ubo = buffers->lastWrittenWorldUniform();
    const glm::dvec3 pos(ubo.cameraPos.x, ubo.cameraPos.y, ubo.cameraPos.z);

    if (!hasPose_) {
        hasPose_ = true;
        lastEffectedView_ = ubo.cameraEffectedViewMat;
        lastProj_ = ubo.cameraProjMat;
        lastCameraPos_ = pos;
        return true;
    }

    const glm::dvec3 delta = pos - lastCameraPos_;
    if (glm::dot(delta, delta) > 1e-10 || matricesDiffer(ubo.cameraEffectedViewMat, lastEffectedView_) ||
        matricesDiffer(ubo.cameraProjMat, lastProj_)) {
        lastEffectedView_ = ubo.cameraEffectedViewMat;
        lastProj_ = ubo.cameraProjMat;
        lastCameraPos_ = pos;
        return true;
    }
    return false;
}

bool PhotoMode::canSkipRayTracing(uint32_t frameSlotCount) {
    std::scoped_lock lock(mutex_);
    if (resetRequested_.exchange(false, std::memory_order_relaxed) || checkPoseChangedAndUpdate()) {
        sampleCount_.store(0, std::memory_order_relaxed);
        convergedSlotFrames_ = 0;
        return false;
    }
    const uint32_t target = targetSamples_.load(std::memory_order_relaxed);
    const uint32_t current = sampleCount_.load(std::memory_order_relaxed);
    if (target == 0 || current < target || accumImage_ == nullptr) return false;
    if (convergedSlotFrames_ < frameSlotCount) {
        convergedSlotFrames_++;
        return false;
    }
    return true;
}

bool PhotoMode::ensureResources(Framework &framework, uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return false;
    auto device = framework.device();
    auto vma = framework.vma();
    if (device == nullptr || vma == nullptr) return false;

    if (device_ != device) {
        accumImage_ = nullptr;
        pipeline_ = nullptr;
        tables_.clear();
        device_ = device;
        vma_ = vma;
    }

    if (pipeline_ == nullptr) {
        tables_.clear();
        tables_.reserve(kTableRing);
        for (size_t i = 0; i < kTableRing; i++) {
            gpu::DescriptorTableBuilder builder;
            auto &bindings = builder.beginDescriptorLayoutSet().beginDescriptorLayoutSetBinding();
            for (uint32_t binding = 0; binding < 3; binding++) {
                bindings.defineDescriptorLayoutSetBinding({
                    .binding = binding,
                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    .descriptorCount = 1,
                    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                });
            }
            tables_.push_back(bindings.endDescriptorLayoutSetBinding()
                                  .endDescriptorLayoutSet()
                                  .definePushConstant({
                                      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                                      .offset = 0,
                                      .size = sizeof(PushConstants),
                                  })
                                  .build(device_));
        }
        auto shader =
            gpu::Shader::create(device_, (Renderer::folderPath / "shaders/world/photo/photo_accumulate_comp.spv").string());
        pipeline_ = gpu::ComputePipelineBuilder{}.defineShader(shader).definePipelineLayout(tables_[0]).build(device_);
    }

    if (accumImage_ == nullptr || accumImage_->width() != width || accumImage_->height() != height) {
        gpu::ImageDesc desc;
        desc.width = width;
        desc.height = height;
        desc.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        desc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        accumImage_ = gpu::DeviceLocalImage::create(device_, vma_, desc);
        sampleCount_.store(0, std::memory_order_relaxed);
        convergedSlotFrames_ = 0;
    }
    return true;
}

void PhotoMode::recordAccumulate(Framework &framework,
                                 gpu::CommandBuffer &commandBuffer,
                                 const std::shared_ptr<gpu::DeviceLocalImage> &rtRadiance,
                                 const std::shared_ptr<gpu::DeviceLocalImage> &outAccumulated,
                                 bool tracedNewSample) {
    if (rtRadiance == nullptr || outAccumulated == nullptr) return;
    std::scoped_lock lock(mutex_);
    const uint32_t width = outAccumulated->width();
    const uint32_t height = outAccumulated->height();
    if (!ensureResources(framework, width, height)) return;

    const uint32_t target = targetSamples_.load(std::memory_order_relaxed);
    uint32_t current = sampleCount_.load(std::memory_order_relaxed);
    uint32_t shaderSampleIndex = 0;
    if (tracedNewSample && (target == 0 || current < target)) {
        current++;
        sampleCount_.store(current, std::memory_order_relaxed);
        shaderSampleIndex = current;
    }

    commandBuffer.sync({
        {rtRadiance, gpu::ImageUsage::StorageRead, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT},
        {accumImage_, gpu::ImageUsage::Storage, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, shaderSampleIndex == 1},
        {outAccumulated, gpu::ImageUsage::StorageWrite, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, true},
    });

    const auto &table = tables_[nextTable_];
    nextTable_ = (nextTable_ + 1) % tables_.size();
    table->bindImage(rtRadiance, VK_IMAGE_LAYOUT_GENERAL, 0, 0);
    table->bindImage(accumImage_, VK_IMAGE_LAYOUT_GENERAL, 0, 1);
    table->bindImage(outAccumulated, VK_IMAGE_LAYOUT_GENERAL, 0, 2);

    const PushConstants pc{
        .sampleIndex = shaderSampleIndex,
        .fireflyClamp = 64.0f,
    };
    VkCommandBuffer cmd = commandBuffer.vkCommandBuffer();
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_->vkPipeline());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, table->vkPipelineLayout(), 0,
                            static_cast<uint32_t>(table->descriptorSet().size()), table->descriptorSet().data(), 0,
                            nullptr);
    vkCmdPushConstants(cmd, table->vkPipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
    vkCmdDispatch(cmd, (width + 15) / 16, (height + 15) / 16, 1);
}

void PhotoMode::recordCaptureIfReady(Framework &framework,
                                     gpu::CommandBuffer &commandBuffer,
                                     const std::shared_ptr<gpu::DeviceLocalImage> &finalOutput) {
    if (finalOutput == nullptr) return;
    std::scoped_lock lock(mutex_);
    if (!captureQueued_ || accumImage_ == nullptr) return;

    const uint32_t target = targetSamples_.load(std::memory_order_relaxed);
    const uint32_t current = sampleCount_.load(std::memory_order_relaxed);
    if (captureWaitForTarget_ && target > 0 && current < target) return;

    const uint32_t width = finalOutput->width();
    const uint32_t height = finalOutput->height();
    if (width == 0 || height == 0 || accumImage_->width() != width || accumImage_->height() != height) return;

    PendingReadback item;
    item.pngPath = std::move(queuedPngPath_);
    item.exrPath = std::move(queuedExrPath_);
    item.width = width;
    item.height = height;
    item.samples = current;
    captureQueued_ = false;

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {width, height, 1};

    if (!item.pngPath.empty()) {
        const size_t pngBytes = static_cast<size_t>(width) * height * 4u * sizeof(uint16_t);
        item.pngBuffer =
            gpu::HostVisibleBuffer::create(framework.vma(), framework.device(), pngBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        commandBuffer.sync({{finalOutput, gpu::ImageUsage::TransferSrc, VK_PIPELINE_STAGE_2_TRANSFER_BIT}});
        vkCmdCopyImageToBuffer(commandBuffer.vkCommandBuffer(), finalOutput->vkImage(),
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, item.pngBuffer->vkBuffer(), 1, &region);
    }

    if (!item.exrPath.empty()) {
        const size_t exrBytes = static_cast<size_t>(width) * height * 4u * sizeof(float);
        item.exrBuffer =
            gpu::HostVisibleBuffer::create(framework.vma(), framework.device(), exrBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        commandBuffer.sync({{accumImage_, gpu::ImageUsage::TransferSrc, VK_PIPELINE_STAGE_2_TRANSFER_BIT}});
        vkCmdCopyImageToBuffer(commandBuffer.vkCommandBuffer(), accumImage_->vkImage(),
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, item.exrBuffer->vkBuffer(), 1, &region);
    }

    commandBuffer.memoryBarrier(VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);

    pendingReadbacks_.push_back(std::move(item));
}

void PhotoMode::noteSubmitted(VkSemaphore timeline, uint64_t value) {
    std::scoped_lock lock(mutex_);
    for (PendingReadback &item : pendingReadbacks_) {
        if (!item.submitted) {
            item.submitted = true;
            item.timeline = timeline;
            item.timelineValue = value;
        }
    }
}

void PhotoMode::flushPendingCapture(bool waitAll) {
    std::vector<PendingReadback> ready;
    std::shared_ptr<gpu::Device> device;
    {
        std::scoped_lock lock(mutex_);
        if (pendingReadbacks_.empty() || device_ == nullptr) return;
        device = device_;
        if (waitAll) {
            ready.swap(pendingReadbacks_);
        } else {
            auto it = pendingReadbacks_.begin();
            while (it != pendingReadbacks_.end()) {
                if (!it->submitted) {
                    ++it;
                    continue;
                }
                bool done = false;
                if (it->timeline != VK_NULL_HANDLE) {
                    uint64_t completed = 0;
                    if (vkGetSemaphoreCounterValue(device->vkDevice(), it->timeline, &completed) == VK_SUCCESS &&
                        completed >= it->timelineValue) {
                        done = true;
                    }
                }
                if (done) {
                    ready.push_back(std::move(*it));
                    it = pendingReadbacks_.erase(it);
                } else {
                    ++it;
                }
            }
            if (ready.empty()) return;
        }
    }

    if (waitAll) {
        vkDeviceWaitIdle(device->vkDevice());
    }

    int finalStatus = 2;
    std::string message;
    for (const PendingReadback &item : ready) {
        const size_t pixelCount = static_cast<size_t>(item.width) * item.height;
        if (!item.pngPath.empty() && item.pngBuffer != nullptr && item.pngBuffer->mappedPtr() != nullptr) {
            const auto *halfPixels = static_cast<const uint16_t *>(item.pngBuffer->mappedPtr());
            std::vector<uint8_t> rgba(pixelCount * 4u, 255);
            // RenderPearl keeps its main render target bottom-up and flips vertically on present; flip here too so
            // saved PNGs and EXRs have row 0 at the top of the frame.
            for (uint32_t y = 0; y < item.height; y++) {
                const uint32_t srcY = item.height - 1u - y;
                for (uint32_t x = 0; x < item.width; x++) {
                    const size_t srcIdx = (static_cast<size_t>(srcY) * item.width + x) * 4u;
                    const size_t dstIdx = (static_cast<size_t>(y) * item.width + x) * 4u;
                    rgba[dstIdx + 0u] = floatToByte(glm::unpackHalf1x16(halfPixels[srcIdx + 0u]));
                    rgba[dstIdx + 1u] = floatToByte(glm::unpackHalf1x16(halfPixels[srcIdx + 1u]));
                    rgba[dstIdx + 2u] = floatToByte(glm::unpackHalf1x16(halfPixels[srcIdx + 2u]));
                    rgba[dstIdx + 3u] = 255;
                }
            }
            const std::filesystem::path pngFile(item.pngPath);
            if (!writePngAtomic(pngFile, item.width, item.height, rgba.data())) {
                finalStatus = -1;
                message = "could not write " + pngFile.filename().string();
            } else {
                message = pngFile.filename().string();
                radiante::out() << "[PhotoMode] saved PNG " << item.pngPath << " (" << item.width << "x" << item.height
                                << ", " << item.samples << " samples)" << std::endl;
            }
        }
        if (finalStatus > 0 && !item.exrPath.empty() && item.exrBuffer != nullptr &&
            item.exrBuffer->mappedPtr() != nullptr) {
            const auto *floatPixels = static_cast<const float *>(item.exrBuffer->mappedPtr());
            const std::filesystem::path exrFile(item.exrPath);
            const std::string err = exr_writer::writeHalfRgbExrFile(exrFile, item.width, item.height, floatPixels, true);
            if (!err.empty()) {
                finalStatus = -1;
                message = err;
            } else {
                if (!message.empty()) message += " + ";
                message += exrFile.filename().string();
                radiante::out() << "[PhotoMode] saved EXR " << item.exrPath << " (" << item.width << "x" << item.height
                                << ", " << item.samples << " samples)" << std::endl;
            }
        }
    }

    std::scoped_lock lock(mutex_);
    captureStatus_ = finalStatus;
    captureMessage_ = std::move(message);
}

void PhotoMode::release() {
    flushPendingCapture(true);
    active_.store(false, std::memory_order_release);
    sampleCount_.store(0, std::memory_order_relaxed);
    std::scoped_lock lock(mutex_);
    lens_ = glm::vec4(0.0f, 12.0f, 0.0f, 0.0f);
    hasPose_ = false;
    convergedSlotFrames_ = 0;
    captureQueued_ = false;
    accumImage_ = nullptr;
    pipeline_ = nullptr;
    tables_.clear();
    vma_ = nullptr;
    device_ = nullptr;
}
