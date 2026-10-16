#pragma once

#include "prism/all_extern.hpp"

namespace gpu {
class VMA;
class Device;
class CommandBuffer;

// A VkBuffer with its memory. Destruction is deferred until no frame in flight can use it (gpu::retire).
struct BufferMemory {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    void *mapped = nullptr;
    VkDeviceAddress address = 0;

    static BufferMemory allocate(const std::shared_ptr<VMA> &vma,
                                 const std::shared_ptr<Device> &device,
                                 VkDeviceSize size,
                                 VkBufferUsageFlags usage,
                                 VmaAllocationCreateFlags flags,
                                 VmaMemoryUsage memoryUsage,
                                 VkDeviceSize alignment = 0);
    // Hands the memory to gpu::retire and empties this one.
    void retire(const std::shared_ptr<VMA> &vma);
    explicit operator bool() const {
        return buffer != VK_NULL_HANDLE;
    }
};

class Buffer {
  public:
    virtual ~Buffer() = default;
    virtual size_t size() = 0;
    virtual VkBuffer &vkBuffer() = 0;
};

// Memory the CPU writes and the GPU reads directly (uniforms, small per-frame data, readbacks).
class HostVisibleBuffer : public Buffer, public SharedObject<HostVisibleBuffer> {
  public:
    HostVisibleBuffer(std::shared_ptr<VMA> vma, std::shared_ptr<Device> device, size_t size, VkBufferUsageFlags usage);
    HostVisibleBuffer(std::shared_ptr<VMA> vma,
                      std::shared_ptr<Device> device,
                      size_t size,
                      VkBufferUsageFlags usage,
                      VkDeviceSize minAlignment);
    ~HostVisibleBuffer() override;

    // Makes what the GPU wrote visible to the CPU.
    void downloadFromBuffer();
    void downloadFromBuffer(size_t size, size_t offset);
    // Writes and flushes.
    void uploadToBuffer(void *src);
    void uploadToBuffer(void *src, size_t size, size_t offset);
    void flush();

    size_t size() override;
    VkBuffer &vkBuffer() override;
    void *mappedPtr();
    VkDeviceAddress &bufferAddress();

  private:
    std::shared_ptr<VMA> vma_;
    size_t size_;
    VkBufferUsageFlags usage_;
    BufferMemory memory_;
};

// Memory only the GPU touches, filled through a staging copy (kept, or made for each upload and retired after it).
class DeviceLocalBuffer : public Buffer, public SharedObject<DeviceLocalBuffer> {
  public:
    DeviceLocalBuffer(std::shared_ptr<VMA> vma,
                      std::shared_ptr<Device> device,
                      size_t size,
                      VkBufferUsageFlags usageExceptTransfer);
    DeviceLocalBuffer(std::shared_ptr<VMA> vma,
                      std::shared_ptr<Device> device,
                      bool persistStaging,
                      size_t size,
                      VkBufferUsageFlags usageExceptTransfer);
    DeviceLocalBuffer(std::shared_ptr<VMA> vma,
                      std::shared_ptr<Device> device,
                      bool persistStaging,
                      size_t size,
                      VkBufferUsageFlags usageExceptTransfer,
                      VmaAllocationCreateFlags allocationFlag,
                      VmaMemoryUsage vmaUsage,
                      VkDeviceSize minAlignment = 0);
    ~DeviceLocalBuffer() override;

    void downloadFromStagingBuffer(void *dest);
    void downloadFromStagingBuffer(void *dest, size_t size, size_t offset);

    void uploadToStagingBuffer(void *src);
    void uploadToStagingBuffer(void *src, size_t size, size_t offset);
    void flushStagingBuffer();
    void releaseStaging();

    void downloadFromBuffer(VkCommandBuffer cmdBuffer);
    void downloadFromBuffer(VkCommandBuffer cmdBuffer, size_t size, size_t srcOffset, size_t dstOffset);

    void uploadToBuffer(VkCommandBuffer cmdBuffer);
    void uploadToBuffer(VkCommandBuffer cmdBuffer, size_t size, size_t srcOffset, size_t dstOffset);
    void uploadToBuffer(std::shared_ptr<CommandBuffer> cmdBuffer);
    void uploadToBuffer(std::shared_ptr<CommandBuffer> cmdBuffer, size_t size, size_t srcOffset, size_t dstOffset);

    size_t size() override;
    VkBuffer &vkStagingBuffer();
    VkBuffer &vkBuffer() override;
    void *mappedPtr();
    VkDeviceAddress &bufferAddress();

  private:
    BufferMemory &ensureStaging();

    std::shared_ptr<VMA> vma_;
    std::shared_ptr<Device> device_;
    bool persistStaging_;
    size_t size_;
    VkBufferUsageFlags usage_;
    BufferMemory staging_;
    BufferMemory memory_;
};
}; // namespace gpu
