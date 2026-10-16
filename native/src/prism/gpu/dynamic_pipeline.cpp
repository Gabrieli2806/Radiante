#include "prism/gpu/dynamic_pipeline.hpp"

#include "prism/gpu/descriptor.hpp"
#include "prism/gpu/device.hpp"
#include "prism/gpu/render_pass.hpp"
#include "prism/gpu/shader.hpp"

#include <array>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>

namespace gpu {
namespace {

// The state a dynamic pipeline leaves to the command buffer. The topology is not among them: it pairs with the
// shaders. Neither is the vertex input layout.
constexpr VkDynamicState kDynamicStates[] = {
    VK_DYNAMIC_STATE_VIEWPORT,
    VK_DYNAMIC_STATE_SCISSOR,

    VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
    VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
    VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
    VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE,
    VK_DYNAMIC_STATE_STENCIL_OP,
    VK_DYNAMIC_STATE_STENCIL_REFERENCE,
    VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
    VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,

    VK_DYNAMIC_STATE_CULL_MODE,
    VK_DYNAMIC_STATE_FRONT_FACE,
    VK_DYNAMIC_STATE_POLYGON_MODE_EXT,
    VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE,
    VK_DYNAMIC_STATE_DEPTH_BIAS,
    VK_DYNAMIC_STATE_LINE_WIDTH,

    VK_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT,
    VK_DYNAMIC_STATE_COLOR_BLEND_EQUATION_EXT,
    VK_DYNAMIC_STATE_COLOR_WRITE_MASK_EXT,
    // Only where the device enabled the logic op dynamic state.
    VK_DYNAMIC_STATE_LOGIC_OP_EXT,
    VK_DYNAMIC_STATE_LOGIC_OP_ENABLE_EXT,
    VK_DYNAMIC_STATE_BLEND_CONSTANTS,
};

bool isLogicOpState(VkDynamicState state) {
    return state == VK_DYNAMIC_STATE_LOGIC_OP_EXT || state == VK_DYNAMIC_STATE_LOGIC_OP_ENABLE_EXT;
}

} // namespace

// ---- DynamicGraphicsPipelineBuilder

DynamicGraphicsPipelineBuilder::ShaderStageBuilder::ShaderStageBuilder(DynamicGraphicsPipelineBuilder &parent)
    : parent_(parent) {}

DynamicGraphicsPipelineBuilder::ShaderStageBuilder &
DynamicGraphicsPipelineBuilder::ShaderStageBuilder::defineShaderStage(
    VkPipelineShaderStageCreateInfo shaderStageCreateInfo) {
    parent_.shaderStages_.push_back(shaderStageCreateInfo);
    return *this;
}

DynamicGraphicsPipelineBuilder::ShaderStageBuilder &
DynamicGraphicsPipelineBuilder::ShaderStageBuilder::defineShaderStage(VkShaderModule shaderModule,
                                                                      VkShaderStageFlagBits shaderStage) {
    parent_.shaderStages_.push_back(shaderStageInfo(shaderModule, shaderStage));
    return *this;
}

DynamicGraphicsPipelineBuilder::ShaderStageBuilder &
DynamicGraphicsPipelineBuilder::ShaderStageBuilder::defineShaderStage(std::shared_ptr<Shader> shader,
                                                                      VkShaderStageFlagBits shaderStage) {
    parent_.shaderStages_.push_back(shaderStageInfo(shader->vkShaderModule(), shaderStage));
    parent_.referenced_.push_back(std::move(shader));
    return *this;
}

DynamicGraphicsPipelineBuilder &DynamicGraphicsPipelineBuilder::ShaderStageBuilder::endShaderStage() {
    return parent_;
}

DynamicGraphicsPipelineBuilder::DynamicGraphicsPipelineBuilder(int numColorAttachments) {
    if (numColorAttachments < 0) {
        throw std::invalid_argument("a pipeline cannot have " + std::to_string(numColorAttachments) +
                                    " colour attachments");
    }
    colorBlendAttachments_.resize(static_cast<size_t>(numColorAttachments), VkPipelineColorBlendAttachmentState{});
}

DynamicGraphicsPipelineBuilder &DynamicGraphicsPipelineBuilder::defineRenderPass(std::shared_ptr<RenderPass> renderPass,
                                                                                 uint32_t subpassIndex) {
    renderPass_ = renderPass->vkRenderPass();
    subpassIndex_ = subpassIndex;
    referenced_.push_back(std::move(renderPass));
    return *this;
}

DynamicGraphicsPipelineBuilder::ShaderStageBuilder &DynamicGraphicsPipelineBuilder::beginShaderStage() {
    return shaderStageBuilder_;
}

DynamicGraphicsPipelineBuilder &
DynamicGraphicsPipelineBuilder::defineVertexInputState(const VertexLayoutInfo &vertexLayoutInfo) {
    vertexInputState_ = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInputState_.vertexBindingDescriptionCount = 1;
    vertexInputState_.pVertexBindingDescriptions = &vertexLayoutInfo.bindingDescription;
    vertexInputState_.vertexAttributeDescriptionCount =
        static_cast<uint32_t>(vertexLayoutInfo.attributeDescriptions.size());
    vertexInputState_.pVertexAttributeDescriptions = vertexLayoutInfo.attributeDescriptions.data();
    return *this;
}

template <>
DynamicGraphicsPipelineBuilder &DynamicGraphicsPipelineBuilder::defineVertexInputState<void>() {
    vertexInputState_ = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    return *this;
}

DynamicGraphicsPipelineBuilder &
DynamicGraphicsPipelineBuilder::definePipelineLayout(std::shared_ptr<DescriptorTable> descriptorTable) {
    pipelineLayout_ = descriptorTable->vkPipelineLayout();
    referenced_.push_back(std::move(descriptorTable));
    return *this;
}

DynamicGraphicsPipelineBuilder &DynamicGraphicsPipelineBuilder::defineInputAssemblyState(VkPrimitiveTopology topology) {
    topology_ = topology;
    return *this;
}

std::shared_ptr<DynamicGraphicsPipeline> DynamicGraphicsPipelineBuilder::build(std::shared_ptr<Device> device) const {
    const bool logicOp = device->hasExtendedDynamicState2LogicOp();
    std::array<VkDynamicState, std::size(kDynamicStates)> dynamicStates{};
    uint32_t dynamicStateCount = 0;
    for (VkDynamicState state : kDynamicStates) {
        if (isLogicOpState(state) && !logicOp) continue;
        dynamicStates[dynamicStateCount++] = state;
    }
    const VkPipelineDynamicStateCreateInfo dynamicState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = dynamicStateCount,
        .pDynamicStates = dynamicStates.data(),
    };

    // Below, what is dynamic is left zero: the command buffer sets it.
    const VkPipelineInputAssemblyStateCreateInfo inputAssemblyState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = topology_,
        .primitiveRestartEnable = VK_FALSE,
    };
    const VkPipelineViewportStateCreateInfo viewportState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    const VkPipelineRasterizationStateCreateInfo rasterizationState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
    };
    const VkPipelineMultisampleStateCreateInfo multisampleState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 1.0f,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE,
    };
    const VkPipelineColorBlendStateCreateInfo colorBlendState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = static_cast<uint32_t>(colorBlendAttachments_.size()),
        .pAttachments = colorBlendAttachments_.data(),
    };
    const VkPipelineDepthStencilStateCreateInfo depthStencilState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthBoundsTestEnable = VK_FALSE,
        .minDepthBounds = 0.0f,
        .maxDepthBounds = 1.0f,
    };

    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.stageCount = static_cast<uint32_t>(shaderStages_.size());
    info.pStages = shaderStages_.data();
    info.pVertexInputState = &vertexInputState_;
    info.pInputAssemblyState = &inputAssemblyState;
    info.pViewportState = &viewportState;
    info.pRasterizationState = &rasterizationState;
    info.pMultisampleState = &multisampleState;
    info.pDepthStencilState = &depthStencilState;
    info.pColorBlendState = &colorBlendState;
    info.pDynamicState = &dynamicState;
    info.layout = pipelineLayout_;
    info.renderPass = renderPass_;
    info.subpass = subpassIndex_;
    info.basePipelineHandle = VK_NULL_HANDLE;
    info.basePipelineIndex = -1;

    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkResult result =
        vkCreateGraphicsPipelines(device->vkDevice(), device->pipelineCache(), 1, &info, nullptr, &pipeline);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("dynamic graphics pipeline could not be built (" + std::to_string(result) + ")");
    }
    return DynamicGraphicsPipeline::create(std::move(device), pipeline);
}

} // namespace gpu
