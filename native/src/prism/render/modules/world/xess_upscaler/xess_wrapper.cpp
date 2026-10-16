#include "prism/render/modules/world/xess_upscaler/xess_wrapper.hpp"

#include "prism/gpu/retire.hpp"
#include "prism/util/logging.hpp"

#ifdef PRISM_ENABLE_XESS
#    include <xess/xess.h>
#    include <xess/xess_vk.h>
#    ifdef _WIN32
#        include <windows.h>
#    endif
#endif

namespace prism {
namespace {

bool usable(const XeSSImage &image) {
    return image.image != VK_NULL_HANDLE && image.imageView != VK_NULL_HANDLE &&
           image.format != VK_FORMAT_UNDEFINED && image.width > 0 && image.height > 0;
}

#ifdef PRISM_ENABLE_XESS

xess_context_handle_t handleOf(void *context) {
    return static_cast<xess_context_handle_t>(context);
}

void report(const char *call, xess_result_t result) {
    radiante::err() << "[XeSS] " << call << " failed: " << static_cast<int>(result) << std::endl;
}

xess_quality_settings_t qualitySetting(XeSSQualityMode mode) {
    switch (mode) {
        case XeSSQualityMode::NativeAA: return XESS_QUALITY_SETTING_AA;
        case XeSSQualityMode::UltraQualityPlus: return XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS;
        case XeSSQualityMode::UltraQuality: return XESS_QUALITY_SETTING_ULTRA_QUALITY;
        case XeSSQualityMode::Quality: return XESS_QUALITY_SETTING_QUALITY;
        case XeSSQualityMode::Balanced: return XESS_QUALITY_SETTING_BALANCED;
        case XeSSQualityMode::Performance: return XESS_QUALITY_SETTING_PERFORMANCE;
        case XeSSQualityMode::UltraPerformance: return XESS_QUALITY_SETTING_ULTRA_PERFORMANCE;
    }
    return XESS_QUALITY_SETTING_QUALITY;
}

xess_vk_image_view_info viewInfo(const XeSSImage &image) {
    xess_vk_image_view_info info{};
    info.imageView = image.imageView;
    info.image = image.image;
    info.subresourceRange = image.subresourceRange;
    info.format = image.format;
    info.width = image.width;
    info.height = image.height;
    return info;
}

// Copies the list XeSS returned. The strings stay owned by libxess.dll and live as long as it is loaded.
bool takeNames(uint32_t count, const char *const *names, std::vector<const char *> &extensions) {
    if (count > 0 && names == nullptr) return false;
    if (count > 0) extensions.assign(names, names + count);
    return true;
}

#endif

} // namespace

XeSSWrapper::~XeSSWrapper() {
    // The module destroys the context itself once the GPU is idle; a context still here may be in a frame in flight.
    retire();
}

bool XeSSWrapper::runtimePresent() {
#if defined(PRISM_ENABLE_XESS) && defined(_WIN32)
    // libxess.dll is delay-loaded: it only exists once the player has downloaded XeSS, and the mod then loads it before
    // the renderer's library. Any XeSS call without it would fault in the delay-load stub.
    return GetModuleHandleW(L"libxess.dll") != nullptr;
#else
    return false;
#endif
}

bool XeSSWrapper::getRequiredInstanceExtensions(std::vector<const char *> &extensions, uint32_t *minVkApiVersion) {
    extensions.clear();
#ifdef PRISM_ENABLE_XESS
    if (!runtimePresent()) return false;
    uint32_t count = 0;
    const char *const *names = nullptr;
    uint32_t minVersion = 0;
    const xess_result_t result = xessVKGetRequiredInstanceExtensions(&count, &names, &minVersion);
    if (result != XESS_RESULT_SUCCESS) {
        report("xessVKGetRequiredInstanceExtensions", result);
        return false;
    }
    if (!takeNames(count, names, extensions)) return false;
    if (minVkApiVersion != nullptr) *minVkApiVersion = minVersion;
    return true;
#else
    if (minVkApiVersion != nullptr) *minVkApiVersion = 0;
    return false;
#endif
}

bool XeSSWrapper::getRequiredDeviceExtensions([[maybe_unused]] VkInstance instance,
                                              [[maybe_unused]] VkPhysicalDevice physicalDevice,
                                              std::vector<const char *> &extensions) {
    extensions.clear();
#ifdef PRISM_ENABLE_XESS
    if (!runtimePresent()) return false;
    uint32_t count = 0;
    const char *const *names = nullptr;
    const xess_result_t result = xessVKGetRequiredDeviceExtensions(instance, physicalDevice, &count, &names);
    if (result != XESS_RESULT_SUCCESS) {
        report("xessVKGetRequiredDeviceExtensions", result);
        return false;
    }
    return takeNames(count, names, extensions);
#else
    return false;
#endif
}

bool XeSSWrapper::getRequiredDeviceFeatures([[maybe_unused]] VkInstance instance,
                                            [[maybe_unused]] VkPhysicalDevice physicalDevice,
                                            [[maybe_unused]] void **features) {
#ifdef PRISM_ENABLE_XESS
    if (!runtimePresent()) return false;
    const xess_result_t result = xessVKGetRequiredDeviceFeatures(instance, physicalDevice, features);
    if (result != XESS_RESULT_SUCCESS) {
        report("xessVKGetRequiredDeviceFeatures", result);
        return false;
    }
    return true;
#else
    return false;
#endif
}

bool XeSSWrapper::queryOptimalInputResolution([[maybe_unused]] VkInstance instance,
                                              [[maybe_unused]] VkPhysicalDevice physicalDevice,
                                              [[maybe_unused]] VkDevice device,
                                              [[maybe_unused]] uint32_t outputWidth,
                                              [[maybe_unused]] uint32_t outputHeight,
                                              [[maybe_unused]] XeSSQualityMode qualityMode,
                                              [[maybe_unused]] uint32_t *outInputWidth,
                                              [[maybe_unused]] uint32_t *outInputHeight) {
#ifdef PRISM_ENABLE_XESS
    if (instance == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE || device == VK_NULL_HANDLE ||
        outputWidth == 0 || outputHeight == 0 || outInputWidth == nullptr || outInputHeight == nullptr ||
        !runtimePresent()) {
        return false;
    }

    // Only a context can answer, so one is made for the question and dropped.
    xess_context_handle_t context = nullptr;
    if (xessVKCreateContext(instance, physicalDevice, device, &context) != XESS_RESULT_SUCCESS || context == nullptr) {
        return false;
    }
    const xess_2d_t output = {outputWidth, outputHeight};
    const xess_quality_settings_t quality = qualitySetting(qualityMode);
    xess_2d_t optimal{};
    xess_2d_t minimum{};
    xess_2d_t maximum{};
    const xess_result_t result = xessGetOptimalInputResolution(context, &output, quality, &optimal, &minimum, &maximum);
    if (result != XESS_RESULT_SUCCESS || optimal.x == 0 || optimal.y == 0) {
        // Without an optimal size, the preset's fixed size is the next best answer.
        xess_2d_t fixed{};
        if (xessGetInputResolution(context, &output, quality, &fixed) == XESS_RESULT_SUCCESS && fixed.x > 0 &&
            fixed.y > 0) {
            optimal = fixed;
        }
    }
    xessDestroyContext(context);

    if (optimal.x == 0 || optimal.y == 0) return false;
    *outInputWidth = optimal.x;
    *outInputHeight = optimal.y;
    return true;
#else
    return false;
#endif
}

bool XeSSWrapper::initialize(const XeSSConfig &config) {
    retire();

    if (config.instance == VK_NULL_HANDLE || config.physicalDevice == VK_NULL_HANDLE ||
        config.device == VK_NULL_HANDLE || config.renderWidth == 0 || config.renderHeight == 0 ||
        config.displayWidth == 0 || config.displayHeight == 0) {
        radiante::err() << "[XeSS] Invalid init config" << std::endl;
        return false;
    }

#ifdef PRISM_ENABLE_XESS
    if (!runtimePresent()) {
        radiante::err() << "[XeSS] libxess.dll is not loaded" << std::endl;
        return false;
    }

    xess_context_handle_t context = nullptr;
    const xess_result_t created = xessVKCreateContext(config.instance, config.physicalDevice, config.device, &context);
    if (created != XESS_RESULT_SUCCESS || context == nullptr) {
        report("xessVKCreateContext", created);
        return false;
    }

    // Node masks, heaps and pipeline cache stay zero: XeSS allocates its own memory and builds its own pipelines.
    xess_vk_init_params_t params{};
    params.outputResolution = {config.displayWidth, config.displayHeight};
    params.qualitySetting = qualitySetting(config.qualityMode);
    params.initFlags = config.initFlags;
    const xess_result_t initialized = xessVKInit(context, &params);
    if (initialized != XESS_RESULT_SUCCESS) {
        report("xessVKInit", initialized);
        xessDestroyContext(context); // nothing has been recorded with it
        return false;
    }

    // Not fatal: XeSS then keeps its default scale.
    const xess_result_t scaled = xessSetVelocityScale(context, config.velocityScaleX, config.velocityScaleY);
    if (scaled != XESS_RESULT_SUCCESS) report("xessSetVelocityScale", scaled);

    context_ = context;
    return true;
#else
    radiante::err() << "[XeSS] XeSS support is not enabled at compile time" << std::endl;
    return false;
#endif
}

bool XeSSWrapper::dispatch(const XeSSInput &input) {
    if (context_ == nullptr || input.commandBuffer == VK_NULL_HANDLE) return false;
    if (!usable(input.colorTexture) || !usable(input.velocityTexture) || !usable(input.outputTexture) ||
        input.inputWidth == 0 || input.inputHeight == 0) {
        radiante::err() << "[XeSS] Invalid execute input" << std::endl;
        return false;
    }

#ifdef PRISM_ENABLE_XESS
    xess_vk_execute_params_t params{};
    params.colorTexture = viewInfo(input.colorTexture);
    params.velocityTexture = viewInfo(input.velocityTexture);
    params.depthTexture = viewInfo(input.depthTexture);
    params.exposureScaleTexture = viewInfo(input.exposureTexture);
    params.responsivePixelMaskTexture = viewInfo(input.responsiveMaskTexture);
    params.outputTexture = viewInfo(input.outputTexture);
    params.jitterOffsetX = input.jitterOffsetX;
    params.jitterOffsetY = input.jitterOffsetY;
    params.exposureScale = input.exposureScale;
    params.resetHistory = input.resetHistory ? 1u : 0u;
    params.inputWidth = input.inputWidth;
    params.inputHeight = input.inputHeight;

    const xess_result_t result = xessVKExecute(handleOf(context_), input.commandBuffer, &params);
    if (result != XESS_RESULT_SUCCESS) {
        report("xessVKExecute", result);
        return false;
    }
    return true;
#else
    return false;
#endif
}

void XeSSWrapper::destroy() {
#ifdef PRISM_ENABLE_XESS
    if (context_ != nullptr) xessDestroyContext(handleOf(context_));
#endif
    context_ = nullptr;
}

void XeSSWrapper::retire() {
#ifdef PRISM_ENABLE_XESS
    if (context_ != nullptr) {
        gpu::retire::later([context = handleOf(context_)] { xessDestroyContext(context); });
    }
#endif
    context_ = nullptr;
}

} // namespace prism
