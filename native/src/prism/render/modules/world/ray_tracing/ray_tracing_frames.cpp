#include "prism/render/modules/world/ray_tracing/ray_tracing_internal.hpp"

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
    // After the time is set the cache holds the light of the hour before, and at its usual pace gave it up over
    // seconds: with /time set night by day, sand and other pale blocks stayed lit by a sun that was gone and faded
    // slowly. For half a second each entry is averaged over a few frames only, so the old light is replaced as soon
    // as the entry is met again; then the pace returns to the setting step by step.
    constexpr uint32_t kFastFrames = 30;
    constexpr uint32_t kFastAccumulation = 4;
    const uint32_t sinceTimeSet = Renderer::instance().buffers()->framesSinceTimeSet();
    if (sinceTimeSet < kFastFrames) {
        config.accumulationFrameNum = std::min(config.accumulationFrameNum, kFastAccumulation);
    } else if (sinceTimeSet - kFastFrames < settings_.sharcAccumulationFrameNum) {
        config.accumulationFrameNum =
            std::min(config.accumulationFrameNum, kFastAccumulation + (sinceTimeSet - kFastFrames));
    }
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
