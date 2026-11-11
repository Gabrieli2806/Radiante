#pragma once

// What the files the post render module is spread over share (post_render_module.cpp, post_render_passes.cpp,
// post_render_frames.cpp). Not for the rest of the renderer: that includes post_render_module.hpp.

#include "prism/render/modules/world/post_render/post_render_module.hpp"

#include "prism/render/modules/world/tone_mapping/tone_mapping_module.hpp"
#include "prism/gpu/diagnostics.hpp"
#include "prism/render/buffers.hpp"
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

namespace post_render_detail {

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

// What a render pass may draw. Only the stars are drawn here: particles, weather, text and name tags are traced in the
// world, so a pack's passes for them (accepted, as packs written for other renderers have them) draw nothing.
constexpr std::string_view kStarContents[] = {"star", "stars"};
constexpr std::string_view kTracedContents[] = {"weather", "particle", "particles", "text",
                                                "name_tag", "nametag", "name-tag"};

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

// Whether a render pass of this content draws anything.
inline bool drawsContent(const std::string &content) {
    const std::string name = ShaderPackLoader::toLower(content);
    if (std::ranges::find(kStarContents, name) != std::end(kStarContents)) return true;
    if (std::ranges::find(kTracedContents, name) != std::end(kTracedContents)) return false;
    throw std::runtime_error("unsupported post_render render content: " + content);
}

inline VkCompareOp parseDepthCompare(const std::string &value) {
    const std::string name = ShaderPackLoader::toLower(value);
    for (const auto &[key, compare] : kDepthCompares) {
        if (name == key) return compare;
    }
    throw std::runtime_error("unsupported post_render depth_compare: " + value);
}

inline uint32_t parseCount(const std::string &value, uint32_t fallback) {
    try {
        const long long parsed = std::stoll(value);
        if (parsed < 0) return fallback;
        return static_cast<uint32_t>(std::min<long long>(parsed, std::numeric_limits<uint32_t>::max()));
    } catch (const std::exception &) { return fallback; }
}

inline float parseFloat(const std::string &value, float fallback) {
    try {
        return std::stof(value);
    } catch (const std::exception &) { return fallback; }
}

inline std::shared_ptr<gpu::Shader> loadShader(const std::shared_ptr<gpu::Device> &device, std::string_view name) {
    return gpu::Shader::create(device, (Renderer::folderPath / "shaders" / name).string());
}

// An attachment that keeps its layout through the pass and stores what the pass leaves in it.
inline VkAttachmentDescription attachment(VkFormat format, VkAttachmentLoadOp loadOp, VkImageLayout layout) {
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

inline gpu::GraphicsPipelineBuilder::ViewPortAndScissor wholeTarget(VkExtent2D extent) {
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
inline VkPipelineColorBlendAttachmentState outputBlend(bool blend) {
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

inline double millisecondsSince(std::chrono::steady_clock::time_point start) {
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

inline gpu::VertexFormat::PBRVertex starVertex(const glm::vec3 &position, const glm::vec4 &color) {
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
inline void extendReadDependencies(gpu::CommandBuffer &commandBuffer, std::span<const gpu::ImageUse> uses) {
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

} // namespace post_render_detail

using namespace post_render_detail;

// Everything recording one frame needs.
struct PostRenderModule::Recording {
    std::shared_ptr<gpu::CommandBuffer> commandBuffer; // the frame's world command buffer
    const FrameResources &frame;
    uint32_t frameIndex;
    uint32_t queueIndex;
};
