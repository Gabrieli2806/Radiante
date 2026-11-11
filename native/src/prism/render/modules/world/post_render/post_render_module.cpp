#include "prism/render/modules/world/post_render/post_render_internal.hpp"

// The post render module's set-up and its own resources. The shader pack's passes are in
// post_render_passes.cpp, what the module records each frame in post_render_frames.cpp.

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
    inertPassNames_.clear();
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
    // 3: the frame's images, sampled: output, first-hit distances, hdr, motion vectors, normal and roughness, and the
    // tone mapping module's bloom pyramid (black when it draws none) for a pack's post passes.
    addSet({
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
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
    createBlackImage();

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
        table.bindSamplerImage(colorSampler_, blackImage_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 3, 5, 0);
    }
}

// One black texel, cleared once and left for sampling.
void PostRenderModule::createBlackImage() {
    auto framework = framework_.lock();
    auto device = framework->device();
    blackImage_ = gpu::DeviceLocalImage::create(
        device, framework->vma(),
        gpu::ImageDesc{.width = 1,
                       .height = 1,
                       .format = VK_FORMAT_R16G16B16A16_SFLOAT,
                       .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT});
    auto commandBuffer = gpu::CommandBuffer::create(device, framework->mainCommandPool());
    commandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    commandBuffer->sync({{blackImage_, gpu::ImageUsage::TransferDst, 0, true}});
    const VkClearColorValue black{};
    vkCmdClearColorImage(commandBuffer->vkCommandBuffer(), blackImage_->vkImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         &black, 1, &gpu::wholeColorSubresourceRange);
    commandBuffer->sync({{blackImage_, gpu::ImageUsage::Sampled, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT}});
    commandBuffer->end();
    auto fence = gpu::Fence::create(device);
    commandBuffer->submitMainQueueIndividual(device, fence);
    const VkResult waited = vkWaitForFences(device->vkDevice(), 1, &fence->vkFence(), VK_TRUE, UINT64_MAX);
    if (waited != VK_SUCCESS) {
        gpu::diagnostics::onResult(waited, device->mainVkQueue(), device->secondaryQueue());
        throw std::runtime_error("the post render pass could not prepare its black image on the GPU");
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
    const VkResult waited = vkWaitForFences(device->vkDevice(), 1, &fence->vkFence(), VK_TRUE, UINT64_MAX);
    if (waited != VK_SUCCESS) {
        gpu::diagnostics::onResult(waited, device->mainVkQueue(), device->secondaryQueue());
        throw std::runtime_error("the post render pass could not upload its star field to the GPU");
    }
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
