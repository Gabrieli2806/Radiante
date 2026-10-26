#pragma once

#include "prism/all_extern.hpp"

#include <initializer_list>
#include <span>
#include <utility>
#include <vector>

namespace gpu {
class PhysicalDevice;
class Device;
class DeviceLocalBuffer;
class DeviceLocalImage;
class DescriptorTable;
class GraphicsPipeline;
class ComputePipeline;
class RayTracingPipeline;
class RenderPass;
class Framebuffer;
class Buffer;
class Image;
class SBT;
class Fence;

class CommandPool : public SharedObject<CommandPool> {
  public:
    CommandPool(std::shared_ptr<PhysicalDevice> physicalDevice, std::shared_ptr<Device> device);
    CommandPool(std::shared_ptr<PhysicalDevice> physicalDevice, std::shared_ptr<Device> device, uint32_t queueIndex);
    ~CommandPool();

    VkCommandPool &vkCommandPool() {
        return pool_;
    }

  private:
    std::shared_ptr<Device> device_;
    VkCommandPool pool_ = VK_NULL_HANDLE;
};

// What a command does with an image. Each use has the one layout it needs and the accesses it makes.
enum class ImageUsage {
    Sampled,      // read through a sampler or texelFetch
    StorageRead,  // imageLoad
    StorageWrite, // imageStore
    Storage,      // imageLoad and imageStore
    TransferSrc,
    TransferDst,
    ColorAttachment,
    DepthAttachment,
    Present,
    General, // handed to code that is not ours (upscalers, denoisers): anything, anywhere
};

// One image's next use, for CommandBuffer::sync. Stages default to every shader stage that can make the use.
struct ImageUse {
    Image *image;
    ImageUsage usage;
    VkPipelineStageFlags2 stages = 0;
    // The contents are about to be overwritten in full: the transition may drop them.
    bool discard = false;

    template <typename T>
    ImageUse(const std::shared_ptr<T> &image, ImageUsage usage, VkPipelineStageFlags2 stages = 0, bool discard = false)
        : image(image.get()), usage(usage), stages(stages), discard(discard) {}
    ImageUse(Image *image, ImageUsage usage, VkPipelineStageFlags2 stages = 0, bool discard = false)
        : image(image), usage(usage), stages(stages), discard(discard) {}
};

class CommandBuffer : public SharedObject<CommandBuffer> {
  public:
    struct BufferMemoryBarrier {
        VkPipelineStageFlags2 srcStageMask;
        VkAccessFlags2 srcAccessMask;
        VkPipelineStageFlags2 dstStageMask;
        VkAccessFlags2 dstAccessMask;
        uint32_t srcQueueFamilyIndex;
        uint32_t dstQueueFamilyIndex;
        std::shared_ptr<Buffer> buffer;
        VkDeviceSize offset = 0;
        VkDeviceSize size = VK_WHOLE_SIZE;
    };

    // A hand-written image transition. It updates the image's tracked state to what it leaves behind.
    struct ImageMemoryBarrier {
        VkPipelineStageFlags2 srcStageMask;
        VkAccessFlags2 srcAccessMask;
        VkPipelineStageFlags2 dstStageMask;
        VkAccessFlags2 dstAccessMask;
        VkImageLayout oldLayout;
        VkImageLayout newLayout;
        uint32_t srcQueueFamilyIndex;
        uint32_t dstQueueFamilyIndex;
        std::shared_ptr<Image> image;
        VkImageSubresourceRange subresourceRange;
    };

    struct RenderPassBeginInfo {
        std::shared_ptr<RenderPass> renderPass;
        std::shared_ptr<Framebuffer> framebuffer;
        VkOffset2D renderAreaOffset = {0, 0};
        VkExtent2D renderAreaExtent;
        std::vector<VkClearValue> clearValues;
    };

    CommandBuffer(std::shared_ptr<Device> device, std::shared_ptr<CommandPool> pool);

    VkCommandBuffer &vkCommandBuffer() {
        return commandBuffer_;
    }

    std::shared_ptr<CommandBuffer> begin(VkCommandBufferUsageFlags flags = 0);
    std::shared_ptr<CommandBuffer> end();
    std::shared_ptr<CommandBuffer> reset(VkCommandBufferResetFlags flags = 0);

    // Brings every image into its next use with one barrier, waiting only on what each image was last used for.
    // Reads after reads in the same layout need no barrier and get none.
    void sync(std::span<const ImageUse> uses);
    void sync(std::initializer_list<ImageUse> uses) {
        sync(std::span<const ImageUse>(uses.begin(), uses.size()));
    }
    // An execution and memory dependency over everything (buffers, structures).
    void memoryBarrier(VkPipelineStageFlags2 srcStages,
                       VkAccessFlags2 srcAccess,
                       VkPipelineStageFlags2 dstStages,
                       VkAccessFlags2 dstAccess);

    std::shared_ptr<CommandBuffer> barriersBufferImage(const std::vector<BufferMemoryBarrier> &bufferBarriers,
                                                       const std::vector<ImageMemoryBarrier> &imageBarriers);

    std::shared_ptr<CommandBuffer> beginRenderPass(const RenderPassBeginInfo &info);
    std::shared_ptr<CommandBuffer> endRenderPass();
    std::shared_ptr<CommandBuffer> bindDescriptorTable(std::shared_ptr<DescriptorTable> descriptorTable,
                                                       VkPipelineBindPoint bindPoint);
    std::shared_ptr<CommandBuffer> bindGraphicsPipeline(std::shared_ptr<GraphicsPipeline> pipeline);
    std::shared_ptr<CommandBuffer> bindRTPipeline(std::shared_ptr<RayTracingPipeline> pipeline);
    std::shared_ptr<CommandBuffer> bindComputePipeline(std::shared_ptr<ComputePipeline> pipeline);
    std::shared_ptr<CommandBuffer> bindVertexBuffers(std::shared_ptr<DeviceLocalBuffer> buffer);
    std::shared_ptr<CommandBuffer> bindIndexBuffer(std::shared_ptr<DeviceLocalBuffer> buffer,
                                                   VkIndexType indexType = VK_INDEX_TYPE_UINT32);
    std::shared_ptr<CommandBuffer>
    draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex = 0, uint32_t firstInstance = 0);
    std::shared_ptr<CommandBuffer> drawIndexed(uint32_t indexCount,
                                               uint32_t instanceCount,
                                               uint32_t firstIndex = 0,
                                               int32_t vertexOffset = 0,
                                               uint32_t firstInstance = 0);
    std::shared_ptr<CommandBuffer> raytracing(std::shared_ptr<SBT> sbt, uint32_t width, uint32_t height, uint32_t depth);

    void submitMainQueueIndividual(std::shared_ptr<Device> device, std::shared_ptr<Fence> fence = nullptr);

  private:
    std::shared_ptr<Device> device_;
    std::shared_ptr<CommandPool> pool_;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
    std::vector<VkImageMemoryBarrier2> pendingImages_;
};
} // namespace gpu
