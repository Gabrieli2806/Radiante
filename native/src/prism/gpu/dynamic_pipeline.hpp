#pragma once

#include "prism/all_extern.hpp"
#include "prism/gpu/pipeline.hpp"
#include "prism/gpu/vertex.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace gpu {
class Device;
class RenderPass;
class DescriptorTable;
class Shader;

// A graphics pipeline that leaves most of its state to the command buffer: viewport and scissor, depth and stencil
// tests, culling and front face, polygon mode, depth bias, line width, blending and blend constants, colour write
// masks and (where the device supports it) the logic op are set while recording. Fixed with the pipeline: the
// shaders, vertex input, topology, number of colour attachments, one sample and no depth bounds test.
class DynamicGraphicsPipeline : public PipelineObject, public SharedObject<DynamicGraphicsPipeline> {
  public:
    using PipelineObject::PipelineObject;
};

// The begin...() scope hands out a part of this builder, so it is neither copied nor moved.
class DynamicGraphicsPipelineBuilder {
  public:
    class ShaderStageBuilder {
      public:
        explicit ShaderStageBuilder(DynamicGraphicsPipelineBuilder &parent);

        ShaderStageBuilder &defineShaderStage(VkPipelineShaderStageCreateInfo shaderStageCreateInfo);
        ShaderStageBuilder &defineShaderStage(VkShaderModule shaderModule, VkShaderStageFlagBits shaderStage);
        // Keeps the shader alive until the builder goes.
        ShaderStageBuilder &defineShaderStage(std::shared_ptr<Shader> shader, VkShaderStageFlagBits shaderStage);
        DynamicGraphicsPipelineBuilder &endShaderStage();

      private:
        DynamicGraphicsPipelineBuilder &parent_;
    };

    explicit DynamicGraphicsPipelineBuilder(int numColorAttachments);
    DynamicGraphicsPipelineBuilder(const DynamicGraphicsPipelineBuilder &) = delete;
    DynamicGraphicsPipelineBuilder &operator=(const DynamicGraphicsPipelineBuilder &) = delete;

    DynamicGraphicsPipelineBuilder &defineRenderPass(std::shared_ptr<RenderPass> renderPass, uint32_t subpassIndex);

    ShaderStageBuilder &beginShaderStage();

    // Reads vertex format T from binding 0 (Vertex::vertexLayoutInfo); void reads no vertex buffer at all.
    template <typename T>
    DynamicGraphicsPipelineBuilder &defineVertexInputState();
    // The layout must outlive build().
    DynamicGraphicsPipelineBuilder &defineVertexInputState(const VertexLayoutInfo &vertexLayoutInfo);

    // Keeps the table alive until the builder goes.
    DynamicGraphicsPipelineBuilder &definePipelineLayout(std::shared_ptr<DescriptorTable> descriptorTable);

    // Fixed with the pipeline: the topology pairs with the shaders. Point lists unless defined.
    DynamicGraphicsPipelineBuilder &defineInputAssemblyState(VkPrimitiveTopology topology);

    // Through the device's pipeline cache; throws when the driver cannot build the pipeline.
    std::shared_ptr<DynamicGraphicsPipeline> build(std::shared_ptr<Device> device) const;

  private:
    ShaderStageBuilder shaderStageBuilder_{*this};

    std::vector<VkPipelineShaderStageCreateInfo> shaderStages_;
    VkPipelineVertexInputStateCreateInfo vertexInputState_{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPrimitiveTopology topology_ = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    // Their contents are dynamic state; only how many there are is fixed.
    std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments_;

    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    uint32_t subpassIndex_ = 0;

    // What the create info names by handle (shaders, render pass, layout), kept alive until the builder goes.
    std::vector<std::shared_ptr<void>> referenced_;
};

template <typename T>
DynamicGraphicsPipelineBuilder &DynamicGraphicsPipelineBuilder::defineVertexInputState() {
    return defineVertexInputState(Vertex::vertexLayoutInfo<T>());
}

template <>
DynamicGraphicsPipelineBuilder &DynamicGraphicsPipelineBuilder::defineVertexInputState<void>();

} // namespace gpu
