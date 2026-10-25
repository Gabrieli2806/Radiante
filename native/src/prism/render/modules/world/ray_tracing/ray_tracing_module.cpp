#include "prism/render/modules/world/ray_tracing/ray_tracing_module.hpp"

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
#include <exception>
#include <filesystem>
#include <future>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace {

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
std::optional<int> readInt(const std::string &text) {
    try {
        return std::stoi(text);
    } catch (const std::exception &) {
        return std::nullopt;
    }
}

std::optional<float> readFloat(const std::string &text) {
    try {
        return std::stof(text);
    } catch (const std::exception &) {
        return std::nullopt;
    }
}

// A count of frames: no less than `minimum`, with negative numbers read as 0.
std::optional<uint32_t> readFrameCount(const std::string &text, uint32_t minimum) {
    const std::optional<int> value = readInt(text);
    if (!value.has_value()) return std::nullopt;
    return std::max(minimum, static_cast<uint32_t>(std::max(0, *value)));
}

uint32_t readSharcDebugMode(const std::string &text) {
    if (text == "off" || text == "0" || text == "render_pipeline.false") return 0;
    if (text == "hash_grid" || text == "1") return 1;
    if (text == "occupancy" || text == "2") return 2;
    if (text == "heatmap" || text == "3") return 3;
    return static_cast<uint32_t>(std::max(0, readInt(text).value_or(0)));
}

std::filesystem::path builtInShaderPackPath() {
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

std::optional<uint32_t> targetIndex(std::string_view name) {
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
void defineSet(gpu::DescriptorTableBuilder &builder, std::span<const BindingSpec> bindings) {
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
gpu::CommandBuffer::ImageMemoryBarrier
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

gpu::CommandBuffer::BufferMemoryBarrier
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

ShaderPack::ShaderCreateInfo shaderRequest(const std::filesystem::path &path,
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
    vkQueueWaitIdle(device->mainVkQueue());
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

} // namespace

// ---- RayTracingModule: setup

RayTracingModule::RayTracingModule() = default;

void RayTracingModule::init(std::shared_ptr<Framework> framework, std::shared_ptr<WorldPipeline> worldPipeline) {
    WorldModule::init(framework, worldPipeline);
    targets_.assign(framework->swapchain()->imageCount(), TargetImages{});
    worldPrepare_ = WorldPrepare::create(framework);
}

bool RayTracingModule::setOrCreateInputImages(std::vector<std::shared_ptr<gpu::DeviceLocalImage>> &,
                                              std::vector<VkFormat> &,
                                              uint32_t) {
    return true;
}

bool RayTracingModule::setOrCreateOutputImages(std::vector<std::shared_ptr<gpu::DeviceLocalImage>> &images,
                                               std::vector<VkFormat> &formats,
                                               uint32_t frameIndex) {
    if (frameIndex >= targets_.size() || images.size() != outputImageNum || formats.size() != outputImageNum) {
        return false;
    }

    std::optional<VkExtent2D> size;
    for (const auto &image : images) {
        if (image == nullptr) continue;
        if (!size.has_value()) {
            size = VkExtent2D{image->width(), image->height()};
        } else if (image->width() != size->width || image->height() != size->height) {
            return false;
        }
    }
    if (!size.has_value()) return false;

    auto framework = framework_.lock();
    for (size_t i = 0; i < images.size(); i++) {
        if (images[i] == nullptr) {
            images[i] = gpu::DeviceLocalImage::create(
                framework->device(), framework->vma(), false, size->width, size->height, 1, formats[i],
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        }
        targets_[frameIndex][i] = images[i];
    }
    return true;
}

std::string RayTracingModule::getAttributes(const std::vector<std::string> &attributes, const std::string &language) {
    std::string packPath;
    for (size_t i = 0; i + 1 < attributes.size(); i += 2) {
        if (attributes[i] == kShaderPackPathAttribute) {
            packPath = attributes[i + 1];
            break;
        }
    }
    const ShaderPackLoader::LoadResult loaded =
        ShaderPackLoader::load(std::filesystem::path(packPath), builtInShaderPackPath(), language);

    nlohmann::json result = nlohmann::json::object();
    result["attributes"] = nlohmann::json::array();
    result["translations"] = nlohmann::json::object();
    if (!loaded.success) return result.dump();

    for (const auto &attribute : loaded.shaderPack.attributes) {
        nlohmann::json entry = {
            {"name", attribute.name},
            {"type", attribute.type},
            {"default_value", attribute.defaultValue},
        };
        // Tells Java the setting can change without a rebuild (Pipeline.setRuntimeAttribute).
        if (attribute.runtime) entry["runtime"] = true;
        result["attributes"].push_back(std::move(entry));
    }
    for (const auto &[key, value] : loaded.shaderPack.translations) result["translations"][key] = value;
    return result.dump();
}

void RayTracingModule::setAttributes(int attributeCount, std::vector<std::string> &attributeKVs) {
    const size_t pairs = std::min(static_cast<size_t>(std::max(attributeCount, 0)), attributeKVs.size() / 2);
    // Of a key given more than once, the last value counts.
    const auto valueOf = [&](std::string_view key) -> const std::string * {
        for (size_t i = pairs; i-- > 0;) {
            if (attributeKVs[2 * i] == key) return &attributeKVs[2 * i + 1];
        }
        return nullptr;
    };

    Settings settings;
    if (const std::string *value = valueOf(kShaderPackPathAttribute)) settings.shaderPackPath = *value;
    if (const std::string *value = valueOf(kUseJitterAttribute)) settings.jitter = *value == kTrue;
    if (const std::string *value = valueOf(kUseSharcAttribute)) settings.sharc = *value == kTrue;
    if (const std::string *value = valueOf(kSharcDebugModeAttribute)) {
        settings.sharcDebugMode = readSharcDebugMode(*value);
    }
    if (const std::string *value = valueOf(kSharcSceneScaleAttribute)) {
        if (const auto scale = readFloat(*value)) settings.sharcSceneScale = std::max(0.001f, *scale);
    }
    if (const std::string *value = valueOf(kSharcAccumulationAttribute)) {
        settings.sharcAccumulationFrameNum = readFrameCount(*value, 1).value_or(settings.sharcAccumulationFrameNum);
    }
    if (const std::string *value = valueOf(kSharcStaleAttribute)) {
        settings.sharcStaleFrameNumMax = readFrameCount(*value, 8).value_or(settings.sharcStaleFrameNumMax);
    }
    if (const std::string *value = valueOf(kSharcDownsampleAttribute)) {
        settings.sharcUpdateDownsampleFactor =
            readFrameCount(*value, 1).value_or(settings.sharcUpdateDownsampleFactor);
    }
    settings_ = settings;
    Renderer::instance().buffers()->setUseJitter(settings_.jitter);
}

ShaderPrewarm::Work RayTracingModule::preparePrewarm(const std::vector<std::string> &attributeKVs) {
    auto framework = framework_.lock();
    if (framework == nullptr || shaderPack_ == nullptr) return {};
    if (descriptorTables_.empty() || descriptorTables_[0] == nullptr) return {};
    // The rebuild loads the pack these attributes name and lays the pipelines out by it and by SHaRC; the pipelines
    // warmed here use this build's layout, so they only match when neither changes. The defines are worked out as
    // ShaderPack::initialize will, from the same pack's attribute list.
    const ShaderPack::BuildConfig config = ShaderPack::buildConfigFromRayTracingAttributes(attributeKVs);
    if (config.shaderPackPath != settings_.shaderPackPath || config.shouldUseSharc != settings_.sharc) return {};
    auto definitions =
        ShaderPackLoader::buildAttributeDefinitions(shaderPack_->shaderPack().attributes, config.staticAttributes);

    // Fresh passes with only their configuration, so collectShaders asks for exactly what the rebuild's own passes
    // will (the hit groups, SHaRC variants and SER flag included) and nothing built for the frames is shared.
    std::vector<Pass> passes;
    std::vector<ShaderPack::ShaderCreateInfo> requests;
    passes.reserve(passes_.size());
    for (const Pass &built : passes_) {
        Pass &entry = passes.emplace_back();
        entry.name = built.name;
        entry.target = built.target;
        entry.targetExtent = built.targetExtent;
        entry.targetLayers = built.targetLayers;
        std::visit(
            [&](const auto &source) {
                using PassType = typename std::decay_t<decltype(source)>::element_type;
                auto pass = std::make_shared<PassType>();
                pass->config = source->config;
                if constexpr (std::is_same_v<PassType, RayTracingPass>) {
                    pass->querySharcEnabled = source->querySharcEnabled;
                    pass->isSharcUpdatePass = source->isSharcUpdatePass;
                }
                entry.firstShader = requests.size();
                collectShaders(entry, *pass, requests);
                entry.shaderCount = requests.size() - entry.firstShader;
                entry.pass = std::move(pass);
            },
            built.pass);
    }

    return [shaderPack = shaderPack_, device = framework->device(), layout = descriptorTables_[0],
            passes = std::move(passes), requests = std::move(requests),
            definitions = std::move(definitions)](ShaderPrewarm::Progress &progress) mutable {
        const auto shaders = shaderPack->createShaders(
            device, requests, definitions,
            {.done = &progress.shadersDone, .total = &progress.shadersTotal, .cancel = &progress.cancel});
        if (progress.cancel.load()) return;
        if (shaders.size() != requests.size()) throw std::runtime_error("prewarm shader count mismatch");

        // Only the ray tracing pipelines: the driver takes seconds over each, while compute and full-screen
        // pipelines take milliseconds in the rebuild. One at a time, so the compile keeps to the cores a background
        // deferred operation may use.
        std::vector<std::pair<const RayTracingPass *, std::shared_ptr<gpu::Shader>>> pipelines;
        for (Pass &entry : passes) {
            auto *pass = std::get_if<std::shared_ptr<RayTracingPass>>(&entry.pass);
            if (pass == nullptr) continue;
            takeShaders(**pass, ShaderList(shaders.data() + entry.firstShader, entry.shaderCount));
            pipelines.emplace_back(pass->get(), (*pass)->rayGenQueryShader);
            if ((*pass)->isSharcUpdatePass) pipelines.emplace_back(pass->get(), (*pass)->rayGenUpdateShader);
        }
        progress.pipelinesTotal.store(static_cast<uint32_t>(pipelines.size()));
        for (const auto &[pass, rayGenShader] : pipelines) {
            if (progress.cancel.load()) return;
            // Dropped at once: it only had to pass through the driver's pipeline cache.
            buildRayTracingPipeline(*pass, rayGenShader, layout, device);
            progress.pipelinesDone.fetch_add(1);
        }
    };
}

void RayTracingModule::build() {
    StepTimer timer("[RayTracing build]");
    loadShaderPack();
    timer.step("loadShaderPack");

    executionVariableConfigs_.clear();
    globalVariables_.clear();
    shaderPack_->copyStageExecutionState(ShaderPackLoader::Stage::RayTracing, executionVariableConfigs_,
                                         globalVariables_);
    timer.step("initExecutionVariables");

    worldPrepare_->build();
    timer.step("worldPrepare build");
    initDescriptorTables();
    timer.step("initDescriptorTables");
    initRuntimeResources();
    timer.step("initRuntimeResources");
    initSharc();
    timer.step("initSharc");
    initPipelines();
    timer.step("initPipelines");
    initContexts();
    timer.step("initContexts");
}

std::vector<std::shared_ptr<WorldModuleContext>> &RayTracingModule::contexts() {
    return contexts_;
}

void RayTracingModule::bindTexture(std::shared_ptr<gpu::Sampler> sampler,
                                   std::shared_ptr<gpu::DeviceLocalImage> image,
                                   int index) {
    for (const auto &table : descriptorTables_) {
        if (table == nullptr) continue;
        table->bindSamplerImage(sampler, image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, kTextureSet, 0,
                                static_cast<uint32_t>(index));
    }
}

void RayTracingModule::preClose() {
    contexts_.clear();
    passes_.clear();
    passIndices_.clear();
    sharcUpdatePass_.reset();
    executionVariableConfigs_.clear();
    globalVariables_.clear();
    worldPrepare_ = nullptr;
    shaderPack_ = nullptr;
}

// ---- RayTracingModule: build steps

void RayTracingModule::loadShaderPack() {
    auto worldPipeline = worldPipeline_.lock();
    shaderPack_ = worldPipeline != nullptr ? worldPipeline->shaderPack() : nullptr;
    if (shaderPack_ == nullptr) {
        radiante::err() << "[Ray Tracing] Failed to get shared shader pack runtime." << std::endl;
        throw std::runtime_error("the world pipeline has no shader pack");
    }
    hasSharcRuntime_ = shaderPack_->hasSharcRuntime();
}

void RayTracingModule::initDescriptorTables() {
    auto framework = framework_.lock();
    descriptorTables_.assign(targets_.size(), nullptr);
    for (uint32_t frameIndex = 0; frameIndex < targets_.size(); frameIndex++) {
        gpu::DescriptorTableBuilder builder;
        defineSet(builder, kTextureBindings);
        defineSet(builder, kSceneBindings);
        defineSet(builder, kUniformBindings);
        defineSet(builder, kOutputBindings);
        defineSet(builder, kSharcBindings);
        shaderPack_->defineRuntimeResourceDescriptorSet(builder, kAllStages, kAllStages, kAllStages);
        shaderPack_->defineExecutionDescriptorSet(builder, ShaderPackLoader::Stage::RayTracing, kAllStages);

        auto table = builder.build(framework->device());
        for (uint32_t output = 0; output < outputImageNum; output++) {
            table->bindImage(targets_[frameIndex][output], VK_IMAGE_LAYOUT_GENERAL, kOutputSet, output);
        }
        descriptorTables_[frameIndex] = std::move(table);
    }
}

void RayTracingModule::initRuntimeResources() {
    auto framework = framework_.lock();
    fullScreenVertexShader_ =
        gpu::Shader::create(framework->device(), (Renderer::folderPath / "shaders/full_screen_vert.spv").string());

    // The pack's textures are sized relative to the radiance output; its buffers may also depend on the scene.
    const auto &expressionVariables = updateExpressionVariables();
    const std::shared_ptr<gpu::DeviceLocalImage> radiance = targets_.empty() ? nullptr : targets_[0][0];
    if (radiance != nullptr) {
        shaderPack_->setRuntimeResourceExpressionVariables(expressionVariables);
        shaderPack_->ensureRuntimeResources(radiance->width(), radiance->height());
    }
    shaderPack_->setRuntimeResourceExpressionVariables(expressionVariables);
    shaderPack_->refreshRuntimeBuffers();
    for (uint32_t frameIndex = 0; frameIndex < descriptorTables_.size(); frameIndex++) {
        shaderPack_->bindRuntimeResources(descriptorTables_[frameIndex], kRuntimeResourceSet, frameIndex);
    }
}

void RayTracingModule::initSharc() {
    sharcConfigBuffers_.clear();
    sharcHashEntriesBuffer_ = nullptr;
    sharcLockBuffer_ = nullptr;
    sharcAccumulationBuffer_ = nullptr;
    sharcResolvedBuffer_ = nullptr;
    if (!hasSharcRuntime_) return;

    auto framework = framework_.lock();
    auto device = framework->device();
    auto vma = framework->vma();
    const auto makeBuffer = [&](size_t entryBytes) {
        return gpu::DeviceLocalBuffer::create(vma, device, false, static_cast<size_t>(sharcCapacity) * entryBytes,
                                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                  VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                              0, VMA_MEMORY_USAGE_GPU_ONLY);
    };
    sharcHashEntriesBuffer_ = makeBuffer(sizeof(uint64_t));
    sharcLockBuffer_ = makeBuffer(sizeof(uint32_t));
    sharcAccumulationBuffer_ = makeBuffer(sizeof(uint32_t) * 4);
    sharcResolvedBuffer_ = makeBuffer(sizeof(uint32_t) * 4);

    // The cache starts empty.
    submitAndWait(*framework, [&](const std::shared_ptr<gpu::CommandBuffer> &commandBuffer) {
        for (const auto &buffer :
             {sharcHashEntriesBuffer_, sharcLockBuffer_, sharcAccumulationBuffer_, sharcResolvedBuffer_}) {
            vkCmdFillBuffer(commandBuffer->vkCommandBuffer(), buffer->vkBuffer(), 0, VK_WHOLE_SIZE, 0);
        }
    });

    sharcConfigBuffers_.resize(descriptorTables_.size());
    for (size_t frameIndex = 0; frameIndex < descriptorTables_.size(); frameIndex++) {
        sharcConfigBuffers_[frameIndex] =
            gpu::HostVisibleBuffer::create(vma, device, sizeof(SharcConfigData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        const auto &table = descriptorTables_[frameIndex];
        table->bindBuffer(sharcConfigBuffers_[frameIndex], kSharcSet, 0);
        table->bindBuffer(sharcHashEntriesBuffer_, kSharcSet, 1);
        table->bindBuffer(sharcAccumulationBuffer_, kSharcSet, 2);
        table->bindBuffer(sharcResolvedBuffer_, kSharcSet, 3);
        table->bindBuffer(sharcLockBuffer_, kSharcSet, 4);
    }
}

void RayTracingModule::initPipelines() {
    StepTimer timer("[RayTracing initPipelines]");
    const ShaderPackLoader::ShaderPack &pack = shaderPack_->shaderPack();
    const size_t executionBufferSize = pack.rayTracingExecution.variables.size() * sizeof(float);

    passes_.clear();
    passIndices_.clear();
    sharcUpdatePass_.reset();
    for (const auto &config : pack.passes) {
        if (config.stage == ShaderPackLoader::Stage::RayTracing) addPass(config, executionBufferSize);
    }
    if (hasSharcRuntime_) selectSharcUpdatePass();
    timer.step("create all passes");

    // Every pass's shaders are compiled in one batch; each pass then takes its own range of it.
    std::vector<ShaderPack::ShaderCreateInfo> requests;
    for (Pass &entry : passes_) {
        entry.firstShader = requests.size();
        std::visit([&](auto &pass) { collectShaders(entry, *pass, requests); }, entry.pass);
        entry.shaderCount = requests.size() - entry.firstShader;
    }
    timer.step("collect shader requests");

    auto device = framework_.lock()->device();
#ifdef DEBUG
    ShaderPack::ShaderBatchStats stats;
    const std::vector<std::shared_ptr<gpu::Shader>> shaders = shaderPack_->createShaders(device, requests, &stats);
    std::string compiled = "build shader modules (" + std::to_string(stats.requestCount) + " requests, " +
                           std::to_string(stats.uniqueShaderCount) + " unique, " +
                           std::to_string(stats.cacheHitCount) + " cache hits, " +
                           std::to_string(stats.cacheMissCount) + " compiled";
    if (stats.cacheReadFailureCount > 0) {
        compiled += ", " + std::to_string(stats.cacheReadFailureCount) + " cache read failures";
    }
    timer.step(compiled + ")");
#else
    const std::vector<std::shared_ptr<gpu::Shader>> shaders = shaderPack_->createShaders(device, requests);
#endif
    if (shaders.size() != requests.size()) throw std::runtime_error("ray tracing shader offset count mismatch");

    Pipeline::rebuildPassesTotal.fetch_add(static_cast<uint32_t>(passes_.size()));
    prism::parallelFor(
        passes_.size(),
        [&](size_t index) {
            Pass &entry = passes_[index];
            const ShaderList passShaders(shaders.data() + entry.firstShader, entry.shaderCount);
            std::visit([&](auto &pass) { buildPass(entry, *pass, passShaders); }, entry.pass);
            Pipeline::rebuildPassesDone.fetch_add(1);
        },
        "PRISM_RT_PIPELINE_BUILD_THREADS");
    uploadStaticSbts();
    timer.step("build all passes");
}

void RayTracingModule::initContexts() {
    auto framework = framework_.lock();
    auto worldPipeline = worldPipeline_.lock();
    const uint32_t frameCount = framework->swapchain()->imageCount();
    contexts_.clear();
    contexts_.reserve(frameCount);
    for (uint32_t frameIndex = 0; frameIndex < frameCount; frameIndex++) {
        contexts_.push_back(RayTracingModuleContext::create(framework->contexts()[frameIndex],
                                                            worldPipeline->contexts()[frameIndex],
                                                            shared_from_this()));
    }
}

// ---- RayTracingModule: passes

void RayTracingModule::addPass(const ShaderPackLoader::PassConfig &config, size_t executionBufferSize) {
    auto framework = framework_.lock();
    auto device = framework->device();
    auto vma = framework->vma();

    Pass entry;
    switch (config.type) {
        case ShaderPackLoader::PassConfig::Type::FullScreen: {
            const auto &fullScreen = config.fullScreen;
            entry.name = fullScreen.name;
            entry.target = {
                .name = fullScreen.target,
                .target = targetIndex(fullScreen.target),
                .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            };
            const auto target = findImage(entry.target, 0);
            if (target == nullptr) throw std::runtime_error("Invalid full-screen pass target: " + fullScreen.target);
            if (fullScreen.face.has_value() && target->layer() != 6) {
                throw std::runtime_error("Full-screen pass face requires cube target: " + fullScreen.name);
            }
            if (fullScreen.face.has_value() && *fullScreen.face >= target->layer()) {
                throw std::runtime_error("Full-screen pass face out of range: " + fullScreen.name);
            }
            if (fullScreen.layer.has_value() && target->layer() <= 1) {
                throw std::runtime_error("Full-screen pass layer requires array target: " + fullScreen.name);
            }
            if (fullScreen.layer.has_value() && *fullScreen.layer >= target->layer()) {
                throw std::runtime_error("Full-screen pass layer out of range: " + fullScreen.name);
            }
            entry.targetExtent = {target->width(), target->height()};
            entry.targetLayers = target->layer();

            auto pass = std::make_shared<FullScreenPass>();
            pass->config = fullScreen;
            pass->executionBuffer = ShaderPack::createPassExecutionBuffer(device, vma, executionBufferSize);
            makeRenderTargets(entry, *pass);
            entry.pass = std::move(pass);
            break;
        }
        case ShaderPackLoader::PassConfig::Type::Render:
            throw std::runtime_error("render pass is only supported by post_render stage: " + config.render.name);
        case ShaderPackLoader::PassConfig::Type::RayTracing: {
            auto pass = std::make_shared<RayTracingPass>();
            pass->config = config.rayTracing;
            pass->querySharcEnabled = hasSharcRuntime_ && config.rayTracing.querySharc;
            pass->executionBuffer = ShaderPack::createPassExecutionBuffer(device, vma, executionBufferSize);
            entry.name = pass->config.name;
            entry.pass = std::move(pass);
            break;
        }
        case ShaderPackLoader::PassConfig::Type::Compute: {
            auto pass = std::make_shared<ComputePass>();
            pass->config = config.compute;
            pass->executionBuffer = ShaderPack::createPassExecutionBuffer(device, vma, executionBufferSize);
            entry.name = pass->config.name;
            entry.pass = std::move(pass);
            break;
        }
    }
    entry.profilerMark = "rt:" + entry.name;
    // The execution script calls passes by name; of two with the same name, the first is the one it gets.
    passIndices_.emplace(entry.name, passes_.size());
    passes_.push_back(std::move(entry));
}

void RayTracingModule::selectSharcUpdatePass() {
    const auto &sharc = shaderPack_->shaderPack().sharc;
    if (!sharc.has_value()) throw std::runtime_error("missing sharc config");
    const auto found = passIndices_.find(sharc->updatePassName);
    if (found == passIndices_.end()) throw std::runtime_error("unknown sharc update pass: " + sharc->updatePassName);
    auto *pass = std::get_if<std::shared_ptr<RayTracingPass>>(&passes_[found->second].pass);
    if (pass == nullptr || *pass == nullptr) {
        throw std::runtime_error("sharc update pass must be ray_tracing: " + sharc->updatePassName);
    }
    (*pass)->isSharcUpdatePass = true;
    sharcUpdatePass_ = found->second;
}

void RayTracingModule::makeRenderTargets(Pass &entry, FullScreenPass &pass) {
    auto device = framework_.lock()->device();
    pass.renderPass = gpu::RenderPassBuilder{}
                          .beginAttachmentDescription()
                          .defineAttachmentDescription({
                              .format = findImage(entry.target, 0)->vkFormat(),
                              .samples = VK_SAMPLE_COUNT_1_BIT,
                              .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                              .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                              .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                              .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                              .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                              .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          })
                          .endAttachmentDescription()
                          .beginAttachmentReference()
                          .defineAttachmentReference({
                              .attachment = 0,
                              .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          })
                          .endAttachmentReference()
                          .beginSubpassDescription()
                          .defineSubpassDescription({
                              .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                              .colorAttachmentIndices = {0},
                          })
                          .endSubpassDescription()
                          .build(device);

    // A target with layers gets a framebuffer per layer: view 0 is the whole image, view 1 + k its layer k.
    pass.framebuffers.assign(targets_.size(), {});
    for (uint32_t frameIndex = 0; frameIndex < targets_.size(); frameIndex++) {
        const auto image = findImage(entry.target, frameIndex);
        if (image == nullptr) throw std::runtime_error("Invalid full-screen pass target: " + entry.target.name);
        auto &framebuffers = pass.framebuffers[frameIndex];
        if (image->layer() == 1) {
            framebuffers.push_back(gpu::FramebufferBuilder{}
                                       .beginAttachment()
                                       .defineAttachment(image)
                                       .endAttachment()
                                       .build(device, pass.renderPass));
            continue;
        }
        for (uint32_t layer = 0; layer < image->layer(); layer++) {
            framebuffers.push_back(gpu::FramebufferBuilder{}
                                       .beginAttachment()
                                       .defineAttachment(image, static_cast<int>(layer + 1))
                                       .endAttachment()
                                       .build(device, pass.renderPass));
        }
    }
}

void RayTracingModule::collectShaders(Pass &entry,
                                      FullScreenPass &pass,
                                      std::vector<ShaderPack::ShaderCreateInfo> &requests) {
    const auto &config = pass.config;
    const uint32_t executionSet = shaderPack_->executionSet(kRuntimeResourceSet);
    // A cube target named without a face or layer is drawn face by face, with a pipeline each.
    const bool cube = entry.targetLayers == 6;
    const uint32_t pipelines = cube && !config.face.has_value() && !config.layer.has_value() ? 6u : 1u;
    for (uint32_t i = 0; i < pipelines; i++) {
        auto definitions = config.definitions;
        if (cube) definitions["FACE"] = std::to_string(config.face.value_or(i));
        requests.push_back(shaderRequest(config.fragmentShaderPath, VK_SHADER_STAGE_FRAGMENT_BIT,
                                         std::move(definitions), executionSet));
    }
}

void RayTracingModule::collectShaders(Pass &,
                                      RayTracingPass &pass,
                                      std::vector<ShaderPack::ShaderCreateInfo> &requests) {
    const auto &config = pass.config;
    const uint32_t executionSet = shaderPack_->executionSet(kRuntimeResourceSet);
    auto shaderDefinitions = config.definitions;
    // Shadow rays read the opacity micromaps of cut-out terrain in two states where the device has them (util/ray.glsl).
    {
        auto owner = framework_.lock();
        shaderDefinitions["VPT_OMM_SUPPORTED"] =
            owner != nullptr && owner->device()->isOpacityMicromapEnabled() ? "1" : "0";
    }

    // The miss shaders, then each hit group's closest-hit, any-hit and intersection shaders, as it has them. The
    // hit groups are named here already: the scene resolves its geometries' group names against them.
    for (const auto &miss : config.missShaders) {
        requests.push_back(
            shaderRequest(miss.shaderPath, VK_SHADER_STAGE_MISS_BIT_KHR, shaderDefinitions, executionSet));
    }
    pass.hitShaderGroups.resize(config.hitGroups.size());
    for (size_t index = 0; index < config.hitGroups.size(); index++) {
        const auto &groupConfig = config.hitGroups[index];
        if (groupConfig.closestHit.has_value()) {
            requests.push_back(shaderRequest(*groupConfig.closestHit, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
                                             shaderDefinitions, executionSet));
        }
        if (groupConfig.anyHit.has_value()) {
            requests.push_back(
                shaderRequest(*groupConfig.anyHit, VK_SHADER_STAGE_ANY_HIT_BIT_KHR, shaderDefinitions, executionSet));
        }
        if (groupConfig.intersection.has_value()) {
            requests.push_back(shaderRequest(*groupConfig.intersection, VK_SHADER_STAGE_INTERSECTION_BIT_KHR,
                                             shaderDefinitions, executionSet));
        }

        HitShaderGroup &group = pass.hitShaderGroups[index];
        group.name = groupConfig.name;
        group.type = groupConfig.type;
        const auto groupIndex = static_cast<uint32_t>(index);
        pass.hitGroupNameToIndex[group.name] = groupIndex;
        if (group.name == config.defaultHitGroupName) pass.fallbackHitGroupIndex = groupIndex;
        if (group.name == "shadow") pass.shadowHitGroupIndex = groupIndex;
    }
    // Without a shadow group the default group answers shadow rays.
    if (!pass.hitGroupNameToIndex.contains("shadow")) pass.shadowHitGroupIndex = pass.fallbackHitGroupIndex;

    // The ray generation shader, once querying SHaRC where the pass does; the update pass compiles it a second
    // time to update SHaRC, with the compute shader that resolves the cache after the update. Shader Execution
    // Reordering is only compiled in where the device was created with it.
    auto framework = framework_.lock();
    auto definitions = shaderDefinitions;
    definitions["VPT_SER_SUPPORTED"] =
        framework != nullptr && framework->device()->isShaderExecutionReorderingEnabled() ? "1" : "0";
    auto queryDefinitions = definitions;
    if (pass.querySharcEnabled) {
        queryDefinitions["USE_SHARC"] = "1";
        queryDefinitions["SHARC_QUERY"] = "1";
    }
    requests.push_back(shaderRequest(config.rayGenShaderPath, VK_SHADER_STAGE_RAYGEN_BIT_KHR,
                                     std::move(queryDefinitions), executionSet));
    if (pass.isSharcUpdatePass) {
        definitions["USE_SHARC"] = "1";
        definitions["SHARC_UPDATE"] = "1";
        requests.push_back(
            shaderRequest(config.rayGenShaderPath, VK_SHADER_STAGE_RAYGEN_BIT_KHR, definitions, executionSet));
        requests.push_back(shaderRequest(shaderPack_->shaderPack().sharc->resolveCompShaderPath,
                                         VK_SHADER_STAGE_COMPUTE_BIT, std::move(definitions), executionSet));
    }
}

void RayTracingModule::collectShaders(Pass &, ComputePass &pass, std::vector<ShaderPack::ShaderCreateInfo> &requests) {
    requests.push_back(shaderRequest(pass.config.computeShaderPath, VK_SHADER_STAGE_COMPUTE_BIT,
                                     pass.config.definitions, shaderPack_->executionSet(kRuntimeResourceSet)));
}

void RayTracingModule::buildPass(Pass &entry, FullScreenPass &pass, ShaderList shaders) {
    auto device = framework_.lock()->device();
    const VkExtent2D extent = entry.targetExtent;
    pass.fragmentShaders.assign(shaders.begin(), shaders.end());
    pass.pipelines.resize(shaders.size());
    for (size_t i = 0; i < shaders.size(); i++) {
        pass.pipelines[i] = gpu::GraphicsPipelineBuilder{}
                                .defineRenderPass(pass.renderPass, 0)
                                .beginShaderStage()
                                .defineShaderStage(fullScreenVertexShader_, VK_SHADER_STAGE_VERTEX_BIT)
                                .defineShaderStage(pass.fragmentShaders[i], VK_SHADER_STAGE_FRAGMENT_BIT)
                                .endShaderStage()
                                .defineVertexInputState<void>()
                                .defineViewportScissorState({
                                    .viewport =
                                        {
                                            .x = 0.0f,
                                            .y = 0.0f,
                                            .width = static_cast<float>(extent.width),
                                            .height = static_cast<float>(extent.height),
                                            .minDepth = 0.0f,
                                            .maxDepth = 1.0f,
                                        },
                                    .scissor = {.offset = {.x = 0, .y = 0}, .extent = extent},
                                })
                                .beginColorBlendAttachmentState()
                                .defineDefaultColorBlendAttachmentState()
                                .endColorBlendAttachmentState()
                                .definePipelineLayout(descriptorTables_[0])
                                .build(device);
    }
}

void RayTracingModule::takeShaders(RayTracingPass &pass, ShaderList shaders) {
    size_t next = 0;
    const auto take = [&] { return shaders[next++]; };
    const auto &config = pass.config;
    for (size_t index = 0; index < config.missShaders.size(); index++) {
        pass.missShaders.push_back({
            .name = config.missShaders[index].name,
            .index = static_cast<uint32_t>(index),
            .shader = take(),
        });
    }
    for (size_t index = 0; index < config.hitGroups.size(); index++) {
        const auto &groupConfig = config.hitGroups[index];
        HitShaderGroup &group = pass.hitShaderGroups[index];
        if (groupConfig.closestHit.has_value()) group.closestHitShader = take();
        if (groupConfig.anyHit.has_value()) group.anyHitShader = take();
        if (groupConfig.intersection.has_value()) group.intersectionShader = take();
    }
    pass.rayGenQueryShader = take();
    pass.rayGenUpdateShader = pass.isSharcUpdatePass ? take() : pass.rayGenQueryShader;
    if (pass.isSharcUpdatePass) pass.sharcResolveCompShader = take();
    pass.missGroupCount = static_cast<uint32_t>(pass.missShaders.size());
    pass.hitGroupCount = static_cast<uint32_t>(pass.hitShaderGroups.size());
}

std::shared_ptr<gpu::RayTracingPipeline>
RayTracingModule::buildRayTracingPipeline(const RayTracingPass &pass,
                                          const std::shared_ptr<gpu::Shader> &rayGenShader,
                                          const std::shared_ptr<gpu::DescriptorTable> &layout,
                                          const std::shared_ptr<gpu::Device> &device) {
    // Stage 0 is ray generation, the miss shaders follow, then each hit group's shaders; the groups are ray
    // generation, the misses, then the hit groups, as the shader binding table lays out their records.
    gpu::RayTracingPipelineBuilder builder;
    auto &stages = builder.beginShaderStage();
    stages.defineShaderStage(rayGenShader, VK_SHADER_STAGE_RAYGEN_BIT_KHR);
    for (const MissShader &miss : pass.missShaders) {
        stages.defineShaderStage(miss.shader, VK_SHADER_STAGE_MISS_BIT_KHR);
    }

    struct GroupStages {
        uint32_t closestHit = VK_SHADER_UNUSED_KHR;
        uint32_t anyHit = VK_SHADER_UNUSED_KHR;
        uint32_t intersection = VK_SHADER_UNUSED_KHR;
    };
    std::vector<GroupStages> groupStages(pass.hitShaderGroups.size());
    uint32_t stageCount = 1 + pass.missGroupCount;
    for (size_t index = 0; index < pass.hitShaderGroups.size(); index++) {
        const HitShaderGroup &group = pass.hitShaderGroups[index];
        if (group.closestHitShader != nullptr) {
            groupStages[index].closestHit = stageCount++;
            stages.defineShaderStage(group.closestHitShader, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);
        }
        if (group.anyHitShader != nullptr) {
            groupStages[index].anyHit = stageCount++;
            stages.defineShaderStage(group.anyHitShader, VK_SHADER_STAGE_ANY_HIT_BIT_KHR);
        }
        if (group.intersectionShader != nullptr) {
            groupStages[index].intersection = stageCount++;
            stages.defineShaderStage(group.intersectionShader, VK_SHADER_STAGE_INTERSECTION_BIT_KHR);
        }
    }
    stages.endShaderStage();

    auto &groups = builder.beginShaderGroup();
    groups.defineShaderGroup(VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR, 0, VK_SHADER_UNUSED_KHR,
                             VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR);
    for (uint32_t miss = 0; miss < pass.missGroupCount; miss++) {
        groups.defineShaderGroup(VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR, 1 + miss, VK_SHADER_UNUSED_KHR,
                                 VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR);
    }
    for (size_t index = 0; index < pass.hitShaderGroups.size(); index++) {
        groups.defineShaderGroup(pass.hitShaderGroups[index].type, VK_SHADER_UNUSED_KHR,
                                 groupStages[index].closestHit, groupStages[index].anyHit,
                                 groupStages[index].intersection);
    }
    groups.endShaderGroup();

    // Ray generation traces; the hit shaders trace shadow and edge queries, all of which the shadow group answers
    // without tracing further. Two levels, so each invocation reserves no stack for a third.
    return builder.definePipelineLayout(layout).defineMaxRecursion(2).build(device);
}

void RayTracingModule::buildPass(Pass &, RayTracingPass &pass, ShaderList shaders) {
    auto framework = framework_.lock();
    auto device = framework->device();
    takeShaders(pass, shaders);
    const auto buildPipeline = [&](const std::shared_ptr<gpu::Shader> &rayGenShader) {
        return buildRayTracingPipeline(pass, rayGenShader, descriptorTables_[0], device);
    };

    if (pass.isSharcUpdatePass) {
        // The query and the update pipeline share every other shader and take as long each to compile: built side by
        // side, the driver compiles them at once.
        auto update = std::async(std::launch::async, [&] { return buildPipeline(pass.rayGenUpdateShader); });
        pass.queryPipeline = buildPipeline(pass.rayGenQueryShader);
        pass.updatePipeline = update.get();
        pass.sharcResolvePipeline = gpu::ComputePipelineBuilder{}
                                        .defineShader(pass.sharcResolveCompShader)
                                        .definePipelineLayout(descriptorTables_[0])
                                        .build(device);
    } else {
        pass.queryPipeline = buildPipeline(pass.rayGenQueryShader);
        pass.updatePipeline = pass.queryPipeline;
    }

    const auto makeSbt = [&](const std::shared_ptr<gpu::RayTracingPipeline> &pipeline) {
        return gpu::SBT::create(framework->physicalDevice(), device, framework->vma(), pipeline, pass.missGroupCount,
                                pass.hitGroupCount);
    };
    const auto frameCount = static_cast<uint32_t>(targets_.size());
    pass.querySbts.resize(frameCount);
    if (pass.isSharcUpdatePass) pass.updateSbts.resize(frameCount);
    for (uint32_t frameIndex = 0; frameIndex < frameCount; frameIndex++) {
        if (pass.isSharcUpdatePass) pass.updateSbts[frameIndex] = makeSbt(pass.updatePipeline);
        pass.querySbts[frameIndex] = makeSbt(pass.queryPipeline);
    }
}

void RayTracingModule::buildPass(Pass &, ComputePass &pass, ShaderList shaders) {
    pass.computeShader = shaders[0];
    pass.pipeline = gpu::ComputePipelineBuilder{}
                        .defineShader(pass.computeShader)
                        .definePipelineLayout(descriptorTables_[0])
                        .build(framework_.lock()->device());
}

void RayTracingModule::uploadStaticSbts() {
    std::vector<std::shared_ptr<gpu::SBT>> sbts;
    for (const Pass &entry : passes_) {
        if (const auto *pass = std::get_if<std::shared_ptr<RayTracingPass>>(&entry.pass)) {
            sbts.insert(sbts.end(), (*pass)->updateSbts.begin(), (*pass)->updateSbts.end());
            sbts.insert(sbts.end(), (*pass)->querySbts.begin(), (*pass)->querySbts.end());
        }
    }
    auto framework = framework_.lock();
    if (sbts.empty() || framework == nullptr) return;

    // The ray generation and miss records live in device memory, copied there once before any frame traces.
    submitAndWait(*framework, [&](const std::shared_ptr<gpu::CommandBuffer> &commandBuffer) {
        for (const auto &sbt : sbts) {
            if (sbt != nullptr) sbt->uploadStaticSBT(commandBuffer);
        }
    });
}

// ---- RayTracingModule: frames

void RayTracingModule::render(RayTracingModuleContext &context) {
    auto frameworkContext = context.frameworkContext.lock();
    auto framework = frameworkContext != nullptr ? frameworkContext->framework.lock() : nullptr;
    if (framework == nullptr || shaderPack_ == nullptr) return;

    const Frame frame{
        .context = context,
        .commandBuffer = frameworkContext->worldCommandBuffer,
        .index = frameworkContext->frameIndex,
        .queueIndex = framework->physicalDevice()->mainQueueIndex(),
    };
    const VkCommandBuffer commandBuffer = frame.commandBuffer->vkCommandBuffer();
    const auto &table = context.rayTracingDescriptorTable;

    // The pack's buffers may be sized by the scene (CHUNK_NUM), so they are looked at every frame.
    shaderPack_->setRuntimeResourceExpressionVariables(updateExpressionVariables());
    shaderPack_->refreshRuntimeBuffers();
    shaderPack_->bindRuntimeResources(table, kRuntimeResourceSet, frame.index);

    auto buffers = Renderer::instance().buffers();
    table->bindBuffer(buffers->worldUniformBuffer(), kUniformSet, 0);
    table->bindBuffer(buffers->lastWorldUniformBuffer(), kUniformSet, 1);
    table->bindBuffer(buffers->skyUniformBuffer(), kUniformSet, 2);
    table->bindBuffer(buffers->lightMeterBuffer(), kUniformSet, 3);

    GpuProfiler::mark(commandBuffer, "rt:before");
    context.worldPrepareContext->render();
    GpuProfiler::mark(commandBuffer, "rt:prepare");
    bindScene(context);

    ExecutionVariables variables = frameVariables();
    if (hasSharcRuntime_ && settings_.sharc && sharcUpdatePass_.has_value()) {
        Pass &entry = passes_[*sharcUpdatePass_];
        RayTracingPass &pass = *std::get<std::shared_ptr<RayTracingPass>>(entry.pass);
        if (pass.executionBuffer != nullptr) uploadExecution(pass.executionBuffer, variables, frame);
        renderSharcUpdate(entry, pass, frame, variables);
        GpuProfiler::mark(commandBuffer, "rt:sharc_resolve");
    }

    shaderPack_->executeCommands(
        ShaderPackLoader::Stage::RayTracing, shaderPack_->execution(ShaderPackLoader::Stage::RayTracing).commands,
        variables, expressionVariables_, true, executionLoopLimit,
        [&](const std::string &passName, ExecutionVariables &passVariables) {
            const auto found = passIndices_.find(passName);
            if (found == passIndices_.end()) throw std::runtime_error("unknown pass: " + passName);
            Pass &entry = passes_[found->second];
            std::visit(
                [&](auto &pass) {
                    if (pass->executionBuffer != nullptr) uploadExecution(pass->executionBuffer, passVariables, frame);
                    renderPass(entry, *pass, frame, passVariables);
                },
                entry.pass);
            GpuProfiler::mark(commandBuffer, entry.profilerMark);
        });

    // What the script leaves in its global variables is where the next frame starts.
    for (auto &[name, value] : globalVariables_) {
        const auto found = variables.find(name);
        if (found != variables.end()) value = found->second.value;
    }
}

const std::vector<ExpressionEvaluator::Variable> &RayTracingModule::updateExpressionVariables() {
    constexpr std::array<const char *, 7> kNames = {
        "RENDER_WIDTH", "RENDER_HEIGHT", "CHUNK_SIZE_X", "CHUNK_SIZE_Y",
        "CHUNK_SIZE_Z", "CHUNK_BOTTOM_SECTION_COORD", "CHUNK_NUM",
    };
    std::array<double, kNames.size()> values{};
    size_t count = 2;

    const std::shared_ptr<gpu::DeviceLocalImage> radiance = targets_.empty() ? nullptr : targets_[0][0];
    if (radiance != nullptr) {
        values[0] = static_cast<double>(radiance->width());
        values[1] = static_cast<double>(radiance->height());
    }
    auto world = Renderer::instance().world();
    auto chunks = world != nullptr ? world->chunks() : nullptr;
    if (chunks != nullptr) {
        const glm::ivec4 grid = chunks->chunkGridInfo();
        values[2] = static_cast<double>(grid.x);
        values[3] = static_cast<double>(grid.y);
        values[4] = static_cast<double>(grid.z);
        values[5] = static_cast<double>(grid.w);
        values[6] = static_cast<double>(chunks->chunks().size());
        count = kNames.size();
    }

    // Updated in place: the names stay, so this snapshot is not rebuilt each frame.
    expressionVariables_.resize(count);
    for (size_t i = 0; i < count; i++) {
        if (expressionVariables_[i].name.empty()) expressionVariables_[i].name = kNames[i];
        expressionVariables_[i].value = values[i];
    }
    return expressionVariables_;
}

void RayTracingModule::bindScene(RayTracingModuleContext &context) {
    const auto &table = context.rayTracingDescriptorTable;
    auto world = Renderer::instance().world();
    auto chunks = world != nullptr ? world->chunks() : nullptr;
    if (Renderer::options.collectChunkEmission && chunks != nullptr) {
        if (auto packedData = chunks->chunkPackedData(); packedData != nullptr) {
            table->bindBuffer(packedData, kSceneSet, kChunkPackedDataBinding);
        }
    }

    const WorldPrepareContext &scene = *context.worldPrepareContext;
    if (scene.tlas == nullptr) return;
    table->bindAS(scene.tlas, kSceneSet, kTlasBinding);
    for (const auto &[sceneTable, binding] : kSceneTables) {
        table->bindBuffer(scene.table(sceneTable), kSceneSet, binding);
    }
    table->bindBuffer(Renderer::instance().buffers()->textureMappingBuffer(), kSceneSet, kTextureMappingBinding);
}

RayTracingModule::ExecutionVariables RayTracingModule::frameVariables() const {
    ExecutionVariables variables;
    variables.reserve(globalVariables_.size());
    for (const auto &[name, value] : globalVariables_) {
        const auto config = executionVariableConfigs_.find(name);
        variables.emplace(name, ShaderPack::ExecutionVariable{
                                    .name = name,
                                    .value = value,
                                    .type = config != executionVariableConfigs_.end() ? config->second.type : "",
                                });
    }
    return variables;
}

void RayTracingModule::uploadExecution(const std::shared_ptr<gpu::DeviceLocalBuffer> &buffer,
                                       const ExecutionVariables &variables,
                                       const Frame &frame) {
    shaderPack_->uploadExecutionBuffer(ShaderPackLoader::Stage::RayTracing, buffer, variables, frame.commandBuffer,
                                       frame.context.rayTracingDescriptorTable,
                                       shaderPack_->executionSet(kRuntimeResourceSet), frame.queueIndex, kPassStages);
}

void RayTracingModule::updateSharcConfig(uint32_t frameIndex) {
    const auto *world =
        static_cast<const gpu::Data::WorldUBO *>(Renderer::instance().buffers()->worldUniformBuffer()->mappedPtr());
    if (world == nullptr) return;

    const glm::dvec3 camera(world->cameraPos.x, world->cameraPos.y, world->cameraPos.z);
    if (!sharcCameraKnown_) {
        sharcPreviousCamera_ = camera;
        sharcCameraKnown_ = true;
    }

    // Shaders take the 64-bit buffer addresses as two 32-bit halves, low first.
    const auto split = [](VkDeviceAddress address, uint32_t (&halves)[2]) {
        halves[0] = static_cast<uint32_t>(address & 0xFFFFFFFFu);
        halves[1] = static_cast<uint32_t>(address >> 32);
    };
    SharcConfigData config{};
    split(sharcHashEntriesBuffer_->bufferAddress(), config.hashEntriesAddress);
    split(sharcLockBuffer_->bufferAddress(), config.lockAddress);
    split(sharcAccumulationBuffer_->bufferAddress(), config.accumulationAddress);
    split(sharcResolvedBuffer_->bufferAddress(), config.resolvedAddress);
    config.cameraPosition = glm::vec4(glm::vec3(camera), 0.0f);
    config.cameraPositionPrev = glm::vec4(glm::vec3(sharcPreviousCamera_), 0.0f);
    config.sceneScale = settings_.sharcSceneScale;
    config.radianceScale = 1000.0f;
    config.accumulationFrameNum = settings_.sharcAccumulationFrameNum;
    config.staleFrameNumMax = settings_.sharcStaleFrameNumMax;
    config.capacity = sharcCapacity;
    config.frameIndex = sharcFrameIndex_;
    config.enableAntiFireflyFilter = 1;
    config.useLockBuffer = 0;
    config.debugMode = settings_.sharcDebugMode;
    config.updateDownsampleFactor = sharcDownsample_;
    sharcConfigBuffers_[frameIndex]->uploadToBuffer(&config);

    sharcPreviousCamera_ = camera;
    sharcFrameIndex_++;
}

std::shared_ptr<gpu::DeviceLocalImage> RayTracingModule::findImage(const ImageResource &resource,
                                                                   uint32_t frameIndex) {
    if (!resource.imported && resource.target.has_value()) return targets_[frameIndex][*resource.target];
    auto texture = shaderPack_->findRuntimeTexture(resource.name);
    if (!texture.has_value() || texture->get().config.imported != resource.imported) return nullptr;
    return shaderPack_->findRuntimeVKTexture(texture->get(), frameIndex);
}

RayTracingModule::Resources RayTracingModule::passResources(const ShaderPackLoader::ResourceList &inputs,
                                                            const ShaderPackLoader::ResourceList &outputs) {
    Resources resources;
    const auto findImageResource = [&](const std::string &name) -> ImageResource * {
        for (ImageResource &image : resources.images) {
            if (image.name == name) return &image;
        }
        return nullptr;
    };
    const auto findBufferResource = [&](const std::string &name) -> BufferResource * {
        for (BufferResource &buffer : resources.buffers) {
            if (buffer.name == name) return &buffer;
        }
        return nullptr;
    };

    for (const std::string &name : inputs.images) {
        if (findImageResource(name) != nullptr) continue;
        // Imported textures, and textures the pack only binds for sampling, are read through samplers; anything
        // else is read as storage.
        const auto texture = shaderPack_->findRuntimeTexture(name);
        const bool imported = texture.has_value() && texture->get().config.imported;
        const bool sampledOnly = texture.has_value() && !texture->get().config.storageBinding.has_value() &&
                                 texture->get().config.sampledBinding.has_value();
        resources.images.push_back({
            .name = name,
            .target = targetIndex(name),
            .imported = imported,
            .layout = imported || sampledOnly ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL,
        });
    }
    for (const std::string &name : inputs.buffers) {
        if (findBufferResource(name) == nullptr) resources.buffers.push_back({.name = name, .writable = false});
    }
    for (const std::string &name : outputs.images) {
        const auto texture = shaderPack_->findRuntimeTexture(name);
        if (texture.has_value() && texture->get().config.imported) {
            throw std::runtime_error("pass output cannot be an imported texture: " + name);
        }
        ImageResource *image = findImageResource(name);
        if (image == nullptr) {
            image = &resources.images.emplace_back(ImageResource{.name = name, .target = targetIndex(name)});
        }
        image->layout = VK_IMAGE_LAYOUT_GENERAL;
    }
    for (const std::string &name : outputs.buffers) {
        BufferResource *buffer = findBufferResource(name);
        if (buffer == nullptr) buffer = &resources.buffers.emplace_back(BufferResource{.name = name});
        buffer->writable = true;
    }
    return resources;
}

void RayTracingModule::addPassBarriers(Pass &entry,
                                       const ShaderPackLoader::ResourceList &inputs,
                                       const ShaderPackLoader::ResourceList &outputs,
                                       const Frame &frame) {
    bufferBarriers_.clear();
    imageBarriers_.clear();
    if (!entry.resources.has_value()) entry.resources = passResources(inputs, outputs);

    for (const ImageResource &resource : entry.resources->images) {
        auto image = findImage(resource, frame.index);
        if (image == nullptr) {
            // An imported texture without an image has nothing to wait for; anything else missing is an error.
            if (resource.imported) continue;
            throw std::runtime_error("unknown pass resource: " + resource.name);
        }
        imageBarriers_.push_back(passImageBarrier(image, resource.layout, frame.queueIndex));
    }
    for (const BufferResource &resource : entry.resources->buffers) {
        auto buffer = shaderPack_->findRuntimeBuffer(resource.name);
        if (!buffer.has_value()) throw std::runtime_error("unknown pass buffer resource: " + resource.name);
        if (auto frameBuffer = shaderPack_->findRuntimeVKBuffer(buffer->get(), frame.index); frameBuffer != nullptr) {
            bufferBarriers_.push_back(passBufferBarrier(frameBuffer, resource.writable, frame.queueIndex));
        }
    }
}

void RayTracingModule::recordBarriers(const Frame &frame) {
    if (!bufferBarriers_.empty() || !imageBarriers_.empty()) {
        frame.commandBuffer->barriersBufferImage(bufferBarriers_, imageBarriers_);
    }
    bufferBarriers_.clear();
    imageBarriers_.clear();
}

uint32_t RayTracingModule::dispatchSize(const std::string &expression,
                                        const ExecutionVariables &variables,
                                        std::string_view what,
                                        const std::string &passName) {
    const double value = shaderPack_->evaluateNumericExpression(ShaderPackLoader::Stage::RayTracing, expression,
                                                                variables, expressionVariables_, true);
    if (!std::isfinite(value) || value <= 0.0) {
        throw std::runtime_error("invalid " + std::string(what) + " for pass " + passName + ": " + expression);
    }
    return static_cast<uint32_t>(std::max(1.0, std::ceil(value)));
}

void RayTracingModule::renderPass(Pass &entry, FullScreenPass &pass, const Frame &frame, const ExecutionVariables &) {
    addPassBarriers(entry, pass.config.inputs, pass.config.outputs, frame);
    const auto target = findImage(entry.target, frame.index);
    if (target == nullptr) throw std::runtime_error("unknown pass resource: " + entry.target.name);
    imageBarriers_.push_back(passImageBarrier(target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, frame.queueIndex));
    recordBarriers(frame);

    // The face or layer the pack names is the only one drawn; otherwise every layer is, a cube's with a pipeline per
    // face.
    const auto &framebuffers = pass.framebuffers[frame.index];
    uint32_t first = 0;
    uint32_t end = static_cast<uint32_t>(framebuffers.size());
    if (pass.config.face.has_value()) {
        first = *pass.config.face;
        end = first + 1;
    } else if (pass.config.layer.has_value()) {
        first = *pass.config.layer;
        end = first + 1;
    }
    for (uint32_t layer = first; layer < end; layer++) {
        const size_t pipeline = pass.pipelines.size() == 1 ? 0 : layer;
        frame.commandBuffer->beginRenderPass({
            .renderPass = pass.renderPass,
            .framebuffer = framebuffers[layer],
            .renderAreaExtent = {target->width(), target->height()},
            .clearValues = {},
        });
        frame.commandBuffer->bindGraphicsPipeline(pass.pipelines[pipeline])
            ->bindDescriptorTable(frame.context.rayTracingDescriptorTable, VK_PIPELINE_BIND_POINT_GRAPHICS);
        frame.commandBuffer->draw(3, 1)->endRenderPass();
    }
}

void RayTracingModule::renderPass(Pass &entry,
                                  RayTracingPass &pass,
                                  const Frame &frame,
                                  const ExecutionVariables &variables) {
    WorldPrepareContext &scene = *frame.context.worldPrepareContext;
    if (scene.tlas == nullptr) return;
    scene.setupHitGroupSbt(pass.hitGroupNameToIndex, pass.fallbackHitGroupIndex, pass.shadowHitGroupIndex,
                           frame.commandBuffer, nullptr, pass.querySbts[frame.index]);

    addPassBarriers(entry, pass.config.inputs, pass.config.outputs, frame);
    recordBarriers(frame);

    const uint32_t width = dispatchSize(pass.config.width, variables, "ray tracing dispatch width", pass.config.name);
    const uint32_t height =
        dispatchSize(pass.config.height, variables, "ray tracing dispatch height", pass.config.name);
    const uint32_t depth = dispatchSize(pass.config.depth, variables, "ray tracing dispatch depth", pass.config.name);

    if (pass.querySharcEnabled) {
        // Waits for SHaRC's update and resolve before the cache is read.
        frame.commandBuffer->memoryBarrier(
            VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, kShaderReadWrite,
            VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR, kShaderReadWrite);
    }

    frame.commandBuffer
        ->bindDescriptorTable(frame.context.rayTracingDescriptorTable, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR)
        ->bindRTPipeline(pass.queryPipeline)
        ->raytracing(pass.querySbts[frame.index], width, height, depth);
}

void RayTracingModule::renderSharcUpdate(Pass &entry,
                                         RayTracingPass &pass,
                                         const Frame &frame,
                                         const ExecutionVariables &variables) {
    WorldPrepareContext &scene = *frame.context.worldPrepareContext;
    if (scene.tlas == nullptr) return;
    if (!pass.isSharcUpdatePass || pass.updatePipeline == nullptr || pass.sharcResolvePipeline == nullptr ||
        pass.updateSbts.empty()) {
        throw std::runtime_error("invalid sharc update pass state: " + pass.config.name);
    }
    scene.setupHitGroupSbt(pass.hitGroupNameToIndex, pass.fallbackHitGroupIndex, pass.shadowHitGroupIndex,
                           frame.commandBuffer, pass.updateSbts[frame.index], nullptr);

    // The update traces a sparse grid of the pass's rays: one per downsample factor squared. The cache is a grid in
    // the world, whose cells do not get smaller with the render size, so above 1280x720 the factor grows with it and
    // the cache gets as many paths a frame as at 1280x720 (where DLSS Ultra Performance renders a 4K picture): at
    // 2560x1440 four times as many paths had cost a millisecond more for a cache no different to look at.
    const uint32_t width = dispatchSize(pass.config.width, variables, "ray tracing dispatch width", pass.config.name);
    const uint32_t height =
        dispatchSize(pass.config.height, variables, "ray tracing dispatch height", pass.config.name);
    const uint32_t baseDownsample = std::max(1u, settings_.sharcUpdateDownsampleFactor);
    const double sizeRatio = std::sqrt(static_cast<double>(width) * height / (1280.0 * 720.0));
    sharcDownsample_ = std::max(baseDownsample,
                                static_cast<uint32_t>(std::lround(static_cast<double>(baseDownsample) * sizeRatio)));
    const uint32_t downsample = sharcDownsample_;
    updateSharcConfig(frame.index);

    addPassBarriers(entry, pass.config.inputs, pass.config.outputs, frame);
    recordBarriers(frame);

    const uint32_t updateWidth = (width + downsample - 1) / downsample;
    const uint32_t updateHeight = (height + downsample - 1) / downsample;

    const auto &table = frame.context.rayTracingDescriptorTable;
    frame.commandBuffer->bindDescriptorTable(table, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR)
        ->bindRTPipeline(pass.updatePipeline)
        ->raytracing(pass.updateSbts[frame.index], updateWidth, updateHeight, 1);
    // The paths and the resolve apart: the paths cost with the render size, the resolve with the cache's capacity.
    GpuProfiler::mark(frame.commandBuffer->vkCommandBuffer(), "rt:sharc_paths");

    frame.commandBuffer->memoryBarrier(VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR, kShaderReadWrite,
                                       VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, kShaderReadWrite);
    frame.commandBuffer->bindDescriptorTable(table, VK_PIPELINE_BIND_POINT_COMPUTE)
        ->bindComputePipeline(pass.sharcResolvePipeline);
    vkCmdDispatch(frame.commandBuffer->vkCommandBuffer(),
                  (sharcCapacity + sharcResolveWorkgroupSize - 1) / sharcResolveWorkgroupSize, 1, 1);
}

void RayTracingModule::renderPass(Pass &entry,
                                  ComputePass &pass,
                                  const Frame &frame,
                                  const ExecutionVariables &variables) {
    addPassBarriers(entry, pass.config.inputs, pass.config.outputs, frame);
    recordBarriers(frame);

    const uint32_t x = dispatchSize(pass.config.gx, variables, "compute dispatch gx", pass.config.name);
    const uint32_t y = dispatchSize(pass.config.gy, variables, "compute dispatch gy", pass.config.name);
    const uint32_t z = dispatchSize(pass.config.gz, variables, "compute dispatch gz", pass.config.name);

    frame.commandBuffer->bindDescriptorTable(frame.context.rayTracingDescriptorTable, VK_PIPELINE_BIND_POINT_COMPUTE)
        ->bindComputePipeline(pass.pipeline);
    vkCmdDispatch(frame.commandBuffer->vkCommandBuffer(), x, y, z);
}

// ---- RayTracingModuleContext

RayTracingModuleContext::RayTracingModuleContext(std::shared_ptr<FrameworkContext> frameworkContext,
                                                 std::shared_ptr<WorldPipelineContext> worldPipelineContext,
                                                 std::shared_ptr<RayTracingModule> rayTracingModule)
    : WorldModuleContext(frameworkContext, worldPipelineContext),
      rayTracingModule(rayTracingModule),
      rayTracingDescriptorTable(rayTracingModule->descriptorTables_[frameworkContext->frameIndex]),
      worldPrepareContext(rayTracingModule->worldPrepare_->context(frameworkContext->frameIndex)) {}

void RayTracingModuleContext::render() {
    if (auto module = rayTracingModule.lock()) module->render(*this);
}
