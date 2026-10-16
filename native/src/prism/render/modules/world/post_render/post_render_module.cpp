#include "prism/render/modules/world/post_render/post_render_module.hpp"

#include "prism/render/buffers.hpp"
#include "prism/render/entities.hpp"
#include "prism/render/pipeline.hpp"
#include "prism/render/render_framework.hpp"
#include "prism/render/renderer.hpp"
#include "prism/render/world.hpp"
#include "prism/util/logging.hpp"
#include "prism/util/parallel.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <random>
#include <span>
#include <stdexcept>

namespace {

#ifdef DEBUG
constexpr bool kLogBuildTimes = true;
#else
constexpr bool kLogBuildTimes = false;
#endif

constexpr std::string_view kStarCountAttribute = "render_pipeline.module.post_render.attribute.star_count";
constexpr std::string_view kStarMinSizeAttribute = "render_pipeline.module.post_render.attribute.star_min_size";
constexpr std::string_view kStarMaxSizeAttribute = "render_pipeline.module.post_render.attribute.star_max_size";
constexpr std::string_view kStarRadiusAttribute = "render_pipeline.module.post_render.attribute.star_radius";

// The names the shader pack gives this module's images.
constexpr std::string_view kOutputImage = "out:ldr";
constexpr std::string_view kFirstHitDepthImage = "out:first_hit_depth";
constexpr std::string_view kHdrImage = "out:hdr";
constexpr std::string_view kMotionVectorImage = "out:motion_vector";
constexpr std::string_view kNormalRoughnessImage = "out:normal_roughness";

// Descriptor sets 0 to 3 are the module's own (createDescriptorTable); the shader pack's runtime resources take the
// next one and its execution variables the one after.
constexpr uint32_t kRuntimeResourceSet = 4;
constexpr uint32_t kBindlessTextureCount = 4096;
// A while loop in the execution script fails after this many rounds.
constexpr uint32_t kScriptLoopLimit = 1u << 16;

constexpr VkShaderStageFlags kDrawShaders = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
constexpr VkPipelineStageFlags2 kDrawShaderStages =
    VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
constexpr VkAccessFlags2 kWriteAccess = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT |
                                        VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                                        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT |
                                        VK_ACCESS_2_HOST_WRITE_BIT;

// The layout the world pipeline's output is handed on in.
#ifdef USE_AMD
constexpr VkImageLayout kOutputHandOffLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
#else
constexpr VkImageLayout kOutputHandOffLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
#endif

// Geometry without a content name looks for a shader variant named "".
const std::string kNoContentName;

constexpr std::pair<std::string_view, RenderPass::Target> kRenderContents[] = {
    {"weather", RenderPass::Target::Weather},    {"particle", RenderPass::Target::Particle},
    {"particles", RenderPass::Target::Particle}, {"text", RenderPass::Target::Text},
    {"name_tag", RenderPass::Target::NameTag},   {"nametag", RenderPass::Target::NameTag},
    {"name-tag", RenderPass::Target::NameTag},   {"star", RenderPass::Target::Star},
    {"stars", RenderPass::Target::Star},
};

constexpr std::pair<std::string_view, VkCompareOp> kDepthCompares[] = {
    {"never", VK_COMPARE_OP_NEVER},
    {"less", VK_COMPARE_OP_LESS},
    {"equal", VK_COMPARE_OP_EQUAL},
    {"less_or_equal", VK_COMPARE_OP_LESS_OR_EQUAL},
    {"lequal", VK_COMPARE_OP_LESS_OR_EQUAL},
    {"greater", VK_COMPARE_OP_GREATER},
    {"not_equal", VK_COMPARE_OP_NOT_EQUAL},
    {"greater_or_equal", VK_COMPARE_OP_GREATER_OR_EQUAL},
    {"gequal", VK_COMPARE_OP_GREATER_OR_EQUAL},
    {"always", VK_COMPARE_OP_ALWAYS},
};

RenderPass::Target parseRenderContent(const std::string &content) {
    const std::string name = ShaderPackLoader::toLower(content);
    for (const auto &[key, target] : kRenderContents) {
        if (name == key) return target;
    }
    throw std::runtime_error("unsupported post_render render content: " + content);
}

VkCompareOp parseDepthCompare(const std::string &value) {
    const std::string name = ShaderPackLoader::toLower(value);
    for (const auto &[key, compare] : kDepthCompares) {
        if (name == key) return compare;
    }
    throw std::runtime_error("unsupported post_render depth_compare: " + value);
}

// The flag an entity of the frame's post-rendered batch carries when passes of this content draw it. Stars are not
// entities.
int entityPostFlag(RenderPass::Target target) {
    switch (target) {
        case RenderPass::Target::Weather: return 0b0001;
        case RenderPass::Target::Particle: return 0b0010;
        case RenderPass::Target::Text: return 0b0100;
        case RenderPass::Target::NameTag: return 0b1000;
        case RenderPass::Target::Star: return 0;
    }
    return 0;
}

// Text, name tags and stars write depth unless their pass says otherwise; particles and weather do not.
bool writesDepthByDefault(RenderPass::Target target) {
    return target == RenderPass::Target::Text || target == RenderPass::Target::NameTag ||
           target == RenderPass::Target::Star;
}

uint32_t parseCount(const std::string &value, uint32_t fallback) {
    try {
        const long long parsed = std::stoll(value);
        if (parsed < 0) return fallback;
        return static_cast<uint32_t>(std::min<long long>(parsed, std::numeric_limits<uint32_t>::max()));
    } catch (const std::exception &) { return fallback; }
}

float parseFloat(const std::string &value, float fallback) {
    try {
        return std::stof(value);
    } catch (const std::exception &) { return fallback; }
}

std::shared_ptr<gpu::Shader> loadShader(const std::shared_ptr<gpu::Device> &device, std::string_view name) {
    return gpu::Shader::create(device, (Renderer::folderPath / "shaders" / name).string());
}

// An attachment that keeps its layout through the pass and stores what the pass leaves in it.
VkAttachmentDescription attachment(VkFormat format, VkAttachmentLoadOp loadOp, VkImageLayout layout) {
    return {
        .format = format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = loadOp,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = layout,
        .finalLayout = layout,
    };
}

gpu::GraphicsPipelineBuilder::ViewPortAndScissor wholeTarget(VkExtent2D extent) {
    return {
        .viewport = {.x = 0.0f,
                     .y = 0.0f,
                     .width = static_cast<float>(extent.width),
                     .height = static_cast<float>(extent.height),
                     .minDepth = 0.0f,
                     .maxDepth = 1.0f},
        .scissor = {.offset = {.x = 0, .y = 0}, .extent = extent},
    };
}

constexpr VkColorComponentFlags kRgba =
    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

// What a render pass draws is blended over the output by its alpha, unless the pass turns blending off.
VkPipelineColorBlendAttachmentState outputBlend(bool blend) {
    return {
        .blendEnable = blend ? VK_TRUE : VK_FALSE,
        .srcColorBlendFactor = blend ? VK_BLEND_FACTOR_SRC_ALPHA : VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = blend ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA : VK_BLEND_FACTOR_ZERO,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = kRgba,
    };
}

// The first-hit distance a render pass writes replaces the traced one.
constexpr VkPipelineColorBlendAttachmentState kFirstHitDepthBlend = {
    .blendEnable = VK_FALSE,
    .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
    .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
    .colorBlendOp = VK_BLEND_OP_ADD,
    .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
    .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
    .alphaBlendOp = VK_BLEND_OP_ADD,
    .colorWriteMask = VK_COLOR_COMPONENT_R_BIT,
};

// Particles, rain and text are flat quads seen from either side.
constexpr VkPipelineRasterizationStateCreateInfo kDoubleSidedFill = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
    .depthClampEnable = VK_FALSE,
    .rasterizerDiscardEnable = VK_FALSE,
    .polygonMode = VK_POLYGON_MODE_FILL,
    .cullMode = VK_CULL_MODE_NONE,
    .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
    .depthBiasEnable = VK_FALSE,
    .depthBiasConstantFactor = 0.0f,
    .depthBiasClamp = 0.0f,
    .depthBiasSlopeFactor = 0.0f,
    .lineWidth = 1.0f,
};

double millisecondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

// Times the steps of a build; only debug builds print them.
class StepLog {
  public:
    explicit StepLog(std::string_view scope) : scope_(scope) {}

    void step(std::string_view label) {
        if constexpr (kLogBuildTimes) {
            radiante::err() << scope_ << label << ": " << millisecondsSince(last_) << " ms" << std::endl;
            last_ = std::chrono::steady_clock::now();
        }
    }

  private:
    std::string_view scope_;
    std::chrono::steady_clock::time_point last_ = std::chrono::steady_clock::now();
};

gpu::VertexFormat::PBRVertex starVertex(const glm::vec3 &position, const glm::vec4 &color) {
    gpu::VertexFormat::PBRVertex vertex{};
    vertex.pos = position;
    vertex.useColorLayer = 1;
    vertex.colorLayer = color;
    vertex.coordinate = static_cast<uint32_t>(World::Coordinates::CAMERA_SHIFT);
    return vertex;
}

// CommandBuffer::sync records nothing for a read after reads in the same layout, so a read in stages the earlier
// readers did not cover would have no dependency on the write before them. This makes those stages wait on the earlier
// readers, which chains them to that write, and adds them to what the image's next writer waits on.
void extendReadDependencies(gpu::CommandBuffer &commandBuffer, std::span<const gpu::ImageUse> uses) {
    VkPipelineStageFlags2 srcStages = VK_PIPELINE_STAGE_2_NONE;
    VkPipelineStageFlags2 dstStages = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 dstAccess = VK_ACCESS_2_NONE;
    for (const gpu::ImageUse &use : uses) {
        if (use.image == nullptr || use.stages == 0) continue;
        VkImageLayout layout;
        VkAccessFlags2 access;
        if (use.usage == gpu::ImageUsage::Sampled) {
            layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        } else if (use.usage == gpu::ImageUsage::StorageRead) {
            layout = VK_IMAGE_LAYOUT_GENERAL;
            access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
        } else {
            continue;
        }
        gpu::ImageState &state = use.image->state();
        if (state.layout != layout || (state.access & kWriteAccess) != 0) continue; // sync records a barrier itself
        const VkPipelineStageFlags2 missing = use.stages & ~state.stages;
        if (missing == 0 || state.stages == VK_PIPELINE_STAGE_2_NONE) continue;
        srcStages |= state.stages;
        dstStages |= missing;
        dstAccess |= access;
        state.stages |= missing;
        state.access |= access;
    }
    if (dstStages != VK_PIPELINE_STAGE_2_NONE) commandBuffer.memoryBarrier(srcStages, VK_ACCESS_2_NONE, dstStages, dstAccess);
}

} // namespace

// Everything recording one frame needs.
struct PostRenderModule::Recording {
    std::shared_ptr<gpu::CommandBuffer> commandBuffer; // the frame's world command buffer
    const FrameResources &frame;
    uint32_t frameIndex;
    uint32_t queueIndex;
};

PostRenderModule::PostRenderModule() = default;

void PostRenderModule::init(std::shared_ptr<Framework> framework, std::shared_ptr<WorldPipeline> worldPipeline) {
    WorldModule::init(framework, worldPipeline);
    frames_.resize(framework->swapchain()->imageCount());
}

bool PostRenderModule::setOrCreateInputImages(std::vector<std::shared_ptr<gpu::DeviceLocalImage>> &images,
                                              std::vector<VkFormat> &formats,
                                              uint32_t frameIndex) {
    if (images.size() != inputImageNum) return false;

    // The pipeline builds its modules from last to first, so an input is usually still missing here; it is then made
    // with the uses the modules before this one put it to.
    constexpr VkImageUsageFlags drawnInto =
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    constexpr VkImageUsageFlags copied = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                         VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    constexpr std::array<VkImageUsageFlags, inputImageNum> usages = {drawnInto, drawnInto, copied, copied, copied};

    FrameResources &frame = frames_[frameIndex];
    const std::array<std::shared_ptr<gpu::DeviceLocalImage> *, inputImageNum> slots = {
        &frame.ldr, &frame.firstHitDepth, &frame.hdr, &frame.motionVector, &frame.normalRoughness};

    auto framework = framework_.lock();
    for (uint32_t i = 0; i < inputImageNum; i++) {
        if (images[i] == nullptr) {
            images[i] = gpu::DeviceLocalImage::create(
                framework->device(), framework->vma(),
                gpu::ImageDesc{.width = width_, .height = height_, .format = formats[i], .usage = usages[i]});
        } else if (images[i]->width() != width_ || images[i]->height() != height_) {
            return false;
        }
        *slots[i] = images[i];
    }
    return true;
}

bool PostRenderModule::setOrCreateOutputImages(std::vector<std::shared_ptr<gpu::DeviceLocalImage>> &images,
                                               std::vector<VkFormat> &,
                                               uint32_t frameIndex) {
    if (images.size() != outputImageNum || images[0] == nullptr) return false;

    width_ = images[0]->width();
    height_ = images[0]->height();
    frames_[frameIndex].output = images[0];
    return true;
}

void PostRenderModule::setAttributes(int attributeCount, std::vector<std::string> &attributeKVs) {
    for (int i = 0; i < attributeCount; i++) {
        const std::string &key = attributeKVs[2 * i];
        const std::string &value = attributeKVs[2 * i + 1];

        if (key == kStarCountAttribute) {
            starCount_ = std::max(1u, parseCount(value, starCount_));
        } else if (key == kStarMinSizeAttribute) {
            starSizeMin_ = std::max(0.001f, parseFloat(value, starSizeMin_));
        } else if (key == kStarMaxSizeAttribute) {
            starSizeMax_ = std::max(0.001f, parseFloat(value, starSizeMax_));
        } else if (key == kStarRadiusAttribute) {
            starRadius_ = std::max(1.0f, parseFloat(value, starRadius_));
        }
    }

    if (starSizeMin_ > starSizeMax_) std::swap(starSizeMin_, starSizeMax_);
}

bool PostRenderModule::setRuntimeAttribute(const std::string &key, const std::string &value) {
    if (key != kStarCountAttribute && key != kStarMinSizeAttribute && key != kStarMaxSizeAttribute &&
        key != kStarRadiusAttribute) {
        return false;
    }
    std::vector<std::string> attribute = {key, value};
    setAttributes(1, attribute);
    // The stars are baked into a vertex buffer, so it is made again. The one it replaces is freed only once no frame
    // in flight can still draw from it (DeviceLocalBuffer retires its memory).
    createStarField();
    return true;
}

void PostRenderModule::build() {
    if constexpr (kLogBuildTimes) radiante::err() << "[PostRender build] ====== start ======" << std::endl;
    StepLog log("[PostRender build] ");

    auto framework = framework_.lock();
    auto worldPipeline = worldPipeline_.lock();

    shaderPack_ = worldPipeline != nullptr ? worldPipeline->shaderPack() : nullptr;
    executionVariableConfigs_.clear();
    globalVariables_.clear();
    scriptVariables_.clear();
    if (shaderPack_ != nullptr) {
        shaderPack_->copyStageExecutionState(ShaderPackLoader::Stage::PostRender, executionVariableConfigs_,
                                             globalVariables_);
    }
    expressionVariables_ = {
        {.name = "RENDER_WIDTH", .value = static_cast<double>(width_)},
        {.name = "RENDER_HEIGHT", .value = static_cast<double>(height_)},
    };
    log.step("shader pack");

    createFrameResources();
    log.step("frame resources");
    createDepthPass();
    log.step("depth pass");
    fullScreenVertexShader_ = loadShader(framework->device(), "full_screen_vert.spv");
    createStarField();
    log.step("star field");

    contexts_.resize(frames_.size());
    for (size_t i = 0; i < frames_.size(); i++) {
        contexts_[i] =
            PostRenderModuleContext::create(framework->contexts()[i], worldPipeline->contexts()[i], shared_from_this());
    }
    log.step("contexts");

    // Usually too early: the pack's runtime resources are made by the ray tracing module, which is built after this
    // one. The first frame builds the passes then.
    ensureDynamicPipelines();
    log.step("ensureDynamicPipelines");
    if constexpr (kLogBuildTimes) radiante::err() << "[PostRender build] ====== done ======" << std::endl;
}

std::vector<std::shared_ptr<WorldModuleContext>> &PostRenderModule::contexts() {
    return contexts_;
}

void PostRenderModule::bindTexture(std::shared_ptr<gpu::Sampler> sampler,
                                   std::shared_ptr<gpu::DeviceLocalImage> image,
                                   int index) {
    for (const FrameResources &frame : frames_) {
        if (frame.descriptorTable == nullptr) continue;
        frame.descriptorTable->bindSamplerImage(sampler, image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, 0, index);
    }
}

void PostRenderModule::preClose() {
    fullScreenPasses_.clear();
    renderPasses_.clear();
    fullScreenPassByName_.clear();
    renderPassByName_.clear();
    executionVariableConfigs_.clear();
    globalVariables_.clear();
    scriptVariables_.clear();
    shaderPack_ = nullptr;
    dynamicPipelinesReady_ = false;
}

// ---- resources

std::shared_ptr<gpu::DescriptorTable>
PostRenderModule::createDescriptorTable(const std::shared_ptr<gpu::Device> &device) const {
    gpu::DescriptorTableBuilder builder;
    auto addSet = [&builder](std::initializer_list<VkDescriptorSetLayoutBinding> bindings) {
        auto &set = builder.beginDescriptorLayoutSet().beginDescriptorLayoutSetBinding();
        for (const VkDescriptorSetLayoutBinding &binding : bindings) set.defineDescriptorLayoutSetBinding(binding);
        set.endDescriptorLayoutSetBinding().endDescriptorLayoutSet();
    };
    // 0: the bindless textures, and the first-hit distances as a storage image (binding 2).
    addSet({
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kBindlessTextureCount, kDrawShaders},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
    });
    // 1: the world and sky uniforms.
    addSet({
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, kDrawShaders},
        {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, kDrawShaders},
    });
    // 2: the texture mapping.
    addSet({{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, kDrawShaders}});
    // 3: the frame's images, sampled: output, first-hit distances, hdr, motion vectors, normal and roughness.
    addSet({
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
    });
    if (shaderPack_ != nullptr) {
        shaderPack_->defineRuntimeResourceDescriptorSet(builder, kDrawShaders, kDrawShaders, kDrawShaders);
        shaderPack_->defineExecutionDescriptorSet(builder, ShaderPackLoader::Stage::PostRender, kDrawShaders);
    }
    return builder.build(device);
}

void PostRenderModule::createFrameResources() {
    auto framework = framework_.lock();
    auto device = framework->device();

    colorSampler_ = gpu::Sampler::create(device, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR,
                                         VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    depthSampler_ = gpu::Sampler::create(device, VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST,
                                         VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

    for (FrameResources &frame : frames_) {
        frame.depth =
            gpu::DeviceLocalImage::create(device, framework->vma(),
                                          gpu::ImageDesc{.width = width_,
                                                         .height = height_,
                                                         .format = VK_FORMAT_D32_SFLOAT,
                                                         .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT});

        frame.descriptorTable = createDescriptorTable(device);
        gpu::DescriptorTable &table = *frame.descriptorTable;
        table.bindImage(frame.firstHitDepth, VK_IMAGE_LAYOUT_GENERAL, 0, 2);
        table.bindSamplerImage(colorSampler_, frame.output, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 3, 0, 0);
        table.bindSamplerImage(depthSampler_, frame.firstHitDepth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 3, 1, 0);
        table.bindSamplerImage(colorSampler_, frame.hdr, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 3, 2, 0);
        table.bindSamplerImage(colorSampler_, frame.motionVector, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 3, 3, 0);
        table.bindSamplerImage(colorSampler_, frame.normalRoughness, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 3, 4, 0);
    }
}

// The depth pass turns the first-hit distances into the depth buffer the render passes test against.
void PostRenderModule::createDepthPass() {
    auto framework = framework_.lock();
    auto device = framework->device();
    const FrameResources &first = frames_.front();

    depthRenderPass_ =
        gpu::RenderPassBuilder{}
            .beginAttachmentDescription()
            .defineAttachmentDescription(attachment(first.depth->vkFormat(), VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL))
            .endAttachmentDescription()
            .beginAttachmentReference()
            .defineAttachmentReference({.attachment = 0, .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL})
            .endAttachmentReference()
            .beginSubpassDescription()
            .defineSubpassDescription(
                {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .depthStencilAttachmentIndex = 0})
            .endSubpassDescription()
            .build(device);

    for (FrameResources &frame : frames_) {
        frame.depthPass = {
            .renderPass = depthRenderPass_,
            .framebuffer = gpu::FramebufferBuilder{}
                               .beginAttachment()
                               .defineAttachment(frame.depth)
                               .endAttachment()
                               .build(device, depthRenderPass_),
            .renderAreaExtent = {frame.depth->width(), frame.depth->height()},
            .clearValues = {VkClearValue{.depthStencil = {.depth = 1.0f}}},
        };
    }

    depthPipeline_ = gpu::GraphicsPipelineBuilder{}
                         .defineRenderPass(depthRenderPass_, 0)
                         .beginShaderStage()
                         .defineShaderStage(loadShader(device, "world/post_render/color_to_depth_vert.spv"),
                                            VK_SHADER_STAGE_VERTEX_BIT)
                         .defineShaderStage(loadShader(device, "world/post_render/color_to_depth_frag.spv"),
                                            VK_SHADER_STAGE_FRAGMENT_BIT)
                         .endShaderStage()
                         .defineVertexInputState<void>()
                         .defineViewportScissorState(wholeTarget({first.depth->width(), first.depth->height()}))
                         .defineDepthStencilState({
                             .depthTestEnable = VK_TRUE,
                             .depthWriteEnable = VK_TRUE,
                             .depthCompareOp = VK_COMPARE_OP_LESS,
                             .depthBoundsTestEnable = VK_FALSE,
                             .stencilTestEnable = VK_FALSE,
                             .minDepthBounds = 0.0f,
                             .maxDepthBounds = 1.0f,
                         })
                         .beginColorBlendAttachmentState()
                         .endColorBlendAttachmentState()
                         .definePipelineLayout(first.descriptorTable)
                         .build(device);
}

// A fixed sky of star quads around the camera. The star pass's vertex shader turns the field with the sun, so the
// field leaves the sun's disc (+x) and the moon's (-x) empty.
void PostRenderModule::createStarField() {
    auto framework = framework_.lock();
    auto device = framework->device();

    // Sizes are given for a 1440 pixel tall image and shrink as the image grows taller, so a star covers about as
    // many pixels at any resolution.
    constexpr float referenceHeight = 1440.0f;
    const float resolutionScale = std::clamp(referenceHeight / static_cast<float>(std::max(height_, 1u)), 0.25f, 8.0f);
    const uint32_t count = std::max(1u, starCount_);
    const float sizeMin = std::max(0.001f, starSizeMin_ * resolutionScale);
    const float sizeMax = std::max(sizeMin, starSizeMax_ * resolutionScale);
    const float radius = std::max(1.0f, starRadius_);

    const float cosSunCone = std::cos(0.05f);
    const float cosMoonCone = std::cos(0.08f);

    // The same seed draws the same sky every time; the order of the draws below is what places each star.
    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    auto random = [&] { return unit(rng); };

    std::vector<gpu::VertexFormat::PBRVertex> vertices;
    vertices.reserve(static_cast<size_t>(count) * 6);
    for (uint32_t i = 0; i < count; i++) {
        glm::vec3 direction;
        do {
            // Uniform on the unit sphere.
            const float u = random();
            const float v = random();
            const float z = 1.0f - 2.0f * u;
            const float azimuth = 2.0f * glm::pi<float>() * v;
            const float ring = std::sqrt(std::max(0.0f, 1.0f - z * z));
            direction = glm::vec3(ring * std::cos(azimuth), ring * std::sin(azimuth), z);
        } while (direction.x > cosSunCone || direction.x < -cosMoonCone);

        // About a third of the stars are bright; the rest are faint, most of them very faint.
        const float brightness = random() < 0.35f ? 0.75f + 0.25f * random() : 0.02f + 0.35f * std::pow(random(), 3.0f);

        // Most are white, some warm, a few cold.
        const float tint = random();
        const glm::vec3 rgb = tint < 0.75f ? glm::vec3(1.0f, 1.0f, 1.0f) :
                              tint < 0.9f  ? glm::vec3(1.0f, 0.95f, 0.85f) :
                                             glm::vec3(0.85f, 0.9f, 1.0f);
        const glm::vec4 color(rgb * brightness, 1.0f);

        const glm::vec3 center = direction * radius;
        const float half = 0.5f * (sizeMin + random() * (sizeMax - sizeMin));

        // A square facing the camera, as two triangles.
        const glm::vec3 up = std::abs(direction.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1);
        const glm::vec3 right = glm::normalize(glm::cross(up, direction));
        const glm::vec3 down = glm::cross(direction, right);
        const glm::vec3 dx = right * half;
        const glm::vec3 dy = down * half;
        const glm::vec3 corners[4] = {center - dx - dy, center + dx - dy, center + dx + dy, center - dx + dy};
        for (int corner : {0, 1, 2, 2, 3, 0}) vertices.push_back(starVertex(corners[corner], color));
    }

    starVertexCount_ = static_cast<uint32_t>(vertices.size());
    // The staging copy is only needed until the upload below has run.
    starVertexBuffer_ = gpu::DeviceLocalBuffer::create(framework->vma(), device, false,
                                                       vertices.size() * sizeof(gpu::VertexFormat::PBRVertex),
                                                       VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    starVertexBuffer_->uploadToStagingBuffer(vertices.data());

    auto commandBuffer = gpu::CommandBuffer::create(device, framework->mainCommandPool());
    commandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    starVertexBuffer_->uploadToBuffer(commandBuffer);
    // Makes the copy visible to the vertex fetches of all later work on this queue, the frames' included.
    commandBuffer->memoryBarrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                 VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT, VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT);
    commandBuffer->end();
    auto fence = gpu::Fence::create(device);
    commandBuffer->submitMainQueueIndividual(device, fence);
    vkWaitForFences(device->vkDevice(), 1, &fence->vkFence(), VK_TRUE, UINT64_MAX);
}

// ---- the shader pack's passes

void PostRenderModule::ensureDynamicPipelines() {
    if (dynamicPipelinesReady_ || shaderPack_ == nullptr || !shaderPack_->runtimeResourcesReady()) return;
    StepLog log("[PostRender ensureDynamicPipelines] ");

    auto framework = framework_.lock();
    auto device = framework->device();
    const ShaderPackLoader::ShaderPack &pack = shaderPack_->shaderPack();
    const uint32_t executionSet = shaderPack_->executionSet(kRuntimeResourceSet);
    const size_t executionBufferSize = pack.postRenderExecution.variables.size() * sizeof(float);

    fullScreenPasses_.clear();
    renderPasses_.clear();
    fullScreenPassByName_.clear();
    renderPassByName_.clear();
    if (shaderPack_->hasRuntimeResources()) {
        for (uint32_t i = 0; i < frames_.size(); i++) {
            shaderPack_->bindRuntimeResources(frames_[i].descriptorTable, kRuntimeResourceSet, i);
        }
    }

    // Every pass's shaders are compiled together; each compiled shader goes into the slot its request names.
    std::vector<ShaderPack::ShaderCreateInfo> shaderRequests;
    std::vector<std::shared_ptr<gpu::Shader> *> shaderSlots;
    auto requestShader = [&](std::shared_ptr<gpu::Shader> &slot, const std::filesystem::path &path,
                             VkShaderStageFlagBits stage,
                             const std::unordered_map<std::string, std::string> &definitions) {
        shaderRequests.push_back({
            .path = path,
            .stage = stage,
            .definitions = definitions,
            .executionStage = ShaderPackLoader::Stage::PostRender,
            .executionSet = executionSet,
        });
        shaderSlots.push_back(&slot);
    };

    std::vector<VkExtent2D> fullScreenExtents;
    for (const ShaderPackLoader::PassConfig &config : pack.passes) {
        if (config.stage != ShaderPackLoader::Stage::PostRender) continue;
        const auto passStart = std::chrono::steady_clock::now();

        switch (config.type) {
            case ShaderPackLoader::PassConfig::Type::FullScreen: {
                auto [pass, extent] = createFullScreenPass(config.fullScreen, executionBufferSize);
                pass->fragmentShaders.resize(1);
                requestShader(pass->fragmentShaders[0], pass->config.fragmentShaderPath, VK_SHADER_STAGE_FRAGMENT_BIT,
                              pass->config.definitions);
                fullScreenPasses_.push_back(std::move(pass));
                fullScreenExtents.push_back(extent);
                break;
            }
            case ShaderPackLoader::PassConfig::Type::Render: {
                auto pass = createRenderPass(config.render, executionBufferSize);
                for (const auto &[variantName, shaders] : pass->config.shaderConfigs) {
                    RenderPass::ShaderVariant &variant = pass->shaderVariants[variantName];
                    requestShader(variant.vertexShader, shaders.vertexShaderPath, VK_SHADER_STAGE_VERTEX_BIT,
                                  pass->config.definitions);
                    requestShader(variant.fragmentShader, shaders.fragmentShaderPath, VK_SHADER_STAGE_FRAGMENT_BIT,
                                  pass->config.definitions);
                }
                renderPasses_.push_back(std::move(pass));
                break;
            }
            case ShaderPackLoader::PassConfig::Type::RayTracing:
            case ShaderPackLoader::PassConfig::Type::Compute:
                throw std::runtime_error("post_render stage only supports full_screen and render passes");
        }

        if constexpr (kLogBuildTimes) {
            const std::string &name = config.type == ShaderPackLoader::PassConfig::Type::FullScreen ?
                                          config.fullScreen.name :
                                          config.render.name;
            radiante::err() << "[PostRender ensureDynamicPipelines]   create pass '" << name
                            << "': " << millisecondsSince(passStart) << " ms" << std::endl;
        }
    }
    log.step("create all passes");

#ifdef DEBUG
    ShaderPack::ShaderBatchStats stats;
    auto shaders = shaderPack_->createShaders(device, shaderRequests, &stats);
    std::string compileInfo = "build shader modules (" + std::to_string(stats.requestCount) + " requests, " +
                              std::to_string(stats.uniqueShaderCount) + " unique, " +
                              std::to_string(stats.cacheHitCount) + " cache hits, " +
                              std::to_string(stats.cacheMissCount) + " compiled";
    if (stats.cacheReadFailureCount > 0) {
        compileInfo += ", " + std::to_string(stats.cacheReadFailureCount) + " cache read failures";
    }
    log.step(compileInfo + ")");
#else
    auto shaders = shaderPack_->createShaders(device, shaderRequests);
#endif
    for (size_t i = 0; i < shaders.size(); i++) *shaderSlots[i] = std::move(shaders[i]);

    // The driver compiles each pipeline; all the passes' pipelines are spread over one set of threads.
    const size_t fullScreenCount = fullScreenPasses_.size();
    std::vector<double> buildTimes(fullScreenCount + renderPasses_.size(), 0.0);
    prism::parallelFor(
        buildTimes.size(),
        [&](size_t job) {
            const auto start = std::chrono::steady_clock::now();
            if (job < fullScreenCount) {
                buildFullScreenPipeline(device, *fullScreenPasses_[job], fullScreenExtents[job]);
            } else {
                buildRenderPipelines(device, *renderPasses_[job - fullScreenCount]);
            }
            buildTimes[job] = millisecondsSince(start);
        },
        "PRISM_POST_PIPELINE_BUILD_THREADS");

    for (size_t i = 0; i < fullScreenPasses_.size(); i++) {
        const auto &pass = fullScreenPasses_[i];
        if constexpr (kLogBuildTimes) {
            radiante::err() << "[PostRender ensureDynamicPipelines]   build full_screen pass '" << pass->config.name
                            << "': " << buildTimes[i] << " ms" << std::endl;
        }
        if (!fullScreenPassByName_.emplace(pass->config.name, pass).second) {
            throw std::runtime_error("duplicate post_render pass name: " + pass->config.name);
        }
    }
    for (size_t i = 0; i < renderPasses_.size(); i++) {
        const auto &pass = renderPasses_[i];
        if constexpr (kLogBuildTimes) {
            radiante::err() << "[PostRender ensureDynamicPipelines]   build render pass '" << pass->config.name
                            << "': " << buildTimes[fullScreenCount + i] << " ms" << std::endl;
        }
        if (!renderPassByName_.emplace(pass->config.name, pass).second ||
            fullScreenPassByName_.contains(pass->config.name)) {
            throw std::runtime_error("duplicate post_render pass name: " + pass->config.name);
        }
    }
    log.step("build all passes");

    dynamicPipelinesReady_ = true;
}

std::pair<std::shared_ptr<FullScreenPass>, VkExtent2D>
PostRenderModule::createFullScreenPass(const ShaderPackLoader::FullScreenPassConfig &config,
                                       size_t executionBufferSize) {
    if (config.outputs.images.size() != 1 || config.outputs.images[0] != config.target) {
        throw std::runtime_error("post_render full_screen pass must only write to its target: " + config.name);
    }
    auto target = findTargetImage(config.target, 0);
    if (target == nullptr) throw std::runtime_error("invalid post_render target: " + config.target);

    // A pass draws into one layer of an array target, through that layer's own view (view 0 shows every layer).
    uint32_t targetView = 0;
    if (target->layer() > 1) {
        if (!config.layer.has_value()) {
            throw std::runtime_error("post_render full_screen array target requires layer: " + config.name);
        }
        if (*config.layer >= target->layer()) {
            throw std::runtime_error("post_render full_screen layer out of range: " + config.name);
        }
        targetView = *config.layer + 1;
    }

    auto framework = framework_.lock();
    auto device = framework->device();
    auto pass = std::make_shared<FullScreenPass>();
    pass->config = config;
    if (executionBufferSize > 0) {
        pass->executionBuffer = ShaderPack::createPassExecutionBuffer(device, framework->vma(), executionBufferSize);
    }

    // Every texel is drawn, so what the target held is not loaded.
    pass->renderPass =
        gpu::RenderPassBuilder{}
            .beginAttachmentDescription()
            .defineAttachmentDescription(attachment(target->vkFormat(), VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL))
            .endAttachmentDescription()
            .beginAttachmentReference()
            .defineAttachmentReference({.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL})
            .endAttachmentReference()
            .beginSubpassDescription()
            .defineSubpassDescription(
                {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentIndices = {0}})
            .endSubpassDescription()
            .build(device);

    pass->framebuffers.resize(frames_.size());
    for (uint32_t i = 0; i < frames_.size(); i++) {
        pass->framebuffers[i].push_back(gpu::FramebufferBuilder{}
                                            .beginAttachment()
                                            .defineAttachment(findTargetImage(config.target, i), targetView)
                                            .endAttachment()
                                            .build(device, pass->renderPass));
    }
    return {std::move(pass), VkExtent2D{target->width(), target->height()}};
}

std::shared_ptr<RenderPass> PostRenderModule::createRenderPass(const ShaderPackLoader::RenderPassConfig &config,
                                                               size_t executionBufferSize) {
    const std::vector<std::string> &outputs = config.outputs.images;
    auto writes = [&outputs](std::string_view image) {
        return std::find(outputs.begin(), outputs.end(), image) != outputs.end();
    };
    if (!writes(kOutputImage)) {
        throw std::runtime_error("post_render render pass must output out:ldr: " + config.name);
    }
    for (const std::string &output : outputs) {
        if (output != kOutputImage && output != kFirstHitDepthImage) {
            throw std::runtime_error("post_render render pass only supports out:ldr and out:first_hit_depth outputs: " +
                                     config.name);
        }
    }

    auto framework = framework_.lock();
    auto device = framework->device();
    auto pass = std::make_shared<RenderPass>();
    pass->config = config;
    pass->target = parseRenderContent(config.content);
    pass->writesFirstHitDepth = writes(kFirstHitDepthImage);
    if (executionBufferSize > 0) {
        pass->executionBuffer = ShaderPack::createPassExecutionBuffer(device, framework->vma(), executionBufferSize);
    }

    // Drawn over the output (and the first-hit distances, when the pass writes them), tested against the traced
    // depth; attachments and references share their numbering, the depth last.
    const FrameResources &first = frames_.front();
    std::vector<VkFormat> colorFormats = {first.output->vkFormat()};
    if (pass->writesFirstHitDepth) colorFormats.push_back(first.firstHitDepth->vkFormat());
    const auto depthAttachment = static_cast<uint32_t>(colorFormats.size());
    std::vector<uint32_t> colorAttachments;

    gpu::RenderPassBuilder builder;
    auto &descriptions = builder.beginAttachmentDescription();
    for (VkFormat format : colorFormats) {
        descriptions.defineAttachmentDescription(
            attachment(format, VK_ATTACHMENT_LOAD_OP_LOAD, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL));
    }
    descriptions
        .defineAttachmentDescription(attachment(first.depth->vkFormat(), VK_ATTACHMENT_LOAD_OP_LOAD,
                                                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL))
        .endAttachmentDescription();
    auto &references = builder.beginAttachmentReference();
    for (uint32_t i = 0; i < depthAttachment; i++) {
        references.defineAttachmentReference({.attachment = i, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
        colorAttachments.push_back(i);
    }
    references
        .defineAttachmentReference(
            {.attachment = depthAttachment, .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL})
        .endAttachmentReference();
    builder.beginSubpassDescription()
        .defineSubpassDescription({
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentIndices = colorAttachments,
            .depthStencilAttachmentIndex = depthAttachment,
        })
        .endSubpassDescription();
    pass->renderPass = builder.build(device);

    pass->framebuffers.resize(frames_.size());
    for (uint32_t i = 0; i < frames_.size(); i++) {
        const FrameResources &frame = frames_[i];
        gpu::FramebufferBuilder framebuffer;
        auto &images = framebuffer.beginAttachment().defineAttachment(frame.output);
        if (pass->writesFirstHitDepth) images.defineAttachment(frame.firstHitDepth);
        pass->framebuffers[i] = images.defineAttachment(frame.depth).endAttachment().build(device, pass->renderPass);
    }
    return pass;
}

void PostRenderModule::buildFullScreenPipeline(const std::shared_ptr<gpu::Device> &device,
                                               FullScreenPass &pass,
                                               VkExtent2D extent) const {
    pass.pipelines = {gpu::GraphicsPipelineBuilder{}
                          .defineRenderPass(pass.renderPass, 0)
                          .beginShaderStage()
                          .defineShaderStage(fullScreenVertexShader_, VK_SHADER_STAGE_VERTEX_BIT)
                          .defineShaderStage(pass.fragmentShaders[0], VK_SHADER_STAGE_FRAGMENT_BIT)
                          .endShaderStage()
                          .defineVertexInputState<void>()
                          .defineViewportScissorState(wholeTarget(extent))
                          .beginColorBlendAttachmentState()
                          .defineDefaultColorBlendAttachmentState()
                          .endColorBlendAttachmentState()
                          .definePipelineLayout(frames_.front().descriptorTable)
                          .build(device)};
}

// One pipeline for every shader variant of the pass; geometry picks its variant by content name (see
// collectEntityDraws), and the "default" variant's is also the pass's own pipeline.
void PostRenderModule::buildRenderPipelines(const std::shared_ptr<gpu::Device> &device, RenderPass &pass) const {
    const ShaderPackLoader::RenderPassConfig &config = pass.config;
    const bool depthTest = config.depthTest.value_or(true);
    const bool depthWrite = config.depthWrite.value_or(writesDepthByDefault(pass.target));
    // Name tags show through walls unless the pass says otherwise.
    const VkCompareOp depthCompare =
        parseDepthCompare(config.depthCompare.value_or(pass.target == RenderPass::Target::NameTag ? "always" : "less"));
    const FrameResources &first = frames_.front();
    const VkExtent2D extent = {first.output->width(), first.output->height()};

    for (auto &[variantName, variant] : pass.shaderVariants) {
        gpu::GraphicsPipelineBuilder builder;
        auto &blend = builder.defineRenderPass(pass.renderPass, 0)
                          .beginShaderStage()
                          .defineShaderStage(variant.vertexShader, VK_SHADER_STAGE_VERTEX_BIT)
                          .defineShaderStage(variant.fragmentShader, VK_SHADER_STAGE_FRAGMENT_BIT)
                          .endShaderStage()
                          .defineVertexInputState<gpu::VertexFormat::PBRVertex>()
                          .defineViewportScissorState(wholeTarget(extent))
                          .defineDepthStencilState({
                              .depthTestEnable = depthTest ? VK_TRUE : VK_FALSE,
                              .depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE,
                              .depthCompareOp = depthCompare,
                              .depthBoundsTestEnable = VK_FALSE,
                              .stencilTestEnable = VK_FALSE,
                              .minDepthBounds = 0.0f,
                              .maxDepthBounds = 1.0f,
                          })
                          .defineRasterizationState(kDoubleSidedFill)
                          .beginColorBlendAttachmentState()
                          .defineColorBlendAttachmentState(outputBlend(config.colorBlend));
        if (pass.writesFirstHitDepth) blend.defineColorBlendAttachmentState(kFirstHitDepthBlend);
        variant.pipeline =
            blend.endColorBlendAttachmentState().definePipelineLayout(first.descriptorTable).build(device);
        if (variantName == "default") pass.pipeline = variant.pipeline;
    }
}

// ---- recording a frame

void PostRenderModule::recordFrame(FrameworkContext &frameworkContext) {
    auto framework = frameworkContext.framework.lock();
    const uint32_t frameIndex = frameworkContext.frameIndex;
    Recording recording{
        .commandBuffer = frameworkContext.worldCommandBuffer,
        .frame = frames_[frameIndex],
        .frameIndex = frameIndex,
        .queueIndex = framework->physicalDevice()->mainQueueIndex(),
    };

    auto buffers = Renderer::instance().buffers();
    gpu::DescriptorTable &table = *recording.frame.descriptorTable;
    table.bindBuffer(buffers->worldUniformBuffer(), 1, 0);
    table.bindBuffer(buffers->skyUniformBuffer(), 1, 1);
    table.bindBuffer(buffers->textureMappingBuffer(), 2, 0);

    recordFirstHitDepth(recording);
    recordCopyToOutput(recording);

    ensureDynamicPipelines();
    // The pack may have made its runtime resources again since the last frame.
    if (shaderPack_ != nullptr && shaderPack_->hasRuntimeResources()) {
        shaderPack_->bindRuntimeResources(recording.frame.descriptorTable, kRuntimeResourceSet, frameIndex);
    }
    runScript(recording);

    recordOutputHandOff(recording);
}

void PostRenderModule::recordFirstHitDepth(Recording &recording) {
    gpu::CommandBuffer &commandBuffer = *recording.commandBuffer;
    const FrameResources &frame = recording.frame;

    // The pass clears the depth, so what the image held may be dropped.
    const gpu::ImageUse uses[] = {
        {frame.firstHitDepth, gpu::ImageUsage::StorageRead, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT},
        {frame.depth, gpu::ImageUsage::DepthAttachment, 0, true},
    };
    extendReadDependencies(commandBuffer, uses);
    commandBuffer.sync(uses);
    commandBuffer.beginRenderPass(frame.depthPass);
    commandBuffer.bindDescriptorTable(frame.descriptorTable, VK_PIPELINE_BIND_POINT_GRAPHICS)
        ->bindGraphicsPipeline(depthPipeline_)
        ->draw(3, 1)
        ->endRenderPass();
}

void PostRenderModule::recordCopyToOutput(Recording &recording) {
    gpu::CommandBuffer &commandBuffer = *recording.commandBuffer;
    const FrameResources &frame = recording.frame;

    // The blit writes every texel of the output, so what it held may be dropped.
    commandBuffer.sync({
        {frame.ldr, gpu::ImageUsage::TransferSrc},
        {frame.output, gpu::ImageUsage::TransferDst, 0, true},
    });

    VkImageBlit region{};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.srcOffsets[1] = {static_cast<int32_t>(frame.ldr->width()), static_cast<int32_t>(frame.ldr->height()), 1};
    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstOffsets[1] = {static_cast<int32_t>(frame.output->width()), static_cast<int32_t>(frame.output->height()),
                            1};
    vkCmdBlitImage(commandBuffer.vkCommandBuffer(), frame.ldr->vkImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   frame.output->vkImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_LINEAR);
}

void PostRenderModule::runScript(Recording &recording) {
    if (!dynamicPipelinesReady_) return;
    const auto &commands = shaderPack_->execution(ShaderPackLoader::Stage::PostRender).commands;
    if (commands.empty()) return;

    resetScriptVariables();
    shaderPack_->executeCommands(ShaderPackLoader::Stage::PostRender, commands, scriptVariables_, expressionVariables_,
                                 true, kScriptLoopLimit,
                                 [this, &recording](const std::string &passName, ExecutionVariables &variables) {
                                     runPass(recording, passName, variables);
                                 });
    for (auto &[name, value] : globalVariables_) {
        if (auto variable = scriptVariables_.find(name); variable != scriptVariables_.end()) {
            value = variable->second.value;
        }
    }
}

// The script starts every frame with only its globals, at the values the last frame left them; anything else it
// assigns lives for one frame.
void PostRenderModule::resetScriptVariables() {
    std::erase_if(scriptVariables_, [this](const auto &entry) { return !globalVariables_.contains(entry.first); });
    for (const auto &[name, value] : globalVariables_) {
        ExecutionVariable &variable = scriptVariables_[name];
        variable.name = name;
        variable.value = value;
        if (auto config = executionVariableConfigs_.find(name); config != executionVariableConfigs_.end()) {
            variable.type = config->second.type;
        } else {
            variable.type.clear();
        }
    }
}

void PostRenderModule::runPass(Recording &recording, const std::string &passName, const ExecutionVariables &variables) {
    if (auto pass = fullScreenPassByName_.find(passName); pass != fullScreenPassByName_.end()) {
        runFullScreenPass(recording, *pass->second, variables);
        return;
    }
    auto pass = renderPassByName_.find(passName);
    if (pass == renderPassByName_.end()) throw std::runtime_error("unknown post_render pass: " + passName);
    runRenderPass(recording, *pass->second, variables);
}

void PostRenderModule::runFullScreenPass(Recording &recording,
                                         const FullScreenPass &pass,
                                         const ExecutionVariables &variables) {
    gpu::CommandBuffer &commandBuffer = *recording.commandBuffer;
    auto target = findTargetImage(pass.config.target, recording.frameIndex);
    if (target == nullptr) throw std::runtime_error("unknown post_render target: " + pass.config.target);

    syncInputs(recording, pass.config.inputs, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
    commandBuffer.sync({{target, gpu::ImageUsage::ColorAttachment}});
    uploadExecutionValues(recording, pass.executionBuffer, variables);

    commandBuffer.beginRenderPass({
        .renderPass = pass.renderPass,
        .framebuffer = pass.framebuffers[recording.frameIndex][0],
        .renderAreaExtent = {target->width(), target->height()},
    });
    commandBuffer.bindGraphicsPipeline(pass.pipelines[0])
        ->bindDescriptorTable(recording.frame.descriptorTable, VK_PIPELINE_BIND_POINT_GRAPHICS)
        ->draw(3, 1)
        ->endRenderPass();
}

void PostRenderModule::runRenderPass(Recording &recording,
                                     const RenderPass &pass,
                                     const ExecutionVariables &variables) {
    gpu::CommandBuffer &commandBuffer = *recording.commandBuffer;
    const FrameResources &frame = recording.frame;

    syncInputs(recording, pass.config.inputs, kDrawShaderStages);
    imageUses_.clear();
    imageUses_.emplace_back(frame.output, gpu::ImageUsage::ColorAttachment);
    if (pass.writesFirstHitDepth) imageUses_.emplace_back(frame.firstHitDepth, gpu::ImageUsage::ColorAttachment);
    imageUses_.emplace_back(frame.depth, gpu::ImageUsage::DepthAttachment);
    commandBuffer.sync(imageUses_);
    uploadExecutionValues(recording, pass.executionBuffer, variables);

    // Nothing to draw: no render pass at all.
    const bool stars = pass.target == RenderPass::Target::Star;
    std::shared_ptr<EntityPostBatch> batch;
    if (stars) {
        if (starVertexBuffer_ == nullptr || starVertexCount_ == 0 || pass.pipeline == nullptr) return;
    } else {
        batch = Renderer::instance().world()->entities()->entityPostBatch();
        if (!collectEntityDraws(pass, batch.get())) return;
    }

    commandBuffer.beginRenderPass({
        .renderPass = pass.renderPass,
        .framebuffer = pass.framebuffers[recording.frameIndex],
        .renderAreaExtent = {frame.output->width(), frame.output->height()},
    });
    commandBuffer.bindDescriptorTable(frame.descriptorTable, VK_PIPELINE_BIND_POINT_GRAPHICS);
    if (stars) {
        commandBuffer.bindGraphicsPipeline(pass.pipeline)
            ->bindVertexBuffers(starVertexBuffer_)
            ->draw(starVertexCount_, 1);
    } else {
        const RenderPass::ShaderVariant *bound = nullptr;
        for (const EntityDraw &draw : entityDraws_) {
            if (draw.variant != bound) {
                commandBuffer.bindGraphicsPipeline(draw.variant->pipeline);
                bound = draw.variant;
            }
            commandBuffer.bindVertexBuffers(draw.entity->vertexBuffers[draw.geometry])
                ->bindIndexBuffer(draw.entity->indexBuffers[draw.geometry])
                ->drawIndexed(draw.entity->indexCounts[draw.geometry], 1);
        }
    }
    commandBuffer.endRenderPass();
}

// The geometry of this frame's post-rendered entities that the pass draws, in batch order: each piece with the shader
// variant named after its content, or else the pass's "default" one. True when there is any.
bool PostRenderModule::collectEntityDraws(const RenderPass &pass, const EntityPostBatch *batch) {
    entityDraws_.clear();
    if (batch == nullptr) return false;

    const int flag = entityPostFlag(pass.target);
    const auto fallback = pass.shaderVariants.find("default");
    for (const auto &entity : batch->entities) {
        if (entity->postRenderFlag != flag) continue;
        for (uint32_t geometry = 0; geometry < entity->geometryCount; geometry++) {
            const std::string &content = geometry < entity->geometryContentNames.size() ?
                                             entity->geometryContentNames[geometry] :
                                             kNoContentName;
            auto variant = pass.shaderVariants.find(content);
            if (variant == pass.shaderVariants.end()) variant = fallback;
            if (variant == pass.shaderVariants.end() || variant->second.pipeline == nullptr) continue;
            entityDraws_.push_back({&variant->second, entity.get(), geometry});
        }
    }
    return !entityDraws_.empty();
}

// Brings a pass's inputs into the layout they are sampled in, for the given shader stages.
void PostRenderModule::syncInputs(Recording &recording,
                                  const ShaderPackLoader::ResourceList &inputs,
                                  VkPipelineStageFlags2 stages) {
    imageUses_.clear();
    for (const std::string &input : inputs.images) {
        if (auto image = findBuiltInImage(input, recording.frameIndex); image != nullptr) {
            imageUses_.emplace_back(image, gpu::ImageUsage::Sampled, stages);
            continue;
        }
        auto texture = shaderPack_->findRuntimeTexture(input);
        if (!texture.has_value()) throw std::runtime_error("unknown post_render input: " + input);
        const ShaderPack::RuntimeTexture &runtime = texture->get();
        // A texture the pack can also write as a storage image is bound, and so sampled, in GENERAL; General is the
        // use that keeps that layout and covers reads through a sampler.
        const bool general = !runtime.config.imported && runtime.config.storageBinding.has_value();
        imageUses_.emplace_back(shaderPack_->findRuntimeVKTexture(runtime, recording.frameIndex),
                                general ? gpu::ImageUsage::General : gpu::ImageUsage::Sampled, stages);
    }
    extendReadDependencies(*recording.commandBuffer, imageUses_);
    recording.commandBuffer->sync(imageUses_);
}

void PostRenderModule::uploadExecutionValues(Recording &recording,
                                             const std::shared_ptr<gpu::DeviceLocalBuffer> &executionBuffer,
                                             const ExecutionVariables &variables) {
    if (executionBuffer == nullptr) return;
    shaderPack_->uploadExecutionBuffer(ShaderPackLoader::Stage::PostRender, executionBuffer, variables,
                                       recording.commandBuffer, recording.frame.descriptorTable,
                                       shaderPack_->executionSet(kRuntimeResourceSet), recording.queueIndex,
                                       kDrawShaderStages);
}

// The output goes on in the layout the world pipeline hands it over in, for whatever reads it next in a fragment
// shader or a copy. The barrier waits on exactly what this frame last did to it.
void PostRenderModule::recordOutputHandOff(Recording &recording) {
    const std::shared_ptr<gpu::DeviceLocalImage> &output = recording.frame.output;
    const gpu::ImageState last = output->state();
    recording.commandBuffer->barriersBufferImage(
        {},
        {{
            .srcStageMask = last.stages == VK_PIPELINE_STAGE_2_NONE ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT : last.stages,
            .srcAccessMask = last.access & kWriteAccess,
            .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
            .oldLayout = last.layout,
            .newLayout = kOutputHandOffLayout,
            .srcQueueFamilyIndex = recording.queueIndex,
            .dstQueueFamilyIndex = recording.queueIndex,
            .image = output,
            .subresourceRange = gpu::wholeColorSubresourceRange,
        }});
}

std::shared_ptr<gpu::DeviceLocalImage> PostRenderModule::findBuiltInImage(std::string_view name,
                                                                          uint32_t frameIndex) const {
    const FrameResources &frame = frames_[frameIndex];
    if (name == kOutputImage) return frame.output;
    if (name == kFirstHitDepthImage) return frame.firstHitDepth;
    if (name == kHdrImage) return frame.hdr;
    if (name == kMotionVectorImage) return frame.motionVector;
    if (name == kNormalRoughnessImage) return frame.normalRoughness;
    return nullptr;
}

// What a full-screen pass may draw into: the output, the first-hit distances, or a runtime texture the pack makes
// (not one it imports from a file).
std::shared_ptr<gpu::DeviceLocalImage> PostRenderModule::findTargetImage(std::string_view target,
                                                                         uint32_t frameIndex) const {
    if (target == kOutputImage) return frames_[frameIndex].output;
    if (target == kFirstHitDepthImage) return frames_[frameIndex].firstHitDepth;
    if (shaderPack_ == nullptr) return nullptr;

    auto texture = shaderPack_->findRuntimeTexture(target);
    if (!texture.has_value() || texture->get().config.imported) return nullptr;
    return shaderPack_->findRuntimeVKTexture(texture->get(), frameIndex);
}

// ---- context

PostRenderModuleContext::PostRenderModuleContext(std::shared_ptr<FrameworkContext> frameworkContext,
                                                 std::shared_ptr<WorldPipelineContext> worldPipelineContext,
                                                 std::shared_ptr<PostRenderModule> postRenderModule)
    : WorldModuleContext(std::move(frameworkContext), std::move(worldPipelineContext)),
      postRenderModule(std::move(postRenderModule)) {}

void PostRenderModuleContext::render() {
    auto module = postRenderModule.lock();
    auto context = frameworkContext.lock();
    if (module == nullptr || context == nullptr) return;
    module->recordFrame(*context);
}
