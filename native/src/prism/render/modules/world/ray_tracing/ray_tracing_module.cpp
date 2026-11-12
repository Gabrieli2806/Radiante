#include "prism/render/modules/world/ray_tracing/ray_tracing_internal.hpp"

// The ray tracing module's set-up and build steps. Its passes are in ray_tracing_passes.cpp, what it does each
// frame in ray_tracing_frames.cpp.

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
    // The views /radiante debug view offers with this pack (Pipeline.availableDebugViews).
    result["debug_views"] = nlohmann::json::array();
    for (const auto &view : loaded.shaderPack.debugViews) result["debug_views"].push_back(view.name);
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
    ShaderPackLoader::addDebugViewDefinitions(shaderPack_->shaderPack().debugViews, config.debugView, definitions);
    ShaderPackLoader::addResourceDefinitions(shaderPack_->shaderPack().textures, shaderPack_->shaderPack().buffers,
                                             definitions);

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
