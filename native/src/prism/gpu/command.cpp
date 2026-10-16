#include "prism/gpu/command.hpp"

#include "prism/gpu/buffer.hpp"
#include "prism/gpu/descriptor.hpp"
#include "prism/gpu/device.hpp"
#include "prism/gpu/framebuffer.hpp"
#include "prism/gpu/image.hpp"
#include "prism/gpu/physical_device.hpp"
#include "prism/gpu/pipeline.hpp"
#include "prism/gpu/render_pass.hpp"
#include "prism/gpu/retire.hpp"
#include "prism/gpu/sbt.hpp"
#include "prism/gpu/sync.hpp"

#include <stdexcept>

namespace gpu {

// ---- CommandPool

CommandPool::CommandPool(std::shared_ptr<PhysicalDevice> physicalDevice, std::shared_ptr<Device> device)
    : CommandPool(physicalDevice, std::move(device), physicalDevice->mainQueueIndex()) {}

CommandPool::CommandPool(std::shared_ptr<PhysicalDevice>, std::shared_ptr<Device> device, uint32_t queueIndex)
    : device_(std::move(device)) {
    VkCommandPoolCreateInfo info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    info.queueFamilyIndex = queueIndex;
    if (vkCreateCommandPool(device_->vkDevice(), &info, nullptr, &pool_) != VK_SUCCESS) {
        throw std::runtime_error("command pool could not be made");
    }
}

CommandPool::~CommandPool() {
    // Its command buffers may still be executing; they are freed with the pool.
    retire::later([device = device_, pool = pool_] { vkDestroyCommandPool(device->vkDevice(), pool, nullptr); });
}

// ---- image uses

namespace {

constexpr VkPipelineStageFlags2 kShaderStages = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                                                VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR |
                                                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
constexpr VkAccessFlags2 kWrites = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT |
                                   VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                                   VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT |
                                   VK_ACCESS_2_HOST_WRITE_BIT;

struct UsageInfo {
    VkImageLayout layout;
    VkPipelineStageFlags2 stages;
    VkAccessFlags2 access;
};

UsageInfo describe(ImageUsage usage) {
    switch (usage) {
        case ImageUsage::Sampled:
            return {VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, kShaderStages, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT};
        case ImageUsage::StorageRead:
            return {VK_IMAGE_LAYOUT_GENERAL, kShaderStages, VK_ACCESS_2_SHADER_STORAGE_READ_BIT};
        case ImageUsage::StorageWrite:
            return {VK_IMAGE_LAYOUT_GENERAL, kShaderStages, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT};
        case ImageUsage::Storage:
            return {VK_IMAGE_LAYOUT_GENERAL, kShaderStages,
                    VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT};
        case ImageUsage::TransferSrc:
            return {VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_READ_BIT};
        case ImageUsage::TransferDst:
            return {VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT};
        case ImageUsage::ColorAttachment:
            return {VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT};
        case ImageUsage::DepthAttachment:
            return {VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
        case ImageUsage::Present:
            return {VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE};
        case ImageUsage::General:
            return {VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                    VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT};
    }
    throw std::invalid_argument("unknown image usage");
}

} // namespace

// ---- CommandBuffer

CommandBuffer::CommandBuffer(std::shared_ptr<Device> device, std::shared_ptr<CommandPool> pool)
    : device_(std::move(device)), pool_(std::move(pool)) {
    VkCommandBufferAllocateInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    info.commandPool = pool_->vkCommandPool();
    info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    info.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(device_->vkDevice(), &info, &commandBuffer_) != VK_SUCCESS) {
        throw std::runtime_error("command buffer could not be allocated");
    }
}

std::shared_ptr<CommandBuffer> CommandBuffer::begin(VkCommandBufferUsageFlags flags) {
    VkCommandBufferBeginInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    info.flags = flags;
    vkBeginCommandBuffer(commandBuffer_, &info);
    return shared_from_this();
}

std::shared_ptr<CommandBuffer> CommandBuffer::end() {
    vkEndCommandBuffer(commandBuffer_);
    return shared_from_this();
}

std::shared_ptr<CommandBuffer> CommandBuffer::reset(VkCommandBufferResetFlags flags) {
    vkResetCommandBuffer(commandBuffer_, flags);
    return shared_from_this();
}

void CommandBuffer::sync(std::span<const ImageUse> uses) {
    pendingImages_.clear();
    for (const ImageUse &use : uses) {
        if (use.image == nullptr) continue;
        UsageInfo next = describe(use.usage);
        if (use.stages != 0) next.stages = use.stages;
        ImageState &state = use.image->state();

        const bool reading = (next.access & kWrites) == 0;
        const bool wasReading = (state.access & kWrites) == 0;
        if (state.layout == next.layout && reading && wasReading && state.layout != VK_IMAGE_LAYOUT_UNDEFINED) {
            // Readers run side by side; the next writer waits on all of them.
            state.stages |= next.stages;
            state.access |= next.access;
            continue;
        }

        VkImageMemoryBarrier2 &barrier = pendingImages_.emplace_back(VkImageMemoryBarrier2{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2});
        barrier.srcStageMask = state.stages == VK_PIPELINE_STAGE_2_NONE ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT : state.stages;
        barrier.srcAccessMask = state.access & kWrites; // reads need no flushing, only the execution dependency
        barrier.dstStageMask = next.stages == VK_PIPELINE_STAGE_2_NONE ? VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT : next.stages;
        barrier.dstAccessMask = next.access;
        barrier.oldLayout = use.discard ? VK_IMAGE_LAYOUT_UNDEFINED : state.layout;
        barrier.newLayout = next.layout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = use.image->vkImage();
        barrier.subresourceRange = use.image->subresources();

        state = {next.layout, next.stages, next.access};
    }
    if (pendingImages_.empty()) return;

    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = static_cast<uint32_t>(pendingImages_.size());
    dependency.pImageMemoryBarriers = pendingImages_.data();
    vkCmdPipelineBarrier2(commandBuffer_, &dependency);
}

void CommandBuffer::memoryBarrier(VkPipelineStageFlags2 srcStages,
                                  VkAccessFlags2 srcAccess,
                                  VkPipelineStageFlags2 dstStages,
                                  VkAccessFlags2 dstAccess) {
    VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    barrier.srcStageMask = srcStages;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = dstStages;
    barrier.dstAccessMask = dstAccess;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commandBuffer_, &dependency);
}

std::shared_ptr<CommandBuffer> CommandBuffer::barriersBufferImage(const std::vector<BufferMemoryBarrier> &bufferBarriers,
                                                                  const std::vector<ImageMemoryBarrier> &imageBarriers) {
    std::vector<VkBufferMemoryBarrier2> buffers;
    buffers.reserve(bufferBarriers.size());
    for (const auto &b : bufferBarriers) {
        VkBufferMemoryBarrier2 &barrier = buffers.emplace_back(VkBufferMemoryBarrier2{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2});
        barrier.srcStageMask = b.srcStageMask;
        barrier.srcAccessMask = b.srcAccessMask;
        barrier.dstStageMask = b.dstStageMask;
        barrier.dstAccessMask = b.dstAccessMask;
        barrier.srcQueueFamilyIndex = b.srcQueueFamilyIndex;
        barrier.dstQueueFamilyIndex = b.dstQueueFamilyIndex;
        barrier.buffer = b.buffer->vkBuffer();
        barrier.offset = b.offset;
        barrier.size = b.size;
    }

    std::vector<VkImageMemoryBarrier2> images;
    images.reserve(imageBarriers.size());
    for (const auto &b : imageBarriers) {
        VkImageMemoryBarrier2 &barrier = images.emplace_back(VkImageMemoryBarrier2{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2});
        barrier.srcStageMask = b.srcStageMask;
        barrier.srcAccessMask = b.srcAccessMask;
        barrier.dstStageMask = b.dstStageMask;
        barrier.dstAccessMask = b.dstAccessMask;
        barrier.oldLayout = b.oldLayout;
        barrier.newLayout = b.newLayout;
        barrier.srcQueueFamilyIndex = b.srcQueueFamilyIndex;
        barrier.dstQueueFamilyIndex = b.dstQueueFamilyIndex;
        barrier.image = b.image->vkImage();
        barrier.subresourceRange = b.subresourceRange;
        // What the barrier leaves is what the next tracked use has to wait for.
        b.image->state() = {b.newLayout, b.dstStageMask, b.dstAccessMask};
    }

    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.bufferMemoryBarrierCount = static_cast<uint32_t>(buffers.size());
    dependency.pBufferMemoryBarriers = buffers.data();
    dependency.imageMemoryBarrierCount = static_cast<uint32_t>(images.size());
    dependency.pImageMemoryBarriers = images.data();
    dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    vkCmdPipelineBarrier2(commandBuffer_, &dependency);
    return shared_from_this();
}

std::shared_ptr<CommandBuffer> CommandBuffer::barriersMemory(const std::vector<MemoryBarrier> &memoryBarriers) {
    std::vector<VkMemoryBarrier2> barriers;
    barriers.reserve(memoryBarriers.size());
    for (const auto &b : memoryBarriers) {
        VkMemoryBarrier2 &barrier = barriers.emplace_back(VkMemoryBarrier2{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2});
        barrier.srcStageMask = b.srcStageMask;
        barrier.srcAccessMask = b.srcAccessMask;
        barrier.dstStageMask = b.dstStageMask;
        barrier.dstAccessMask = b.dstAccessMask;
    }
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.memoryBarrierCount = static_cast<uint32_t>(barriers.size());
    dependency.pMemoryBarriers = barriers.data();
    vkCmdPipelineBarrier2(commandBuffer_, &dependency);
    return shared_from_this();
}

std::shared_ptr<CommandBuffer> CommandBuffer::copyToDeviceLocalBuffer(std::shared_ptr<DeviceLocalBuffer> buffer) {
    buffer->uploadToBuffer(commandBuffer_);
    return shared_from_this();
}

std::shared_ptr<CommandBuffer> CommandBuffer::copyToDeviceLocalImage(std::shared_ptr<DeviceLocalImage> image) {
    image->uploadToImage(commandBuffer_);
    return shared_from_this();
}

std::shared_ptr<CommandBuffer> CommandBuffer::beginRenderPass(const RenderPassBeginInfo &info) {
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = info.renderPass->vkRenderPass();
    begin.framebuffer = info.framebuffer->vkFramebuffer();
    begin.renderArea.offset = info.renderAreaOffset;
    begin.renderArea.extent = info.renderAreaExtent;
    begin.clearValueCount = static_cast<uint32_t>(info.clearValues.size());
    begin.pClearValues = info.clearValues.data();
    vkCmdBeginRenderPass(commandBuffer_, &begin, VK_SUBPASS_CONTENTS_INLINE);
    return shared_from_this();
}

std::shared_ptr<CommandBuffer> CommandBuffer::endRenderPass() {
    vkCmdEndRenderPass(commandBuffer_);
    return shared_from_this();
}

std::shared_ptr<CommandBuffer> CommandBuffer::bindDescriptorTable(std::shared_ptr<DescriptorTable> descriptorTable,
                                                                  VkPipelineBindPoint bindPoint) {
    // Dynamic offsets are bound as zero; tables that use them rebind with real offsets.
    const uint32_t dynamicCount = descriptorTable->dynamicDescriptorCount();
    std::vector<uint32_t> dynamicOffsets(dynamicCount, 0);
    vkCmdBindDescriptorSets(commandBuffer_, bindPoint, descriptorTable->vkPipelineLayout(), 0,
                            static_cast<uint32_t>(descriptorTable->descriptorSet().size()),
                            descriptorTable->descriptorSet().data(), dynamicCount,
                            dynamicOffsets.empty() ? nullptr : dynamicOffsets.data());
    return shared_from_this();
}

std::shared_ptr<CommandBuffer> CommandBuffer::bindGraphicsPipeline(std::shared_ptr<GraphicsPipeline> pipeline) {
    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->vkPipeline());
    return shared_from_this();
}

std::shared_ptr<CommandBuffer> CommandBuffer::bindRTPipeline(std::shared_ptr<RayTracingPipeline> pipeline) {
    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline->vkPipeline());
    return shared_from_this();
}

std::shared_ptr<CommandBuffer> CommandBuffer::bindComputePipeline(std::shared_ptr<ComputePipeline> pipeline) {
    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->vkPipeline());
    return shared_from_this();
}

std::shared_ptr<CommandBuffer> CommandBuffer::bindVertexBuffers(std::shared_ptr<DeviceLocalBuffer> buffer) {
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commandBuffer_, 0, 1, &buffer->vkBuffer(), &offset);
    return shared_from_this();
}

std::shared_ptr<CommandBuffer> CommandBuffer::bindIndexBuffer(std::shared_ptr<DeviceLocalBuffer> buffer,
                                                              VkIndexType indexType) {
    vkCmdBindIndexBuffer(commandBuffer_, buffer->vkBuffer(), 0, indexType);
    return shared_from_this();
}

std::shared_ptr<CommandBuffer>
CommandBuffer::draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
    vkCmdDraw(commandBuffer_, vertexCount, instanceCount, firstVertex, firstInstance);
    return shared_from_this();
}

std::shared_ptr<CommandBuffer> CommandBuffer::drawIndexed(
    uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
    vkCmdDrawIndexed(commandBuffer_, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
    return shared_from_this();
}

std::shared_ptr<CommandBuffer>
CommandBuffer::raytracing(std::shared_ptr<SBT> sbt, uint32_t width, uint32_t height, uint32_t depth) {
    vkCmdTraceRaysKHR(commandBuffer_, &sbt->raygenRegion(), &sbt->missRegion(), &sbt->hitRegion(),
                      &sbt->callableRegion(), width, height, depth);
    return shared_from_this();
}

void CommandBuffer::submitMainQueueIndividual(std::shared_ptr<Device> device, std::shared_ptr<Fence> fence) {
    VkSubmitInfo info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    info.commandBufferCount = 1;
    info.pCommandBuffers = &commandBuffer_;
    vkQueueSubmit(device->mainVkQueue(), 1, &info, fence == nullptr ? VK_NULL_HANDLE : fence->vkFence());
}

void CommandBuffer::submitMainQueue(std::shared_ptr<Device> device, const SubmitInfo &submitInfo) {
    std::vector<VkSemaphore> waitSemaphores;
    std::vector<VkPipelineStageFlags> waitStages;
    for (const auto &[semaphore, stages] : submitInfo.waitSemaphoresAndStageMasks) {
        waitSemaphores.push_back(semaphore);
        waitStages.push_back(stages);
    }
    VkSubmitInfo info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    info.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
    info.pWaitSemaphores = waitSemaphores.data();
    info.pWaitDstStageMask = waitStages.data();
    info.commandBufferCount = 1;
    info.pCommandBuffers = &commandBuffer_;
    info.signalSemaphoreCount = static_cast<uint32_t>(submitInfo.signalSemaphores.size());
    info.pSignalSemaphores = submitInfo.signalSemaphores.data();
    vkQueueSubmit(device->mainVkQueue(), 1, &info, submitInfo.signalFence);
}

} // namespace gpu
