#include "prism/render/modules/world/ray_tracing/ray_tracing_internal.hpp"

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
    // RADIANTE_NO_SOBOL draws the path's sampling decisions from plain random numbers again (pt/pt_sampling.glsl),
    // to compare the two.
    auto shaderDefinitions = config.definitions;
    if (std::getenv("RADIANTE_NO_SOBOL") != nullptr) shaderDefinitions["VPT_LOW_DISCREPANCY"] = "0";
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
    // time to update SHaRC, with the compute shader that resolves the cache after the update.
    auto framework = framework_.lock();
    auto definitions = shaderDefinitions;
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
