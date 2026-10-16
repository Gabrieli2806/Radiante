// DLSS frame generation through NGX, without Streamline.
#include "prism/render/framegen/generator.hpp"

#include "prism/gpu/image.hpp"
#include "prism/render/modules/world/dlss/dlss_wrapper.hpp"
#include "prism/render/render_framework.hpp"

#include <nvsdk_ngx_defs_dlssg.h>
#include <nvsdk_ngx_helpers_dlssg_vk.h>
#include <nvsdk_ngx_helpers_vk.h>

#include <glm/gtc/type_ptr.hpp>

#include <cstring>
#include <ostream>

namespace framegen {
namespace {

NVSDK_NGX_Resource_VK ngxResource(gpu::Image &image, bool writable) {
    return NVSDK_NGX_Create_ImageView_Resource_VK(image.vkImageView(), image.vkImage(),
                                                  {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}, image.vkFormat(),
                                                  image.width(), image.height(), writable);
}

void copyMatrix(float (&to)[4][4], const glm::mat4 &from) {
    std::memcpy(to, glm::value_ptr(from), sizeof(to));
}

void copyVector(float (&to)[3], const glm::vec3 &from) {
    std::memcpy(to, glm::value_ptr(from), sizeof(to));
}

class DlssGenerator final : public Generator {
  public:
    DlssGenerator(std::shared_ptr<NgxContext> ngx, std::weak_ptr<Framework> framework)
        : ngx_(std::move(ngx)), framework_(std::move(framework)) {}

    ~DlssGenerator() override {
        release();
    }

    bool generate(VkCommandBuffer cmd,
                  const GenerationInputs &inputs,
                  gpu::Image &output,
                  uint32_t count,
                  uint32_t index) override {
        if (ngx_ == nullptr || !ensureFeature(cmd, inputs)) return false;

        NVSDK_NGX_Resource_VK backbuffer = ngxResource(*inputs.finalColor, false);
        NVSDK_NGX_Resource_VK depth = ngxResource(*inputs.depth, false);
        NVSDK_NGX_Resource_VK motion = ngxResource(*inputs.motionVectors, false);
        NVSDK_NGX_Resource_VK generated = ngxResource(output, true);
        NVSDK_NGX_Resource_VK hudless{};

        NVSDK_NGX_VK_DLSSG_Eval_Params eval{};
        eval.pBackbuffer = &backbuffer;
        eval.pDepth = &depth;
        eval.pMVecs = &motion;
        eval.pOutputInterpFrame = &generated;
        if (inputs.hudless != nullptr) {
            hudless = ngxResource(*inputs.hudless, false);
            eval.pHudless = &hudless;
        }

        const CameraFrame &camera = inputs.camera;
        NVSDK_NGX_DLSSG_Opt_Eval_Params options{};
        options.multiFrameCount = count;
        options.multiFrameIndex = index;
        copyMatrix(options.cameraViewToClip, camera.viewToClip);
        copyMatrix(options.clipToCameraView, camera.clipToView);
        copyMatrix(options.clipToLensClip, glm::mat4(1.0f));
        copyMatrix(options.clipToPrevClip, camera.clipToPrevClip);
        copyMatrix(options.prevClipToClip, camera.prevClipToClip);
        options.jitterOffset[0] = camera.jitter.x;
        options.jitterOffset[1] = camera.jitter.y;
        // The traced motion vectors are in pixels; DLSS frame generation wants them normalised.
        options.mvecScale[0] = 1.0f / static_cast<float>(inputs.motionVectors->width());
        options.mvecScale[1] = 1.0f / static_cast<float>(inputs.motionVectors->height());
        options.cameraPinholeOffset[0] = 0.0f;
        options.cameraPinholeOffset[1] = 0.0f;
        copyVector(options.cameraPos, camera.position);
        copyVector(options.cameraUp, camera.up);
        copyVector(options.cameraRight, camera.right);
        copyVector(options.cameraFwd, camera.forward);
        options.cameraNear = kCameraNear;
        options.cameraFar = kCameraFar;
        options.cameraFOV = camera.verticalFov;
        options.cameraAspectRatio = camera.aspect;
        options.colorBuffersHDR = false;
        options.depthInverted = false;
        options.cameraMotionIncluded = true;
        options.reset = inputs.reset;
        options.automodeOverrideReset = false;
        options.notRenderingGameFrames = false;
        options.orthoProjection = false;
        options.motionVectorsInvalidValue = 0.0f;
        options.motionVectorsDilated = false;
        options.menuDetectionEnabled = true;
        options.mvecsSubrectSize = {inputs.motionVectors->width(), inputs.motionVectors->height()};
        options.depthSubrectSize = {inputs.depth->width(), inputs.depth->height()};
        options.backbufferSubrectSize = {inputs.displayWidth, inputs.displayHeight};
        if (eval.pHudless != nullptr) options.hudLessSubrectSize = {inputs.displayWidth, inputs.displayHeight};

        const NVSDK_NGX_Result result =
            checkNgxResult(NGX_VK_EVALUATE_DLSSG(cmd, feature_, ngx_->parameters(), &eval, &options),
                           "NGX_VK_EVALUATE_DLSSG", __LINE__);
        return NVSDK_NGX_SUCCEED(result);
    }

    void release() override {
        if (feature_ != nullptr) {
            // The feature's own images may still be in use by frames in flight.
            if (auto framework = framework_.lock()) framework->waitRenderQueueIdle();
            NVSDK_NGX_VULKAN_ReleaseFeature(feature_);
            feature_ = nullptr;
        }
        display_ = {};
        render_ = {};
        format_ = VK_FORMAT_UNDEFINED;
    }

  private:
    // The feature is made for one display size, render size and backbuffer format; another makes it again.
    bool ensureFeature(VkCommandBuffer cmd, const GenerationInputs &inputs) {
        const VkFormat format = inputs.finalColor->vkFormat();
        if (feature_ != nullptr && display_.width == inputs.displayWidth && display_.height == inputs.displayHeight &&
            render_.width == inputs.renderWidth && render_.height == inputs.renderHeight && format_ == format) {
            return true;
        }
        release();

        NVSDK_NGX_Parameter *parameters = ngx_->parameters();
        if (parameters == nullptr) return false;
        NVSDK_NGX_DLSSG_Create_Params create{};
        create.Width = inputs.displayWidth;
        create.Height = inputs.displayHeight;
        create.NativeBackbufferFormat = static_cast<unsigned int>(format);
        create.RenderWidth = inputs.renderWidth;
        create.RenderHeight = inputs.renderHeight;
        create.DynamicResolutionScaling = false;
        parameters->Set(NVSDK_NGX_DLSSG_Parameter_Width, create.Width);
        parameters->Set(NVSDK_NGX_DLSSG_Parameter_Height, create.Height);
        parameters->Set(NVSDK_NGX_DLSSG_Parameter_InternalWidth, create.RenderWidth);
        parameters->Set(NVSDK_NGX_DLSSG_Parameter_InternalHeight, create.RenderHeight);
        parameters->Set(NVSDK_NGX_DLSSG_Parameter_DynamicResolution, 0u);

        const NVSDK_NGX_Result result = checkNgxResult(NGX_VK_CREATE_DLSSG(cmd, 1, 1, &feature_, parameters, &create),
                                                       "NGX_VK_CREATE_DLSSG", __LINE__);
        if (NVSDK_NGX_FAILED(result)) {
            feature_ = nullptr;
            return false;
        }
        display_ = {inputs.displayWidth, inputs.displayHeight};
        render_ = {inputs.renderWidth, inputs.renderHeight};
        format_ = format;
        out() << "DLSS frame generation created for " << display_.width << "x" << display_.height << std::endl;
        return true;
    }

    std::shared_ptr<NgxContext> ngx_;
    std::weak_ptr<Framework> framework_;
    NVSDK_NGX_Handle *feature_ = nullptr;
    VkExtent2D display_{};
    VkExtent2D render_{};
    VkFormat format_ = VK_FORMAT_UNDEFINED;
};

} // namespace

std::unique_ptr<Generator> makeDlssGenerator(std::shared_ptr<NgxContext> ngx, std::weak_ptr<Framework> framework) {
    return std::make_unique<DlssGenerator>(std::move(ngx), std::move(framework));
}

} // namespace framegen
