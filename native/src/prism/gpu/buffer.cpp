#include "prism/gpu/buffer.hpp"

#include "prism/gpu/command.hpp"
#include "prism/gpu/device.hpp"
#include "prism/gpu/retire.hpp"
#include "prism/gpu/vma.hpp"
#include "prism/util/logging.hpp"

#include <cstring>
#include <stdexcept>

namespace gpu {
namespace {

constexpr VkBufferUsageFlags kTransfer = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
constexpr VmaAllocationCreateFlags kHostWrite =
    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

} // namespace

BufferMemory BufferMemory::allocate(const std::shared_ptr<VMA> &vma,
                                    const std::shared_ptr<Device> &device,
                                    VkDeviceSize size,
                                    VkBufferUsageFlags usage,
                                    VmaAllocationCreateFlags flags,
                                    VmaMemoryUsage memoryUsage,
                                    VkDeviceSize alignment) {
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size == 0 ? 1 : size;
    info.usage = usage;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.flags = flags;
    allocInfo.usage = memoryUsage;

    BufferMemory memory;
    VmaAllocationInfo result{};
    VkResult status = alignment > 0 ? vmaCreateBufferWithAlignment(vma->allocator(), &info, &allocInfo, alignment,
                                                                   &memory.buffer, &memory.allocation, &result)
                                    : vmaCreateBuffer(vma->allocator(), &info, &allocInfo, &memory.buffer,
                                                      &memory.allocation, &result);
    if (status != VK_SUCCESS) {
        radiante::err() << "[gpu] buffer of " << size << " bytes could not be allocated (" << status << ")"
                        << std::endl;
        throw std::runtime_error("gpu buffer allocation failed");
    }
    memory.mapped = result.pMappedData;
    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
        VkBufferDeviceAddressInfo addressInfo{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
        addressInfo.buffer = memory.buffer;
        memory.address = vkGetBufferDeviceAddress(device->vkDevice(), &addressInfo);
    }
    return memory;
}

void BufferMemory::retire(const std::shared_ptr<VMA> &vma) {
    if (buffer == VK_NULL_HANDLE && allocation == VK_NULL_HANDLE) return;
    retire::later([vma, buffer = buffer, allocation = allocation] {
        vmaDestroyBuffer(vma->allocator(), buffer, allocation);
    });
    *this = BufferMemory{};
}

// ---- HostVisibleBuffer

HostVisibleBuffer::HostVisibleBuffer(std::shared_ptr<VMA> vma,
                                     std::shared_ptr<Device> device,
                                     size_t size,
                                     VkBufferUsageFlags usage)
    : HostVisibleBuffer(std::move(vma), std::move(device), size, usage, 0) {}

HostVisibleBuffer::HostVisibleBuffer(std::shared_ptr<VMA> vma,
                                     std::shared_ptr<Device> device,
                                     size_t size,
                                     VkBufferUsageFlags usage,
                                     VkDeviceSize minAlignment)
    : vma_(std::move(vma)), size_(size), usage_(usage) {
    memory_ = BufferMemory::allocate(vma_, device, size, usage, kHostWrite, VMA_MEMORY_USAGE_AUTO, minAlignment);
}

HostVisibleBuffer::~HostVisibleBuffer() {
    memory_.retire(vma_);
}

void HostVisibleBuffer::downloadFromBuffer() {
    downloadFromBuffer(size_, 0);
}

void HostVisibleBuffer::downloadFromBuffer(size_t size, size_t offset) {
    vmaInvalidateAllocation(vma_->allocator(), memory_.allocation, offset, size);
}

void HostVisibleBuffer::uploadToBuffer(void *src) {
    uploadToBuffer(src, size_, 0);
}

void HostVisibleBuffer::uploadToBuffer(void *src, size_t size, size_t offset) {
    std::memcpy(static_cast<uint8_t *>(memory_.mapped) + offset, src, size);
    vmaFlushAllocation(vma_->allocator(), memory_.allocation, offset, size);
}

void HostVisibleBuffer::flush() {
    vmaFlushAllocation(vma_->allocator(), memory_.allocation, 0, size_);
}

size_t HostVisibleBuffer::size() {
    return size_;
}

VkBuffer &HostVisibleBuffer::vkBuffer() {
    return memory_.buffer;
}

void *HostVisibleBuffer::mappedPtr() {
    return memory_.mapped;
}

VkDeviceAddress &HostVisibleBuffer::bufferAddress() {
    if (!(usage_ & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)) {
        throw std::logic_error("buffer address asked of a buffer made without SHADER_DEVICE_ADDRESS");
    }
    return memory_.address;
}

// ---- DeviceLocalBuffer

DeviceLocalBuffer::DeviceLocalBuffer(std::shared_ptr<VMA> vma,
                                     std::shared_ptr<Device> device,
                                     size_t size,
                                     VkBufferUsageFlags usageExceptTransfer)
    : DeviceLocalBuffer(std::move(vma), std::move(device), true, size, usageExceptTransfer) {}

DeviceLocalBuffer::DeviceLocalBuffer(std::shared_ptr<VMA> vma,
                                     std::shared_ptr<Device> device,
                                     bool persistStaging,
                                     size_t size,
                                     VkBufferUsageFlags usageExceptTransfer)
    : DeviceLocalBuffer(std::move(vma),
                        std::move(device),
                        persistStaging,
                        size,
                        usageExceptTransfer,
                        0,
                        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE) {}

DeviceLocalBuffer::DeviceLocalBuffer(std::shared_ptr<VMA> vma,
                                     std::shared_ptr<Device> device,
                                     bool persistStaging,
                                     size_t size,
                                     VkBufferUsageFlags usageExceptTransfer,
                                     VmaAllocationCreateFlags allocationFlag,
                                     VmaMemoryUsage vmaUsage,
                                     VkDeviceSize minAlignment)
    : vma_(std::move(vma)),
      device_(std::move(device)),
      persistStaging_(persistStaging),
      size_(size),
      usage_(kTransfer | usageExceptTransfer) {
    if (persistStaging_) ensureStaging();
    memory_ = BufferMemory::allocate(vma_, device_, size_, usage_, allocationFlag, vmaUsage, minAlignment);
}

DeviceLocalBuffer::~DeviceLocalBuffer() {
    staging_.retire(vma_);
    memory_.retire(vma_);
}

BufferMemory &DeviceLocalBuffer::ensureStaging() {
    if (!staging_) {
        staging_ = BufferMemory::allocate(vma_, device_, size_, kTransfer, kHostWrite, VMA_MEMORY_USAGE_AUTO);
    }
    return staging_;
}

void DeviceLocalBuffer::downloadFromStagingBuffer(void *dest) {
    downloadFromStagingBuffer(dest, size_, 0);
}

void DeviceLocalBuffer::downloadFromStagingBuffer(void *dest, size_t size, size_t offset) {
    BufferMemory &staging = ensureStaging();
    vmaInvalidateAllocation(vma_->allocator(), staging.allocation, offset, size);
    std::memcpy(dest, static_cast<uint8_t *>(staging.mapped) + offset, size);
    if (!persistStaging_) staging_.retire(vma_);
}

void DeviceLocalBuffer::uploadToStagingBuffer(void *src) {
    uploadToStagingBuffer(src, size_, 0);
}

void DeviceLocalBuffer::uploadToStagingBuffer(void *src, size_t size, size_t offset) {
    BufferMemory &staging = ensureStaging();
    std::memcpy(static_cast<uint8_t *>(staging.mapped) + offset, src, size);
    vmaFlushAllocation(vma_->allocator(), staging.allocation, offset, size);
}

void DeviceLocalBuffer::flushStagingBuffer() {
    if (staging_) vmaFlushAllocation(vma_->allocator(), staging_.allocation, 0, size_);
}

void DeviceLocalBuffer::releaseStaging() {
    staging_.retire(vma_);
}

void DeviceLocalBuffer::downloadFromBuffer(VkCommandBuffer cmdBuffer) {
    downloadFromBuffer(cmdBuffer, size_, 0, 0);
}

void DeviceLocalBuffer::downloadFromBuffer(VkCommandBuffer cmdBuffer,
                                           size_t size,
                                           size_t srcOffset,
                                           size_t dstOffset) {
    VkBufferCopy region{srcOffset, dstOffset, size};
    vkCmdCopyBuffer(cmdBuffer, memory_.buffer, ensureStaging().buffer, 1, &region);
}

void DeviceLocalBuffer::uploadToBuffer(VkCommandBuffer cmdBuffer) {
    uploadToBuffer(cmdBuffer, size_, 0, 0);
}

void DeviceLocalBuffer::uploadToBuffer(VkCommandBuffer cmdBuffer, size_t size, size_t srcOffset, size_t dstOffset) {
    if (!staging_) return;
    VkBufferCopy region{srcOffset, dstOffset, size};
    vkCmdCopyBuffer(cmdBuffer, staging_.buffer, memory_.buffer, 1, &region);
}

void DeviceLocalBuffer::uploadToBuffer(std::shared_ptr<CommandBuffer> cmdBuffer) {
    uploadToBuffer(cmdBuffer->vkCommandBuffer(), size_, 0, 0);
}

void DeviceLocalBuffer::uploadToBuffer(std::shared_ptr<CommandBuffer> cmdBuffer,
                                       size_t size,
                                       size_t srcOffset,
                                       size_t dstOffset) {
    uploadToBuffer(cmdBuffer->vkCommandBuffer(), size, srcOffset, dstOffset);
    // A one-off staging copy is done with once this copy has run: it goes with the frame.
    if (!persistStaging_) staging_.retire(vma_);
}

size_t DeviceLocalBuffer::size() {
    return size_;
}

VkBuffer &DeviceLocalBuffer::vkStagingBuffer() {
    return staging_.buffer;
}

VkBuffer &DeviceLocalBuffer::vkBuffer() {
    return memory_.buffer;
}

void *DeviceLocalBuffer::mappedPtr() {
    return staging_.mapped;
}

VkDeviceAddress &DeviceLocalBuffer::bufferAddress() {
    if (!(usage_ & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)) {
        throw std::logic_error("buffer address asked of a buffer made without SHADER_DEVICE_ADDRESS");
    }
    return memory_.address;
}

} // namespace gpu
