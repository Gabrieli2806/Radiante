#include "prism/gpu/pipeline.hpp"

#include "prism/gpu/descriptor.hpp"
#include "prism/gpu/device.hpp"
#include "prism/gpu/micromap.hpp"
#include "prism/gpu/render_pass.hpp"
#include "prism/gpu/retire.hpp"
#include "prism/gpu/shader.hpp"
#include "prism/util/background_work.hpp"
#include "prism/util/logging.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace gpu {
namespace {

[[noreturn]] void throwBuildFailure(const char *what, VkResult result) {
    throw std::runtime_error(std::string(what) + " could not be built (" + std::to_string(result) + ")");
}

// Works on a deferred operation from this thread and as many helper threads as the driver can use, until it is
// complete. Returns the operation's result; `threads` is how many threads took part. A background compile (a prewarm
// while the game runs) leaves the other half of the cores to the game.
VkResult joinFromAllCores(VkDevice device, VkDeferredOperationKHR operation, uint32_t &threads) {
    const bool background = prism::isBackgroundThread();
    const uint32_t cores =
        background ? prism::backgroundThreadLimit() : std::max(1u, std::thread::hardware_concurrency());
    const uint32_t useful = vkGetDeferredOperationMaxConcurrencyKHR(device, operation);
    const uint32_t wanted = std::max(1u, std::min(useful, cores));

    auto work = [device, operation, background] {
        if (background) prism::enterBackgroundThread();
        // VK_THREAD_IDLE_KHR: no work for this thread right now, but the operation is not done. Anything else ends
        // this thread's part; an error is reported by the operation's result.
        while (vkDeferredOperationJoinKHR(device, operation) == VK_THREAD_IDLE_KHR) std::this_thread::yield();
    };

    std::vector<std::thread> helpers;
    try {
        helpers.reserve(wanted - 1);
        while (helpers.size() + 1 < wanted) helpers.emplace_back(work);
    } catch (const std::exception &) {
        // The system would not start another thread: the ones running and this one finish the work between them.
    }
    work();
    for (std::thread &helper : helpers) helper.join();

    threads = static_cast<uint32_t>(helpers.size()) + 1;
    return vkGetDeferredOperationResultKHR(device, operation);
}

bool deferredCompileDisabled() {
    static const bool disabled = std::getenv("RADIANTE_NO_DEFERRED_COMPILE") != nullptr;
    return disabled;
}

// Runs create(operation) as a deferred host operation that every core helps with. A ray tracing pipeline compiled on
// the calling thread alone took most of a minute for a new set of settings. Without deferred operations (or with
// RADIANTE_NO_DEFERRED_COMPILE set) create(VK_NULL_HANDLE) runs on the calling thread.
template <typename Create>
VkResult createOnAllCores(VkDevice device, Create &&create, uint32_t &threads) {
    threads = 1;
    VkDeferredOperationKHR operation = VK_NULL_HANDLE;
    if (deferredCompileDisabled() || vkCreateDeferredOperationKHR == nullptr ||
        vkCreateDeferredOperationKHR(device, nullptr, &operation) != VK_SUCCESS) {
        return create(VK_NULL_HANDLE);
    }

    VkResult result = create(operation);
    if (result == VK_OPERATION_DEFERRED_KHR) {
        result = joinFromAllCores(device, operation, threads);
    } else if (result == VK_OPERATION_NOT_DEFERRED_KHR) {
        result = VK_SUCCESS;
    }
    vkDestroyDeferredOperationKHR(device, operation, nullptr);
    return result;
}

} // namespace

VkPipelineShaderStageCreateInfo shaderStageInfo(VkShaderModule shaderModule, VkShaderStageFlagBits shaderStage) {
    VkPipelineShaderStageCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    info.stage = shaderStage;
    info.module = shaderModule;
    info.pName = "main";
    return info;
}

// ---- PipelineObject

PipelineObject::PipelineObject(std::shared_ptr<Device> device, VkPipeline pipeline)
    : device_(std::move(device)), pipeline_(pipeline) {}

PipelineObject::~PipelineObject() {
    if (pipeline_ == VK_NULL_HANDLE) return;
    retire::later([device = std::move(device_), pipeline = pipeline_] {
        vkDestroyPipeline(device->vkDevice(), pipeline, nullptr);
    });
}

VkPipeline &PipelineObject::vkPipeline() {
    return pipeline_;
}

// ---- GraphicsPipelineBuilder

GraphicsPipelineBuilder::ShaderStageBuilder::ShaderStageBuilder(GraphicsPipelineBuilder &parent) : parent_(parent) {}

GraphicsPipelineBuilder::ShaderStageBuilder &
GraphicsPipelineBuilder::ShaderStageBuilder::defineShaderStage(VkPipelineShaderStageCreateInfo shaderStageCreateInfo) {
    parent_.shaderStages_.push_back(shaderStageCreateInfo);
    return *this;
}

GraphicsPipelineBuilder::ShaderStageBuilder &
GraphicsPipelineBuilder::ShaderStageBuilder::defineShaderStage(VkShaderModule shaderModule,
                                                               VkShaderStageFlagBits shaderStage) {
    parent_.shaderStages_.push_back(shaderStageInfo(shaderModule, shaderStage));
    return *this;
}

GraphicsPipelineBuilder::ShaderStageBuilder &
GraphicsPipelineBuilder::ShaderStageBuilder::defineShaderStage(std::shared_ptr<Shader> shader,
                                                               VkShaderStageFlagBits shaderStage) {
    parent_.shaderStages_.push_back(shaderStageInfo(shader->vkShaderModule(), shaderStage));
    parent_.referenced_.push_back(std::move(shader));
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::ShaderStageBuilder::endShaderStage() {
    return parent_;
}

GraphicsPipelineBuilder::ColorBlendAttachmentStateBuilder::ColorBlendAttachmentStateBuilder(
    GraphicsPipelineBuilder &parent)
    : parent_(parent) {}

GraphicsPipelineBuilder::ColorBlendAttachmentStateBuilder &
GraphicsPipelineBuilder::ColorBlendAttachmentStateBuilder::defineColorBlendAttachmentState(
    VkPipelineColorBlendAttachmentState colorBlendAttachmentState) {
    parent_.colorBlendAttachments_.push_back(colorBlendAttachmentState);
    return *this;
}

GraphicsPipelineBuilder::ColorBlendAttachmentStateBuilder &
GraphicsPipelineBuilder::ColorBlendAttachmentStateBuilder::defineDefaultColorBlendAttachmentState() {
    return defineColorBlendAttachmentState({
        .blendEnable = VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    });
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::ColorBlendAttachmentStateBuilder::endColorBlendAttachmentState() {
    parent_.colorBlendState_.attachmentCount = static_cast<uint32_t>(parent_.colorBlendAttachments_.size());
    parent_.blendsDefinedAttachments_ = true;
    return parent_;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::defineRenderPass(std::shared_ptr<RenderPass> renderPass,
                                                                   uint32_t subpassIndex) {
    renderPass_ = renderPass->vkRenderPass();
    subpassIndex_ = subpassIndex;
    referenced_.push_back(std::move(renderPass));
    return *this;
}

GraphicsPipelineBuilder::ShaderStageBuilder &GraphicsPipelineBuilder::beginShaderStage() {
    return shaderStageBuilder_;
}

GraphicsPipelineBuilder &
GraphicsPipelineBuilder::defineVertexInputState(VkPipelineVertexInputStateCreateInfo vertexInputStateCreateInfo) {
    vertexInputState_ = vertexInputStateCreateInfo;
    return *this;
}

template <>
GraphicsPipelineBuilder &GraphicsPipelineBuilder::defineVertexInputState<void>() {
    vertexInputState_ = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::useVertexLayout(const VertexLayoutInfo &layout) {
    vertexInputState_ = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInputState_.vertexBindingDescriptionCount = 1;
    vertexInputState_.pVertexBindingDescriptions = &layout.bindingDescription;
    vertexInputState_.vertexAttributeDescriptionCount = static_cast<uint32_t>(layout.attributeDescriptions.size());
    vertexInputState_.pVertexAttributeDescriptions = layout.attributeDescriptions.data();
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::defineViewportScissorState(ViewPortAndScissor viewPortAndScissor) {
    viewPortAndScissor_ = viewPortAndScissor;
    viewportState_ = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &viewPortAndScissor_.viewport,
        .scissorCount = 1,
        .pScissors = &viewPortAndScissor_.scissor,
    };
    return *this;
}

GraphicsPipelineBuilder &
GraphicsPipelineBuilder::defineRasterizationState(VkPipelineRasterizationStateCreateInfo rasterizationStateCreateInfo) {
    rasterizationState_ = rasterizationStateCreateInfo;
    return *this;
}

GraphicsPipelineBuilder::ColorBlendAttachmentStateBuilder &GraphicsPipelineBuilder::beginColorBlendAttachmentState() {
    return colorBlendAttachmentStateBuilder_;
}

GraphicsPipelineBuilder &
GraphicsPipelineBuilder::definePipelineLayout(std::shared_ptr<DescriptorTable> descriptorTable) {
    pipelineLayout_ = descriptorTable->vkPipelineLayout();
    referenced_.push_back(std::move(descriptorTable));
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::definePipelineLayout(VkPipelineLayout pipelineLayout) {
    pipelineLayout_ = pipelineLayout;
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::defineDepthStencilState(DepthStencilState depthStencilState) {
    depthStencilState_ = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .flags = depthStencilState.flags,
        .depthTestEnable = depthStencilState.depthTestEnable,
        .depthWriteEnable = depthStencilState.depthWriteEnable,
        .depthCompareOp = depthStencilState.depthCompareOp,
        .depthBoundsTestEnable = depthStencilState.depthBoundsTestEnable,
        .stencilTestEnable = depthStencilState.stencilTestEnable,
        .front = depthStencilState.front,
        .back = depthStencilState.back,
        .minDepthBounds = depthStencilState.minDepthBounds,
        .maxDepthBounds = depthStencilState.maxDepthBounds,
    };
    return *this;
}

std::shared_ptr<GraphicsPipeline> GraphicsPipelineBuilder::build(std::shared_ptr<Device> device) const {
    VkPipelineColorBlendStateCreateInfo colorBlendState = colorBlendState_;
    if (blendsDefinedAttachments_) colorBlendState.pAttachments = colorBlendAttachments_.data();

    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.stageCount = static_cast<uint32_t>(shaderStages_.size());
    info.pStages = shaderStages_.data();
    info.pVertexInputState = &vertexInputState_;
    info.pInputAssemblyState = &inputAssemblyState_;
    info.pViewportState = &viewportState_;
    info.pRasterizationState = &rasterizationState_;
    info.pMultisampleState = &multisampleState_;
    info.pDepthStencilState = &depthStencilState_;
    info.pColorBlendState = &colorBlendState;
    info.layout = pipelineLayout_;
    info.renderPass = renderPass_;
    info.subpass = subpassIndex_;
    info.basePipelineHandle = VK_NULL_HANDLE;
    info.basePipelineIndex = -1;

    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkResult result =
        vkCreateGraphicsPipelines(device->vkDevice(), device->pipelineCache(), 1, &info, nullptr, &pipeline);
    if (result != VK_SUCCESS) throwBuildFailure("graphics pipeline", result);
    return GraphicsPipeline::create(std::move(device), pipeline);
}

// ---- RayTracingPipelineBuilder

RayTracingPipelineBuilder::ShaderStageBuilder::ShaderStageBuilder(RayTracingPipelineBuilder &parent)
    : parent_(parent) {}

RayTracingPipelineBuilder::ShaderStageBuilder &
RayTracingPipelineBuilder::ShaderStageBuilder::defineShaderStage(std::shared_ptr<Shader> shader,
                                                                 VkShaderStageFlagBits shaderStage) {
    parent_.shaderStages_.push_back(shaderStageInfo(shader->vkShaderModule(), shaderStage));
    parent_.referenced_.push_back(std::move(shader));
    return *this;
}

RayTracingPipelineBuilder &RayTracingPipelineBuilder::ShaderStageBuilder::endShaderStage() {
    return parent_;
}

RayTracingPipelineBuilder::ShaderGroupBuilder::ShaderGroupBuilder(RayTracingPipelineBuilder &parent)
    : parent_(parent) {}

RayTracingPipelineBuilder::ShaderGroupBuilder &
RayTracingPipelineBuilder::ShaderGroupBuilder::defineShaderGroup(VkRayTracingShaderGroupTypeKHR type,
                                                                 uint32_t generalShader,
                                                                 uint32_t closestHitShader,
                                                                 uint32_t anyHitShader,
                                                                 uint32_t intersectionShader) {
    VkRayTracingShaderGroupCreateInfoKHR group{VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
    group.type = type;
    group.generalShader = generalShader;
    group.closestHitShader = closestHitShader;
    group.anyHitShader = anyHitShader;
    group.intersectionShader = intersectionShader;
    parent_.shaderGroups_.push_back(group);
    return *this;
}

RayTracingPipelineBuilder &RayTracingPipelineBuilder::ShaderGroupBuilder::endShaderGroup() {
    return parent_;
}

RayTracingPipelineBuilder::ShaderStageBuilder &RayTracingPipelineBuilder::beginShaderStage() {
    return shaderStageBuilder_;
}

RayTracingPipelineBuilder::ShaderGroupBuilder &RayTracingPipelineBuilder::beginShaderGroup() {
    return shaderGroupBuilder_;
}

RayTracingPipelineBuilder &
RayTracingPipelineBuilder::definePipelineLayout(std::shared_ptr<DescriptorTable> descriptorTable) {
    pipelineLayout_ = descriptorTable->vkPipelineLayout();
    referenced_.push_back(std::move(descriptorTable));
    return *this;
}

RayTracingPipelineBuilder &RayTracingPipelineBuilder::defineMaxRecursion(uint32_t depth) {
    maxRecursion_ = depth;
    return *this;
}

std::shared_ptr<RayTracingPipeline> RayTracingPipelineBuilder::build(std::shared_ptr<Device> device) const {
    // A deferred creation reads this create info (and everything it points to) until the operation completes,
    // which createOnAllCores waits for.
    VkRayTracingPipelineCreateInfoKHR info{VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
    info.stageCount = static_cast<uint32_t>(shaderStages_.size());
    info.pStages = shaderStages_.data();
    info.groupCount = static_cast<uint32_t>(shaderGroups_.size());
    info.pGroups = shaderGroups_.data();
    info.maxPipelineRayRecursionDepth = maxRecursion_;
    info.layout = pipelineLayout_;
    // Section structures carry opacity micromaps wherever the device has them, and a pipeline may only trace those
    // when made for it.
    if (opacityMicromapsEnabled()) info.flags |= VK_PIPELINE_CREATE_RAY_TRACING_OPACITY_MICROMAP_BIT_EXT;

    const auto started = std::chrono::steady_clock::now();
    VkPipeline pipeline = VK_NULL_HANDLE;
    uint32_t threads = 1;
    const VkResult result = createOnAllCores(
        device->vkDevice(),
        [&](VkDeferredOperationKHR operation) {
            return vkCreateRayTracingPipelinesKHR(device->vkDevice(), operation, device->pipelineCache(), 1, &info,
                                                  nullptr, &pipeline);
        },
        threads);
    if (result != VK_SUCCESS) throwBuildFailure("ray tracing pipeline", result);

    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    // One piece, so pipelines built side by side do not interleave their lines.
    radiante::out() << "[Pipeline] ray tracing pipeline of " + std::to_string(info.stageCount) + " stages built in " +
                           std::to_string(milliseconds) + " ms on " + std::to_string(threads) + " threads"
                    << std::endl;
    return RayTracingPipeline::create(std::move(device), pipeline);
}

// ---- ComputePipelineBuilder

ComputePipelineBuilder &ComputePipelineBuilder::defineShader(std::shared_ptr<Shader> shader) {
    shaderModule_ = shader->vkShaderModule();
    shader_ = std::move(shader);
    return *this;
}

ComputePipelineBuilder &ComputePipelineBuilder::definePipelineLayout(std::shared_ptr<DescriptorTable> descriptorTable) {
    pipelineLayout_ = descriptorTable->vkPipelineLayout();
    descriptorTable_ = std::move(descriptorTable);
    return *this;
}

std::shared_ptr<ComputePipeline> ComputePipelineBuilder::build(std::shared_ptr<Device> device) const {
    VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    info.stage = shaderStageInfo(shaderModule_, VK_SHADER_STAGE_COMPUTE_BIT);
    info.layout = pipelineLayout_;

    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkResult result =
        vkCreateComputePipelines(device->vkDevice(), device->pipelineCache(), 1, &info, nullptr, &pipeline);
    if (result != VK_SUCCESS) throwBuildFailure("compute pipeline", result);
    return ComputePipeline::create(std::move(device), pipeline);
}

} // namespace gpu
