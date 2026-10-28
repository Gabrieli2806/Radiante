#pragma once

#include "common/shared.hpp"
#include "common/singleton.hpp"
#include "prism/all_extern.hpp"
#include "prism/gpu/gpu.hpp"
#include "prism/render/modules/world/shader_pack/shader_pack.hpp"
#include "prism/render/modules/world/world_module.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

struct PostRenderModuleContext;

// The world pipeline's last module. Every frame it turns the tracer's first-hit distances into a depth buffer, copies
// the tone mapped image into the pipeline's output and runs the shader pack's post_render stage on that output, in
// the order the stage's execution script gives: render passes that draw the stars, which the tracer leaves out, and
// full-screen passes.
class PostRenderModule : public WorldModule, public SharedObject<PostRenderModule> {
    friend PostRenderModuleContext;

  public:
    constexpr static std::string_view NAME = "render_pipeline.module.post_render.name";
    constexpr static uint32_t inputImageNum = 5;
    constexpr static uint32_t outputImageNum = 1;

    PostRenderModule();

    void init(std::shared_ptr<Framework> framework, std::shared_ptr<WorldPipeline> worldPipeline);

    bool setOrCreateInputImages(std::vector<std::shared_ptr<gpu::DeviceLocalImage>> &images,
                                std::vector<VkFormat> &formats,
                                uint32_t frameIndex) override;
    bool setOrCreateOutputImages(std::vector<std::shared_ptr<gpu::DeviceLocalImage>> &images,
                                 std::vector<VkFormat> &formats,
                                 uint32_t frameIndex) override;

    void setAttributes(int attributeCount, std::vector<std::string> &attributeKVs) override;
    // The star settings: the star field is made again from them.
    bool setRuntimeAttribute(const std::string &key, const std::string &value) override;

    void build() override;

    std::vector<std::shared_ptr<WorldModuleContext>> &contexts() override;

    void bindTexture(std::shared_ptr<gpu::Sampler> sampler,
                     std::shared_ptr<gpu::DeviceLocalImage> image,
                     int index) override;

    void preClose() override;

  private:
    using ExecutionVariable = ShaderPack::ExecutionVariable;
    using ExecutionVariables = ShaderPack::ExecutionVariables;

    // What one frame slot draws with.
    struct FrameResources {
        // Inputs.
        std::shared_ptr<gpu::DeviceLocalImage> ldr;           // the tone mapped image
        std::shared_ptr<gpu::DeviceLocalImage> firstHitDepth; // how far each pixel's camera ray went
        std::shared_ptr<gpu::DeviceLocalImage> hdr;
        std::shared_ptr<gpu::DeviceLocalImage> motionVector;
        std::shared_ptr<gpu::DeviceLocalImage> normalRoughness;
        // The world pipeline's output; "out:ldr" to the shader pack.
        std::shared_ptr<gpu::DeviceLocalImage> output;
        // The traced world's depth, which the render passes test against, and the pass that writes it.
        std::shared_ptr<gpu::DeviceLocalImage> depth;
        gpu::CommandBuffer::RenderPassBeginInfo depthPass{};
        std::shared_ptr<gpu::DescriptorTable> descriptorTable;
    };

    struct Recording;

    std::shared_ptr<gpu::DescriptorTable> createDescriptorTable(const std::shared_ptr<gpu::Device> &device) const;
    void createFrameResources();
    void createBlackImage();
    void bindBloom(Recording &recording);
    void createDepthPass();
    void createStarField();

    void ensureDynamicPipelines();
    std::pair<std::shared_ptr<FullScreenPass>, VkExtent2D>
    createFullScreenPass(const ShaderPackLoader::FullScreenPassConfig &config, size_t executionBufferSize);
    std::shared_ptr<RenderPass> createRenderPass(const ShaderPackLoader::RenderPassConfig &config,
                                                 size_t executionBufferSize);
    void
    buildFullScreenPipeline(const std::shared_ptr<gpu::Device> &device, FullScreenPass &pass, VkExtent2D extent) const;
    void buildRenderPipeline(const std::shared_ptr<gpu::Device> &device, RenderPass &pass) const;

    void recordFrame(FrameworkContext &frameworkContext);
    void recordFirstHitDepth(Recording &recording);
    void recordCopyToOutput(Recording &recording);
    void runScript(Recording &recording);
    void resetScriptVariables();
    void runPass(Recording &recording, const std::string &passName, const ExecutionVariables &variables);
    void runFullScreenPass(Recording &recording, const FullScreenPass &pass, const ExecutionVariables &variables);
    void runRenderPass(Recording &recording, const RenderPass &pass, const ExecutionVariables &variables);
    void syncInputs(Recording &recording, const ShaderPackLoader::ResourceList &inputs, VkPipelineStageFlags2 stages);
    void uploadExecutionValues(Recording &recording,
                               const std::shared_ptr<gpu::DeviceLocalBuffer> &executionBuffer,
                               const ExecutionVariables &variables);
    void recordOutputHandOff(Recording &recording);

    std::shared_ptr<gpu::DeviceLocalImage> findBuiltInImage(std::string_view name, uint32_t frameIndex) const;
    std::shared_ptr<gpu::DeviceLocalImage> findTargetImage(std::string_view target, uint32_t frameIndex) const;

    std::vector<FrameResources> frames_;
    std::vector<std::shared_ptr<WorldModuleContext>> contexts_;

    // How the shader pack's passes sample this module's images.
    std::shared_ptr<gpu::Sampler> colorSampler_;
    std::shared_ptr<gpu::Sampler> depthSampler_;
    // Set 3 binding 5 when the tone mapping module draws no bloom pyramid (common/pack_post.glsl): one black texel.
    std::shared_ptr<gpu::DeviceLocalImage> blackImage_;

    std::shared_ptr<gpu::RenderPass> depthRenderPass_;
    std::shared_ptr<gpu::GraphicsPipeline> depthPipeline_;
    // Vertex shader of the shader pack's full-screen passes.
    std::shared_ptr<gpu::Shader> fullScreenVertexShader_;

    std::shared_ptr<gpu::DeviceLocalBuffer> starVertexBuffer_;
    uint32_t starVertexCount_ = 0;

    // The shader pack's post_render stage.
    std::shared_ptr<ShaderPack> shaderPack_;
    std::unordered_map<std::string, ShaderPackLoader::VariableConfig> executionVariableConfigs_;
    // The script's global variables, carried from one frame to the next.
    std::unordered_map<std::string, std::string> globalVariables_;
    // The variables one run of the script works on; kept so that each frame reuses the map.
    ExecutionVariables scriptVariables_;
    // What the script's expressions read besides its variables.
    std::vector<ExpressionEvaluator::Variable> expressionVariables_;
    std::vector<std::shared_ptr<FullScreenPass>> fullScreenPasses_;
    std::vector<std::shared_ptr<RenderPass>> renderPasses_;
    std::unordered_map<std::string, std::shared_ptr<FullScreenPass>> fullScreenPassByName_;
    std::unordered_map<std::string, std::shared_ptr<RenderPass>> renderPassByName_;
    // Render passes of content the tracer draws (particles, weather, text, name tags): running one does nothing.
    std::unordered_set<std::string> inertPassNames_;
    bool dynamicPipelinesReady_ = false;

    // Reused by every frame.
    std::vector<gpu::ImageUse> imageUses_;

    uint32_t starCount_ = 3000;
    float starSizeMin_ = 0.5f;
    float starSizeMax_ = 0.7f;
    float starRadius_ = 400.0f;

    uint32_t width_ = 0;
    uint32_t height_ = 0;
};

struct PostRenderModuleContext : public WorldModuleContext, SharedObject<PostRenderModuleContext> {
    std::weak_ptr<PostRenderModule> postRenderModule;

    PostRenderModuleContext(std::shared_ptr<FrameworkContext> frameworkContext,
                            std::shared_ptr<WorldPipelineContext> worldPipelineContext,
                            std::shared_ptr<PostRenderModule> postRenderModule);

    void render() override;
};
