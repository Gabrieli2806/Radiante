#pragma once

#include <volk.h>

#include <cstdint>

namespace prism {

// AMD FSR 3 upscaling through the FidelityFX API: one context, made for a render size and a display size. Without
// FidelityFX in the build it never initializes.
class FSR3Upscaler {
  public:
    struct Config {
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        uint32_t maxRenderWidth = 0;
        uint32_t maxRenderHeight = 0;
        uint32_t maxDisplayWidth = 0;
        uint32_t maxDisplayHeight = 0;
        bool hdr = false;           // the color input is linear radiance, not display values
        bool depthInverted = false; // depth runs from 1 at the near plane to 0 at the far one
        bool depthInfinite = false; // the projection has no far plane
        bool autoExposure = false;  // FSR finds the exposure itself instead of taking preExposure
    };

    struct Image {
        VkImage image = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
    };

    // One upscale. The inputs (render size) must be in SHADER_READ_ONLY_OPTIMAL and the output (display size) in
    // GENERAL; FSR leaves them in those layouts.
    struct Frame {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        Image color;
        Image depth;
        Image motionVectors;
        Image output;
        uint32_t renderWidth = 0;
        uint32_t renderHeight = 0;
        uint32_t displayWidth = 0;
        uint32_t displayHeight = 0;
        float jitterX = 0.0f; // the camera's subpixel offset, in FSR's sign convention
        float jitterY = 0.0f;
        float motionVectorScaleX = 1.0f;
        float motionVectorScaleY = 1.0f;
        float cameraNear = 0.0f;
        float cameraFar = 0.0f;
        float cameraFovVertical = 0.0f; // radians
        float frameTimeDelta = 0.0f;    // milliseconds
        float preExposure = 1.0f;
        bool reset = false; // the camera jumped: the history is dropped
        bool enableSharpening = false;
        float sharpness = 0.0f;
    };

    FSR3Upscaler() = default;
    // A context still alive here is destroyed once no frame in flight can be using it.
    ~FSR3Upscaler();

    FSR3Upscaler(const FSR3Upscaler &) = delete;
    FSR3Upscaler &operator=(const FSR3Upscaler &) = delete;

    // Makes the context (an earlier one is retired); false, with the reason in the error log, when it cannot be made.
    bool initialize(const Config &config);
    // Records the upscale into frame.commandBuffer. Nothing is recorded without a context.
    void dispatch(const Frame &frame);
    // Destroys the context now: the GPU must have finished every frame that used it.
    void destroy();

  private:
    void *context_ = nullptr; // the ffx::Context
};

} // namespace prism
