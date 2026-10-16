// Frame generation without Streamline, with DLSS and FSR backends (dlss_generator.cpp, fsr_generator.cpp). Minecraft
// owns the swapchain here, so the capture and present paths go through its own present.
#include "prism/render/framegen/native_frame_generation.hpp"

#include "common/shared.hpp"
#include "prism/gpu/gpu.hpp"
#include "prism/render/buffers.hpp"
#include "prism/render/modules/world/dlss/dlss_module.hpp"
#include "prism/render/modules/world/dlss/dlss_wrapper.hpp"
#include "prism/render/render_framework.hpp"
#include "prism/render/renderer.hpp"
#include "prism/util/logging.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <ostream>
#include <string>
#include <utility>

namespace framegen {
namespace {

// DLSS reports how many frames the GPU can add (up to 5 on RTX 50, a 6x multiplier); this only bounds the request.
constexpr uint32_t kMaxGeneratedFrames = 7;
constexpr uint32_t kDepthPassTables = 4;

constexpr VkImageUsageFlags kColorUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                          VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
constexpr VkImageUsageFlags kDepthPassUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

// linear_to_device_depth.comp's push constants.
struct DepthPassConstants {
    float cameraNear;
    float cameraFar;
    uint32_t width;
    uint32_t height;
    float jitterX;
    float jitterY;
};

const char *backendName(NativeFrameGeneration::Backend backend) {
    switch (backend) {
        case NativeFrameGeneration::Backend::Dlss: return "DLSS";
        case NativeFrameGeneration::Backend::Fsr: return "FSR";
        default: return "none";
    }
}

glm::vec3 safeNormalize(const glm::vec3 &value, const glm::vec3 &fallback) {
    const float length = glm::length(value);
    if (!(length > 1e-6f)) return fallback;
    return value / length;
}

// Development: RADIANTE_FG_SHOW_GENERATED shows only generated frames, to judge them on screen.
bool showOnlyGenerated() {
    static const bool only = std::getenv("RADIANTE_FG_SHOW_GENERATED") != nullptr;
    return only;
}

// Layout transitions recorded together, as one barrier. The images are shared with code whose stages are not known
// here (Minecraft, the world pipeline, NGX, FidelityFX), so every transition, like the optional memory barrier, waits
// on all earlier work and holds back all later work.
class Transitions {
  public:
    explicit Transitions(VkCommandBuffer cmd) : cmd_(cmd) {}

    Transitions &memory() {
        memory_ = true;
        return *this;
    }

    Transitions &image(VkImage image, VkImageLayout from, VkImageLayout to) {
        // Two transitions of one image in a single barrier would not be ordered.
        const auto queued = images_.begin() + count_;
        if (count_ == images_.size() ||
            std::any_of(images_.begin(), queued, [image](const auto &barrier) { return barrier.image == image; })) {
            record();
        }
        VkImageMemoryBarrier2 &barrier = images_[count_++];
        barrier = VkImageMemoryBarrier2{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        barrier.srcAccessMask = kAnyAccess;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        barrier.dstAccessMask = kAnyAccess;
        barrier.oldLayout = from;
        barrier.newLayout = to;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        return *this;
    }

    void record() {
        if (!memory_ && count_ == 0) return;
        VkMemoryBarrier2 everything{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        everything.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        everything.srcAccessMask = kAnyAccess;
        everything.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        everything.dstAccessMask = kAnyAccess;
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.memoryBarrierCount = memory_ ? 1 : 0;
        dependency.pMemoryBarriers = &everything;
        dependency.imageMemoryBarrierCount = count_;
        dependency.pImageMemoryBarriers = images_.data();
        vkCmdPipelineBarrier2(cmd_, &dependency);
        memory_ = false;
        count_ = 0;
    }

  private:
    static constexpr VkAccessFlags2 kAnyAccess = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;

    VkCommandBuffer cmd_;
    bool memory_ = false;
    std::array<VkImageMemoryBarrier2, 16> images_{};
    uint32_t count_ = 0;
};

std::shared_ptr<gpu::DescriptorTable> makeDepthPassTable(const std::shared_ptr<gpu::Device> &device) {
    const auto storageImage = [](uint32_t binding) {
        return VkDescriptorSetLayoutBinding{
            .binding = binding,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        };
    };
    return gpu::DescriptorTableBuilder{}
        .beginDescriptorLayoutSet()
        .beginDescriptorLayoutSetBinding()
        .defineDescriptorLayoutSetBinding(storageImage(0)) // traced hit distance
        .defineDescriptorLayoutSetBinding(storageImage(1)) // device depth
        .defineDescriptorLayoutSetBinding(storageImage(2)) // motion vectors
        .defineDescriptorLayoutSetBinding(storageImage(3)) // motion vectors without NaNs
        .endDescriptorLayoutSetBinding()
        .endDescriptorLayoutSet()
        .definePushConstant(VkPushConstantRange{
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .offset = 0,
            .size = sizeof(DepthPassConstants),
        })
        .build(device);
}

// Development: RADIANTE_FG_DUMP=<dir> writes the first generated frame and the real one of eight consecutive frames,
// from the 300th shown with generated frames on, as raw texels: <n>_gen_<w>x<h>.rgba and <n>_real_<w>x<h>.rgba.
class FrameDump {
  public:
    // Records copies of this frame's two images into the readback buffer, once they are written.
    void record(Transitions &barriers,
                VkCommandBuffer cmd,
                Framework &framework,
                gpu::Image &generated,
                gpu::Image &real,
                uint32_t width,
                uint32_t height,
                VkFormat format) {
        if (dir_ == nullptr || ++frames_ < kFirstFrame || dumped_ >= kFrames) return;
        const size_t bytes = static_cast<size_t>(width) * height * gpu::formatToByte(format);
        if (buffer_ == nullptr || bytes_ != bytes || width_ != width || height_ != height) {
            buffer_ = gpu::HostVisibleBuffer::create(framework.vma(), framework.device(), bytes * 2,
                                                     VK_BUFFER_USAGE_TRANSFER_DST_BIT);
            bytes_ = bytes;
            width_ = width;
            height_ = height;
        }
        barriers.memory().record();
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {width, height, 1};
        vkCmdCopyImageToBuffer(cmd, generated.vkImage(), VK_IMAGE_LAYOUT_GENERAL, buffer_->vkBuffer(), 1, &region);
        region.bufferOffset = bytes;
        vkCmdCopyImageToBuffer(cmd, real.vkImage(), VK_IMAGE_LAYOUT_GENERAL, buffer_->vkBuffer(), 1, &region);
        barriers.memory();
        dumped_++;
        pending_ = true;
    }

    // Writes what the last record copied, waiting for the GPU to have made it.
    void writePending(const std::weak_ptr<Framework> &framework) {
        if (!pending_) return;
        pending_ = false;
        if (auto locked = framework.lock()) locked->waitRenderQueueIdle();
        buffer_->downloadFromBuffer();
        const auto *texels = static_cast<const char *>(buffer_->mappedPtr());
        const char *names[2] = {"gen", "real"};
        for (size_t k = 0; k < 2; k++) {
            const std::string path = std::string(dir_) + "/" + std::to_string(dumped_) + "_" + names[k] + "_" +
                                     std::to_string(width_) + "x" + std::to_string(height_) + ".rgba";
            std::ofstream(path, std::ios::binary).write(texels + k * bytes_, static_cast<std::streamsize>(bytes_));
        }
    }

  private:
    static constexpr int kFirstFrame = 300;
    static constexpr int kFrames = 8;

    const char *dir_ = std::getenv("RADIANTE_FG_DUMP");
    int frames_ = 0;
    int dumped_ = 0;
    bool pending_ = false;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    size_t bytes_ = 0;
    std::shared_ptr<gpu::HostVisibleBuffer> buffer_;
};

FrameDump &frameDump() {
    static FrameDump *dump = new FrameDump();
    return *dump;
}

} // namespace

std::ostream &out() {
    return radiante::out() << "[Frame Generation] ";
}

NativeFrameGeneration &NativeFrameGeneration::instance() {
    // Never destroyed: it holds Vulkan objects and a thread, and at process exit the device may already be gone.
    static NativeFrameGeneration *instance = new NativeFrameGeneration();
    return *instance;
}

bool NativeFrameGeneration::probe() {
    if (probed_) return backend_ != Backend::Off;
    if (!Renderer::is_initialized()) return false;
    auto framework = Renderer::instance().framework();
    if (framework == nullptr || framework->device() == nullptr) return false;
    probed_ = true;
    framework_ = framework;

    // Both backends are looked for: the player may pick FSR frame generation on an NVIDIA card, next to DLSS
    // upscaling or anything else (the two do not depend on each other).
    auto ngx = DLSSModule::ngxContext();
    dlssAvailable_ = ngx != nullptr && framework->device()->isDlssFrameGenerationDeviceExtensionsCompatible() &&
                     NVSDK_NGX_SUCCEED(ngx->queryDlssFrameGenerationAvailable());
    dlssMaxFrames_ = 0;
    if (dlssAvailable_) {
        const uint32_t reported = ngx->queryDlssFrameGenerationMaxFrames();
        dlssMaxFrames_ = std::clamp<uint32_t>(reported, 1, kMaxGeneratedFrames);
        out() << "DLSS reports up to " << reported << " generated frames" << std::endl;
    }
#ifdef PRISM_ENABLE_FFX_UPSCALER
    fsrAvailable_ = framework->device()->isFsrFrameGenerationDeviceExtensionsCompatible();
#endif
    out() << "available: DLSS " << (dlssAvailable_ ? std::to_string(dlssMaxFrames_ + 1) + "x" : "no") << ", FSR "
          << (fsrAvailable_ ? "2x" : "no") << std::endl;

    // Development: RADIANTE_FRAME_GENERATION=dlss or fsr picks the backend whatever the settings say.
    if (const char *forced = std::getenv("RADIANTE_FRAME_GENERATION")) {
        if (std::strcmp(forced, "fsr") == 0) preference_ = Backend::Fsr;
        if (std::strcmp(forced, "dlss") == 0) preference_ = Backend::Dlss;
    }
    selectBackend();
    return backend_ != Backend::Off;
}

void NativeFrameGeneration::selectBackend() {
    Backend wanted = Backend::Off;
    if (preference_ == Backend::Fsr && fsrAvailable_) {
        wanted = Backend::Fsr;
    } else if (preference_ == Backend::Dlss && dlssAvailable_) {
        wanted = Backend::Dlss;
    } else if (dlssAvailable_) {
        wanted = Backend::Dlss; // automatic, or the one asked for is missing
    } else if (fsrAvailable_) {
        wanted = Backend::Fsr;
    }
    if (wanted == backend_ && (generator_ != nullptr || wanted == Backend::Off)) return;

    if (generator_ != nullptr) generator_->release();
    generator_.reset();
    backend_ = wanted;
    switch (wanted) {
        case Backend::Dlss:
            generator_ = makeDlssGenerator(DLSSModule::ngxContext(), framework_);
            maxGeneratedFrames_ = dlssMaxFrames_;
            break;
        case Backend::Fsr:
            generator_ = makeFsrGenerator(framework_);
            maxGeneratedFrames_ = 1;
            break;
        default: maxGeneratedFrames_ = 0; break;
    }
    reset();
    out() << "using " << backendName(wanted) << std::endl;
}

void NativeFrameGeneration::setPreference(Backend preference) {
    preference_ = preference;
    if (probed_) selectBackend();
}

uint32_t NativeFrameGeneration::maxGeneratedFramesFor(Backend which) {
    probe();
    switch (which) {
        case Backend::Dlss: return dlssAvailable_ ? dlssMaxFrames_ : 0;
        case Backend::Fsr: return fsrAvailable_ ? 1 : 0;
        default: return 0;
    }
}

bool NativeFrameGeneration::available(Backend which) {
    probe();
    switch (which) {
        case Backend::Dlss: return dlssAvailable_;
        case Backend::Fsr: return fsrAvailable_;
        default: return false;
    }
}

NativeFrameGeneration::Backend NativeFrameGeneration::backend() {
    probe();
    return backend_;
}

uint32_t NativeFrameGeneration::maxGeneratedFrames() {
    return probe() ? maxGeneratedFrames_ : 0;
}

void NativeFrameGeneration::setGeneratedFrames(uint32_t frames) {
    const uint32_t clamped = std::min(frames, kMaxGeneratedFrames);
    if (clamped == generatedFrames_) return;
    generatedFrames_ = clamped;
    reset();
}

uint32_t NativeFrameGeneration::generatedFrames() const {
    return generatedFrames_;
}

bool NativeFrameGeneration::active() {
    return generatedFrames_ > 0 && probe();
}

int NativeFrameGeneration::presentedFrameRate() const {
    return rate_.perSecond;
}

void NativeFrameGeneration::reset() {
    historyValid_ = false;
    previous_.valid = false;
    capturedThisFrame_ = false;
    pending_.images.clear();
    rate_.inWindow = 0;
    rate_.perSecond = 0;
}

void NativeFrameGeneration::captureWorldFrame(std::shared_ptr<Framework> framework,
                                              std::shared_ptr<gpu::DeviceLocalImage> hudless,
                                              std::shared_ptr<gpu::DeviceLocalImage> linearDepth,
                                              std::shared_ptr<gpu::DeviceLocalImage> motionVectors) {
    capturedThisFrame_ = false;
    if (!active() || hudless == nullptr || linearDepth == nullptr || motionVectors == nullptr) return;

    auto buffers = Renderer::instance().buffers();
    if (buffers == nullptr) return;
    auto uniforms = buffers->worldUniformBuffer();
    if (uniforms == nullptr || uniforms->mappedPtr() == nullptr) return;
    const auto &ubo = *static_cast<const gpu::Data::WorldUBO *>(uniforms->mappedPtr());

    const glm::mat4 view = ubo.cameraViewMat;
    const glm::mat4 projection = ubo.cameraProjMat;
    const glm::mat4 viewInverse = ubo.cameraViewMatInv;
    const glm::mat4 projectionInverse = ubo.cameraProjMatInv;
    const glm::dvec3 position = glm::dvec3(ubo.cameraPos);
    if (!previous_.valid) previous_ = {view, projection, position, true};

    // The world is traced relative to the camera, so the camera's own movement is the translation between frames.
    glm::mat4 cameraMotion{1.0f};
    cameraMotion[3] = glm::vec4(glm::vec3(previous_.position - position), 1.0f);

    CameraFrame camera;
    camera.viewToClip = projection;
    camera.clipToView = projectionInverse;
    camera.clipToPrevClip = previous_.projection * previous_.view * cameraMotion * viewInverse * projectionInverse;
    camera.prevClipToClip = glm::inverse(camera.clipToPrevClip);
    // Upscaled depth and motion vectors carry no jitter any more.
    const bool renderResolution =
        linearDepth->width() != hudless->width() || linearDepth->height() != hudless->height();
    camera.jitter = renderResolution ? glm::vec2(ubo.cameraJitter) : glm::vec2(0.0f);
    camera.position = glm::vec3(position);
    camera.right = safeNormalize(glm::vec3(viewInverse[0]), {1.0f, 0.0f, 0.0f});
    camera.up = safeNormalize(glm::vec3(viewInverse[1]), {0.0f, 1.0f, 0.0f});
    camera.forward = safeNormalize(-glm::vec3(viewInverse[2]), {0.0f, 0.0f, -1.0f});
    const float focalY = std::abs(projection[1][1]);
    const float focalX = std::abs(projection[0][0]);
    camera.verticalFov = focalY > 0.0f ? 2.0f * std::atan(1.0f / focalY) : 1.0f;
    camera.aspect = focalX > 0.0f ? focalY / focalX : 1.0f;

    previous_ = {view, projection, position, true};

    framework_ = std::move(framework);
    capture_.hudless = std::move(hudless);
    capture_.linearDepth = std::move(linearDepth);
    capture_.motionVectors = std::move(motionVectors);
    capture_.camera = camera;
    capture_.valid = true;
    capturedThisFrame_ = true;
}

bool NativeFrameGeneration::evaluateAndBlit(VkCommandBuffer cmd,
                                            VkImage finalImage,
                                            VkFormat finalFormat,
                                            uint32_t width,
                                            uint32_t height,
                                            VkImage swapchainImage,
                                            uint32_t swapchainWidth,
                                            uint32_t swapchainHeight,
                                            bool flipY) {
    pending_.images.clear();
    frameDump().writePending(framework_);

    const bool captured = std::exchange(capturedThisFrame_, false);
    if (!active() || !captured || !capture_.valid || cmd == VK_NULL_HANDLE || generator_ == nullptr) {
        historyValid_ = false;
        return false;
    }

    try {
        return evaluate(cmd, finalImage, finalFormat, width, height, swapchainImage, swapchainWidth, swapchainHeight,
                        flipY);
    } catch (const std::exception &e) {
        out() << "evaluation failed: " << e.what() << std::endl;
        historyValid_ = false;
        pending_.images.clear();
        return false;
    }
}

bool NativeFrameGeneration::evaluate(VkCommandBuffer cmd,
                                     VkImage finalImage,
                                     VkFormat finalFormat,
                                     uint32_t width,
                                     uint32_t height,
                                     VkImage swapchainImage,
                                     uint32_t swapchainWidth,
                                     uint32_t swapchainHeight,
                                     bool flipY) {
    const uint32_t frames = std::min(generatedFrames_, maxGeneratedFrames_);
    outputTurn_ ^= 1u;
    OutputSet &outputs = outputs_[outputTurn_];

    auto framework = framework_.lock();
    if (framework == nullptr) return false;
    gpu::DeviceLocalImage &hudless = *capture_.hudless;
    gpu::DeviceLocalImage &linearDepth = *capture_.linearDepth;
    gpu::DeviceLocalImage &motionVectors = *capture_.motionVectors;
    const uint32_t renderWidth = linearDepth.width();
    const uint32_t renderHeight = linearDepth.height();
    if (frames == 0 ||
        !ensureResources(*framework, outputs, frames, width, height, finalFormat, renderWidth, renderHeight)) {
        return false;
    }
    gpu::Image *finalColor = finalImageView(*framework, finalImage, finalFormat, width, height);
    if (finalColor == nullptr) return false;

    // Written before anything is recorded, so a failure leaves Minecraft's command buffer as it was.
    const std::shared_ptr<gpu::DescriptorTable> &table = depthPass_.tables[depthPass_.next];
    depthPass_.next = (depthPass_.next + 1) % kDepthPassTables;
    table->bindImage(capture_.linearDepth, VK_IMAGE_LAYOUT_GENERAL, 0, 0);
    table->bindImage(deviceDepth_, VK_IMAGE_LAYOUT_GENERAL, 0, 1);
    table->bindImage(capture_.motionVectors, VK_IMAGE_LAYOUT_GENERAL, 0, 2);
    table->bindImage(motionVectors_, VK_IMAGE_LAYOUT_GENERAL, 0, 3);

    Transitions barriers(cmd);

    // The world image without the UI, scaled to the final image's size and converted to its format.
    const VkImageLayout hudlessLayout = hudless.imageLayout();
    barriers.memory()
        .image(hudless.vkImage(), hudlessLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
        .image(hudlessCopy_->vkImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        .record();
    VkImageBlit scale{};
    scale.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    scale.srcOffsets[1] = {static_cast<int32_t>(hudless.width()), static_cast<int32_t>(hudless.height()), 1};
    scale.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    scale.dstOffsets[1] = {static_cast<int32_t>(width), static_cast<int32_t>(height), 1};
    vkCmdBlitImage(cmd, hudless.vkImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, hudlessCopy_->vkImage(),
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &scale, VK_FILTER_LINEAR);
    barriers.image(hudless.vkImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, hudlessLayout)
        .image(hudlessCopy_->vkImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    hudlessCopy_->imageLayout() = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Device depth from the traced hit distance, and the motion vectors without NaNs.
    const VkImageLayout depthLayout = linearDepth.imageLayout();
    const VkImageLayout motionLayout = motionVectors.imageLayout();
    barriers.image(linearDepth.vkImage(), depthLayout, VK_IMAGE_LAYOUT_GENERAL)
        .image(motionVectors.vkImage(), motionLayout, VK_IMAGE_LAYOUT_GENERAL)
        .image(deviceDepth_->vkImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL)
        .image(motionVectors_->vkImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL)
        .record();
    const DepthPassConstants constants{kCameraNear, kCameraFar, renderWidth, renderHeight, capture_.camera.jitter.x,
                                       capture_.camera.jitter.y};
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, depthPass_.pipeline->vkPipeline());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, table->vkPipelineLayout(), 0,
                            static_cast<uint32_t>(table->descriptorSet().size()), table->descriptorSet().data(), 0,
                            nullptr);
    vkCmdPushConstants(cmd, table->vkPipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
    vkCmdDispatch(cmd, (renderWidth + 15) / 16, (renderHeight + 15) / 16, 1);
    barriers.image(linearDepth.vkImage(), VK_IMAGE_LAYOUT_GENERAL, depthLayout)
        .image(motionVectors.vkImage(), VK_IMAGE_LAYOUT_GENERAL, motionLayout)
        .image(deviceDepth_->vkImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        .image(motionVectors_->vkImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    deviceDepth_->imageLayout() = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    motionVectors_->imageLayout() = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // The backends read in SHADER_READ_ONLY_OPTIMAL and write in GENERAL. Minecraft keeps its textures in GENERAL.
    barriers.image(finalImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    for (uint32_t i = 0; i < frames; i++) {
        barriers.image(outputs.generated[i]->vkImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        outputs.generated[i]->imageLayout() = VK_IMAGE_LAYOUT_GENERAL;
    }
    barriers.record();

    GenerationInputs inputs;
    inputs.finalColor = finalColor;
    inputs.hudless = hudlessCopy_.get();
    inputs.depth = deviceDepth_.get();
    inputs.motionVectors = motionVectors_.get();
    inputs.displayWidth = width;
    inputs.displayHeight = height;
    inputs.renderWidth = renderWidth;
    inputs.renderHeight = renderHeight;
    inputs.camera = capture_.camera;
    inputs.reset = !historyValid_;
    const auto now = std::chrono::steady_clock::now();
    if (historyValid_ && lastEvaluation_ != std::chrono::steady_clock::time_point{}) {
        inputs.frameTimeMs = std::chrono::duration<float, std::milli>(now - lastEvaluation_).count();
    }
    lastEvaluation_ = now;

    bool evaluated = true;
    for (uint32_t i = 0; i < frames && evaluated; i++) {
        evaluated = generator_->generate(cmd, inputs, *outputs.generated[i], frames, i + 1);
    }

    // Without history the backends have nothing to generate from yet, so this frame shows the real one.
    const bool showGenerated = evaluated && historyValid_;
    historyValid_ = evaluated;

    barriers.memory().image(finalImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    if (showGenerated) {
        barriers.image(outputs.real->vkImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    }
    barriers.record();
    if (showGenerated) {
        // The real frame is shown last, when Minecraft is already drawing the next one into its image, so it is kept
        // aside.
        VkImageCopy region{};
        region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.extent = {width, height, 1};
        vkCmdCopyImage(cmd, finalImage, VK_IMAGE_LAYOUT_GENERAL, outputs.real->vkImage(),
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        barriers.image(outputs.real->vkImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
        outputs.real->imageLayout() = VK_IMAGE_LAYOUT_GENERAL;
        frameDump().record(barriers, cmd, *framework, *outputs.generated[0], *outputs.real, width, height,
                           finalFormat);
        barriers.record();
    }

    const VkImage first = showGenerated ? outputs.generated[0]->vkImage() : finalImage;
    const VkImageBlit blit = swapchainBlit(width, height, swapchainWidth, swapchainHeight, flipY);
    vkCmdBlitImage(cmd, first, VK_IMAGE_LAYOUT_GENERAL, swapchainImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                   VK_FILTER_NEAREST);

    if (showGenerated) {
        for (uint32_t i = 1; i < frames; i++) pending_.images.push_back(outputs.generated[i]->vkImage());
        pending_.images.push_back(showOnlyGenerated() ? outputs.generated[frames - 1]->vkImage()
                                                      : outputs.real->vkImage());
        pending_.width = width;
        pending_.height = height;
        pending_.flipY = flipY;
    }
    return true;
}

bool NativeFrameGeneration::ensureResources(Framework &framework,
                                            OutputSet &outputs,
                                            uint32_t frames,
                                            uint32_t width,
                                            uint32_t height,
                                            VkFormat format,
                                            uint32_t renderWidth,
                                            uint32_t renderHeight) {
    const auto fits = [](const std::shared_ptr<gpu::DeviceLocalImage> &image, uint32_t w, uint32_t h, VkFormat f) {
        return image != nullptr && image->width() == w && image->height() == h && image->vkFormat() == f;
    };
    const auto outgrown = [&](const std::shared_ptr<gpu::DeviceLocalImage> &image, uint32_t w, uint32_t h,
                              VkFormat f) { return image != nullptr && !fits(image, w, h, f); };
    const auto make = [&](uint32_t w, uint32_t h, VkFormat f, VkImageUsageFlags usage) {
        return gpu::DeviceLocalImage::create(framework.device(), framework.vma(),
                                             gpu::ImageDesc{.width = w, .height = h, .format = f, .usage = usage});
    };

    // What is dropped may still be read by frames in flight, and by the present thread's last blits. Fewer generated
    // frames do not wait: the images cut off belong to the set the present thread is not using, and they retire
    // through gpu::retire once the frames in flight are done.
    bool dropping = outgrown(outputs.real, width, height, format) ||
                    outgrown(hudlessCopy_, width, height, format) ||
                    outgrown(deviceDepth_, renderWidth, renderHeight, VK_FORMAT_R32_SFLOAT) ||
                    outgrown(motionVectors_, renderWidth, renderHeight, VK_FORMAT_R16G16_SFLOAT);
    for (const auto &image : outputs.generated) dropping = dropping || outgrown(image, width, height, format);
    if (dropping) framework.waitRenderQueueIdle();

    // A set made anew (its first use, another size or format) starts the history again; more generated frames in a
    // set that fits carry it on.
    outputs.generated.resize(frames);
    for (size_t i = 0; i < outputs.generated.size(); i++) {
        std::shared_ptr<gpu::DeviceLocalImage> &image = outputs.generated[i];
        if (fits(image, width, height, format)) continue;
        if (i == 0 || image != nullptr) historyValid_ = false;
        image = make(width, height, format, kColorUsage);
    }
    if (!fits(outputs.real, width, height, format)) outputs.real = make(width, height, format, kColorUsage);
    if (!fits(hudlessCopy_, width, height, format)) {
        hudlessCopy_ = make(width, height, format, kColorUsage);
        historyValid_ = false;
    }
    if (!fits(deviceDepth_, renderWidth, renderHeight, VK_FORMAT_R32_SFLOAT)) {
        deviceDepth_ = make(renderWidth, renderHeight, VK_FORMAT_R32_SFLOAT, kDepthPassUsage);
        historyValid_ = false;
    }
    if (!fits(motionVectors_, renderWidth, renderHeight, VK_FORMAT_R16G16_SFLOAT)) {
        motionVectors_ = make(renderWidth, renderHeight, VK_FORMAT_R16G16_SFLOAT, kDepthPassUsage);
        historyValid_ = false;
    }

    if (depthPass_.tables.empty()) {
        for (uint32_t i = 0; i < kDepthPassTables; i++) {
            depthPass_.tables.push_back(makeDepthPassTable(framework.device()));
        }
    }
    if (depthPass_.pipeline == nullptr) {
        const auto path = Renderer::folderPath / "shaders/world/upscaler/linear_to_device_depth_comp.spv";
        depthPass_.shader = gpu::Shader::create(framework.device(), path.string());
        depthPass_.pipeline = gpu::ComputePipelineBuilder{}
                                  .defineShader(depthPass_.shader)
                                  .definePipelineLayout(depthPass_.tables.front())
                                  .build(framework.device());
    }
    return depthPass_.pipeline != nullptr;
}

gpu::Image *NativeFrameGeneration::finalImageView(
    Framework &framework, VkImage image, VkFormat format, uint32_t width, uint32_t height) {
    if (finalView_ != nullptr && finalView_->vkImage() == image && finalView_->width() == width &&
        finalView_->height() == height && finalView_->vkFormat() == format) {
        return finalView_.get();
    }
    const std::shared_ptr<gpu::Device> device = framework.device();
    if (finalView_ != nullptr) {
        // Evaluations recorded with the old view may still be in flight.
        if (const VkImageView old = finalView_->vkImageView(0); old != VK_NULL_HANDLE) {
            gpu::retire::later([device, old] { vkDestroyImageView(device->vkDevice(), old, nullptr); });
        }
        finalView_ = nullptr;
    }

    VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    info.image = image;
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format = format;
    info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageView view = VK_NULL_HANDLE;
    if (vkCreateImageView(device->vkDevice(), &info, nullptr, &view) != VK_SUCCESS) return nullptr;
    finalView_ = gpu::ExternalImage::create(image, width, height, format);
    finalView_->vkImageView(0) = view;
    return finalView_.get();
}

void NativeFrameGeneration::PresentRate::add(uint32_t frames) {
    inWindow += frames;
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - windowStart).count();
    if (elapsed < 1000) return;
    perSecond = static_cast<int>(inWindow * 1000 / elapsed);
    inWindow = 0;
    windowStart = now;
}

void NativeFrameGeneration::waitPresentIdle() {
    presenter_.waitIdle();
}

int NativeFrameGeneration::presentPending(VkSwapchainKHR swapchain,
                                          const std::vector<VkImage> &swapchainImages,
                                          VkQueue queue,
                                          uint32_t swapchainWidth,
                                          uint32_t swapchainHeight) {
    // The previous frame's generated frames are all shown by now (or this waits for the last of them), so their images
    // are free again and the order on screen stays generated frames, real one, next frame.
    presenter_.waitIdle();
    const PresentThread::Outcome outcome = presenter_.takeOutcome();
    if (outcome.status < 0) historyValid_ = false;

    const auto now = std::chrono::steady_clock::now();
    if (lastMinecraftPresent_ != std::chrono::steady_clock::time_point{}) {
        const double interval = std::chrono::duration<double>(now - lastMinecraftPresent_).count();
        // Stalls (loading, menus) are left out, so one long frame does not stretch the spacing of the next ones.
        if (interval > 0.0 && interval < 0.25) {
            frameInterval_ = frameInterval_ <= 0.0 ? interval : frameInterval_ * 0.85 + interval * 0.15;
        }
    }
    lastMinecraftPresent_ = now;

    if (!active()) {
        pending_.images.clear();
        rate_.perSecond = 0;
        return outcome.status;
    }
    rate_.add(1 + outcome.presented);
    if (pending_.images.empty()) return outcome.status;

    auto framework = framework_.lock();
    if (framework == nullptr || !presenter_.prepare(*framework, swapchain, swapchainImages.size())) {
        pending_.images.clear();
        return outcome.status;
    }
    pending_.swapchain = swapchain;
    pending_.swapchainImages.assign(swapchainImages.begin(), swapchainImages.end());
    pending_.queue = queue;
    pending_.swapchainWidth = swapchainWidth;
    pending_.swapchainHeight = swapchainHeight;
    pending_.start = now;
    // The frames sit evenly between this present and the next one.
    const double interval = frameInterval_ > 0.0 ? frameInterval_ : 1.0 / 60.0;
    pending_.spacing = std::chrono::duration<double>(interval / static_cast<double>(pending_.images.size() + 1));
    presenter_.post(pending_);
    pending_.images.clear();
    return outcome.status;
}

void NativeFrameGeneration::release() {
    presenter_.stop();
    if (auto framework = framework_.lock()) {
        framework->waitRenderQueueIdle();
        presenter_.destroy();
        if (finalView_ != nullptr && finalView_->vkImageView(0) != VK_NULL_HANDLE) {
            vkDestroyImageView(framework->device()->vkDevice(), finalView_->vkImageView(0), nullptr);
        }
    }
    if (generator_ != nullptr) generator_->release();
    generator_.reset();
    presenter_.forget();
    finalView_ = nullptr;
    outputs_ = {};
    hudlessCopy_ = nullptr;
    deviceDepth_ = nullptr;
    motionVectors_ = nullptr;
    depthPass_ = {};
    capture_ = {};
    backend_ = Backend::Off;
    maxGeneratedFrames_ = 0;
    dlssAvailable_ = false;
    fsrAvailable_ = false;
    dlssMaxFrames_ = 0;
    probed_ = false;
    reset();
}

} // namespace framegen
