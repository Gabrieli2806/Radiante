#include "prism/render/pipeline.hpp"

#include "prism/render/buffers.hpp"
#include "prism/render/gpu_profiler.hpp"
#include "prism/render/render_framework.hpp"
#include "prism/render/renderer.hpp"

#include "prism/render/modules/world/dlss/dlss_module.hpp"
#include "prism/render/modules/world/fsr_upscaler/fsr_upscaler_module.hpp"
#include "prism/render/modules/world/nrd/nrd_module.hpp"
#include "prism/render/modules/world/post_render/post_render_module.hpp"
#include "prism/render/modules/world/ray_tracing/ray_tracing_module.hpp"
#include "prism/render/modules/world/shader_pack/shader_pack.hpp"
#include "prism/render/modules/world/temporal_accumulation/temporal_accumulation_module.hpp"
#include "prism/render/modules/world/tone_mapping/tone_mapping_module.hpp"
#include "prism/render/modules/world/world_module.hpp"
#include "prism/render/modules/world/xess_upscaler/xess_sr_module.hpp"

#include "prism/util/logging.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <set>
#include <stdexcept>
#include <typeinfo>
#include <utility>

namespace {

using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

constexpr VkAccessFlags2 ANY_ACCESS = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
constexpr VkPipelineStageFlags2 FRAGMENT_OR_TRANSFER =
    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT;

// How the final picture is first used once made: as the last pass's colour attachment, or read and written by the
// passes and the blit.
#ifdef USE_AMD
constexpr VkPipelineStageFlags2 FIRST_USE_STAGES = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
constexpr VkAccessFlags2 FIRST_USE_ACCESS =
    VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
#else
constexpr VkPipelineStageFlags2 FIRST_USE_STAGES = FRAGMENT_OR_TRANSFER;
constexpr VkAccessFlags2 FIRST_USE_ACCESS = ANY_ACCESS;
#endif

struct Scope {
    VkPipelineStageFlags2 stages;
    VkAccessFlags2 access;
};

// A transition of the final picture written out by hand, as the passes around it expect. Afterwards the image's
// tracked state holds only the layout, so whatever uses it next through CommandBuffer::sync waits on all earlier work.
void transition(gpu::CommandBuffer &commandBuffer,
                const std::shared_ptr<gpu::DeviceLocalImage> &image,
                uint32_t queueFamily,
                VkImageLayout from,
                VkImageLayout to,
                Scope before,
                Scope after) {
    commandBuffer.barriersBufferImage({}, {{
                                              .srcStageMask = before.stages,
                                              .srcAccessMask = before.access,
                                              .dstStageMask = after.stages,
                                              .dstAccessMask = after.access,
                                              .oldLayout = from,
                                              .newLayout = to,
                                              .srcQueueFamilyIndex = queueFamily,
                                              .dstQueueFamilyIndex = queueFamily,
                                              .image = image,
                                              .subresourceRange = gpu::wholeColorSubresourceRange,
                                          }});
    image->imageLayout() = to;
}

template <typename Module>
void registerWorldModule() {
    const std::string name(Module::NAME);
    Pipeline::worldModuleConstructors.emplace(
        name,
        [](std::shared_ptr<Framework> framework,
           std::shared_ptr<WorldPipeline> worldPipeline) -> std::shared_ptr<WorldModule> {
            return Module::create(framework, worldPipeline);
        });
    Pipeline::worldModuleInOutImageNums.emplace(
        name, std::pair<uint32_t, uint32_t>{Module::inputImageNum, Module::outputImageNum});
}

} // namespace

// ---- WorldPipelineBlueprint

WorldPipelineBlueprint::WorldPipelineBlueprint(WorldPipelineBuildParams *params) {
    std::set<uint32_t> usedImages;
    modules_.reserve(static_cast<size_t>(std::max(params->moduleCount, 0)));
    for (int i = 0; i < params->moduleCount; i++) {
        ModuleDesc &module = modules_.emplace_back();
        module.name = params->moduleNames[i];

        for (int j = 0; j < params->attributeCounts[i]; j++) {
            module.attributeKVs.emplace_back(params->attributeKVs[i][2 * j]);
            module.attributeKVs.emplace_back(params->attributeKVs[i][2 * j + 1]);
        }

        // How many images a module reads and writes is the module's own business; one this build lacks has none.
        auto imageCounts = Pipeline::worldModuleInOutImageNums.find(module.name);
        const auto [inputCount, outputCount] = imageCounts != Pipeline::worldModuleInOutImageNums.end() ?
                                                   imageCounts->second :
                                                   std::pair<uint32_t, uint32_t>{0, 0};
        for (uint32_t j = 0; j < inputCount; j++) {
            usedImages.insert(module.inputs.emplace_back(static_cast<uint32_t>(params->inputIndices[i][j])));
        }
        for (uint32_t j = 0; j < outputCount; j++) {
            usedImages.insert(module.outputs.emplace_back(static_cast<uint32_t>(params->outputIndices[i][j])));
        }
    }

    uint32_t next = 0;
    for (uint32_t index : usedImages) {
        if (index != next) throw std::runtime_error("world pipeline image indices are not contiguous");
        imageFormats_.push_back(static_cast<VkFormat>(params->imageFormats[index]));
        next++;
    }
}

void WorldPipelineBlueprint::setAttributeValue(std::string_view moduleName,
                                               const std::string &key,
                                               const std::string &value) {
    for (ModuleDesc &module : modules_) {
        if (module.name != moduleName) continue;
        bool found = false;
        for (size_t j = 0; j + 1 < module.attributeKVs.size(); j += 2) {
            if (module.attributeKVs[j] != key) continue;
            module.attributeKVs[j + 1] = value;
            found = true;
        }
        if (!found) {
            module.attributeKVs.push_back(key);
            module.attributeKVs.push_back(value);
        }
    }
}

const std::vector<std::string> *WorldPipelineBlueprint::attributesOf(std::string_view moduleName) const {
    for (const ModuleDesc &module : modules_) {
        if (module.name == moduleName) return &module.attributeKVs;
    }
    return nullptr;
}

// ---- WorldPipeline

WorldPipeline::WorldPipeline() = default;

void WorldPipeline::init(std::shared_ptr<Framework> framework, std::shared_ptr<Pipeline> pipeline) {
    auto blueprint = pipeline->worldPipelineBlueprint();
    const uint32_t frameCount = framework->swapchain()->imageCount();
    const size_t moduleCount = blueprint->modules_.size();
    if (blueprint->imageFormats_.empty()) throw std::runtime_error("the world pipeline has no images");

    worldModules_.assign(moduleCount, nullptr);
    worldModuleNames_.assign(moduleCount, std::string());
    sharedImages_.assign(frameCount,
                         std::vector<std::shared_ptr<gpu::DeviceLocalImage>>(blueprint->imageFormats_.size(), nullptr));

    // The frame slots' contexts exist before the modules, whose own contexts point back at them; what they run is
    // filled in once every module is built.
    contexts_.resize(frameCount);
    for (uint32_t frame = 0; frame < frameCount; frame++) {
        contexts_[frame] = WorldPipelineContext::create(framework->contexts()[frame], shared_from_this());
    }

    // Image 0 is the final picture, at the size the world is traced at; the modules size everything else from it.
    const VkExtent2D extent = framework->swapchain()->vkExtent();
    for (auto &images : sharedImages_) {
        images[0] = gpu::DeviceLocalImage::create(framework->device(), framework->vma(), false, extent.width,
                                                  extent.height, 1, blueprint->imageFormats_[0],
                                                  VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                                      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    }

    const auto initStart = Clock::now();
    if (loadShaderPack(framework, *blueprint)) {
        radiante::out() << "[Rebuild] shader pack " << msSince(initStart) << " ms" << std::endl;
    }

    // Last to first: a module's outputs are the images the modules after it read, already made at the size those
    // need, and it makes the inputs that no module made yet.
    for (size_t i = moduleCount; i-- > 0;) buildModule(framework, *blueprint, i, frameCount);

    // The settings the shaders read live start at the values this build was made with; published only once every
    // module is built, so a failed build leaves the previous pack's values in place.
    if (shaderPack_ != nullptr) Renderer::instance().buffers()->setPackParams(shaderPack_->runtimeValues());
    radiante::out() << "[Rebuild] world pipeline " << msSince(initStart) << " ms" << std::endl;

    for (uint32_t frame = 0; frame < frameCount; frame++) {
        WorldPipelineContext &context = *contexts_[frame];
        context.outputImage = sharedImages_[frame][0];
        context.worldModuleContexts.clear();
        context.worldModuleContexts.reserve(moduleCount);
        for (const auto &module : worldModules_) context.worldModuleContexts.push_back(module->contexts()[frame]);
    }
}

bool WorldPipeline::loadShaderPack(const std::shared_ptr<Framework> &framework,
                                   const WorldPipelineBlueprint &blueprint) {
    shaderPack_ = nullptr;
    for (const auto &module : blueprint.modules_) {
        if (module.name != RayTracingModule::NAME) continue;

        auto buildConfig = ShaderPack::buildConfigFromRayTracingAttributes(module.attributeKVs);
        auto shaderPack = std::make_shared<ShaderPack>(framework);
        std::string error;
        if (!shaderPack->initialize(buildConfig, error)) {
            // Reported in the game log (radiante::err) and carried in the exception: without it the world is black.
            radiante::err() << "[World Pipeline] Failed to load shared shader pack. Reason: " << error << std::endl;
            throw std::runtime_error("failed to load shared shader pack: " + error);
        }
        shaderPack_ = std::move(shaderPack);
        return true;
    }
    return false;
}

void WorldPipeline::buildModule(const std::shared_ptr<Framework> &framework,
                                WorldPipelineBlueprint &blueprint,
                                size_t index,
                                uint32_t frameCount) {
    auto &desc = blueprint.modules_[index];
    const auto moduleStart = Clock::now();

    auto constructor = Pipeline::worldModuleConstructors.find(desc.name);
    if (constructor == Pipeline::worldModuleConstructors.end() || !constructor->second) {
        throw std::runtime_error("world module " + desc.name + " is not available");
    }
    auto module = constructor->second(framework, shared_from_this());
    worldModules_[index] = module;
    worldModuleNames_[index] = desc.name;
    module->setAttributes(static_cast<int>(desc.attributeKVs.size() / 2), desc.attributeKVs);

    // Hands the module the frame slot's images at `indices` and keeps whatever it put in their places.
    std::vector<std::shared_ptr<gpu::DeviceLocalImage>> images;
    std::vector<VkFormat> formats;
    auto exchange = [&](const std::vector<uint32_t> &indices, uint32_t frame, bool outputs) {
        auto &shared = sharedImages_[frame];
        images.clear();
        formats.clear();
        for (uint32_t image : indices) {
            images.push_back(shared[image]);
            formats.push_back(blueprint.imageFormats_[image]);
        }
        const bool accepted = outputs ? module->setOrCreateOutputImages(images, formats, frame) :
                                        module->setOrCreateInputImages(images, formats, frame);
        if (!accepted) return false;
        if (images.size() != indices.size()) {
            throw std::runtime_error("world module " + desc.name + " changed the number of its images");
        }
        for (size_t j = 0; j < indices.size(); j++) shared[indices[j]] = images[j];
        return true;
    };

    for (uint32_t frame = 0; frame < frameCount; frame++) {
        if (!exchange(desc.outputs, frame, true)) {
            radiante::out() << desc.name << std::endl;
            throw std::runtime_error("Output image not set properly");
        }
        if (!exchange(desc.inputs, frame, false)) throw std::runtime_error("Input image not set properly");
    }

    module->build();
    radiante::out() << "[Rebuild] " << desc.name << " " << msSince(moduleStart) << " ms" << std::endl;
}

std::vector<std::shared_ptr<WorldModule>> &WorldPipeline::worldModules() {
    return worldModules_;
}

std::shared_ptr<gpu::DeviceLocalImage> WorldPipeline::sharedImage(uint32_t frameIndex, uint32_t index) {
    if (frameIndex >= sharedImages_.size() || index >= sharedImages_[frameIndex].size()) return nullptr;
    return sharedImages_[frameIndex][index];
}

std::vector<std::shared_ptr<WorldPipelineContext>> &WorldPipeline::contexts() {
    return contexts_;
}

std::shared_ptr<ShaderPack> WorldPipeline::shaderPack() {
    return shaderPack_;
}

bool WorldPipeline::setRuntimeAttribute(std::string_view moduleName,
                                        const std::string &key,
                                        const std::string &value) {
    bool applied = false;
    for (size_t i = 0; i < worldModules_.size(); i++) {
        if (worldModules_[i] == nullptr || worldModuleNames_[i] != moduleName) continue;
        // A module that refuses means a rebuild, which carries the value to every module anyway.
        if (!worldModules_[i]->setRuntimeAttribute(key, value)) return false;
        applied = true;
    }
    return applied;
}

void WorldPipeline::bindTexture(std::shared_ptr<gpu::Sampler> sampler,
                                std::shared_ptr<gpu::DeviceLocalImage> image,
                                int index) {
    for (auto &module : worldModules_) module->bindTexture(sampler, image, index);
}

// ---- WorldPipelineContext

WorldPipelineContext::WorldPipelineContext(std::shared_ptr<FrameworkContext> frameworkContext,
                                           std::shared_ptr<WorldPipeline> worldPipeline)
    : frameworkContext(frameworkContext), worldPipeline(worldPipeline) {}

void WorldPipelineContext::render() {
    auto context = frameworkContext.lock();
    auto framework = context->framework.lock();
    gpu::CommandBuffer &commandBuffer = *context->worldCommandBuffer;
    const uint32_t queueFamily = framework->physicalDevice()->mainQueueIndex();

    // A picture this build made has no layout yet; it gets the resting one before any pass uses it.
    if (outputImage != nullptr && outputImage->imageLayout() == VK_IMAGE_LAYOUT_UNDEFINED) {
        transition(commandBuffer, outputImage, queueFamily, VK_IMAGE_LAYOUT_UNDEFINED, WorldPipeline::OUTPUT_LAYOUT,
                   {VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0}, {FIRST_USE_STAGES, FIRST_USE_ACCESS});
    }

    GpuProfiler::beginFrame(framework->device()->vkDevice(), framework->physicalDevice()->vkPhysicalDevice(),
                            commandBuffer.vkCommandBuffer(), context->frameIndex);
    for (const auto &moduleContext : worldModuleContexts) {
        moduleContext->render();
        if (GpuProfiler::enabled()) GpuProfiler::mark(commandBuffer.vkCommandBuffer(), typeid(*moduleContext).name());
    }

    // Whatever layout the last pass left the picture in, it rests in OUTPUT_LAYOUT until the framework blits it.
    transition(commandBuffer, outputImage, queueFamily, outputImage->imageLayout(), WorldPipeline::OUTPUT_LAYOUT,
               {FRAGMENT_OR_TRANSFER | VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR, ANY_ACCESS},
               {FRAGMENT_OR_TRANSFER, ANY_ACCESS});
}

void WorldPipelineContext::blitOutput(gpu::CommandBuffer &commandBuffer, gpu::Image &target, uint32_t queueFamily) {
    transition(commandBuffer, outputImage, queueFamily, outputImage->imageLayout(),
               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, {FRAGMENT_OR_TRANSFER, ANY_ACCESS},
               {VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT});

    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[1] = {static_cast<int32_t>(outputImage->width()), static_cast<int32_t>(outputImage->height()), 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstOffsets[1] = {static_cast<int32_t>(target.width()), static_cast<int32_t>(target.height()), 1};
    // Minecraft keeps its textures in GENERAL layout for their whole lifetime. Vulkan's framebuffer origin is top-left
    // for both images, so no flip is needed.
    vkCmdBlitImage(commandBuffer.vkCommandBuffer(), outputImage->vkImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   target.vkImage(), VK_IMAGE_LAYOUT_GENERAL, 1, &blit, VK_FILTER_LINEAR);

    transition(commandBuffer, outputImage, queueFamily, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
               WorldPipeline::OUTPUT_LAYOUT, {VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT},
               {FRAGMENT_OR_TRANSFER, ANY_ACCESS});
}

// ---- Pipeline

std::map<std::string, Pipeline::WorldModuleConstructor> Pipeline::worldModuleConstructors{};
std::map<std::string, std::pair<uint32_t, uint32_t>> Pipeline::worldModuleInOutImageNums{};
std::map<std::string, std::function<void()>> Pipeline::worldModuleStaticPreCloser{};
std::atomic<bool> Pipeline::nativeRebuildActive_{false};
std::atomic<uint32_t> Pipeline::rebuildShadersDone{0};
std::atomic<uint32_t> Pipeline::rebuildShadersTotal{0};
std::atomic<uint32_t> Pipeline::rebuildPassesDone{0};
std::atomic<uint32_t> Pipeline::rebuildPassesTotal{0};

void Pipeline::beginNativeRebuild() {
    rebuildShadersDone.store(0);
    rebuildShadersTotal.store(0);
    rebuildPassesDone.store(0);
    rebuildPassesTotal.store(0);
    nativeRebuildActive_.store(true, std::memory_order_release);
}

void Pipeline::endNativeRebuild() {
    nativeRebuildActive_.store(false, std::memory_order_release);
}

bool Pipeline::nativeRebuildActive() {
    return nativeRebuildActive_.load(std::memory_order_acquire);
}

void Pipeline::collectWorldModules() {
    worldModuleConstructors.clear();
    worldModuleInOutImageNums.clear();
    worldModuleStaticPreCloser.clear();

    auto framework = Renderer::instance().framework();
    auto device = framework != nullptr ? framework->device() : nullptr;

    registerWorldModule<RayTracingModule>();
    registerWorldModule<NrdModule>();
    registerWorldModule<TemporalAccumulationModule>();
    registerWorldModule<FSRUpscalerModule>();

#ifdef PRISM_ENABLE_XESS
    if (device != nullptr && device->isXessDeviceExtensionsCompatible()) {
        registerWorldModule<XessSrModule>();
    } else {
        radiante::out() << "[Pipeline] xess module skipped: incompatible instance/device extension requirements."
                        << std::endl;
    }
#endif

    registerWorldModule<ToneMappingModule>();

    if (device == nullptr || !device->isDlssDeviceExtensionsCompatible()) {
        radiante::out() << "[Pipeline] dlss module skipped: incompatible instance/device extension requirements."
                        << std::endl;
    } else if (!DLSSModule::initNGXContext()) {
        radiante::out() << "[Pipeline] dlss module skipped: NGX initialization/query failed." << std::endl;
    } else {
        registerWorldModule<DLSSModule>();
        worldModuleStaticPreCloser.emplace(std::string(DLSSModule::NAME), DLSSModule::deinitNGXContext);
    }

    registerWorldModule<PostRenderModule>();
}

Pipeline::Pipeline() = default;

Pipeline::~Pipeline() = default;

void Pipeline::init(std::shared_ptr<Framework> framework) {
    framework_ = framework;
    createContexts(framework);
}

void Pipeline::createContexts(const std::shared_ptr<Framework> &framework) {
    const uint32_t frameCount = framework->swapchain()->imageCount();
    auto contexts = std::make_shared<std::vector<std::shared_ptr<PipelineContext>>>();
    contexts->reserve(frameCount);
    for (uint32_t i = 0; i < frameCount; i++) {
        contexts->push_back(PipelineContext::create(framework->contexts()[i], shared_from_this()));
    }
    contexts_ = std::move(contexts);
}

void Pipeline::buildWorldPipelineBlueprint(WorldPipelineBuildParams *params) {
    // Read first: a description that cannot be built leaves the current one in place, with no rebuild pending.
    auto blueprint = WorldPipelineBlueprint::create(params);
    beginNativeRebuild();
    worldPipelineBlueprint_ = std::move(blueprint);
    isRecreationNeeded = true;
}

bool Pipeline::prewarm(WorldPipelineBuildParams *params) {
    auto framework = framework_.lock();
    if (framework == nullptr || framework->lastRebuildFailed() || worldPipeline_ == nullptr) return false;

    // Copied out of Java's memory at once, as a build does; nothing else of the description is kept.
    const auto blueprint = WorldPipelineBlueprint::create(params);
    const std::vector<std::string> *attributes = blueprint->attributesOf(RayTracingModule::NAME);
    if (attributes == nullptr) return false;

    for (const auto &module : worldPipeline_->worldModules()) {
        auto rayTracing = std::dynamic_pointer_cast<RayTracingModule>(module);
        if (rayTracing == nullptr) continue;
        ShaderPrewarm::Work work = rayTracing->preparePrewarm(*attributes);
        if (!work) return false;
        shaderPrewarm_.start(std::move(work));
        return true;
    }
    return false;
}

ShaderPrewarm &Pipeline::shaderPrewarm() {
    return shaderPrewarm_;
}

void Pipeline::recreate(std::shared_ptr<Framework> framework) {
    auto &retainer = framework->frameResourceRetainer();
    std::exception_ptr failure;
    try {
        if (worldPipeline_ != nullptr) {
            // preClose releases what the modules hold outside the retained objects at once (DLSS frees its NGX
            // features), and frames still in flight read those: a pipeline built again two frames after the last
            // one (a profile changing several settings) destroyed a DLSS buffer in use and lost the device.
            framework->waitDeviceIdle();
            for (auto &module : worldPipeline_->worldModules()) module->preClose();
        }
        retainer.retain(std::exchange(worldPipeline_, nullptr));
        if (worldPipelineBlueprint_ != nullptr) worldPipeline_ = WorldPipeline::create(framework, shared_from_this());
    } catch (...) {
        // The pipeline this replaces is closed and its successor is not whole: frames draw no world until a build
        // succeeds.
        retainer.retain(std::exchange(worldPipeline_, nullptr));
        failure = std::current_exception();
    }

    // The frame slots run what this leaves, never the pipeline it replaced.
    retainer.retain(contexts_);
    createContexts(framework);
    if (failure != nullptr) std::rethrow_exception(failure);
}

void Pipeline::close() {
    if (worldPipeline_ != nullptr) {
        for (auto &module : worldPipeline_->worldModules()) module->preClose();
    }
    for (auto &[name, release] : worldModuleStaticPreCloser) release();
}

std::shared_ptr<PipelineContext> Pipeline::acquirePipelineContext(std::shared_ptr<FrameworkContext> context) {
    return contexts_->at(context->frameIndex);
}

std::vector<std::shared_ptr<PipelineContext>> &Pipeline::contexts() {
    return *contexts_;
}

void Pipeline::bindTexture(std::shared_ptr<gpu::Sampler> sampler,
                           std::shared_ptr<gpu::DeviceLocalImage> image,
                           int index) {
    if (worldPipeline_ != nullptr) worldPipeline_->bindTexture(sampler, image, index);
}

std::shared_ptr<WorldPipeline> Pipeline::worldPipeline() {
    return worldPipeline_;
}

std::shared_ptr<WorldPipelineBlueprint> Pipeline::worldPipelineBlueprint() {
    return worldPipelineBlueprint_;
}

// ---- PipelineContext

PipelineContext::PipelineContext(std::shared_ptr<FrameworkContext> context, std::shared_ptr<Pipeline> pipeline)
    : frameworkContext(context) {
    if (auto worldPipeline = pipeline->worldPipeline()) {
        worldPipelineContext = worldPipeline->contexts()[context->frameIndex];
    }
}
