#pragma once

#include "prism/all_extern.hpp"
#include "prism/gpu/vertex.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace gpu {
class Device;
class RenderPass;
class DescriptorTable;
class Shader;

// A shader stage that starts at the module's "main".
VkPipelineShaderStageCreateInfo shaderStageInfo(VkShaderModule shaderModule, VkShaderStageFlagBits shaderStage);

// Owns a VkPipeline. Frames still in flight may bind it, so it is destroyed through gpu::retire.
class PipelineObject {
  public:
    PipelineObject(std::shared_ptr<Device> device, VkPipeline pipeline);
    PipelineObject(const PipelineObject &) = delete;
    PipelineObject &operator=(const PipelineObject &) = delete;

    VkPipeline &vkPipeline();

  protected:
    ~PipelineObject();

  private:
    std::shared_ptr<Device> device_;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

class GraphicsPipeline : public PipelineObject, public SharedObject<GraphicsPipeline> {
  public:
    using PipelineObject::PipelineObject;
};

class RayTracingPipeline : public PipelineObject, public SharedObject<RayTracingPipeline> {
  public:
    using PipelineObject::PipelineObject;
};

class ComputePipeline : public PipelineObject, public SharedObject<ComputePipeline> {
  public:
    using PipelineObject::PipelineObject;
};

// A graphics pipeline whose state is fixed when it is built. What is not defined keeps the defaults: triangle lists,
// back faces culled (counter-clockwise front), one sample, no depth test, no colour attachments.
//
// The begin...() scopes hand out parts of this builder, so it is neither copied nor moved.
class GraphicsPipelineBuilder {
  public:
    class ShaderStageBuilder {
      public:
        explicit ShaderStageBuilder(GraphicsPipelineBuilder &parent);

        ShaderStageBuilder &defineShaderStage(VkPipelineShaderStageCreateInfo shaderStageCreateInfo);
        ShaderStageBuilder &defineShaderStage(VkShaderModule shaderModule, VkShaderStageFlagBits shaderStage);
        // Keeps the shader alive until the builder goes.
        ShaderStageBuilder &defineShaderStage(std::shared_ptr<Shader> shader, VkShaderStageFlagBits shaderStage);
        GraphicsPipelineBuilder &endShaderStage();

      private:
        GraphicsPipelineBuilder &parent_;
    };

    // One blend state per colour attachment of the subpass, in attachment order.
    class ColorBlendAttachmentStateBuilder {
      public:
        explicit ColorBlendAttachmentStateBuilder(GraphicsPipelineBuilder &parent);

        ColorBlendAttachmentStateBuilder &
        defineColorBlendAttachmentState(VkPipelineColorBlendAttachmentState colorBlendAttachmentState);
        // No blending; every channel written.
        ColorBlendAttachmentStateBuilder &defineDefaultColorBlendAttachmentState();
        // The pipeline blends with the states defined so far.
        GraphicsPipelineBuilder &endColorBlendAttachmentState();

      private:
        GraphicsPipelineBuilder &parent_;
    };

    // Without default member initializers: the builder value-initializes one inside its own definition, which older
    // GCC and Clang releases reject when the nested struct has them.
    struct ViewPortAndScissor {
        VkViewport viewport;
        VkRect2D scissor;
    };

    // VkPipelineDepthStencilStateCreateInfo without sType and pNext.
    struct DepthStencilState {
        VkPipelineDepthStencilStateCreateFlags flags{};
        VkBool32 depthTestEnable{};
        VkBool32 depthWriteEnable{};
        VkCompareOp depthCompareOp{};
        VkBool32 depthBoundsTestEnable{};
        VkBool32 stencilTestEnable{};
        VkStencilOpState front{};
        VkStencilOpState back{};
        float minDepthBounds{};
        float maxDepthBounds{};
    };

    GraphicsPipelineBuilder() = default;
    GraphicsPipelineBuilder(const GraphicsPipelineBuilder &) = delete;
    GraphicsPipelineBuilder &operator=(const GraphicsPipelineBuilder &) = delete;

    GraphicsPipelineBuilder &defineRenderPass(std::shared_ptr<RenderPass> renderPass, uint32_t subpassIndex);

    ShaderStageBuilder &beginShaderStage();

    GraphicsPipelineBuilder &defineVertexInputState(VkPipelineVertexInputStateCreateInfo vertexInputStateCreateInfo);
    // Reads vertex format T from binding 0 (Vertex::vertexLayoutInfo); void reads no vertex buffer at all.
    template <typename T>
    GraphicsPipelineBuilder &defineVertexInputState();

    // One viewport and one scissor, kept by the builder.
    GraphicsPipelineBuilder &defineViewportScissorState(ViewPortAndScissor viewPortAndScissor);

    GraphicsPipelineBuilder &
    defineRasterizationState(VkPipelineRasterizationStateCreateInfo rasterizationStateCreateInfo);

    ColorBlendAttachmentStateBuilder &beginColorBlendAttachmentState();

    // Keeps the table alive until the builder goes.
    GraphicsPipelineBuilder &definePipelineLayout(std::shared_ptr<DescriptorTable> descriptorTable);
    GraphicsPipelineBuilder &definePipelineLayout(VkPipelineLayout pipelineLayout);

    GraphicsPipelineBuilder &defineDepthStencilState(DepthStencilState depthStencilState);

    // Through the device's pipeline cache; throws when the driver cannot build the pipeline.
    std::shared_ptr<GraphicsPipeline> build(std::shared_ptr<Device> device) const;

  private:
    GraphicsPipelineBuilder &useVertexLayout(const VertexLayoutInfo &layout);

    ShaderStageBuilder shaderStageBuilder_{*this};
    ColorBlendAttachmentStateBuilder colorBlendAttachmentStateBuilder_{*this};

    std::vector<VkPipelineShaderStageCreateInfo> shaderStages_;
    std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments_;
    // Whether colorBlendState_ takes its attachments from colorBlendAttachments_ (endColorBlendAttachmentState). Its
    // pointer is only taken in build(), so later states cannot leave it dangling.
    bool blendsDefinedAttachments_ = false;

    VkPipelineVertexInputStateCreateInfo vertexInputState_{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};

    VkPipelineInputAssemblyStateCreateInfo inputAssemblyState_{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    ViewPortAndScissor viewPortAndScissor_{};
    VkPipelineViewportStateCreateInfo viewportState_{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};

    // Depth bias and polygon modes other than fill would need device features enabled.
    VkPipelineRasterizationStateCreateInfo rasterizationState_{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .depthBiasConstantFactor = 0.0f,
        .depthBiasClamp = 0.0f,
        .depthBiasSlopeFactor = 0.0f,
        .lineWidth = 1.0f,
    };

    VkPipelineMultisampleStateCreateInfo multisampleState_{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 1.0f,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE,
    };

    VkPipelineColorBlendStateCreateInfo colorBlendState_{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = 0,
        .pAttachments = nullptr,
        .blendConstants = {0.0f},
    };

    VkPipelineDepthStencilStateCreateInfo depthStencilState_{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_FALSE,
        .depthWriteEnable = VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_LESS,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
        .front = VkStencilOpState{},
        .back = VkStencilOpState{},
        .minDepthBounds = 0.0f,
        .maxDepthBounds = 1.0f,
    };

    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    uint32_t subpassIndex_ = 0;

    // What the create info names by handle (shaders, render pass, layout), kept alive until the builder goes.
    std::vector<std::shared_ptr<void>> referenced_;
};

template <typename T>
GraphicsPipelineBuilder &GraphicsPipelineBuilder::defineVertexInputState() {
    return useVertexLayout(Vertex::vertexLayoutInfo<T>());
}

template <>
GraphicsPipelineBuilder &GraphicsPipelineBuilder::defineVertexInputState<void>();

// A ray tracing pipeline: shader stages, then the groups that name them by index (the ray generation group first,
// then the miss groups, then the hit groups, as the shader binding table expects them).
class RayTracingPipelineBuilder {
  public:
    class ShaderStageBuilder {
      public:
        explicit ShaderStageBuilder(RayTracingPipelineBuilder &parent);

        // Keeps the shader alive until the builder goes.
        ShaderStageBuilder &defineShaderStage(std::shared_ptr<Shader> shader, VkShaderStageFlagBits shaderStage);
        RayTracingPipelineBuilder &endShaderStage();

      private:
        RayTracingPipelineBuilder &parent_;
    };

    class ShaderGroupBuilder {
      public:
        explicit ShaderGroupBuilder(RayTracingPipelineBuilder &parent);

        // Stage indices, or VK_SHADER_UNUSED_KHR.
        ShaderGroupBuilder &defineShaderGroup(VkRayTracingShaderGroupTypeKHR type,
                                              uint32_t generalShader,
                                              uint32_t closestHitShader,
                                              uint32_t anyHitShader,
                                              uint32_t intersectionShader);
        RayTracingPipelineBuilder &endShaderGroup();

      private:
        RayTracingPipelineBuilder &parent_;
    };

    RayTracingPipelineBuilder() = default;
    RayTracingPipelineBuilder(const RayTracingPipelineBuilder &) = delete;
    RayTracingPipelineBuilder &operator=(const RayTracingPipelineBuilder &) = delete;

    ShaderStageBuilder &beginShaderStage();
    ShaderGroupBuilder &beginShaderGroup();
    // Keeps the table alive until the builder goes.
    RayTracingPipelineBuilder &definePipelineLayout(std::shared_ptr<DescriptorTable> descriptorTable);
    // How deep traces nest: 1 when only ray generation traces, 2 when hit or miss shaders trace rays whose own hit and
    // miss shaders trace nothing. The driver sizes every invocation's stack for this depth, so it should be no deeper
    // than the shaders need. 3 unless defined.
    RayTracingPipelineBuilder &defineMaxRecursion(uint32_t depth);
    // Compiles as a deferred operation that every core the driver can use helps with (RADIANTE_NO_DEFERRED_COMPILE
    // keeps it on the calling thread), through the device's pipeline cache, and logs how long it took. Throws when
    // the driver cannot build the pipeline.
    std::shared_ptr<RayTracingPipeline> build(std::shared_ptr<Device> device) const;

  private:
    ShaderStageBuilder shaderStageBuilder_{*this};
    ShaderGroupBuilder shaderGroupBuilder_{*this};

    std::vector<VkPipelineShaderStageCreateInfo> shaderStages_;
    std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups_;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    uint32_t maxRecursion_ = 3;

    // What the create info names by handle (shaders, layout), kept alive until the builder goes.
    std::vector<std::shared_ptr<void>> referenced_;
};

class ComputePipelineBuilder {
  public:
    // Each keeps what it is given alive until the builder goes.
    ComputePipelineBuilder &defineShader(std::shared_ptr<Shader> shader);
    ComputePipelineBuilder &definePipelineLayout(std::shared_ptr<DescriptorTable> descriptorTable);

    // Through the device's pipeline cache; throws when the driver cannot build the pipeline.
    std::shared_ptr<ComputePipeline> build(std::shared_ptr<Device> device) const;

  private:
    std::shared_ptr<Shader> shader_;
    std::shared_ptr<DescriptorTable> descriptorTable_;
    VkShaderModule shaderModule_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
};

} // namespace gpu
