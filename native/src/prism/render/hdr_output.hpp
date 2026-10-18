#pragma once

#include "prism/all_extern.hpp"
#include "prism/gpu/gpu.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

class FrameworkContext;
struct WorldPipelineContext;

// HDR display output. Minecraft presents by blitting its main render target (8-bit SDR) into the window's swapchain;
// with HDR on, the swapchain is 16-bit float scRGB instead, and compose() takes the place of that blit:
//
// - Pixels Minecraft left as the traced world (nothing drawn over them since the world was fused in) are rebuilt from
//   the world's HDR radiance with an HDR tone curve, so highlights reach up to the display's peak brightness.
// - Everything else (menus, text, the hotbar, anything translucent over the world, the whole screen when the world is
//   not traced) is Minecraft's SDR image shown at the paper white brightness.
//
// The world pipeline is not changed at all: SDR output, screenshots and every module work as before. The only inputs
// are images the pipeline keeps anyway (the tone mapping module's input, its output and its exposure, and the world
// image fused into Minecraft's target), noted here every frame by Framework::renderFrame.
class HdrOutput {
  public:
    static HdrOutput &instance();

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

    // Drops every Vulkan object; called when the renderer shuts down.
    void release();

  private:
    // What compose() needs of the last traced frame.
    struct WorldFrame {
        std::shared_ptr<gpu::DeviceLocalImage> fused;    // the world as fused into Minecraft's target
        std::shared_ptr<gpu::DeviceLocalImage> output;   // the pipeline's picture; fused unless it was upscaled
        std::shared_ptr<gpu::DeviceLocalImage> mapped;   // tone mapped, before the post render passes
        std::shared_ptr<gpu::DeviceLocalImage> radiance; // the HDR radiance the tone mapping read
        std::shared_ptr<gpu::DeviceLocalBuffer> exposure;
        float exposureBias = 0.0f;
        float saturation = 1.0f;
        float manualExposure = 1.0f;
        bool autoExposure = true;
    };

    struct RetiredView {
        VkImageView view;
        uint32_t age;
    };

    bool prepare(uint32_t width, uint32_t height);
    std::shared_ptr<gpu::ExternalImage> minecraftView(VkImage image, VkFormat format, uint32_t width, uint32_t height);

    // Descriptor sets are written again by every compose(), and one may still be read by a frame in flight: a ring.
    static constexpr uint32_t TABLE_RING = 4;
    // How many compose() calls a replaced view of Minecraft's image outlives, so no frame in flight still reads it.
    static constexpr uint32_t VIEW_RETIREMENT = 8;

    std::mutex mutex_;
    bool ready_ = false;
    // Setting up failed once; SDR from then on.
    bool failed_ = false;
    std::shared_ptr<gpu::Device> device_;
    std::shared_ptr<gpu::VMA> vma_;
    std::vector<std::shared_ptr<gpu::DescriptorTable>> tables_;
    uint32_t nextTable_ = 0;
    std::shared_ptr<gpu::ComputePipeline> pipeline_;
    std::shared_ptr<gpu::Sampler> sampler_;
    std::shared_ptr<gpu::DeviceLocalImage> composed_;
    // Stands in for the exposure buffer while there is no traced world.
    std::shared_ptr<gpu::DeviceLocalBuffer> noExposure_;

    // A view of Minecraft's image, made again when the image changes (a resize).
    std::shared_ptr<gpu::ExternalImage> view_;
    VkImage viewedImage_ = VK_NULL_HANDLE;
    std::vector<RetiredView> retiredViews_;

    WorldFrame world_;
    bool worldFresh_ = false;
};
