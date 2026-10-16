#pragma once

#include "prism/all_extern.hpp"

#include <cstdint>
#include <vector>

namespace prism {

// XeSS's quality presets, numbered like XessSrModule::QualityMode so that one converts to the other by value.
enum class XeSSQualityMode {
    NativeAA = 0,
    UltraQualityPlus = 1,
    UltraQuality = 2,
    Quality = 3,
    Balanced = 4,
    Performance = 5,
    UltraPerformance = 6,
};

// An image handed to XeSS. One left as constructed tells XeSS that the input is not given.
struct XeSSImage {
    VkImageView imageView = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkImageSubresourceRange subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t width = 0;
    uint32_t height = 0;
};

struct XeSSConfig {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;

    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;
    uint32_t displayWidth = 0;
    uint32_t displayHeight = 0;

    XeSSQualityMode qualityMode = XeSSQualityMode::Quality;
    uint32_t initFlags = 0; // xess_init_flags_t bits

    float velocityScaleX = 1.0f;
    float velocityScaleY = 1.0f;
};

// One upscale. When it runs, the inputs must be in SHADER_READ_ONLY_OPTIMAL and the output in GENERAL.
struct XeSSInput {
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

    XeSSImage colorTexture;
    XeSSImage velocityTexture;
    XeSSImage depthTexture;
    XeSSImage exposureTexture;
    XeSSImage responsiveMaskTexture;
    XeSSImage outputTexture;

    float jitterOffsetX = 0.0f;
    float jitterOffsetY = 0.0f;
    float exposureScale = 1.0f;
    bool resetHistory = false;

    uint32_t inputWidth = 0;
    uint32_t inputHeight = 0;
};

// An Intel XeSS super resolution context from libxess.dll. The static queries tell instance and device creation what
// XeSS needs; each returns false when the runtime is missing or XeSS cannot answer.
class XeSSWrapper {
  public:
    XeSSWrapper() = default;
    ~XeSSWrapper();

    XeSSWrapper(const XeSSWrapper &) = delete;
    XeSSWrapper &operator=(const XeSSWrapper &) = delete;

    // Whether libxess.dll is loaded. It is downloaded on demand, and no XeSS function may be called without it.
    static bool runtimePresent();

    static bool getRequiredInstanceExtensions(std::vector<const char *> &extensions, uint32_t *minVkApiVersion);
    static bool getRequiredDeviceExtensions(VkInstance instance,
                                            VkPhysicalDevice physicalDevice,
                                            std::vector<const char *> &extensions);
    static bool getRequiredDeviceFeatures(VkInstance instance, VkPhysicalDevice physicalDevice, void **features);
    // The input size XeSS asks for at this output size and preset.
    static bool queryOptimalInputResolution(VkInstance instance,
                                            VkPhysicalDevice physicalDevice,
                                            VkDevice device,
                                            uint32_t outputWidth,
                                            uint32_t outputHeight,
                                            XeSSQualityMode qualityMode,
                                            uint32_t *outInputWidth,
                                            uint32_t *outInputHeight);

    // Sets up a context, replacing any earlier one. False, and logged, when XeSS could not be set up.
    bool initialize(const XeSSConfig &config);
    // Records the upscale into input.commandBuffer. False when nothing was recorded.
    bool dispatch(const XeSSInput &input);
    // Destroys the context at once, so the GPU must be done with everything recorded through it.
    void destroy();

  private:
    // Hands the context to gpu::retire, which destroys it once no frame in flight can still be using it.
    void retire();

    void *context_ = nullptr; // xess_context_handle_t, set only once the context is fully initialised
};

} // namespace prism
