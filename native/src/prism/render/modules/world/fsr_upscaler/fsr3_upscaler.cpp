#include "prism/render/modules/world/fsr_upscaler/fsr3_upscaler.hpp"

#include "prism/util/logging.hpp"

#include <ostream>

#ifdef PRISM_ENABLE_FFX_UPSCALER

#include "prism/gpu/retire.hpp"
#include "prism/render/modules/world/fsr_upscaler/fsr_setup.hpp"

#include <ffx_api/ffx_api.hpp>
#include <ffx_api/ffx_upscale.hpp>
#include <ffx_api/vk/ffx_api_vk.hpp>

#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

namespace prism {
namespace {

const char *returnCodeName(ffx::ReturnCode code) {
    switch (code) {
        case ffx::ReturnCode::Ok: return "Ok";
        case ffx::ReturnCode::Error: return "Error";
        case ffx::ReturnCode::ErrorUnknownDesctype: return "ErrorUnknownDesctype";
        case ffx::ReturnCode::ErrorRuntimeError: return "ErrorRuntimeError";
        case ffx::ReturnCode::ErrorNoProvider: return "ErrorNoProvider";
        case ffx::ReturnCode::ErrorMemory: return "ErrorMemory";
        case ffx::ReturnCode::ErrorParameter: return "ErrorParameter";
    }
    return "Unknown";
}

// "Name (number)", as the error lines show a FidelityFX result.
std::ostream &operator<<(std::ostream &stream, ffx::ReturnCode code) {
    return stream << returnCodeName(code) << " (" << static_cast<uint32_t>(code) << ")";
}

void destroyContext(ffx::Context context) {
    ffx::ReturnCode result = ffx::DestroyContext(context);
    if (result != ffx::ReturnCode::Ok) {
        radiante::err() << "FSR3: DestroyContext failed with error: " << result << std::endl;
    }
}

// Destroyed once no frame in flight can still be running FSR's passes.
void retireContext(ffx::Context context) {
    gpu::retire::later([context] { destroyContext(context); });
}

bool hasDeviceExtension(VkPhysicalDevice physicalDevice, const char *name) {
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, extensions.data());
    extensions.resize(count);
    return std::ranges::any_of(extensions, [name](const VkExtensionProperties &extension) {
        return std::strcmp(extension.extensionName, name) == 0;
    });
}

// The context, or null with the reason in the error log.
ffx::Context createContext(const FSR3Upscaler::Config &config) {
    {
        ffx::QueryDescGetVersions versions{};
        versions.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
#ifdef _WIN32
        // On Windows FidelityFX takes this device for a D3D12 one (and crashes on a VkDevice); without one only the
        // providers built in here are counted.
        versions.device = nullptr;
#else
        versions.device = config.device;
#endif
        uint64_t versionCount = 0;
        versions.outputCount = &versionCount;
        ffx::Query(versions);
        if (versionCount == 0) radiante::err() << "FSR3: No alternative versions available" << std::endl;
    }

    if (!hasDeviceExtension(config.physicalDevice, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME)) {
        radiante::err() << "FSR3: Missing required extension: " << VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME << std::endl;
    }

    if (fsr::customVkGetDeviceProcAddr(config.device, "vkCreateComputePipelines") == nullptr ||
        fsr::customVkGetDeviceProcAddr(config.device, "vkCmdDispatch") == nullptr) {
        radiante::err() << "FSR3 ERROR: Failed to get required Vulkan function pointers" << std::endl;
        return nullptr;
    }

    ffx::CreateBackendVKDesc backend{};
    backend.vkDevice = config.device;
    backend.vkPhysicalDevice = config.physicalDevice;
    backend.vkDeviceProcAddr = &fsr::customVkGetDeviceProcAddr;

    ffx::CreateContextDescUpscale create{};
    create.maxUpscaleSize = {config.maxDisplayWidth, config.maxDisplayHeight};
    create.maxRenderSize = {config.maxRenderWidth, config.maxRenderHeight};
    create.flags = (config.hdr ? FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE : 0u) |
                   (config.depthInfinite ? FFX_UPSCALE_ENABLE_DEPTH_INFINITE : 0u) |
                   (config.depthInverted ? FFX_UPSCALE_ENABLE_DEPTH_INVERTED : 0u) |
                   (config.autoExposure ? FFX_UPSCALE_ENABLE_AUTO_EXPOSURE : 0u);
    create.fpMessage = &fsr::messageCallback;

    ffx::Context context = nullptr;
    ffx::ReturnCode result = ffx::CreateContext(context, nullptr, create, backend);
    if (result != ffx::ReturnCode::Ok) {
        radiante::err() << "FSR3: CreateContext failed with error: " << result << std::endl;
        radiante::err() << "FSR3: Device=" << config.device << std::endl;
        radiante::err() << "FSR3: PhysicalDevice=" << config.physicalDevice << std::endl;
        if (result == ffx::ReturnCode::ErrorRuntimeError) {
            radiante::err() << "FSR3: Runtime error - check Vulkan validation layers for details" << std::endl;
        }
        return nullptr;
    }

    ffx::InitHelper<ffxQueryGetProviderVersion> provider{};
    if (ffx::Query(context, provider) == ffx::ReturnCode::Ok && provider.versionName != nullptr) {
        radiante::out() << "FSR3: Active provider = " << provider.versionName << " (0x" << std::hex
                        << provider.versionId << std::dec << ")" << std::endl;
    }
    return context;
}

FfxApiResource describe(const FSR3Upscaler::Image &image,
                        uint32_t width,
                        uint32_t height,
                        uint32_t state,
                        uint32_t usage) {
    FfxApiResource resource{};
    resource.resource = reinterpret_cast<void *>(image.image);
    resource.description.type = FFX_API_RESOURCE_TYPE_TEXTURE2D;
    resource.description.format = fsr::vkToFfxFormat(image.format);
    resource.description.width = width;
    resource.description.height = height;
    resource.description.depth = 1;
    resource.description.mipCount = 1;
    resource.description.usage = usage;
    resource.state = state;
    return resource;
}

} // namespace

FSR3Upscaler::~FSR3Upscaler() {
    if (context_ != nullptr) retireContext(context_);
}

bool FSR3Upscaler::initialize(const Config &config) {
    if (context_ != nullptr) retireContext(std::exchange(context_, nullptr));
    context_ = createContext(config);
    if (context_ == nullptr) {
        radiante::err() << "FSR3Upscaler::initialize: Failed to create FFX context" << std::endl;
        return false;
    }
    return true;
}

void FSR3Upscaler::dispatch(const Frame &frame) {
    if (context_ == nullptr) return;

    ffx::DispatchDescUpscale upscale{};
    upscale.commandList = frame.commandBuffer;
    upscale.color = describe(frame.color, frame.renderWidth, frame.renderHeight,
                             FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ, FFX_API_RESOURCE_USAGE_READ_ONLY);
    upscale.depth = describe(frame.depth, frame.renderWidth, frame.renderHeight,
                             FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ, FFX_API_RESOURCE_USAGE_READ_ONLY);
    upscale.motionVectors = describe(frame.motionVectors, frame.renderWidth, frame.renderHeight,
                                     FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ, FFX_API_RESOURCE_USAGE_READ_ONLY);
    // No exposure, reactive or transparency-and-composition masks: those stay empty.
    upscale.output = describe(frame.output, frame.displayWidth, frame.displayHeight,
                              FFX_API_RESOURCE_STATE_UNORDERED_ACCESS, FFX_API_RESOURCE_USAGE_UAV);
    upscale.jitterOffset = {frame.jitterX, frame.jitterY};
    upscale.motionVectorScale = {frame.motionVectorScaleX, frame.motionVectorScaleY};
    upscale.renderSize = {frame.renderWidth, frame.renderHeight};
    upscale.upscaleSize = {frame.displayWidth, frame.displayHeight};
    upscale.enableSharpening = frame.enableSharpening;
    upscale.sharpness = frame.sharpness;
    upscale.frameTimeDelta = frame.frameTimeDelta;
    upscale.preExposure = frame.preExposure;
    upscale.reset = frame.reset;
    upscale.cameraNear = frame.cameraNear;
    upscale.cameraFar = frame.cameraFar;
    upscale.cameraFovAngleVertical = frame.cameraFovVertical;
    upscale.viewSpaceToMetersFactor = 0.0f;
    upscale.flags = 0;

    ffx::ReturnCode result = ffx::Dispatch(context_, upscale);
    if (result != ffx::ReturnCode::Ok) radiante::err() << "FSR3 DISPATCH FAILED: " << result << std::endl;
}

void FSR3Upscaler::destroy() {
    if (context_ != nullptr) destroyContext(std::exchange(context_, nullptr));
}

} // namespace prism

#else

namespace prism {

FSR3Upscaler::~FSR3Upscaler() = default;

bool FSR3Upscaler::initialize(const Config &) {
    radiante::err() << "FSR3Upscaler::initialize: FSR3 support not compiled in" << std::endl;
    return false;
}

void FSR3Upscaler::dispatch(const Frame &) {}

void FSR3Upscaler::destroy() {}

} // namespace prism

#endif
