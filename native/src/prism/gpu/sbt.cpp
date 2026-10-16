#include "prism/gpu/sbt.hpp"

#include "prism/gpu/buffer.hpp"
#include "prism/gpu/command.hpp"
#include "prism/gpu/device.hpp"
#include "prism/gpu/physical_device.hpp"
#include "prism/gpu/pipeline.hpp"
#include "prism/gpu/vma.hpp"

#include <cstring>
#include <stdexcept>

namespace gpu {
namespace {

uint32_t alignUp(uint32_t value, uint32_t alignment) {
    return alignment == 0 ? value : (value + alignment - 1) / alignment * alignment;
}

constexpr VkBufferUsageFlags kTableUsage =
    VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

} // namespace

SBT::SBT(std::shared_ptr<PhysicalDevice> physicalDevice,
         std::shared_ptr<Device> device,
         std::shared_ptr<VMA> vma,
         std::shared_ptr<RayTracingPipeline> pipeline,
         uint32_t missCount,
         uint32_t hitCount)
    : vma_(std::move(vma)), device_(std::move(device)), missCount_(missCount), hitGroupCount_(hitCount) {
    const auto &properties = physicalDevice->rayTracingProperties();
    handleSize_ = properties.shaderGroupHandleSize;
    stride_ = alignUp(handleSize_, properties.shaderGroupHandleAlignment);
    baseAlignment_ = properties.shaderGroupBaseAlignment;

    const uint32_t groupCount = 1 + missCount + hitCount;
    handles_.resize(static_cast<size_t>(groupCount) * handleSize_);
    if (vkGetRayTracingShaderGroupHandlesKHR(device_->vkDevice(), pipeline->vkPipeline(), 0, groupCount,
                                             handles_.size(), handles_.data()) != VK_SUCCESS) {
        throw std::runtime_error("shader group handles could not be read");
    }

    // Raygen at 0, the misses from the next base alignment.
    const uint32_t missOffset = alignUp(stride_, baseAlignment_);
    const uint32_t fixedSize = missOffset + stride_ * std::max(missCount, 1u);
    std::vector<uint8_t> fixed(fixedSize, 0);
    std::memcpy(fixed.data(), handles_.data(), handleSize_);
    for (uint32_t i = 0; i < missCount; i++) {
        std::memcpy(fixed.data() + missOffset + i * stride_, handles_.data() + (1 + i) * handleSize_, handleSize_);
    }
    fixed_ = DeviceLocalBuffer::create(vma_, device_, false, fixedSize, kTableUsage, 0,
                                       VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, baseAlignment_);
    fixed_->uploadToStagingBuffer(fixed.data());

    const VkDeviceAddress address = fixed_->bufferAddress();
    raygenRegion_ = {address, stride_, stride_};
    missRegion_ = {address + missOffset, stride_, static_cast<VkDeviceSize>(stride_) * missCount};
}

void SBT::uploadStaticSBT(std::shared_ptr<CommandBuffer> commandBuffer) {
    fixed_->uploadToBuffer(commandBuffer);
    commandBuffer->memoryBarrier(VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                 VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                                 VK_ACCESS_2_SHADER_BINDING_TABLE_READ_BIT_KHR);
}

void SBT::setupHitSBT(const std::vector<uint32_t> &hitGroupIndices, std::shared_ptr<CommandBuffer>) {
    if (hitGroupIndices.empty()) throw std::invalid_argument("a hit table needs at least one record");
    const size_t size = hitGroupIndices.size() * stride_;

    // Half again as many records of room, so a growing scene does not reallocate every frame.
    if (hits_ == nullptr || hits_->size() < size) {
        hits_ = HostVisibleBuffer::create(vma_, device_, size + size / 2, kTableUsage, baseAlignment_);
        writtenHits_.clear();
    }
    if (hitGroupIndices != writtenHits_) {
        auto *records = static_cast<uint8_t *>(hits_->mappedPtr());
        for (size_t i = 0; i < hitGroupIndices.size(); i++) {
            const uint32_t group = hitGroupCount_ > 0 ? std::min(hitGroupIndices[i], hitGroupCount_ - 1) : 0;
            std::memcpy(records + i * stride_, handles_.data() + (1 + missCount_ + group) * handleSize_, handleSize_);
        }
        hits_->flush();
        writtenHits_ = hitGroupIndices;
    }
    hitRegion_ = {hits_->bufferAddress(), stride_, size};
}

} // namespace gpu
