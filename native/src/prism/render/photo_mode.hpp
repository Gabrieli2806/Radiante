#pragma once

#include "common/shared.hpp"
#include "prism/all_extern.hpp"
#include "prism/gpu/gpu.hpp"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class Framework;

// Photo Mode's progressive Monte Carlo accumulator and PNG / OpenEXR exporter.
//
// While active, WorldPipeline runs RayTracingModule at full window resolution (nrd_signals off, post DoF and motion
// blur off), feeds its raw linear HDR radiance through PhotoMode::recordAccumulate into an FP32 accumulation image,
// and hands the running mean to ToneMappingModule and PostRenderModule. Moving the camera, changing the physical
// thin-lens parameters, or modifying the world (terrain, entities, sky or shader pack parameters) resets the sample
// count; adjusting tone-mapping / exposure sliders leaves the accumulated HDR radiance intact and updates the picture
// immediately.
class PhotoMode {
  public:
    static PhotoMode &instance();
    ~PhotoMode();

    void setActive(bool active);
    bool active() const {
        return active_.load(std::memory_order_acquire);
    }

    void setLens(float apertureRadius, float focusDistance, int blades, float bladeRotation);
    void setExposureOverride(bool enabled, float exposureBias);
    bool exposureOverride(float &outBias) const;
    void setTargetSamples(uint32_t targetSamples);
    uint32_t targetSamples() const {
        return targetSamples_.load(std::memory_order_relaxed);
    }
    uint32_t sampleCount() const {
        return sampleCount_.load(std::memory_order_relaxed);
    }

    void resetAccumulation();

    // Queues saving the tone-mapped output to `pngPath` (when non-empty) and/or the linear FP32 HDR accumulated
    // radiance to `exrPath` (when non-empty). With `waitForTarget`, waits until `sampleCount >= targetSamples` first.
    void requestCapture(std::filesystem::path pngPath, std::filesystem::path exrPath, bool waitForTarget);
    // 0: idle, 1: waiting for target samples, GPU readback or background file write, 2: saved, -1: error.
    // Resets 2 / -1 back to 0 when read.
    int pollCaptureStatus(std::u16string &outMessage);

    // True when the scene has already reached `targetSamples` and filled every frame slot's guides, so RayTracingModule
    // can pause while ToneMappingModule and PostRenderModule keep running live.
    bool canSkipRayTracing(uint32_t frameSlotCount);

    void recordAccumulate(Framework &framework,
                          gpu::CommandBuffer &commandBuffer,
                          const std::shared_ptr<gpu::DeviceLocalImage> &rtRadiance,
                          const std::shared_ptr<gpu::DeviceLocalImage> &outAccumulated,
                          bool tracedNewSample);

    void recordCaptureIfReady(Framework &framework,
                              gpu::CommandBuffer &commandBuffer,
                              const std::shared_ptr<gpu::DeviceLocalImage> &finalOutput);

    void noteSubmitted(VkSemaphore timeline, uint64_t value);
    void flushPendingCapture(bool waitAll = false);
    void release();

  private:
    PhotoMode() = default;

    struct PushConstants {
        uint32_t sampleIndex = 1;
        float fireflyClamp = 64.0f;
    };

    struct PendingReadback {
        std::filesystem::path pngPath;
        std::filesystem::path exrPath;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t samples = 0;
        bool submitted = false;
        VkSemaphore timeline = VK_NULL_HANDLE;
        uint64_t timelineValue = 0;
        std::shared_ptr<gpu::HostVisibleBuffer> pngBuffer;
        std::shared_ptr<gpu::HostVisibleBuffer> exrBuffer;
    };

    struct SaveJob {
        std::filesystem::path pngPath;
        std::filesystem::path exrPath;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t samples = 0;
        std::vector<uint16_t> pngHalfRgba;
        std::vector<float> exrFloatRgba;
    };

    bool ensureResources(Framework &framework, uint32_t width, uint32_t height);
    bool checkSceneChangedAndUpdate();
    void enqueueSaveJob(SaveJob &&job);
    void saveWorkerLoop();
    void stopAndJoinSaveWorker();

    static constexpr size_t kTableRing = 4;

    std::atomic<bool> active_{false};
    std::atomic<uint32_t> targetSamples_{256};
    std::atomic<uint32_t> sampleCount_{0};
    std::atomic<bool> resetRequested_{true};

    mutable std::mutex mutex_;
    glm::vec4 lens_{0.0f, 12.0f, 0.0f, 0.0f};
    bool hasExposureOverride_ = false;
    float exposureBias_ = 0.0f;
    bool hasSceneState_ = false;
    glm::mat4 lastEffectedView_{1.0f};
    glm::mat4 lastProj_{1.0f};
    glm::dvec3 lastCameraPos_{0.0};
    uint64_t lastChunkGeometryVersion_ = 0;
    uint32_t lastSkyType_ = 0;
    glm::vec3 lastSunDirection_{0.0f};
    float lastRainGradient_ = -1.0f;
    glm::vec4 lastHeldMainPos_{0.0f};
    glm::vec4 lastHeldOffPos_{0.0f};
    glm::vec4 lastBrightness_{0.0f};
    std::array<glm::vec4, PRISM_PACK_PARAM_COUNT> lastPackParams_{};
    uint32_t convergedSlotFrames_ = 0;

    std::shared_ptr<gpu::Device> device_;
    std::shared_ptr<gpu::VMA> vma_;
    std::shared_ptr<gpu::DeviceLocalImage> accumImage_;
    std::shared_ptr<gpu::ComputePipeline> pipeline_;
    std::vector<std::shared_ptr<gpu::DescriptorTable>> tables_;
    size_t nextTable_ = 0;

    bool captureQueued_ = false;
    bool captureWaitForTarget_ = false;
    std::filesystem::path queuedPngPath_;
    std::filesystem::path queuedExrPath_;
    std::vector<PendingReadback> pendingReadbacks_;
    int captureStatus_ = 0;
    std::u16string captureMessage_;

    std::mutex saveMutex_;
    std::condition_variable saveCv_;
    std::deque<SaveJob> saveQueue_;
    bool stopSaveWorker_ = false;
    std::thread saveWorker_;
};
