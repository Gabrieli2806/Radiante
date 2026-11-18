#pragma once

#include "prism/all_extern.hpp"
#include "prism/gpu/gpu.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class FrameworkContext;
struct WorldPipelineContext;

// HDR display output. Minecraft presents by blitting its main render target (8-bit SDR) into the window's swapchain;
// with HDR on, the swapchain is 16-bit float scRGB instead, and compose() takes the place of that blit:
//
// - Tone mapping and post render run in 16-bit float, matching SDR up to the highlight knee and extending highlights
//   up to the display's peak brightness while HDR output is active.
// - Pixels Minecraft left as the traced world (or only darkened with a grey overlay) show that HDR world image at
//   paper white.
// - Everything else (menus, text, the hotbar, coloured overlays, or the whole screen when the world is not traced) is
//   Minecraft's SDR image shown at paper white.
class HdrOutput {
  public:
    static HdrOutput &instance();

    // Updates whether the window presents in HDR and its peak/paper-white ratio, read by ToneMappingModule each frame.
    void setTarget(bool active, float paperWhiteNits, float peakNits);

    float targetPeakRatio() const {
        return targetPeakRatio_.load(std::memory_order_relaxed);
    }

    // A world frame was traced and fused into Minecraft's target; compose() may use it until the next one. upscaled
    // is what was fused in place of the pipeline's picture with a lower output resolution (OutputUpscaler), or null.
    void noteWorldFrame(const std::shared_ptr<WorldPipelineContext> &worldContext,
                        const std::shared_ptr<gpu::DeviceLocalImage> &upscaled);

    // Records the HDR composition of Minecraft's final image into `swapchainImage` on Minecraft's present command
    // buffer, in place of its own blit. The swapchain image is in TRANSFER_DST_OPTIMAL, Minecraft's image in
    // `minecraftLayout`. `flipY` presents it upside down, as Minecraft's own blit does. `debugView` tints everything
    // shown as SDR blue. Returns false when it could not compose (the caller then blits as usual).
    bool compose(VkCommandBuffer commandBuffer,
                 VkImage minecraftImage,
                 VkImageLayout minecraftLayout,
                 VkFormat minecraftFormat,
                 uint32_t width,
                 uint32_t height,
                 VkImage swapchainImage,
                 uint32_t swapchainWidth,
                 uint32_t swapchainHeight,
                 float paperWhiteNits,
                 float peakNits,
                 bool flipY,
                 bool debugView);

    // Development: records an HDR composition on `commandBuffer` into a readback buffer and writes a preview PNG and
    // scRGB luminance stats on the next frame once the present submit has completed.
    void capturePreview(VkCommandBuffer commandBuffer,
                        VkImage minecraftImage,
                        VkImageLayout minecraftLayout,
                        VkFormat minecraftFormat,
                        uint32_t width,
                        uint32_t height,
                        float paperWhiteNits,
                        float peakNits,
                        bool flipY,
                        bool debugView,
                        std::string path);

    // The HDR calibration screen: from now on compose() draws, in these rectangles (x, y, width, height, as top-down
    // fractions of the image), the peak pattern (mode 1, rectA) or Minecraft's image with its highlights brought back
    // (mode 2). Mode 0 ends it.
    void setCalibration(int mode, const glm::vec4 &rectA, const glm::vec4 &rectB);

    // Drops every Vulkan object; called when the renderer shuts down.
    void release();

  private:
    struct WorldFrame {
        std::shared_ptr<gpu::DeviceLocalImage> fused;  // the world as fused into Minecraft's target
        std::shared_ptr<gpu::DeviceLocalImage> output; // the pipeline's final picture (RGBA16F)
        std::shared_ptr<gpu::DeviceLocalImage> mapped; // tone mapped picture before post render (RGBA16F)
    };

    struct Calibration {
        int mode = 0;
        glm::vec4 rectA{0.0f};
        glm::vec4 rectB{0.0f};
    };

    struct RetiredView {
        VkImageView view;
        uint32_t age;
    };

    struct PendingReadback {
        std::shared_ptr<gpu::HostVisibleBuffer> buffer;
        uint32_t width = 0;
        uint32_t height = 0;
        float paperWhiteNits = 200.0f;
        float peakNits = 1000.0f;
        bool flipY = false;
        std::string path;
    };

    bool prepare(uint32_t width, uint32_t height);
    std::shared_ptr<gpu::ExternalImage> minecraftView(VkImage image, VkFormat format, uint32_t width, uint32_t height);
    bool recordComposeDispatch(VkCommandBuffer commandBuffer,
                               VkImage minecraftImage,
                               VkImageLayout minecraftLayout,
                               VkFormat minecraftFormat,
                               uint32_t width,
                               uint32_t height,
                               float paperWhiteNits,
                               float peakNits,
                               bool flipY,
                               bool debugView,
                               bool consumeWorldFrame);
    void flushPendingReadback();

    static constexpr uint32_t TABLE_RING = 4;
    static constexpr uint32_t VIEW_RETIREMENT = 8;

    std::atomic<float> targetPeakRatio_{1.0f};
    std::mutex mutex_;
    bool ready_ = false;
    bool failed_ = false;
    std::shared_ptr<gpu::Device> device_;
    std::shared_ptr<gpu::VMA> vma_;
    std::vector<std::shared_ptr<gpu::DescriptorTable>> tables_;
    uint32_t nextTable_ = 0;
    std::shared_ptr<gpu::ComputePipeline> pipeline_;
    std::shared_ptr<gpu::Sampler> sampler_;
    std::shared_ptr<gpu::DeviceLocalImage> composed_;

    std::shared_ptr<gpu::ExternalImage> view_;
    VkImage viewedImage_ = VK_NULL_HANDLE;
    std::vector<RetiredView> retiredViews_;

    WorldFrame world_;
    bool worldFresh_ = false;
    Calibration calibration_;
    std::vector<PendingReadback> pendingReadbacks_;
};
