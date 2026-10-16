#include "prism/render/modules/world/nrd/nrd_module.hpp"

#include "prism/render/buffers.hpp"
#include "prism/render/pipeline.hpp"
#include "prism/render/render_framework.hpp"
#include "prism/render/renderer.hpp"
#include "prism/util/logging.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <limits>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {

// RELAX rather than REBLUR. On this path tracer's one-sample signal REBLUR converged to blotches when still and to
// streaky noise whenever the camera moved (roughly 2.5x DLSS-RR's high-frequency noise), and lost about a quarter of
// the light in dim, bounce-lit rooms. RELAX on the same inputs is as clean as DLSS-RR in motion and within a few
// percent of its brightness. prepare.comp and compose.comp pack and unpack RELAX's signal. The REBLUR settings (the
// module's attributes and clampReblurSettings) are kept on purpose as the switch-back path; they reach NRD only when
// kDenoiser is REBLUR_DIFFUSE_SPECULAR.
constexpr nrd::Denoiser kDenoiser = nrd::Denoiser::RELAX_DIFFUSE_SPECULAR;
constexpr std::array kDenoisers = {kDenoiser};

// The module's input images, in the order the pipeline hands them over.
namespace in {
enum : uint32_t {
    DiffuseIndirectRadiance,
    SpecularIndirectRadiance,
    DirectRadiance,
    DiffuseAlbedoMetallic,
    SpecularAlbedo,
    NormalRoughness,
    MotionVector,
    LinearDepth,
    ClearRadiance,
    BaseEmission,
    Fog,
    DiffuseHitDepth,
    SpecularHitDepth,
    RefractionRadiance,
    FogTransmission,
    Count,
};
} // namespace in
static_assert(in::Count == NrdModule::inputImageNum);

// The shaders' bindings. Set 0 holds the inputs read (binding = position), set 1 the images NRD exchanges (and, for
// compose.comp, the module's output after them), set 2 one uniform buffer.
constexpr std::array<uint32_t, 12> kPrepareReads = {
    in::DirectRadiance,  in::DiffuseIndirectRadiance, in::SpecularIndirectRadiance, in::DiffuseAlbedoMetallic,
    in::SpecularAlbedo,  in::MotionVector,            in::NormalRoughness,          in::LinearDepth,
    in::ClearRadiance,   in::DiffuseHitDepth,         in::SpecularHitDepth,         in::RefractionRadiance,
};
constexpr std::array kPrepareWrites = {
    nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST, nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST,
    nrd::ResourceType::IN_MV,                    nrd::ResourceType::IN_NORMAL_ROUGHNESS,
    nrd::ResourceType::IN_VIEWZ,
};
constexpr std::array<uint32_t, 6> kComposeReads = {
    in::DiffuseAlbedoMetallic, in::SpecularAlbedo, in::ClearRadiance, in::BaseEmission, in::Fog, in::FogTransmission,
};
constexpr std::array kComposeDenoised = {
    nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST,
    nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST,
};

// The images the module hands NRD, but for IN_NORMAL_ROUGHNESS, whose format the NRD library dictates.
constexpr std::array<std::pair<nrd::ResourceType, VkFormat>, 6> kNrdTextureFormats = {{
    {nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST, VK_FORMAT_R16G16B16A16_SFLOAT},
    {nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST, VK_FORMAT_R16G16B16A16_SFLOAT},
    {nrd::ResourceType::IN_MV, VK_FORMAT_R16G16_SFLOAT},
    {nrd::ResourceType::IN_VIEWZ, VK_FORMAT_R16_SFLOAT},
    {nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST, VK_FORMAT_R16G16B16A16_SFLOAT},
    {nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST, VK_FORMAT_R16G16B16A16_SFLOAT},
}};

constexpr uint32_t kGroupSize = 16; // both shaders' local size, in x and y

std::shared_ptr<gpu::DeviceLocalImage> makeImage(const std::shared_ptr<Framework> &framework,
                                                 uint32_t width,
                                                 uint32_t height,
                                                 VkFormat format,
                                                 VkImageUsageFlags usage) {
    const gpu::ImageDesc desc{.width = width, .height = height, .format = format, .usage = usage};
    return gpu::DeviceLocalImage::create(framework->device(), framework->vma(), desc);
}

// Three sets: the storage images a pass reads, the storage images it exchanges with NRD, one uniform buffer.
std::shared_ptr<gpu::DescriptorTable>
makeTable(const std::shared_ptr<gpu::Device> &device, uint32_t readImages, uint32_t nrdImages) {
    gpu::DescriptorTableBuilder builder;
    auto defineSet = [&builder](VkDescriptorType type, uint32_t count) {
        auto &set = builder.beginDescriptorLayoutSet().beginDescriptorLayoutSetBinding();
        for (uint32_t binding = 0; binding < count; binding++) {
            set.defineDescriptorLayoutSetBinding({binding, type, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr});
        }
        set.endDescriptorLayoutSetBinding().endDescriptorLayoutSet();
    };
    defineSet(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, readImages);
    defineSet(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nrdImages);
    defineSet(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1);
    return builder.build(device);
}

nrd::ReblurSettings defaultReblurSettings() {
    nrd::ReblurSettings settings = {};
    settings.maxAccumulatedFrameNum = 60;
    settings.maxFastAccumulatedFrameNum = 3;
    settings.fastHistoryClampingSigmaScale = 1.5f;
    settings.maxBlurRadius = 100.0f;
    settings.enableAntiFirefly = true;
    settings.hitDistanceReconstructionMode = nrd::HitDistanceReconstructionMode::AREA_5X5;
    return settings;
}

nrd::RelaxSettings relaxSettings() {
    nrd::RelaxSettings settings = {};
    settings.hitDistanceReconstructionMode = nrd::HitDistanceReconstructionMode::AREA_3X3;
    return settings;
}

// Keeps every REBLUR parameter in the range NRD accepts, and the two motion vector thresholds in order.
void clampReblurSettings(nrd::ReblurSettings &s) {
    s.hitDistanceParameters.A = std::max(s.hitDistanceParameters.A, 0.0f);
    s.hitDistanceParameters.B = std::max(s.hitDistanceParameters.B, 0.0f);
    s.hitDistanceParameters.C = std::max(s.hitDistanceParameters.C, 1.0f);

    s.antilagSettings.luminanceSigmaScale = std::clamp(s.antilagSettings.luminanceSigmaScale, 1.0f, 5.0f);
    s.antilagSettings.luminanceSensitivity = std::clamp(s.antilagSettings.luminanceSensitivity, 1.0f, 5.0f);

    auto &responsive = s.responsiveAccumulationSettings;
    responsive.roughnessThreshold = std::clamp(responsive.roughnessThreshold, 0.0f, 1.0f);
    responsive.minAccumulatedFrameNum = std::min(responsive.minAccumulatedFrameNum, 3u);

    s.maxAccumulatedFrameNum = std::min(s.maxAccumulatedFrameNum, nrd::REBLUR_MAX_HISTORY_FRAME_NUM);
    s.maxFastAccumulatedFrameNum = std::min(s.maxFastAccumulatedFrameNum, nrd::REBLUR_MAX_HISTORY_FRAME_NUM);
    s.maxStabilizedFrameNum = std::min(s.maxStabilizedFrameNum, nrd::REBLUR_MAX_HISTORY_FRAME_NUM);
    s.historyFixFrameNum = std::min(s.historyFixFrameNum, 3u);
    s.historyFixBasePixelStride = std::max(s.historyFixBasePixelStride, 1u);
    s.historyFixAlternatePixelStride = std::max(s.historyFixAlternatePixelStride, 1u);

    s.fastHistoryClampingSigmaScale = std::clamp(s.fastHistoryClampingSigmaScale, 1.0f, 3.0f);
    s.diffusePrepassBlurRadius = std::max(s.diffusePrepassBlurRadius, 0.0f);
    s.specularPrepassBlurRadius = std::max(s.specularPrepassBlurRadius, 0.0f);
    s.minHitDistanceWeight = std::clamp(s.minHitDistanceWeight, 0.0f, 0.2f);
    s.minBlurRadius = std::max(s.minBlurRadius, 0.0f);
    s.maxBlurRadius = std::max(s.maxBlurRadius, 0.0f);
    s.lobeAngleFraction = std::clamp(s.lobeAngleFraction, 0.0f, 1.0f);
    s.roughnessFraction = std::clamp(s.roughnessFraction, 0.0f, 1.0f);
    s.planeDistanceSensitivity = std::clamp(s.planeDistanceSensitivity, 0.0f, 1.0f);

    float (&thresholds)[2] = s.specularProbabilityThresholdsForMvModification;
    thresholds[0] = std::clamp(thresholds[0], 0.0f, 1.0f);
    thresholds[1] = std::clamp(thresholds[1], 0.0f, 1.0f);
    if (thresholds[0] > thresholds[1]) std::swap(thresholds[0], thresholds[1]);

    s.fireflySuppressorMinRelativeScale = std::clamp(s.fireflySuppressorMinRelativeScale, 1.0f, 3.0f);
    s.minMaterialForDiffuse = std::max(s.minMaterialForDiffuse, 0.0f);
    s.minMaterialForSpecular = std::max(s.minMaterialForSpecular, 0.0f);
}

// Attribute values. Text that does not parse leaves the field as it was; a switch is on only for a "true" spelling.
void parse(const std::string &text, float &field) {
    try {
        field = std::stof(text);
    } catch (...) {}
}

void parse(const std::string &text, uint32_t &field) {
    try {
        constexpr long long kMax = std::numeric_limits<uint32_t>::max();
        field = static_cast<uint32_t>(std::clamp(std::stoll(text), 0LL, kMax));
    } catch (...) {}
}

void parse(const std::string &text, bool &field) {
    field = text == "render_pipeline.true" || text == "1" || text == "true" || text == "True" || text == "TRUE";
}

void parse(const std::string &text, nrd::CheckerboardMode &field) {
    if (text == "off") field = nrd::CheckerboardMode::OFF;
    else if (text == "black") field = nrd::CheckerboardMode::BLACK;
    else if (text == "white") field = nrd::CheckerboardMode::WHITE;
}

void parse(const std::string &text, nrd::HitDistanceReconstructionMode &field) {
    if (text == "off") field = nrd::HitDistanceReconstructionMode::OFF;
    else if (text == "3x3") field = nrd::HitDistanceReconstructionMode::AREA_3X3;
    else if (text == "5x5") field = nrd::HitDistanceReconstructionMode::AREA_5X5;
}

using Reblur = nrd::ReblurSettings;

struct Attribute {
    std::string_view name; // the key after kAttributePrefix
    void (*set)(Reblur &, const std::string &);
};

constexpr std::string_view kAttributePrefix = "render_pipeline.module.nrd.attribute.";

// hit_distance_parameters_d is no longer read: NRD 4.17.2 replaced D with a remapped roughness.
constexpr Attribute kAttributes[] = {
    {"hit_distance_parameters_a", [](Reblur &r, const std::string &v) { parse(v, r.hitDistanceParameters.A); }},
    {"hit_distance_parameters_b", [](Reblur &r, const std::string &v) { parse(v, r.hitDistanceParameters.B); }},
    {"hit_distance_parameters_c", [](Reblur &r, const std::string &v) { parse(v, r.hitDistanceParameters.C); }},
    {"antilag_luminance_sigma_scale",
     [](Reblur &r, const std::string &v) { parse(v, r.antilagSettings.luminanceSigmaScale); }},
    {"antilag_luminance_sensitivity",
     [](Reblur &r, const std::string &v) { parse(v, r.antilagSettings.luminanceSensitivity); }},
    {"responsive_accumulation_roughness_threshold",
     [](Reblur &r, const std::string &v) { parse(v, r.responsiveAccumulationSettings.roughnessThreshold); }},
    {"responsive_accumulation_min_accumulated_frame_num",
     [](Reblur &r, const std::string &v) { parse(v, r.responsiveAccumulationSettings.minAccumulatedFrameNum); }},
    {"max_accumulated_frame_num", [](Reblur &r, const std::string &v) { parse(v, r.maxAccumulatedFrameNum); }},
    {"max_fast_accumulated_frame_num", [](Reblur &r, const std::string &v) { parse(v, r.maxFastAccumulatedFrameNum); }},
    {"max_stabilized_frame_num", [](Reblur &r, const std::string &v) { parse(v, r.maxStabilizedFrameNum); }},
    {"history_fix_frame_num", [](Reblur &r, const std::string &v) { parse(v, r.historyFixFrameNum); }},
    {"history_fix_base_pixel_stride", [](Reblur &r, const std::string &v) { parse(v, r.historyFixBasePixelStride); }},
    {"history_fix_alternate_pixel_stride",
     [](Reblur &r, const std::string &v) { parse(v, r.historyFixAlternatePixelStride); }},
    {"fast_history_clamping_sigma_scale",
     [](Reblur &r, const std::string &v) { parse(v, r.fastHistoryClampingSigmaScale); }},
    {"diffuse_prepass_blur_radius", [](Reblur &r, const std::string &v) { parse(v, r.diffusePrepassBlurRadius); }},
    {"specular_prepass_blur_radius", [](Reblur &r, const std::string &v) { parse(v, r.specularPrepassBlurRadius); }},
    {"min_hit_distance_weight", [](Reblur &r, const std::string &v) { parse(v, r.minHitDistanceWeight); }},
    {"min_blur_radius", [](Reblur &r, const std::string &v) { parse(v, r.minBlurRadius); }},
    {"max_blur_radius", [](Reblur &r, const std::string &v) { parse(v, r.maxBlurRadius); }},
    {"lobe_angle_fraction", [](Reblur &r, const std::string &v) { parse(v, r.lobeAngleFraction); }},
    {"roughness_fraction", [](Reblur &r, const std::string &v) { parse(v, r.roughnessFraction); }},
    {"plane_distance_sensitivity", [](Reblur &r, const std::string &v) { parse(v, r.planeDistanceSensitivity); }},
    {"specular_probability_thresholds_for_mv_modification_min",
     [](Reblur &r, const std::string &v) { parse(v, r.specularProbabilityThresholdsForMvModification[0]); }},
    {"specular_probability_thresholds_for_mv_modification_max",
     [](Reblur &r, const std::string &v) { parse(v, r.specularProbabilityThresholdsForMvModification[1]); }},
    {"firefly_suppressor_min_relative_scale",
     [](Reblur &r, const std::string &v) { parse(v, r.fireflySuppressorMinRelativeScale); }},
    {"min_material_for_diffuse", [](Reblur &r, const std::string &v) { parse(v, r.minMaterialForDiffuse); }},
    {"min_material_for_specular", [](Reblur &r, const std::string &v) { parse(v, r.minMaterialForSpecular); }},
    {"checkerboard_mode", [](Reblur &r, const std::string &v) { parse(v, r.checkerboardMode); }},
    {"enable_anti_firefly", [](Reblur &r, const std::string &v) { parse(v, r.enableAntiFirefly); }},
    {"hit_distance_reconstruction_mode",
     [](Reblur &r, const std::string &v) { parse(v, r.hitDistanceReconstructionMode); }},
    {"use_prepass_only_for_specular_motion_estimation",
     [](Reblur &r, const std::string &v) { parse(v, r.usePrepassOnlyForSpecularMotionEstimation); }},
    {"return_history_length_instead_of_occlusion",
     [](Reblur &r, const std::string &v) { parse(v, r.returnHistoryLengthInsteadOfOcclusion); }},
};

// The camera NRD sees this frame and the last.
nrd::CommonSettings commonSettings(const gpu::Data::WorldUBO &world,
                                   const gpu::Data::WorldUBO &lastWorld,
                                   uint32_t width,
                                   uint32_t height,
                                   uint32_t frameIndex) {
    // NRD's clip space has Y the other way up. NRD also rebuilds view positions as (frustum ray) * viewZ, so viewZ must
    // be the z of the view space its matrices describe. Ours looks down -Z, while prepare.comp hands NRD a positive
    // linear depth: a still camera hides the mismatch, but in motion every pixel reprojected somewhere else, NRD judged
    // the whole frame disoccluded and the image stayed at one noisy sample per pixel. Mirroring Z on both sides gives
    // NRD a +Z-forward view in which that depth is viewZ.
    const glm::mat4 flipZ = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, 1.0f, -1.0f));
    auto viewToClip = [&flipZ](glm::mat4 projection) {
        for (int column = 0; column < 4; column++) projection[column][1] *= -1.0f;
        return projection * flipZ;
    };
    const glm::mat4 viewToClipNow = viewToClip(world.cameraProjMat);
    const glm::mat4 viewToClipPrev = viewToClip(lastWorld.cameraProjMat);
    const glm::mat4 worldToViewNow = flipZ * world.cameraEffectedViewMat;
    const glm::mat4 worldToViewPrev = flipZ * lastWorld.cameraEffectedViewMat;

    nrd::CommonSettings settings = {};
    static_assert(sizeof(settings.viewToClipMatrix) == sizeof(glm::mat4));
    std::memcpy(settings.viewToClipMatrix, glm::value_ptr(viewToClipNow), sizeof(glm::mat4));
    std::memcpy(settings.viewToClipMatrixPrev, glm::value_ptr(viewToClipPrev), sizeof(glm::mat4));
    std::memcpy(settings.worldToViewMatrix, glm::value_ptr(worldToViewNow), sizeof(glm::mat4));
    std::memcpy(settings.worldToViewMatrixPrev, glm::value_ptr(worldToViewPrev), sizeof(glm::mat4));

    // The whole image, this frame and the last: a size change rebuilds the module.
    for (uint16_t *size :
         {settings.resourceSize, settings.rectSize, settings.resourceSizePrev, settings.rectSizePrev}) {
        size[0] = static_cast<uint16_t>(width);
        size[1] = static_cast<uint16_t>(height);
    }
    settings.cameraJitter[0] = world.cameraJitter.x;
    settings.cameraJitter[1] = world.cameraJitter.y;
    settings.cameraJitterPrev[0] = lastWorld.cameraJitter.x;
    settings.cameraJitterPrev[1] = lastWorld.cameraJitter.y;
    settings.frameIndex = frameIndex;
    settings.accumulationMode =
        frameIndex == 0 ? nrd::AccumulationMode::CLEAR_AND_RESTART : nrd::AccumulationMode::CONTINUE;
    // The tracer's motion is in pixels; NRD's in fractions of the image.
    settings.motionVectorScale[0] = 1.0f / width;
    settings.motionVectorScale[1] = 1.0f / height;
    settings.motionVectorScale[2] = 0.0f;
    settings.disocclusionThreshold = 0.02f;
    settings.disocclusionThresholdAlternate = 0.15f;
    return settings;
}

} // namespace

// The module's work in one frame slot. Its type name labels the module in GPU profiles and checkpoints.
struct NrdModuleContext : public WorldModuleContext, public SharedObject<NrdModuleContext> {
    NrdModuleContext(std::shared_ptr<FrameworkContext> frameworkContext,
                     std::shared_ptr<WorldPipelineContext> worldPipelineContext,
                     std::shared_ptr<NrdModule> module)
        : WorldModuleContext(std::move(frameworkContext), std::move(worldPipelineContext)), module_(module) {}

    void render() override {
        auto module = module_.lock();
        auto context = frameworkContext.lock();
        if (module != nullptr && context != nullptr) module->record(*context);
    }

  private:
    std::weak_ptr<NrdModule> module_;
};

struct NrdModule::Frame {
    std::array<std::shared_ptr<gpu::DeviceLocalImage>, inputImageNum> inputs;
    std::shared_ptr<gpu::DeviceLocalImage> output;

    std::shared_ptr<gpu::DescriptorTable> prepareTable;
    std::shared_ptr<gpu::DescriptorTable> composeTable;
    // What each pass does with its images, for CommandBuffer::sync.
    std::vector<gpu::ImageUse> prepareUses;
    std::vector<gpu::ImageUse> composeUses;
    // The uniform buffers bound in the tables. The renderer makes a slot's buffers when it first fills them, which is
    // after the module is built, so they are bound on first use (and again should they ever change).
    std::shared_ptr<gpu::HostVisibleBuffer> boundWorldUniform;
    std::shared_ptr<gpu::HostVisibleBuffer> boundSkyUniform;
};

NrdModule::NrdModule() : reblurSettings_(defaultReblurSettings()) {}

NrdModule::~NrdModule() = default;

void NrdModule::init(std::shared_ptr<Framework> framework, std::shared_ptr<WorldPipeline> worldPipeline) {
    WorldModule::init(framework, worldPipeline);
    frames_.resize(framework->swapchain()->imageCount());
    for (auto &frame : frames_) frame = std::make_unique<Frame>();
}

bool NrdModule::setOrCreateInputImages(std::vector<std::shared_ptr<gpu::DeviceLocalImage>> &images,
                                       std::vector<VkFormat> &formats,
                                       uint32_t frameIndex) {
    if (images.size() != inputImageNum) return false;

    auto framework = framework_.lock();
    for (uint32_t i = 0; i < images.size(); i++) {
        if (images[i] != nullptr) continue;
        if (width_ == 0 || height_ == 0) {
            radiante::err() << "[NrdModule] Error: Cannot create input image " << i
                            << " because dimensions are unknown." << std::endl;
            return false;
        }
        images[i] = makeImage(framework, width_, height_, formats[i],
                              VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                  VK_IMAGE_USAGE_SAMPLED_BIT);
    }
    std::ranges::copy(images, frames_.at(frameIndex)->inputs.begin());
    return true;
}

bool NrdModule::setOrCreateOutputImages(std::vector<std::shared_ptr<gpu::DeviceLocalImage>> &images,
                                        std::vector<VkFormat> &formats,
                                        uint32_t frameIndex) {
    if (images.size() != outputImageNum || images[0] == nullptr) return false;

    width_ = images[0]->width();
    height_ = images[0]->height();
    frames_.at(frameIndex)->output = images[0];
    return true;
}

void NrdModule::build() {
    auto framework = framework_.lock();
    auto worldPipeline = worldPipeline_.lock();
    auto device = framework->device();

    wrapper_ = NrdWrapper::create(framework, static_cast<uint16_t>(width_), static_cast<uint16_t>(height_),
                                  std::span<const nrd::Denoiser>(kDenoisers));
    if constexpr (kDenoiser == nrd::Denoiser::REBLUR_DIFFUSE_SPECULAR) {
        wrapper_->setReblurSettings(kDenoiser, reblurSettings_);
    } else {
        wrapper_->setRelaxSettings(kDenoiser, relaxSettings());
    }
    nrdFrameIndex_ = 0;

    nrdTextures_ = {};
    for (const auto &[type, format] : kNrdTextureFormats) {
        nrdTextures_[size_t(type)] =
            makeImage(framework, width_, height_, format, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    }
    nrdTextures_[size_t(nrd::ResourceType::IN_NORMAL_ROUGHNESS)] =
        makeImage(framework, width_, height_, NrdWrapper::normalRoughnessFormat(),
                  VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

    for (auto &frame : frames_) buildFrame(*frame, device);

    const std::filesystem::path shaders = Renderer::folderPath / "shaders";
    auto prepareShader = gpu::Shader::create(device, (shaders / "world/nrd/prepare_comp.spv").string());
    preparePipeline_ = gpu::ComputePipelineBuilder{}
                           .defineShader(prepareShader)
                           .definePipelineLayout(frames_.front()->prepareTable)
                           .build(device);
    auto composeShader = gpu::Shader::create(device, (shaders / "world/nrd/compose_comp.spv").string());
    composePipeline_ = gpu::ComputePipelineBuilder{}
                           .defineShader(composeShader)
                           .definePipelineLayout(frames_.front()->composeTable)
                           .build(device);

    contexts_.resize(frames_.size());
    for (size_t i = 0; i < frames_.size(); i++) {
        contexts_[i] =
            NrdModuleContext::create(framework->contexts()[i], worldPipeline->contexts()[i], shared_from_this());
    }
}

void NrdModule::buildFrame(Frame &frame, const std::shared_ptr<gpu::Device> &device) {
    const auto missing = [](const std::shared_ptr<gpu::DeviceLocalImage> &image) { return image == nullptr; };
    if (missing(frame.output) || std::ranges::any_of(frame.inputs, missing)) {
        throw std::logic_error("NRD module built before all of its images were set");
    }
    auto nrdTexture = [this](nrd::ResourceType type) -> const std::shared_ptr<gpu::DeviceLocalImage> & {
        return nrdTextures_[size_t(type)];
    };

    frame.prepareTable = makeTable(device, kPrepareReads.size(), kPrepareWrites.size());
    frame.prepareUses.clear();
    for (uint32_t binding = 0; binding < kPrepareReads.size(); binding++) {
        const auto &image = frame.inputs[kPrepareReads[binding]];
        frame.prepareTable->bindImage(image, VK_IMAGE_LAYOUT_GENERAL, 0, binding);
        // Every shader stage: later modules that read the image too may then skip their barrier.
        frame.prepareUses.emplace_back(image, gpu::ImageUsage::StorageRead);
    }
    for (uint32_t binding = 0; binding < kPrepareWrites.size(); binding++) {
        const auto &image = nrdTexture(kPrepareWrites[binding]);
        frame.prepareTable->bindImage(image, VK_IMAGE_LAYOUT_GENERAL, 1, binding);
        frame.prepareUses.emplace_back(image, gpu::ImageUsage::StorageWrite, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    }

    frame.composeTable = makeTable(device, kComposeReads.size(), kComposeDenoised.size() + 1);
    frame.composeUses.clear();
    for (uint32_t binding = 0; binding < kComposeReads.size(); binding++) {
        const auto &image = frame.inputs[kComposeReads[binding]];
        frame.composeTable->bindImage(image, VK_IMAGE_LAYOUT_GENERAL, 0, binding);
        frame.composeUses.emplace_back(image, gpu::ImageUsage::StorageRead);
    }
    for (uint32_t binding = 0; binding < kComposeDenoised.size(); binding++) {
        const auto &image = nrdTexture(kComposeDenoised[binding]);
        frame.composeTable->bindImage(image, VK_IMAGE_LAYOUT_GENERAL, 1, binding);
        frame.composeUses.emplace_back(image, gpu::ImageUsage::StorageRead);
    }
    frame.composeTable->bindImage(frame.output, VK_IMAGE_LAYOUT_GENERAL, 1, kComposeDenoised.size());
    frame.composeUses.emplace_back(frame.output, gpu::ImageUsage::StorageWrite, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);

    frame.boundWorldUniform = nullptr;
    frame.boundSkyUniform = nullptr;
}

void NrdModule::setAttributes(int attributeCount, std::vector<std::string> &attributeKVs) {
    const size_t count = std::min(static_cast<size_t>(std::max(attributeCount, 0)), attributeKVs.size() / 2);
    for (size_t i = 0; i < count; i++) {
        std::string_view key = attributeKVs[2 * i];
        if (!key.starts_with(kAttributePrefix)) continue;
        key.remove_prefix(kAttributePrefix.size());
        const auto attribute = std::ranges::find(kAttributes, key, &Attribute::name);
        if (attribute != std::end(kAttributes)) attribute->set(reblurSettings_, attributeKVs[2 * i + 1]);
    }
    clampReblurSettings(reblurSettings_);
}

bool NrdModule::setRuntimeAttribute(const std::string &key, const std::string &value) {
    std::string_view name = key;
    if (!name.starts_with(kAttributePrefix)) return false;
    name.remove_prefix(kAttributePrefix.size());
    const auto attribute = std::ranges::find(kAttributes, name, &Attribute::name);
    if (attribute == std::end(kAttributes)) return false;
    attribute->set(reblurSettings_, value);
    clampReblurSettings(reblurSettings_);
    // NRD reads a denoiser's settings at every dispatch, so new ones apply from the next frame and its history stays.
    if constexpr (kDenoiser == nrd::Denoiser::REBLUR_DIFFUSE_SPECULAR) {
        if (wrapper_ == nullptr) return false;
        wrapper_->setReblurSettings(kDenoiser, reblurSettings_);
    }
    return true;
}

std::vector<std::shared_ptr<WorldModuleContext>> &NrdModule::contexts() {
    return contexts_;
}

void NrdModule::bindTexture(std::shared_ptr<gpu::Sampler>, std::shared_ptr<gpu::DeviceLocalImage>, int) {}

void NrdModule::preClose() {
    wrapper_ = nullptr;
}

void NrdModule::record(FrameworkContext &context) {
    if (wrapper_ == nullptr) return;

    auto buffers = Renderer::instance().buffers();
    auto worldUniform = buffers->worldUniformBuffer();
    auto lastWorldUniform = buffers->lastWorldUniformBuffer();
    auto skyUniform = buffers->skyUniformBuffer();
    if (worldUniform == nullptr || lastWorldUniform == nullptr || skyUniform == nullptr) {
        static bool reported = false;
        if (!reported) {
            radiante::err() << "[NRD] skipped a frame: the world or sky uniforms are not created yet" << std::endl;
            reported = true;
        }
        return;
    }

    Frame &frame = *frames_.at(context.frameIndex);
    if (frame.boundWorldUniform != worldUniform) {
        frame.prepareTable->bindBuffer(worldUniform, 2, 0);
        frame.boundWorldUniform = worldUniform;
    }
    if (frame.boundSkyUniform != skyUniform) {
        frame.composeTable->bindBuffer(skyUniform, 2, 0);
        frame.boundSkyUniform = skyUniform;
    }

    const auto &world = *static_cast<const gpu::Data::WorldUBO *>(worldUniform->mappedPtr());
    const auto &lastWorld = *static_cast<const gpu::Data::WorldUBO *>(lastWorldUniform->mappedPtr());
    wrapper_->setCommonSettings(commonSettings(world, lastWorld, width_, height_, nrdFrameIndex_));
    nrdFrameIndex_++;

    gpu::CommandBuffer &commands = *context.worldCommandBuffer;
    const uint32_t groupsX = (width_ + kGroupSize - 1) / kGroupSize;
    const uint32_t groupsY = (height_ + kGroupSize - 1) / kGroupSize;

    commands.sync(frame.prepareUses);
    commands.bindDescriptorTable(frame.prepareTable, VK_PIPELINE_BIND_POINT_COMPUTE)
        ->bindComputePipeline(preparePipeline_);
    vkCmdDispatch(commands.vkCommandBuffer(), groupsX, groupsY, 1);

    wrapper_->denoise(commands, context.frameIndex, kDenoiser, nrdTextures_);

    commands.sync(frame.composeUses);
    commands.bindDescriptorTable(frame.composeTable, VK_PIPELINE_BIND_POINT_COMPUTE)
        ->bindComputePipeline(composePipeline_);
    vkCmdDispatch(commands.vkCommandBuffer(), groupsX, groupsY, 1);
}
