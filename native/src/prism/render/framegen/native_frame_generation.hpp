#pragma once

#include "prism/all_extern.hpp"
#include "prism/render/framegen/generator.hpp"
#include "prism/render/framegen/present_thread.hpp"

#include <glm/glm.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

class Framework;

namespace gpu {
class ComputePipeline;
class DescriptorTable;
class DeviceLocalImage;
class ExternalImage;
class Image;
class Shader;
} // namespace gpu

namespace framegen {

/**
 * Frame generation without Streamline: DLSS through NGX, FSR through the FidelityFX API, fitted to Minecraft owning
 * the swapchain.
 *
 * Each frame the world pass hands over its finished image, depth and motion vectors (captureWorldFrame). When
 * Minecraft blits its final image (world plus UI) into the swapchain, the generated frames are evaluated in that same
 * command buffer and the first of them goes to the swapchain in place of the real frame (evaluateAndBlit). After
 * Minecraft has presented it, the other generated frames and then the real one are shown on further swapchain images
 * by a present thread, spread over the time one rendered frame takes (presentPending).
 */
class NativeFrameGeneration {
  public:
    enum class Backend : uint32_t { Off = 0, Dlss = 1, Fsr = 2 };

    static NativeFrameGeneration &instance();

    // The backend this GPU gets, and how many frames it can generate per rendered one (0: none).
    Backend backend();
    uint32_t maxGeneratedFrames();
    // The backend the player asked for; Off is automatic (DLSS where it runs, FSR otherwise).
    void setPreference(Backend preference);
    bool available(Backend which);
    uint32_t maxGeneratedFramesFor(Backend which);

    void setGeneratedFrames(uint32_t frames);
    bool active();

    // Frames reaching the display per second, real plus generated.
    int presentedFrameRate() const;

    // hudless is the world as fused into Minecraft's target. worldOutput is the world pipeline's output size, which a
    // lower output resolution makes smaller than hudless: depth and motion vectors smaller than it are still at the
    // render resolution, with the camera's jitter in them.
    void captureWorldFrame(std::shared_ptr<Framework> framework,
                           std::shared_ptr<gpu::DeviceLocalImage> hudless,
                           std::shared_ptr<gpu::DeviceLocalImage> linearDepth,
                           std::shared_ptr<gpu::DeviceLocalImage> motionVectors,
                           VkExtent2D worldOutput);

    // Records the evaluation into Minecraft's present command buffer, then blits the first generated frame (the real
    // one while there is no history yet) into the swapchain image, which is in TRANSFER_DST_OPTIMAL. Minecraft's final
    // image is in GENERAL and is left so. False when nothing was recorded and Minecraft blits as usual.
    bool evaluateAndBlit(VkCommandBuffer cmd,
                         VkImage finalImage,
                         VkFormat finalFormat,
                         uint32_t width,
                         uint32_t height,
                         VkImage swapchainImage,
                         uint32_t swapchainWidth,
                         uint32_t swapchainHeight,
                         bool flipY);

    // After Minecraft's present: hands the rest of the frame to the present thread. 0 fine (or nothing pending),
    // 1 suboptimal, -1 out of date.
    int presentPending(VkSwapchainKHR swapchain,
                       const std::vector<VkImage> &swapchainImages,
                       VkQueue queue,
                       uint32_t swapchainWidth,
                       uint32_t swapchainHeight);

    // Starts the history again (rebuilds, a new frame count or backend).
    void reset();
    // Frees everything; the backends are looked for again on next use.
    void release();

    // Waits until the frames handed to the present thread are all shown.
    void waitPresentIdle();

  private:
    // The world frame captureWorldFrame handed over, for the evaluation at Minecraft's present.
    struct Capture {
        std::shared_ptr<gpu::DeviceLocalImage> hudless;
        std::shared_ptr<gpu::DeviceLocalImage> linearDepth;
        std::shared_ptr<gpu::DeviceLocalImage> motionVectors;
        CameraFrame camera;
        bool valid = false;
    };

    // The camera of the capture before, which the reprojection matrices start from.
    struct PreviousCamera {
        glm::mat4 view{1.0f};
        glm::mat4 projection{1.0f};
        glm::dvec3 position{0.0};
        bool valid = false;
    };

    // What one evaluation leaves for the present thread: the generated frames and a copy of the real one.
    struct OutputSet {
        std::vector<std::shared_ptr<gpu::DeviceLocalImage>> generated;
        std::shared_ptr<gpu::DeviceLocalImage> real;
    };

    // linear_to_device_depth.comp: device depth from the traced hit distance, and motion vectors without NaNs.
    struct DepthPass {
        // Used in turns, so a table is never rewritten while a frame in flight reads it.
        std::vector<std::shared_ptr<gpu::DescriptorTable>> tables;
        uint32_t next = 0;
        std::shared_ptr<gpu::Shader> shader;
        std::shared_ptr<gpu::ComputePipeline> pipeline;
    };

    // Frames reaching the display per second, counted over windows of at least a second.
    struct PresentRate {
        uint32_t inWindow = 0;
        int perSecond = 0;
        std::chrono::steady_clock::time_point windowStart = std::chrono::steady_clock::now();

        void add(uint32_t frames);
    };

    NativeFrameGeneration() = default;

    bool probe();
    void selectBackend();
    bool evaluate(VkCommandBuffer cmd,
                  VkImage finalImage,
                  VkFormat finalFormat,
                  uint32_t width,
                  uint32_t height,
                  VkImage swapchainImage,
                  uint32_t swapchainWidth,
                  uint32_t swapchainHeight,
                  bool flipY);
    bool ensureResources(Framework &framework,
                         OutputSet &outputs,
                         uint32_t frames,
                         uint32_t width,
                         uint32_t height,
                         VkFormat format,
                         uint32_t renderWidth,
                         uint32_t renderHeight);
    gpu::Image *finalImageView(Framework &framework, VkImage image, VkFormat format, uint32_t width, uint32_t height);

    std::weak_ptr<Framework> framework_;

    // Backend.
    std::unique_ptr<Generator> generator_;
    Backend backend_ = Backend::Off;
    Backend preference_ = Backend::Off;
    bool probed_ = false;
    bool dlssAvailable_ = false;
    bool fsrAvailable_ = false;
    uint32_t dlssMaxFrames_ = 0;
    uint32_t maxGeneratedFrames_ = 0;
    uint32_t generatedFrames_ = 0;

    // Capture and history.
    Capture capture_;
    bool capturedThisFrame_ = false;
    PreviousCamera previous_;
    bool historyValid_ = false;
    std::chrono::steady_clock::time_point lastEvaluation_{};

    // Evaluation resources. The outputs are double buffered: the present thread may still be showing last frame's.
    std::array<OutputSet, 2> outputs_;
    uint32_t outputTurn_ = 0;
    std::shared_ptr<gpu::DeviceLocalImage> hudlessCopy_;
    std::shared_ptr<gpu::DeviceLocalImage> deviceDepth_;
    std::shared_ptr<gpu::DeviceLocalImage> motionVectors_;
    DepthPass depthPass_;
    // Minecraft's final image with a view of ours, which the backends need; made again when the image changes.
    std::shared_ptr<gpu::ExternalImage> finalView_;

    // Presenting: what is still to be shown after Minecraft's own present, in order, until presentPending posts it.
    PresentThread::Job pending_;
    PresentThread presenter_;
    std::chrono::steady_clock::time_point lastMinecraftPresent_{};
    double frameInterval_ = 0.0; // smoothed time between Minecraft's presents, in seconds
    PresentRate rate_;
};

} // namespace framegen
