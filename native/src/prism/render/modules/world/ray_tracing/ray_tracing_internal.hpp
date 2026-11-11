#pragma once

// What the files the ray tracing module is spread over share (ray_tracing_module.cpp, ray_tracing_passes.cpp,
// ray_tracing_frames.cpp): the attribute names, the descriptor sets' layout, the barriers and the build helpers.
// Not for the rest of the renderer: that includes ray_tracing_module.hpp.

#include "prism/render/modules/world/ray_tracing/ray_tracing_module.hpp"

#include "prism/gpu/diagnostics.hpp"
#include "prism/render/buffers.hpp"
#include "prism/render/chunks.hpp"
#include "prism/render/gpu_profiler.hpp"
#include "prism/render/modules/world/ray_tracing/submodules/world_prepare.hpp"
#include "prism/render/pipeline.hpp"
#include "prism/render/render_framework.hpp"
#include "prism/render/renderer.hpp"
#include "prism/render/world.hpp"
#include "prism/util/logging.hpp"
#include "prism/util/parallel.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <future>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace rt_detail {

using Clock = std::chrono::steady_clock;

// ---- Attributes

constexpr std::string_view kTrue = "render_pipeline.true";
constexpr std::string_view kShaderPackPathAttribute = "render_pipeline.module.ray_tracing.attribute.shader_pack_path";
constexpr std::string_view kUseJitterAttribute = "render_pipeline.module.ray_tracing.attribute.use_jitter";
constexpr std::string_view kUseSharcAttribute = "render_pipeline.module.ray_tracing.attribute.use_sharc";
constexpr std::string_view kSharcDebugModeAttribute = "render_pipeline.module.ray_tracing.attribute.sharc_debug_mode";
constexpr std::string_view kSharcSceneScaleAttribute =
    "render_pipeline.module.ray_tracing.attribute.sharc_scene_scale";
constexpr std::string_view kSharcAccumulationAttribute =
    "render_pipeline.module.ray_tracing.attribute.sharc_accumulation_frame_num";
constexpr std::string_view kSharcStaleAttribute =
    "render_pipeline.module.ray_tracing.attribute.sharc_stale_frame_num_max";
constexpr std::string_view kSharcDownsampleAttribute =
    "render_pipeline.module.ray_tracing.attribute.sharc_update_downsample_factor";

// Numbers in settings are read with std::stoi and std::stof, as they always were: a leading number counts.
inline std::optional<int> readInt(const std::string &text) {
    try {
        return std::stoi(text);
    } catch (const std::exception &) {
        return std::nullopt;
    }
}

inline std::optional<float> readFloat(const std::string &text) {
    try {
        return std::stof(text);
    } catch (const std::exception &) {
        return std::nullopt;
    }
}

// A count of frames: no less than `minimum`, with negative numbers read as 0.
inline std::optional<uint32_t> readFrameCount(const std::string &text, uint32_t minimum) {
    const std::optional<int> value = readInt(text);
    if (!value.has_value()) return std::nullopt;
    return std::max(minimum, static_cast<uint32_t>(std::max(0, *value)));
}

inline uint32_t readSharcDebugMode(const std::string &text) {
    if (text == "off" || text == "0" || text == "render_pipeline.false") return 0;
    if (text == "hash_grid" || text == "1") return 1;
    if (text == "occupancy" || text == "2") return 2;
    if (text == "heatmap" || text == "3") return 3;
    return static_cast<uint32_t>(std::max(0, readInt(text).value_or(0)));
}

inline std::filesystem::path builtInShaderPackPath() {
    return Renderer::folderPath / "shaders/world/ray_tracing/radiante-pt.zip";
}

// ---- Outputs

constexpr std::array<std::string_view, RayTracingModule::outputImageNum> kTargets = {
    RayTracingModule::TARGET_RADIANCE,
    RayTracingModule::TARGET_DIFFUSE_ALBEDO_METALLIC,
    RayTracingModule::TARGET_SPECULAR_ALBEDO,
    RayTracingModule::TARGET_NORMAL_ROUGHNESS,
    RayTracingModule::TARGET_MOTION_VECTOR,
    RayTracingModule::TARGET_LINEAR_DEPTH,
    RayTracingModule::TARGET_SPECULAR_HIT_DEPTH,
    RayTracingModule::TARGET_FIRST_HIT_DEPTH,
    RayTracingModule::TARGET_FIRST_HIT_DIFFUSE_DIRECT_LIGHT,
    RayTracingModule::TARGET_FIRST_HIT_DIFFUSE_INDIRECT_LIGHT,
    RayTracingModule::TARGET_FIRST_HIT_SPECULAR,
    RayTracingModule::TARGET_FIRST_HIT_CLEAR,
    RayTracingModule::TARGET_FIRST_HIT_BASE_EMISSION,
    RayTracingModule::TARGET_FOG_IMAGE,
    RayTracingModule::TARGET_FIRST_HIT_REFRACTION,
    RayTracingModule::TARGET_FOG_TRANSMISSION,
};

inline std::optional<uint32_t> targetIndex(std::string_view name) {
    for (uint32_t i = 0; i < kTargets.size(); i++) {
        if (kTargets[i] == name) return i;
    }
    return std::nullopt;
}

// ---- Descriptor sets

// Sets 0 to 4 are the module's own; the pack's runtime textures and buffers follow, then its execution variables.
constexpr uint32_t kTextureSet = 0;
constexpr uint32_t kSceneSet = 1;
constexpr uint32_t kUniformSet = 2;
constexpr uint32_t kOutputSet = 3;
constexpr uint32_t kSharcSet = 4;
constexpr uint32_t kRuntimeResourceSet = 5;

constexpr VkShaderStageFlags kTraceStages = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR |
                                            VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
constexpr VkShaderStageFlags kHitStages = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
// Every stage a shader of the pack may read the module's descriptors in.
constexpr VkShaderStageFlags kAllStages = kTraceStages | VK_SHADER_STAGE_INTERSECTION_BIT_KHR |
                                          VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
                                          VK_SHADER_STAGE_COMPUTE_BIT;

struct BindingSpec {
    VkDescriptorType type;
    VkShaderStageFlags stages;
    uint32_t count = 1;
};

constexpr VkDescriptorType kStorageBuffer = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
constexpr VkDescriptorType kUniformBuffer = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

// The game's textures (bindTexture).
constexpr BindingSpec kTextureBindings[] = {
    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kAllStages, 4096},
};

// The scene WorldPrepare gathers.
constexpr uint32_t kTlasBinding = 0;
constexpr uint32_t kTextureMappingBinding = 7;
constexpr uint32_t kChunkPackedDataBinding = 9;
constexpr BindingSpec kSceneBindings[] = {
    {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
     VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR}, // 0 the top-level structure
    {kStorageBuffer, kAllStages},                                           // 1 each instance's first geometry
    {kStorageBuffer, kTraceStages},                                         // 2 index buffer addresses
    {kStorageBuffer, kTraceStages},                                         // 3 last frame's index buffer addresses
    {kStorageBuffer, kHitStages},                                           // 4 position buffer addresses
    {kStorageBuffer, kHitStages},                                           // 5 material buffer addresses
    {kStorageBuffer, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR},                  // 6 last frame's position addresses
    {kStorageBuffer, kAllStages},                                           // 7 the texture mapping
    {kStorageBuffer, kTraceStages},                                         // 8 each instance's last transform
    {kStorageBuffer, kAllStages},                                           // 9 the chunks' packed data
};
constexpr std::pair<WorldPrepareContext::Table, uint32_t> kSceneTables[] = {
    {WorldPrepareContext::Table::BlasOffsets, 1},
    {WorldPrepareContext::Table::IndexAddresses, 2},
    {WorldPrepareContext::Table::LastIndexAddresses, 3},
    {WorldPrepareContext::Table::PositionAddresses, 4},
    {WorldPrepareContext::Table::MaterialAddresses, 5},
    {WorldPrepareContext::Table::LastPositionAddresses, 6},
    {WorldPrepareContext::Table::LastObjectToWorld, 8},
};

// The world, last frame's world and the sky uniforms, and the incident light meter (Buffers::lightMeterBuffer).
constexpr BindingSpec kUniformBindings[] = {
    {kUniformBuffer, kAllStages},
    {kUniformBuffer, kTraceStages | VK_SHADER_STAGE_COMPUTE_BIT},
    {kUniformBuffer, kAllStages},
    // (Closest hits read the meter too, for lava's steady brightness: see pt_shade_default.glsl.)
    {kStorageBuffer, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR},
};

template <size_t N>
constexpr std::array<BindingSpec, N> repeated(BindingSpec spec) {
    std::array<BindingSpec, N> bindings{};
    bindings.fill(spec);
    return bindings;
}

// The output images, in output order.
constexpr auto kOutputBindings = repeated<RayTracingModule::outputImageNum>(
    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kTraceStages | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT});

// SHaRC: its configuration, hash entries, accumulation, resolved radiance and locks.
constexpr auto kSharcBindings =
    repeated<5>({kStorageBuffer, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_COMPUTE_BIT});

// Defines the next descriptor set, its bindings numbered from 0 in order.
inline void defineSet(gpu::DescriptorTableBuilder &builder, std::span<const BindingSpec> bindings) {
    auto &set = builder.beginDescriptorLayoutSet();
    auto &setBindings = set.beginDescriptorLayoutSetBinding();
    for (uint32_t binding = 0; binding < bindings.size(); binding++) {
        setBindings.defineDescriptorLayoutSetBinding({
            .binding = binding,
            .descriptorType = bindings[binding].type,
            .descriptorCount = bindings[binding].count,
            .stageFlags = bindings[binding].stages,
        });
    }
    setBindings.endDescriptorLayoutSetBinding();
    set.endDescriptorLayoutSet();
}

// ---- Barriers

// Every stage an earlier pass, copy or attachment may have used an image or buffer in. A pass waits on all of them,
// whatever the resource was last used for: other modules transition the same images by hand.
constexpr VkPipelineStageFlags2 kEarlierWork = VK_PIPELINE_STAGE_2_TRANSFER_BIT |
                                               VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                                               VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR |
                                               VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                                               VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
constexpr VkPipelineStageFlags2 kPassStages = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                                              VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR |
                                              VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
constexpr VkAccessFlags2 kAnyAccess = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
constexpr VkAccessFlags2 kShaderReadWrite = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;

// Brings `image` into `layout` for a pass: sampled, as storage (which copies may also touch), or as the colour
// attachment of a full-screen pass. The tracked layout moves on at once, so a second barrier on the same image in
// the same batch starts where this one leaves it.
inline gpu::CommandBuffer::ImageMemoryBarrier
passImageBarrier(const std::shared_ptr<gpu::DeviceLocalImage> &image, VkImageLayout layout, uint32_t queueIndex) {
    VkPipelineStageFlags2 dstStages = kPassStages;
    VkAccessFlags2 dstAccess = VK_ACCESS_2_SHADER_READ_BIT;
    if (layout == VK_IMAGE_LAYOUT_GENERAL) {
        dstStages |= VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        dstAccess = kAnyAccess;
    } else if (layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        dstStages = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        dstAccess = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    }
    const VkImageLayout current = image->state().layout;
    const bool undefined = current == VK_IMAGE_LAYOUT_UNDEFINED;
    image->state().layout = layout;
    return {
        .srcStageMask = undefined ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT : kEarlierWork,
        .srcAccessMask = undefined ? VK_ACCESS_2_NONE : kAnyAccess,
        .dstStageMask = dstStages,
        .dstAccessMask = dstAccess,
        .oldLayout = current,
        .newLayout = layout,
        .srcQueueFamilyIndex = queueIndex,
        .dstQueueFamilyIndex = queueIndex,
        .image = image,
        .subresourceRange = image->fullSubresourceRange(),
    };
}

inline gpu::CommandBuffer::BufferMemoryBarrier
passBufferBarrier(const std::shared_ptr<gpu::DeviceLocalBuffer> &buffer, bool writable, uint32_t queueIndex) {
    return {
        .srcStageMask = kEarlierWork,
        .srcAccessMask = kAnyAccess,
        .dstStageMask = kPassStages,
        .dstAccessMask = writable ? kShaderReadWrite : VK_ACCESS_2_SHADER_READ_BIT,
        .srcQueueFamilyIndex = queueIndex,
        .dstQueueFamilyIndex = queueIndex,
        .buffer = buffer,
    };
}

// ---- Build helpers

inline ShaderPack::ShaderCreateInfo shaderRequest(const std::filesystem::path &path,
                                           VkShaderStageFlagBits stage,
                                           std::unordered_map<std::string, std::string> definitions,
                                           uint32_t executionSet) {
    return {
        .path = path,
        .stage = stage,
        .definitions = std::move(definitions),
        .executionStage = ShaderPackLoader::Stage::RayTracing,
        .executionSet = executionSet,
    };
}

// Records work into a command buffer of its own, submits it and waits until the main queue is idle. For the build
// only: a frame never waits on the queue.
template <typename Record>
void submitAndWait(Framework &framework, Record &&record) {
    auto device = framework.device();
    auto pool = gpu::CommandPool::create(framework.physicalDevice(), device);
    auto commandBuffer = gpu::CommandBuffer::create(device, pool);
    commandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    record(commandBuffer);
    commandBuffer->end();
    commandBuffer->submitMainQueueIndividual(device);
    gpu::diagnostics::onResult(vkQueueWaitIdle(device->mainVkQueue()), device->mainVkQueue(),
                               device->secondaryQueue());
}

#ifdef DEBUG
constexpr bool kBuildTimings = true;
#else
constexpr bool kBuildTimings = false;
#endif

// Debug builds log how long each step of a build took.
class StepTimer {
  public:
    explicit StepTimer(std::string_view scope) : scope_(scope) {}

    void step(std::string_view what) {
        if constexpr (kBuildTimings) {
            const auto now = Clock::now();
            radiante::err() << scope_ << " " << what << ": "
                            << std::chrono::duration<double, std::milli>(now - last_).count() << " ms" << std::endl;
            last_ = now;
        }
    }

  private:
    std::string_view scope_;
    Clock::time_point last_ = Clock::now();
};

} // namespace rt_detail

using namespace rt_detail;
