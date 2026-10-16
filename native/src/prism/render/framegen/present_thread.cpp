#include "prism/render/framegen/present_thread.hpp"

#include "prism/gpu/device.hpp"
#include "prism/gpu/physical_device.hpp"
#include "prism/gpu/queue_lock.hpp"
#include "prism/render/framegen/generator.hpp"
#include "prism/render/render_framework.hpp"

#include <algorithm>
#include <ostream>
#include <utility>

namespace framegen {
namespace {

// Presents that may be in flight at once; each has its own command buffer, fence and acquire semaphore.
constexpr uint32_t kSlots = 8;
constexpr uint64_t kAcquireTimeoutNs = 5'000'000'000ull;
// Sleeping overshoots by up to a scheduler tick, so the last stretch before a present is spun.
constexpr auto kSpin = std::chrono::microseconds(1500);
// A failing present is reported this many times, not every frame.
constexpr uint32_t kFailuresReported = 5;

// Leaves the handle null when making it failed, so cleaning up never meets an undefined one.
template <typename Handle>
bool made(VkResult result, Handle &handle) {
    if (result == VK_SUCCESS) return true;
    handle = VK_NULL_HANDLE;
    return false;
}

void sleepUntil(std::chrono::steady_clock::time_point target) {
    const auto coarse = target - kSpin;
    if (std::chrono::steady_clock::now() < coarse) std::this_thread::sleep_until(coarse);
    while (std::chrono::steady_clock::now() < target) std::this_thread::yield();
}

VkImageMemoryBarrier2 swapchainBarrier(VkImage image,
                                       VkImageLayout from,
                                       VkImageLayout to,
                                       VkPipelineStageFlags2 srcStage,
                                       VkAccessFlags2 srcAccess,
                                       VkPipelineStageFlags2 dstStage,
                                       VkAccessFlags2 dstAccess) {
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = dstStage;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    return barrier;
}

} // namespace

VkImageBlit swapchainBlit(
    uint32_t width, uint32_t height, uint32_t swapchainWidth, uint32_t swapchainHeight, bool flipY) {
    const auto copyWidth = static_cast<int32_t>(std::min(width, swapchainWidth));
    const auto copyHeight = static_cast<int32_t>(std::min(height, swapchainHeight));
    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[0] = {0, 0, 0};
    blit.srcOffsets[1] = {copyWidth, copyHeight, 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstOffsets[0] = {0, flipY ? copyHeight : 0, 0};
    blit.dstOffsets[1] = {copyWidth, flipY ? 0 : copyHeight, 1};
    return blit;
}

PresentThread::~PresentThread() {
    stop();
}

void PresentThread::waitIdle() {
    std::unique_lock lock(mutex_);
    idle_.wait(lock, [this] { return !pending_; });
}

PresentThread::Outcome PresentThread::takeOutcome() {
    std::lock_guard lock(mutex_);
    return std::exchange(outcome_, Outcome{});
}

bool PresentThread::prepare(Framework &framework, VkSwapchainKHR swapchain, size_t imageCount) {
    if (pool_ == VK_NULL_HANDLE &&
        !makeSlots(framework.device()->vkDevice(), framework.physicalDevice()->mainQueueIndex())) {
        return false;
    }
    if (swapchain_ == swapchain && rendered_.size() == imageCount) return true;

    if (!rendered_.empty()) {
        // Presents of the old swapchain may still wait on them.
        framework.waitRenderQueueIdle();
        destroySwapchainSemaphores();
    }
    rendered_.assign(imageCount, VK_NULL_HANDLE);
    const VkSemaphoreCreateInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (VkSemaphore &semaphore : rendered_) {
        if (!made(vkCreateSemaphore(device_, &info, nullptr, &semaphore), semaphore)) {
            destroySwapchainSemaphores();
            return false;
        }
    }
    swapchain_ = swapchain;
    return true;
}

bool PresentThread::makeSlots(VkDevice device, uint32_t queueFamily) {
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamily;
    if (!made(vkCreateCommandPool(device, &poolInfo, nullptr, &pool_), pool_)) return false;
    device_ = device;
    slots_.resize(kSlots);
    for (Slot &slot : slots_) {
        VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocation.commandPool = pool_;
        allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocation.commandBufferCount = 1;
        // Signalled, so a slot's first use does not wait.
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        const VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        if (!made(vkAllocateCommandBuffers(device, &allocation, &slot.commands), slot.commands) ||
            !made(vkCreateFence(device, &fenceInfo, nullptr, &slot.done), slot.done) ||
            !made(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &slot.acquired), slot.acquired)) {
            destroy();
            return false;
        }
    }
    return true;
}

void PresentThread::post(Job &job) {
    if (!thread_.joinable()) {
        stopping_ = false;
        thread_ = std::thread([this] { run(); });
    }
    {
        std::lock_guard lock(mutex_);
        std::swap(job_, job);
        pending_ = true;
    }
    wake_.notify_one();
}

void PresentThread::stop() {
    if (!thread_.joinable()) return;
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_one();
    thread_.join();
    {
        std::lock_guard lock(mutex_);
        pending_ = false;
        stopping_ = false;
    }
    idle_.notify_all();
}

void PresentThread::destroy() {
    if (device_ != VK_NULL_HANDLE) {
        for (const Slot &slot : slots_) {
            if (slot.done != VK_NULL_HANDLE) vkDestroyFence(device_, slot.done, nullptr);
            if (slot.acquired != VK_NULL_HANDLE) vkDestroySemaphore(device_, slot.acquired, nullptr);
        }
        destroySwapchainSemaphores();
        // Frees the command buffers with it.
        if (pool_ != VK_NULL_HANDLE) vkDestroyCommandPool(device_, pool_, nullptr);
    }
    forget();
}

void PresentThread::forget() {
    device_ = VK_NULL_HANDLE;
    pool_ = VK_NULL_HANDLE;
    slots_.clear();
    nextSlot_ = 0;
    swapchain_ = VK_NULL_HANDLE;
    rendered_.clear();
}

void PresentThread::destroySwapchainSemaphores() {
    for (VkSemaphore semaphore : rendered_) {
        if (semaphore != VK_NULL_HANDLE) vkDestroySemaphore(device_, semaphore, nullptr);
    }
    rendered_.clear();
    swapchain_ = VK_NULL_HANDLE;
}

void PresentThread::run() {
    // Swapped with the posted job, so the two jobs' buffers are reused back and forth.
    Job job;
    for (;;) {
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] { return pending_ || stopping_; });
            if (stopping_) return;
            std::swap(job, job_);
        }
        const Outcome shown = show(job);
        {
            std::lock_guard lock(mutex_);
            outcome_.status = shown.status;
            outcome_.presented += shown.presented;
            pending_ = false;
        }
        idle_.notify_all();
    }
}

PresentThread::Outcome PresentThread::show(const Job &job) {
    Outcome shown;
    for (size_t k = 0; k < job.images.size(); k++) {
        // Spread over the time one rendered frame takes, the generated frames sit between the real ones instead of
        // reaching the display all at once.
        const auto due = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            job.spacing * static_cast<double>(k + 1));
        sleepUntil(job.start + due);

        Slot &slot = slots_[nextSlot_];
        nextSlot_ = (nextSlot_ + 1) % kSlots;
        // The slot's command buffer and acquire semaphore are free again once its last blit has run.
        vkWaitForFences(device_, 1, &slot.done, VK_TRUE, UINT64_MAX);
        int result;
        {
            // The queue lock also guards the swapchain: Minecraft acquires under it (VulkanQueueLockMixins).
            std::lock_guard<std::recursive_mutex> queueLock(gpu::queueMutex());
            result = present(job, slot, job.images[k]);
        }
        if (result < 0) {
            if (failuresReported_ < kFailuresReported) {
                failuresReported_++;
                out() << "a generated frame could not be presented; the swapchain is rebuilt" << std::endl;
            }
            shown.status = -1;
            break;
        }
        shown.status = std::max(shown.status, result);
        shown.presented++;
    }
    return shown;
}

int PresentThread::present(const Job &job, Slot &slot, VkImage source) {
    uint32_t imageIndex = 0;
    const VkResult acquired =
        vkAcquireNextImageKHR(device_, job.swapchain, kAcquireTimeoutNs, slot.acquired, VK_NULL_HANDLE, &imageIndex);
    // Without an image (out of date, lost, or timed out and the semaphore never signalled) the job ends here.
    if ((acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) || imageIndex >= job.swapchainImages.size()) {
        return -1;
    }
    vkResetFences(device_, 1, &slot.done);

    const VkImage target = job.swapchainImages[imageIndex];
    vkResetCommandBuffer(slot.commands, 0);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(slot.commands, &begin);
    {
        // The frame was written by Minecraft's present command buffer, submitted earlier on this queue.
        VkMemoryBarrier2 everything{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        everything.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        everything.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        everything.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        everything.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        const VkImageMemoryBarrier2 toTransfer =
            swapchainBarrier(target, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             VK_PIPELINE_STAGE_2_TRANSFER_BIT, 0, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                             VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.memoryBarrierCount = 1;
        dependency.pMemoryBarriers = &everything;
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &toTransfer;
        vkCmdPipelineBarrier2(slot.commands, &dependency);
    }
    const VkImageBlit blit = swapchainBlit(job.width, job.height, job.swapchainWidth, job.swapchainHeight, job.flipY);
    vkCmdBlitImage(slot.commands, source, VK_IMAGE_LAYOUT_GENERAL, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                   &blit, VK_FILTER_NEAREST);
    {
        const VkImageMemoryBarrier2 toPresent =
            swapchainBarrier(target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                             VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                             VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &toPresent;
        vkCmdPipelineBarrier2(slot.commands, &dependency);
    }
    vkEndCommandBuffer(slot.commands);

    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &slot.acquired;
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &slot.commands;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &rendered_[imageIndex];
    if (vkQueueSubmit(job.queue, 1, &submit, slot.done) != VK_SUCCESS) return -1;

    VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &rendered_[imageIndex];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &job.swapchain;
    presentInfo.pImageIndices = &imageIndex;
    const VkResult presented = vkQueuePresentKHR(job.queue, &presentInfo);
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented < 0) return -1;
    return presented == VK_SUBOPTIMAL_KHR || acquired == VK_SUBOPTIMAL_KHR ? 1 : 0;
}

} // namespace framegen
