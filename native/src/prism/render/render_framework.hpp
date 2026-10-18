#pragma once

#include "common/shared.hpp"
#include "common/singleton.hpp"
#include "prism/all_extern.hpp"
#include "prism/render/modules/world/dlss/dlss_wrapper.hpp"
#include "prism/render/mc_depth.hpp"
#include "prism/render/output_upscaler.hpp"
#include "prism/render/pipeline.hpp"
#include "prism/gpu/gpu.hpp"

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

class Framework;

// Keeps an object alive until no frame in flight can use it (gpu::retire). Objects whose destruction already goes
// through gpu::retire need not be kept here.
class FrameResourceRetainer : public SharedObject<FrameResourceRetainer> {
  public:
    template <typename T>
    void retain(std::shared_ptr<T> resource) {
        if (resource != nullptr) gpu::retire::later([held = std::move(resource)]() mutable { held.reset(); });
    }
};

// A frame slot: the command buffers one frame is recorded into, and the point of Minecraft's submit timeline at which
// they last executed. Slots are used in turn, each once the GPU is done with the frame it held before.
struct FrameworkContext : public SharedObject<FrameworkContext> {
    std::weak_ptr<Framework> framework;

    uint32_t frameIndex;

    std::shared_ptr<gpu::Instance> instance;
    std::shared_ptr<gpu::PhysicalDevice> physicalDevice;
    std::shared_ptr<gpu::Device> device;
    std::shared_ptr<gpu::VMA> vma;
    std::shared_ptr<gpu::Swapchain> swapchain;
    std::shared_ptr<gpu::CommandPool> commandPool;

    // Executed in this order: uploads, the world pipeline, then the blit into Minecraft's target and what follows it.
    std::shared_ptr<gpu::CommandBuffer> uploadCommandBuffer;
    std::shared_ptr<gpu::CommandBuffer> worldCommandBuffer;
    std::shared_ptr<gpu::CommandBuffer> fuseCommandBuffer;

    // Minecraft's submit timeline semaphore and the value that marks the submit these command
    // buffers were executed in. Zero means the buffers were never handed to Minecraft.
    VkSemaphore submitTimeline = VK_NULL_HANDLE;
    uint64_t submitTimelineValue = 0;
    // The command buffers are begun and not yet ended.
    bool recording = false;

    FrameworkContext(std::shared_ptr<Framework> framework, uint32_t frameIndex);
    ~FrameworkContext();

    // Records the blit of the traced picture into target; nothing when there is no world pipeline. With upscaled
    // (OutputUpscaler's picture, at the target's size), that is copied in instead.
    void fuseInto(std::shared_ptr<gpu::ExternalImage> target,
                  const std::shared_ptr<gpu::DeviceLocalImage> &upscaled = nullptr);
    // Waits until the GPU has executed what this slot recorded the last time it was used.
    void waitForPreviousSubmit();
};

// Minecraft's Vulkan objects the renderer works with, and the size of the window when it starts.
struct SharedDeviceHandles {
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkQueue mainQueue;
    uint32_t mainQueueFamily;
    VkQueue secondaryQueue;
    uint32_t secondaryQueueFamily;
    uint32_t width;
    uint32_t height;
};

// The renderer's frame loop inside Minecraft's. Minecraft asks for a frame (renderFrame), executes the command buffers
// it gets in its own submit and reports that submit (markSubmitted), which opens the next frame slot. The framework
// owns the device objects it shares with Minecraft, the frame slots and the world pipeline, which it rebuilds when a
// setting or the size changes (recreate).
class Framework : public SharedObject<Framework> {
  public:
    Framework();
    ~Framework();

    void init(const SharedDeviceHandles &handles);

    // Opens the next frame slot: waits for its previous GPU work and begins its command buffers.
    void acquireContext();
    // Records uploads, the world pipeline and the blit into `target`, ends the command buffers and returns them in
    // execution order. The next slot opens when Minecraft reports their submit, or when something records before.
    // width x height is the target's size; the world is traced at traceWidth x traceHeight and scaled into it. The
    // two differ for the extra views some mods draw between the main ones (Vivecraft's handheld camera, telescopes),
    // which would otherwise rebuild the whole pipeline every time they alternate with the main view.
    std::vector<VkCommandBuffer> renderFrame(VkImage target, uint32_t width, uint32_t height, uint32_t traceWidth,
                                             uint32_t traceHeight, VkFormat format);
    // The command buffers renderFrame returned last were executed in the submit that signals `value` on `timeline`.
    void markSubmitted(VkSemaphore timeline, uint64_t value);
    // Minecraft's depth texture for the next renderFrame to fill with the traced world's depth (McDepthWriter), or
    // VK_NULL_HANDLE for none. Asked for again every frame.
    void setOverlayDepthTarget(VkImage image, VkFormat format, const float projection[4]);

    // Rebuilds the frame slots' work and the world pipeline for the current size and blueprint. Throws when that fails
    // (lastRebuildFailed); when the world pipeline itself could not be built, frames draw no world until a rebuild
    // succeeds.
    void recreate();
    // Ends and submits what the current frame recorded so far; see recreate().
    void submitPartialFrame();

    // Runs the rebuild a setting asked for, if one is pending, rather than leaving it to the next frame.
    void recreatePending();
    // Also destroys everything retired so far (gpu::retire::drain).
    void waitDeviceIdle();
    void waitRenderQueueIdle();
    void waitBackendQueueIdle();
    void close();
    bool isRunning();

    // Held by every frame and rebuild throughout, and by whatever changes what a frame reads.
    std::recursive_mutex &recreateMtx();
    bool lastRebuildFailed() const {
        return lastRebuildFailed_;
    }

    std::shared_ptr<gpu::Instance> instance();
    std::shared_ptr<gpu::PhysicalDevice> physicalDevice();
    std::shared_ptr<gpu::Device> device();
    std::shared_ptr<gpu::VMA> vma();
    std::shared_ptr<gpu::Swapchain> swapchain();
    std::shared_ptr<gpu::CommandPool> mainCommandPool();
    std::shared_ptr<gpu::CommandPool> asyncCommandPool();

    std::vector<std::shared_ptr<FrameworkContext>> &contexts();
    // The slot being recorded; opens the next one when none is.
    std::shared_ptr<FrameworkContext> safeAcquireCurrentContext();

    std::shared_ptr<Pipeline> pipeline();

    FrameResourceRetainer &frameResourceRetainer();

  private:
    void createContexts();
    void recordAfterWorld(FrameworkContext &context,
                          const std::shared_ptr<WorldPipelineContext> &worldContext,
                          VkImage target,
                          uint32_t width,
                          uint32_t height,
                          VkFormat format);
    std::shared_ptr<gpu::ExternalImage> fuseTarget(VkImage image, uint32_t width, uint32_t height, VkFormat format);

    std::shared_ptr<gpu::Instance> instance_;
    std::shared_ptr<gpu::PhysicalDevice> physicalDevice_;
    std::shared_ptr<gpu::Device> device_;
    std::shared_ptr<gpu::VMA> vma_;
    std::shared_ptr<gpu::Swapchain> swapchain_;
    std::shared_ptr<gpu::CommandPool> mainCommandPool_;
    std::shared_ptr<gpu::CommandPool> asyncCommandPool_;

    std::shared_ptr<Pipeline> pipeline_;

    std::vector<std::shared_ptr<FrameworkContext>> contexts_;
    std::shared_ptr<FrameworkContext> currentContext_ = nullptr;
    std::shared_ptr<FrameworkContext> lastRenderedContext_ = nullptr;
    uint32_t nextContextIndex_ = 0;

    std::recursive_mutex recreateMtx_;
    // Read without the lock by threads that build chunks (isRunning) while the render thread closes.
    std::atomic<bool> running_{true};
    // The last rebuild threw: the world pipeline is not the one asked for (there is none when it could not be built),
    // and only another rebuild repairs it.
    bool lastRebuildFailed_ = false;

    std::shared_ptr<FrameResourceRetainer> frameResourceRetainer_;
    McDepthWriter mcDepth_;
    McDepthWriter::Target overlayDepth_;
    // The FSR 1 upscale of a lower output resolution to the target's size.
    OutputUpscaler outputUpscaler_;
    // Minecraft's colour target as the blit sees it, kept while the target stays the same.
    std::shared_ptr<gpu::ExternalImage> fuseTarget_;
};
