#include "prism/render/render_framework.hpp"

#include "prism/render/buffers.hpp"
#include "prism/render/chunks.hpp"
#include "prism/render/entities.hpp"
#include "prism/render/framegen/frame_generation.hpp"
#include "prism/render/framegen/native_frame_generation.hpp"
#include "prism/render/gpu_profiler.hpp"
#include "prism/render/hdr_output.hpp"
#include "prism/render/renderer.hpp"
#include "prism/render/textures.hpp"
#include "prism/render/world.hpp"
#include "prism/gpu/queue_lock.hpp"
#include "prism/util/logging.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

namespace {

using Clock = std::chrono::steady_clock;

std::ostream &frameworkErr() {
    return radiante::err() << "[Render Framework] ";
}

double msBetween(Clock::time_point from, Clock::time_point to) {
    return std::chrono::duration<double, std::milli>(to - from).count();
}

// A slot's command buffers in the order they execute.
std::array<VkCommandBuffer, 3> commandBuffersOf(FrameworkContext &context) {
    return {context.uploadCommandBuffer->vkCommandBuffer(), context.worldCommandBuffer->vkCommandBuffer(),
            context.fuseCommandBuffer->vkCommandBuffer()};
}

void beginRecording(FrameworkContext &context) {
    context.uploadCommandBuffer->begin();
    context.worldCommandBuffer->begin();
    context.fuseCommandBuffer->begin();
    context.recording = true;
}

void endRecording(FrameworkContext &context) {
    context.uploadCommandBuffer->end();
    context.worldCommandBuffer->end();
    context.fuseCommandBuffer->end();
    context.recording = false;
}

// Development timings (RADIANTE_DEV_PROFILE): where renderFrame spends its CPU time, printed for every frame slower
// than 25 ms and as averages over 300 frames. Costs nothing when the variable is not set.
class FrameTimer {
  public:
    enum Phase { Acquire, Upload, World, Fuse, PhaseCount };

    FrameTimer() {
        if (enabled()) start_ = last_ = Clock::now();
    }

    // The time since the previous phase ended is counted under `phase`.
    void lap(Phase phase) {
        if (!enabled()) return;
        const auto now = Clock::now();
        ms_[phase] = msBetween(last_, now);
        last_ = now;
    }

    // Ends the frame; the time since the last phase is counted under Fuse.
    void finish() {
        if (!enabled()) return;
        lap(Fuse);
        if (msBetween(start_, last_) > 25.0) {
            std::cout << "[native profile] spike acquire(wait)=" << ms_[Acquire] << "ms upload=" << ms_[Upload]
                      << "ms world=" << ms_[World] << "ms fuse=" << ms_[Fuse] << "ms" << std::endl;
        }

        Totals &totals = runningTotals();
        for (int phase = 0; phase < PhaseCount; phase++) totals.ms[phase] += ms_[phase];
        if (++totals.frames < AVERAGED_FRAMES) return;
        auto average = [&](Phase phase) { return totals.ms[phase] / AVERAGED_FRAMES; };
        std::cout << "[native profile] acquire(wait)=" << average(Acquire) << "ms upload=" << average(Upload)
                  << "ms world=" << average(World) << "ms fuse=" << average(Fuse) << "ms" << std::endl;
        totals = {};
    }

  private:
    static constexpr int AVERAGED_FRAMES = 300;

    struct Totals {
        std::array<double, PhaseCount> ms{};
        int frames = 0;
    };

    static bool enabled() {
        static const bool on = std::getenv("RADIANTE_DEV_PROFILE") != nullptr;
        return on;
    }

    static Totals &runningTotals() {
        static Totals totals;
        return totals;
    }

    Clock::time_point start_;
    Clock::time_point last_;
    std::array<double, PhaseCount> ms_{};
};

// Diagnostics for frames that never complete: when Minecraft stops handing frames to the renderer, a thread reports how
// far the GPU got through the last submit and what the queue is doing. Started with the first frame.
namespace watchdog {

std::atomic<VkSemaphore> g_timeline{VK_NULL_HANDLE};
std::atomic<uint64_t> g_value{0};
// When the last frame was submitted, in steady clock ticks; zero while there is nothing to watch.
std::atomic<int64_t> g_lastFrame{0};
std::atomic<bool> g_started{false};

constexpr double STALL_MS = 2500.0;

int64_t nowTicks() {
    return Clock::now().time_since_epoch().count();
}

double msSinceTicks(int64_t ticks) {
    return std::chrono::duration<double, std::milli>(Clock::duration(nowTicks() - ticks)).count();
}

void run(VkDevice device, VkQueue mainQueue, VkQueue secondaryQueue) {
    bool reported = false;
    for (;;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        const int64_t lastFrame = g_lastFrame.load();
        if (lastFrame == 0) continue;
        const double stalledMs = msSinceTicks(lastFrame);
        if (stalledMs < STALL_MS) {
            reported = false;
            continue;
        }
        // Once per stall.
        if (reported) continue;
        reported = true;

        uint64_t completed = 0;
        const VkSemaphore timeline = g_timeline.load();
        const VkResult result = timeline != VK_NULL_HANDLE ?
                                    vkGetSemaphoreCounterValue(device, timeline, &completed) :
                                    VK_ERROR_UNKNOWN;
        const char *op = gpu::queueOp().load();
        const double opMs = msSinceTicks(gpu::queueOpStart().load());
        std::ostringstream text;
        text << "[Radiante-Watchdog] no frame for " << stalledMs << " ms; last submitted timeline value "
             << g_value.load() << ", completed " << completed << " (result " << result << "); queue op "
             << (op != nullptr ? op : "none")
             << (op != nullptr ? " running for " + std::to_string(opMs) + " ms" : std::string()) << "\n";
        // The render thread that drains the mod's log may be the one stuck: stderr too.
        std::cerr << text.str() << std::flush;
        radiante::out() << text.str() << std::flush;
        gpu::diagnostics::onResult(result, mainQueue, secondaryQueue);
    }
}

void frameSubmitted(VkDevice device, VkQueue mainQueue, VkQueue secondaryQueue, VkSemaphore timeline, uint64_t value) {
    g_timeline.store(timeline);
    g_value.store(value);
    g_lastFrame.store(nowTicks());
    if (!g_started.exchange(true)) std::thread(run, device, mainQueue, secondaryQueue).detach();
}

// The renderer closed: no frames are coming, and the device may soon be gone.
void disarm() {
    g_lastFrame.store(0);
}

} // namespace watchdog

} // namespace

// ---- FrameworkContext

FrameworkContext::FrameworkContext(std::shared_ptr<Framework> framework, uint32_t frameIndex)
    : framework(framework),
      frameIndex(frameIndex),
      instance(framework->instance()),
      physicalDevice(framework->physicalDevice()),
      device(framework->device()),
      vma(framework->vma()),
      swapchain(framework->swapchain()),
      commandPool(framework->mainCommandPool()),
      uploadCommandBuffer(gpu::CommandBuffer::create(device, commandPool)),
      worldCommandBuffer(gpu::CommandBuffer::create(device, commandPool)),
      fuseCommandBuffer(gpu::CommandBuffer::create(device, commandPool)) {}

FrameworkContext::~FrameworkContext() = default;

void FrameworkContext::waitForPreviousSubmit() {
    if (submitTimeline == VK_NULL_HANDLE || submitTimelineValue == 0) return;

    VkSemaphoreWaitInfo waitInfo{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
    waitInfo.semaphoreCount = 1;
    waitInfo.pSemaphores = &submitTimeline;
    waitInfo.pValues = &submitTimelineValue;
    VkResult result = vkWaitSemaphores(device->vkDevice(), &waitInfo, UINT64_MAX);
    gpu::diagnostics::onResult(result, device->mainVkQueue(), device->secondaryQueue());
    if (result != VK_SUCCESS) frameworkErr() << "vkWaitSemaphores failed: " << result << std::endl;
    submitTimelineValue = 0;
}

void FrameworkContext::fuseInto(std::shared_ptr<gpu::ExternalImage> target,
                                const std::shared_ptr<gpu::DeviceLocalImage> &upscaled) {
    auto owner = framework.lock();
    if (owner == nullptr || !owner->isRunning() || target == nullptr) return;

    if (upscaled != nullptr) {
        // Already at the target's size, in GENERAL with its writes made visible (OutputUpscaler): a copy, through a
        // blit for the format conversion. Minecraft keeps its textures in GENERAL.
        VkImageBlit blit{};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blit.srcOffsets[1] = {static_cast<int32_t>(upscaled->width()), static_cast<int32_t>(upscaled->height()), 1};
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blit.dstOffsets[1] = {static_cast<int32_t>(target->width()), static_cast<int32_t>(target->height()), 1};
        vkCmdBlitImage(fuseCommandBuffer->vkCommandBuffer(), upscaled->vkImage(), VK_IMAGE_LAYOUT_GENERAL,
                       target->vkImage(), VK_IMAGE_LAYOUT_GENERAL, 1, &blit, VK_FILTER_NEAREST);
        // Read by the blit outside the layout tracking: its next tracked use waits on everything before it.
        upscaled->imageLayout() = VK_IMAGE_LAYOUT_GENERAL;
        return;
    }

    auto worldContext = owner->pipeline()->acquirePipelineContext(shared_from_this())->worldPipelineContext;
    if (worldContext == nullptr || worldContext->outputImage == nullptr) return;
    worldContext->blitOutput(*fuseCommandBuffer, *target, physicalDevice->mainQueueIndex());
}

// ---- Framework

Framework::Framework() = default;

Framework::~Framework() = default;

void Framework::init(const SharedDeviceHandles &handles) {
    instance_ = gpu::Instance::create(handles.instance);
    physicalDevice_ = gpu::PhysicalDevice::create(instance_, handles.physicalDevice, handles.mainQueueFamily,
                                                 handles.secondaryQueueFamily);
    device_ =
        gpu::Device::create(instance_, physicalDevice_, handles.device, handles.mainQueue, handles.secondaryQueue);
    vma_ = gpu::VMA::create(instance_, physicalDevice_, device_);
    swapchain_ = gpu::Swapchain::create(handles.width, handles.height);
    mainCommandPool_ = gpu::CommandPool::create(physicalDevice_, device_);
    asyncCommandPool_ = gpu::CommandPool::create(physicalDevice_, device_, physicalDevice_->secondaryQueueIndex());
    frameResourceRetainer_ = FrameResourceRetainer::create();

    createContexts();
    pipeline_ = Pipeline::create(shared_from_this());
}

void Framework::createContexts() {
    const uint32_t slots = swapchain_->imageCount();
    uint32_t firstSlot = 0;
    if (contexts_.size() == slots) {
        // The slots are kept across rebuilds: new ones would leave the old command buffers, and the memory of what
        // they recorded, allocated in the pool until it goes. recreate() waited for the queue, which covers every
        // buffer already submitted, so those slots forget the submit they would otherwise wait for. The buffers of the
        // last rendered frame may be handed to Minecraft and not submitted yet (several passes per frame in VR): that
        // slot keeps what markSubmitted told it and is used last, so it is not begun again before its submit ran.
        const auto handedOut = lastRenderedContext_;
        for (auto &context : contexts_) {
            if (context == handedOut) continue;
            context->submitTimeline = VK_NULL_HANDLE;
            context->submitTimelineValue = 0;
        }
        if (handedOut != nullptr) firstSlot = (handedOut->frameIndex + 1) % slots;
    } else {
        std::vector<std::shared_ptr<FrameworkContext>> created;
        created.reserve(slots);
        for (uint32_t i = 0; i < slots; i++) created.push_back(FrameworkContext::create(shared_from_this(), i));
        contexts_ = std::move(created);
    }
    gpu::retire::configure(slots);
    currentContext_ = nullptr;
    lastRenderedContext_ = nullptr;
    nextContextIndex_ = firstSlot;
}

void Framework::acquireContext() {
    if (!running_) return;
    std::unique_lock<std::recursive_mutex> lock(recreateMtx_);

    auto context = contexts_[nextContextIndex_];
    nextContextIndex_ = (nextContextIndex_ + 1) % static_cast<uint32_t>(contexts_.size());

    context->waitForPreviousSubmit();
    currentContext_ = context;
    gpu::retire::openSlot(context->frameIndex);

    Renderer &renderer = Renderer::instance();
    // Flush texture uploads queued while no frame was in flight; resetFrame drops the queue.
    renderer.textures()->performQueuedUpload();

    beginRecording(*context);

    renderer.buffers()->resetFrame();
    renderer.textures()->resetFrame();
    renderer.world()->resetFrame();
    renderer.world()->chunks()->resetFrame();
    renderer.world()->entities()->resetFrame();
}

std::vector<VkCommandBuffer> Framework::renderFrame(VkImage target,
                                                   uint32_t width,
                                                   uint32_t height,
                                                   uint32_t traceWidth,
                                                   uint32_t traceHeight,
                                                   VkFormat format) {
    std::unique_lock<std::recursive_mutex> lock(recreateMtx_);
    FrameTimer timer;
    if (!running_) return {};

    const VkExtent2D extent = swapchain_->vkExtent();
    if (extent.width != traceWidth || extent.height != traceHeight || pipeline_->isRecreationNeeded ||
        Renderer::options.needRecreate) {
        swapchain_->reconstruct(traceWidth, traceHeight);
        recreate();
    }

    auto context = safeAcquireCurrentContext();
    timer.lap(FrameTimer::Acquire);

    Renderer &renderer = Renderer::instance();
    renderer.textures()->performQueuedUpload();
    renderer.buffers()->performQueuedUpload();
    timer.lap(FrameTimer::Upload);

    auto worldContext = pipeline_->acquirePipelineContext(context)->worldPipelineContext;
    if (renderer.world()->shouldRender() && worldContext != nullptr) {
        worldContext->render();
        timer.lap(FrameTimer::World);
        recordAfterWorld(*context, worldContext, target, width, height, format);
    }
    overlayDepth_.image = VK_NULL_HANDLE;

    endRecording(*context);
    lastRenderedContext_ = context;
    currentContext_ = nullptr;
    timer.finish();

    const auto commandBuffers = commandBuffersOf(*context);
    return {commandBuffers.begin(), commandBuffers.end()};
}

void Framework::recordAfterWorld(FrameworkContext &context,
                                 const std::shared_ptr<WorldPipelineContext> &worldContext,
                                 VkImage target,
                                 uint32_t width,
                                 uint32_t height,
                                 VkFormat format) {
    auto worldPipeline = pipeline_->worldPipeline();
    const int depthSlot = framegen::FrameGeneration::depthSlot();
    const std::shared_ptr<gpu::DeviceLocalImage> &output = worldContext->outputImage;

    // A lower output resolution: the picture is upscaled to the target's size with FSR 1 rather than stretched by the
    // blit. Only when it is smaller on both sides; at the target's size (Native) nothing more runs than before.
    std::shared_ptr<gpu::DeviceLocalImage> upscaled;
    if (output != nullptr && output->width() <= width && output->height() <= height &&
        (output->width() < width || output->height() < height)) {
        upscaled = outputUpscaler_.record(*this, *context.fuseCommandBuffer, output, WorldPipeline::OUTPUT_LAYOUT,
                                          width, height, context.frameIndex);
        if (upscaled != nullptr && GpuProfiler::enabled()) {
            GpuProfiler::mark(context.fuseCommandBuffer->vkCommandBuffer(), "output_fsr1");
        }
    }
    // What Minecraft's target receives: frame generation and HDR output compare its final image against it.
    const std::shared_ptr<gpu::DeviceLocalImage> &shown = upscaled != nullptr ? upscaled : output;

    // Frame generation reads the finished world image plus the depth and motion vectors behind it.
    if (worldPipeline != nullptr && output != nullptr) {
        const int motionSlot = framegen::FrameGeneration::motionVectorSlot();
        if (depthSlot >= 0 && motionSlot >= 0) {
            framegen::NativeFrameGeneration::instance().captureWorldFrame(
                shared_from_this(), shown,
                worldPipeline->sharedImage(context.frameIndex, static_cast<uint32_t>(depthSlot)),
                worldPipeline->sharedImage(context.frameIndex, static_cast<uint32_t>(motionSlot)),
                VkExtent2D{output->width(), output->height()});
        }
    }

    context.fuseInto(fuseTarget(target, width, height, format), upscaled);

    // For what Minecraft draws over the traced picture afterwards: the traced depth, in its depth texture.
    if (overlayDepth_.image != VK_NULL_HANDLE && worldPipeline != nullptr && depthSlot >= 0) {
        mcDepth_.write(shared_from_this(), context.fuseCommandBuffer,
                       worldPipeline->sharedImage(context.frameIndex, static_cast<uint32_t>(depthSlot)), overlayDepth_,
                       width, height, context.frameIndex);
    }
    HdrOutput::instance().noteWorldFrame(worldContext, upscaled);
}

std::shared_ptr<gpu::ExternalImage> Framework::fuseTarget(VkImage image,
                                                          uint32_t width,
                                                          uint32_t height,
                                                          VkFormat format) {
    // The target only changes when the window does; its wrapper is not made again every frame.
    if (fuseTarget_ == nullptr || fuseTarget_->vkImage() != image || fuseTarget_->width() != width ||
        fuseTarget_->height() != height || fuseTarget_->vkFormat() != format) {
        fuseTarget_ = gpu::ExternalImage::create(image, width, height, format);
    }
    return fuseTarget_;
}

void Framework::setOverlayDepthTarget(VkImage image, VkFormat format, const float projection[4]) {
    std::unique_lock<std::recursive_mutex> lock(recreateMtx_);
    overlayDepth_.image = image;
    overlayDepth_.format = format;
    std::copy_n(projection, 4, overlayDepth_.projection);
}

void Framework::markSubmitted(VkSemaphore timeline, uint64_t value) {
    watchdog::frameSubmitted(device_->vkDevice(), device_->mainVkQueue(), device_->secondaryQueue(), timeline, value);
    std::unique_lock<std::recursive_mutex> lock(recreateMtx_);
    if (lastRenderedContext_ == nullptr) return;
    lastRenderedContext_->submitTimeline = timeline;
    lastRenderedContext_->submitTimelineValue = value;
    acquireContext();
}

// The rebuild a settings change asked for, run now instead of inside the next frame: the mod calls this from a worker
// thread while the render thread keeps the window alive (ApplyingSettingsScreen).
void Framework::recreatePending() {
    std::unique_lock<std::recursive_mutex> lock(recreateMtx_);
    if (!running_ || !(pipeline_->isRecreationNeeded || Renderer::options.needRecreate)) return;
    recreate();
}

void Framework::submitPartialFrame() {
    std::unique_lock<std::recursive_mutex> lock(recreateMtx_);
    if (currentContext_ == nullptr || !currentContext_->recording) return;
    endRecording(*currentContext_);
    const auto commandBuffers = commandBuffersOf(*currentContext_);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());
    submit.pCommandBuffers = commandBuffers.data();
    vkQueueSubmit(device_->mainVkQueue(), 1, &submit, VK_NULL_HANDLE);
    currentContext_ = nullptr;
}

void Framework::recreate() {
    if (!running_) return;
    std::unique_lock<std::recursive_mutex> lock(recreateMtx_);

    // The progress shown for a rebuild Java asked for ends with this one, whether it succeeds or not.
    struct ProgressEnd {
        bool active;
        ~ProgressEnd() {
            if (active) Pipeline::endNativeRebuild();
        }
    } progressEnd{Pipeline::nativeRebuildActive()};

    // A background prewarm holds the pipeline being replaced (its shader pack, its layout) and compiles into the
    // caches this rebuild reads: it stops first. One already compiling a ray tracing pipeline finishes it, which is
    // the same pipeline this rebuild would otherwise compile itself.
    pipeline_->shaderPrewarm().cancelAndJoin();

    try {
        Renderer::options.needRecreate = false;
        pipeline_->isRecreationNeeded = false;

        // What the frame being recorded holds so far (uploads, structure builds) is not recorded again: it is
        // submitted on its own first. Dropping it left chunks whose acceleration structures were never built in the
        // next scene, and tracing them hung the GPU.
        submitPartialFrame();
        waitRenderQueueIdle();
        framegen::NativeFrameGeneration::instance().reset();
        mcDepth_.reset();
        outputUpscaler_.reset();

        const auto rebuildStart = Clock::now();
        createContexts();
        pipeline_->recreate(shared_from_this());
        Renderer::instance().textures()->bindAllTextures();
        acquireContext();
        radiante::out() << "[Rebuild] total " << msBetween(rebuildStart, Clock::now()) << " ms" << std::endl;

        // What the driver compiled for this build is kept for the next one (see Device::pipelineCache).
        device_->savePipelineCache();
        lastRebuildFailed_ = false;
    } catch (...) {
        lastRebuildFailed_ = true;
        throw;
    }
}

void Framework::waitDeviceIdle() {
    gpu::diagnostics::onResult(vkDeviceWaitIdle(device_->vkDevice()), device_->mainVkQueue(),
                               device_->secondaryQueue());
    gpu::retire::drain();
}

void Framework::waitRenderQueueIdle() {
    vkQueueWaitIdle(device_->mainVkQueue());
}

void Framework::waitBackendQueueIdle() {
    vkQueueWaitIdle(device_->secondaryQueue());
}

void Framework::close() {
    framegen::NativeFrameGeneration::instance().release();
    mcDepth_.reset();
    outputUpscaler_.reset();
    // Before the modules close: a prewarm still compiles with their objects.
    if (pipeline_ != nullptr) pipeline_->shaderPrewarm().cancelAndJoin();
    if (running_ && pipeline_ != nullptr) pipeline_->close();
    running_ = false;
    if (device_ != nullptr) {
        waitDeviceIdle();
        GpuProfiler::release();
    }
    watchdog::disarm();
}

bool Framework::isRunning() {
    return running_;
}

std::recursive_mutex &Framework::recreateMtx() {
    return recreateMtx_;
}

std::shared_ptr<gpu::Instance> Framework::instance() {
    return instance_;
}

std::shared_ptr<gpu::PhysicalDevice> Framework::physicalDevice() {
    return physicalDevice_;
}

std::shared_ptr<gpu::Device> Framework::device() {
    return device_;
}

std::shared_ptr<gpu::VMA> Framework::vma() {
    return vma_;
}

std::shared_ptr<gpu::Swapchain> Framework::swapchain() {
    return swapchain_;
}

std::shared_ptr<gpu::CommandPool> Framework::mainCommandPool() {
    return mainCommandPool_;
}

std::shared_ptr<gpu::CommandPool> Framework::asyncCommandPool() {
    return asyncCommandPool_;
}

std::vector<std::shared_ptr<FrameworkContext>> &Framework::contexts() {
    return contexts_;
}

std::shared_ptr<FrameworkContext> Framework::safeAcquireCurrentContext() {
    std::unique_lock<std::recursive_mutex> lock(recreateMtx_);
    if (currentContext_ == nullptr) acquireContext();
    return currentContext_;
}

std::shared_ptr<Pipeline> Framework::pipeline() {
    return pipeline_;
}

FrameResourceRetainer &Framework::frameResourceRetainer() {
    return *frameResourceRetainer_;
}
