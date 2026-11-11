#include "prism/render/modules/world/post_render/post_render_internal.hpp"

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
    inertPassNames_.clear();
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
                if (!drawsContent(config.render.content)) {
                    radiante::err() << "[PostRender] pass '" << config.render.name << "' draws "
                                    << config.render.content << ", which is traced in the world: skipped" << std::endl;
                    inertPassNames_.insert(config.render.name);
                    break;
                }
                auto pass = createRenderPass(config.render, executionBufferSize);
                // Packs may give shaders per kind of content; the stars are drawn with the "default" ones.
                if (auto shaders = pass->config.shaderConfigs.find("default");
                    shaders != pass->config.shaderConfigs.end()) {
                    requestShader(pass->vertexShader, shaders->second.vertexShaderPath, VK_SHADER_STAGE_VERTEX_BIT,
                                  pass->config.definitions);
                    requestShader(pass->fragmentShader, shaders->second.fragmentShaderPath,
                                  VK_SHADER_STAGE_FRAGMENT_BIT, pass->config.definitions);
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
                buildRenderPipeline(device, *renderPasses_[job - fullScreenCount]);
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

// The pass's pipeline, when it has its "default" shaders; the stars write depth unless the pass says otherwise.
void PostRenderModule::buildRenderPipeline(const std::shared_ptr<gpu::Device> &device, RenderPass &pass) const {
    if (pass.vertexShader == nullptr || pass.fragmentShader == nullptr) return;
    const ShaderPackLoader::RenderPassConfig &config = pass.config;
    const bool depthTest = config.depthTest.value_or(true);
    const bool depthWrite = config.depthWrite.value_or(true);
    const VkCompareOp depthCompare = parseDepthCompare(config.depthCompare.value_or("less"));
    const FrameResources &first = frames_.front();
    const VkExtent2D extent = {first.output->width(), first.output->height()};

    {
        gpu::GraphicsPipelineBuilder builder;
        auto &blend = builder.defineRenderPass(pass.renderPass, 0)
                          .beginShaderStage()
                          .defineShaderStage(pass.vertexShader, VK_SHADER_STAGE_VERTEX_BIT)
                          .defineShaderStage(pass.fragmentShader, VK_SHADER_STAGE_FRAGMENT_BIT)
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
        pass.pipeline = blend.endColorBlendAttachmentState().definePipelineLayout(first.descriptorTable).build(device);
    }
}
