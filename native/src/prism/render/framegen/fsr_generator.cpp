// FSR frame generation through the FidelityFX API, without its swapchain. Descends from Radiance 0.1.6's
// fsr_frame_generation.cpp (GPL-3.0).
#include "prism/render/framegen/generator.hpp"

#ifdef PRISM_ENABLE_FFX_UPSCALER
#    include "prism/gpu/device.hpp"
#    include "prism/gpu/image.hpp"
#    include "prism/gpu/physical_device.hpp"
#    include "prism/render/render_framework.hpp"

#    include <ffx_api/ffx_framegeneration.hpp>
#    include <ffx_api/vk/ffx_api_vk.hpp>

#    include <algorithm>
#    include <cstring>
#    include <ostream>
#endif

namespace framegen {

#ifdef PRISM_ENABLE_FFX_UPSCALER
namespace {

// FidelityFX asks for instance functions through the device lookup too.
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL deviceOrInstanceProcAddr(VkDevice device, const char *name) {
    if (PFN_vkVoidFunction function = vkGetDeviceProcAddr(device, name)) return function;
    const VkInstance instance = volkGetLoadedInstance();
    return instance != VK_NULL_HANDLE ? vkGetInstanceProcAddr(instance, name) : nullptr;
}

FfxApiResource ffxResource(gpu::Image &image, FfxApiResourceState state, FfxApiResourceUsage usage) {
    FfxApiResource resource{};
    resource.resource = reinterpret_cast<void *>(image.vkImage());
    resource.description.type = FFX_API_RESOURCE_TYPE_TEXTURE2D;
    resource.description.format = ffxApiGetSurfaceFormatVK(image.vkFormat());
    resource.description.width = image.width();
    resource.description.height = image.height();
    resource.description.depth = 1;
    resource.description.mipCount = 1;
    resource.description.usage = usage;
    resource.state = state;
    return resource;
}

FfxApiResource ffxInput(gpu::Image &image) {
    return ffxResource(image, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ, FFX_API_RESOURCE_USAGE_READ_ONLY);
}

class FsrGenerator final : public Generator {
  public:
    explicit FsrGenerator(std::weak_ptr<Framework> framework) : framework_(std::move(framework)) {}

    ~FsrGenerator() override {
        release();
    }

    // FSR makes one frame per rendered one.
    bool generate(VkCommandBuffer cmd,
                  const GenerationInputs &inputs,
                  gpu::Image &output,
                  uint32_t count,
                  uint32_t index) override {
        if (count != 1 || index != 1 || !ensureContext(inputs)) return false;
        const uint64_t frameId = ++frameId_;
        const FfxApiRect2D wholeFrame{0, 0, static_cast<int32_t>(inputs.displayWidth),
                                      static_cast<int32_t>(inputs.displayHeight)};

        ffx::ConfigureDescFrameGeneration configure{};
        configure.frameGenerationEnabled = true;
        configure.allowAsyncWorkloads = false;
        configure.flags = FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY;
        if (inputs.hudless != nullptr) configure.HUDLessColor = ffxInput(*inputs.hudless);
        configure.generationRect = wholeFrame;
        configure.frameID = frameId;
        if (ffx::Configure(context_, configure) != ffx::ReturnCode::Ok) return false;

        const CameraFrame &camera = inputs.camera;
        ffx::DispatchDescFrameGenerationPrepare prepare{};
        prepare.frameID = frameId;
        prepare.flags = 0;
        prepare.commandList = cmd;
        prepare.renderSize = {inputs.depth->width(), inputs.depth->height()};
        prepare.jitterOffset = {-camera.jitter.x, -camera.jitter.y};
        prepare.motionVectorScale = {1.0f, 1.0f}; // in pixels, as the FSR upscaler takes them
        prepare.frameTimeDelta = inputs.frameTimeMs;
        prepare.unused_reset = inputs.reset;
        prepare.cameraNear = kCameraNear;
        prepare.cameraFar = kCameraFar;
        prepare.cameraFovAngleVertical = camera.verticalFov;
        prepare.viewSpaceToMetersFactor = 1.0f;
        prepare.depth = ffxInput(*inputs.depth);
        prepare.motionVectors = ffxInput(*inputs.motionVectors);
        ffx::DispatchDescFrameGenerationPrepareCameraInfo cameraInfo{};
        std::memcpy(cameraInfo.cameraPosition, &camera.position, sizeof(cameraInfo.cameraPosition));
        std::memcpy(cameraInfo.cameraUp, &camera.up, sizeof(cameraInfo.cameraUp));
        std::memcpy(cameraInfo.cameraRight, &camera.right, sizeof(cameraInfo.cameraRight));
        std::memcpy(cameraInfo.cameraForward, &camera.forward, sizeof(cameraInfo.cameraForward));
        if (ffx::Dispatch(context_, prepare, cameraInfo) != ffx::ReturnCode::Ok) return false;

        ffx::DispatchDescFrameGeneration dispatch{};
        dispatch.commandList = cmd;
        dispatch.presentColor = ffxInput(*inputs.finalColor);
        dispatch.outputs[0] = ffxResource(output, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS, FFX_API_RESOURCE_USAGE_UAV);
        dispatch.numGeneratedFrames = 1;
        dispatch.reset = inputs.reset;
        dispatch.backbufferTransferFunction = FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
        dispatch.minMaxLuminance[0] = 0.0f;
        dispatch.minMaxLuminance[1] = 1000.0f;
        dispatch.generationRect = wholeFrame;
        dispatch.frameID = frameId;
        return ffx::Dispatch(context_, dispatch) == ffx::ReturnCode::Ok;
    }

    void release() override {
        if (context_ != nullptr) {
            // The context's own images may still be in use by frames in flight.
            if (auto framework = framework_.lock()) framework->waitRenderQueueIdle();
            ffx::DestroyContext(context_);
            context_ = nullptr;
        }
        display_ = {};
        render_ = {};
        format_ = VK_FORMAT_UNDEFINED;
        hudlessFormat_ = VK_FORMAT_UNDEFINED;
        frameId_ = 0;
    }

  private:
    // The context is made for one display size, render size and pair of formats; another makes it again.
    bool ensureContext(const GenerationInputs &inputs) {
        const VkFormat format = inputs.finalColor->vkFormat();
        const VkFormat hudlessFormat = inputs.hudless != nullptr ? inputs.hudless->vkFormat() : VK_FORMAT_UNDEFINED;
        if (context_ != nullptr && display_.width == inputs.displayWidth && display_.height == inputs.displayHeight &&
            render_.width == inputs.renderWidth && render_.height == inputs.renderHeight && format_ == format &&
            hudlessFormat_ == hudlessFormat) {
            return true;
        }
        release();

        auto framework = framework_.lock();
        if (framework == nullptr) return false;
        ffx::CreateBackendVKDesc backend{};
        backend.vkDevice = framework->device()->vkDevice();
        backend.vkPhysicalDevice = framework->physicalDevice()->vkPhysicalDevice();
        backend.vkDeviceProcAddr = &deviceOrInstanceProcAddr;

        ffx::CreateContextDescFrameGeneration create{};
        create.displaySize = {inputs.displayWidth, inputs.displayHeight};
        create.maxRenderSize = {std::max(inputs.renderWidth, inputs.displayWidth),
                                std::max(inputs.renderHeight, inputs.displayHeight)};
        create.backBufferFormat = ffxApiGetSurfaceFormatVK(format);
        create.flags = 0;

        ffx::ReturnCode result;
        if (inputs.hudless != nullptr) {
            ffx::CreateContextDescFrameGenerationHudless hudless{};
            hudless.hudlessBackBufferFormat = ffxApiGetSurfaceFormatVK(hudlessFormat);
            result = ffx::CreateContext(context_, nullptr, create, hudless, backend);
        } else {
            result = ffx::CreateContext(context_, nullptr, create, backend);
        }
        if (result != ffx::ReturnCode::Ok) {
            out() << "FSR frame generation could not be created (" << static_cast<uint32_t>(result) << ")"
                  << std::endl;
            context_ = nullptr;
            return false;
        }
        display_ = {inputs.displayWidth, inputs.displayHeight};
        render_ = {inputs.renderWidth, inputs.renderHeight};
        format_ = format;
        hudlessFormat_ = hudlessFormat;
        out() << "FSR frame generation created for " << display_.width << "x" << display_.height << " (backbuffer "
              << format << " -> ffx " << ffxApiGetSurfaceFormatVK(format) << ", hudless " << hudlessFormat
              << " -> ffx " << ffxApiGetSurfaceFormatVK(hudlessFormat) << ")" << std::endl;
        return true;
    }

    std::weak_ptr<Framework> framework_;
    ffx::Context context_ = nullptr;
    VkExtent2D display_{};
    VkExtent2D render_{};
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkFormat hudlessFormat_ = VK_FORMAT_UNDEFINED;
    // FidelityFX expects consecutive ids; a gap restarts its history.
    uint64_t frameId_ = 0;
};

} // namespace

std::unique_ptr<Generator> makeFsrGenerator(std::weak_ptr<Framework> framework) {
    return std::make_unique<FsrGenerator>(std::move(framework));
}
#else
std::unique_ptr<Generator> makeFsrGenerator(std::weak_ptr<Framework>) {
    return nullptr;
}
#endif

} // namespace framegen
