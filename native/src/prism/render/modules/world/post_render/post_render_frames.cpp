#include "prism/render/modules/world/post_render/post_render_internal.hpp"

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

    // Before anything is drawn with the table: a set written after a draw used it would invalidate the recording.
    bindBloom(recording);
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

// The tone mapping module's bloom pyramid, drawn earlier in this frame's command buffer, for set 3 binding 5; black
// when it draws none. Bound every frame: a rebuild of the tone mapping module makes the pyramid's images again.
void PostRenderModule::bindBloom(Recording &recording) {
    std::shared_ptr<gpu::DeviceLocalImage> bloom;
    if (auto worldPipeline = worldPipeline_.lock()) {
        for (const auto &module : worldPipeline->worldModules()) {
            if (auto toneMapping = std::dynamic_pointer_cast<ToneMappingModule>(module)) {
                bloom = toneMapping->bloomImage(recording.frameIndex);
                break;
            }
        }
    }
    if (bloom == nullptr) {
        bloom = blackImage_;
    } else {
        recording.commandBuffer->sync({{bloom, gpu::ImageUsage::Sampled, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT}});
    }
    recording.frame.descriptorTable->bindSamplerImage(colorSampler_, bloom, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                      3, 5, 0);
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
    if (pass == renderPassByName_.end()) {
        if (inertPassNames_.contains(passName)) return;
        throw std::runtime_error("unknown post_render pass: " + passName);
    }
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
    if (starVertexBuffer_ == nullptr || starVertexCount_ == 0 || pass.pipeline == nullptr) return;

    commandBuffer.beginRenderPass({
        .renderPass = pass.renderPass,
        .framebuffer = pass.framebuffers[recording.frameIndex],
        .renderAreaExtent = {frame.output->width(), frame.output->height()},
    });
    commandBuffer.bindDescriptorTable(frame.descriptorTable, VK_PIPELINE_BIND_POINT_GRAPHICS);
    commandBuffer.bindGraphicsPipeline(pass.pipeline)->bindVertexBuffers(starVertexBuffer_)->draw(starVertexCount_, 1);
    commandBuffer.endRenderPass();
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
