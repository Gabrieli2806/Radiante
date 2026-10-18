#pragma once

#include "common/shared.hpp"
#include "prism/all_extern.hpp"
#include "prism/gpu/gpu.hpp"
#include "prism/render/modules/world/shader_pack/shader_pack.hpp"
#include "prism/render/modules/world/world_module.hpp"
#include "prism/render/shader_prewarm.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

class Framework;
class FrameworkContext;
class WorldPipeline;
struct WorldPipelineContext;
struct WorldModuleContext;
class WorldPrepare;
struct WorldPrepareContext;
struct RayTracingModuleContext;

// The path tracing stage of the world pipeline. It runs the ray tracing stage of the shader pack - its ray tracing,
// compute and full-screen passes, in the order the pack's execution script calls them - over the scene WorldPrepare
// gathers each frame, after updating SHaRC (the radiance cache) when the pack has one, and writes the images the
// stages after it read.
class RayTracingModule : public WorldModule, public SharedObject<RayTracingModule> {
    friend RayTracingModuleContext;

  public:
    constexpr static std::string_view NAME = "render_pipeline.module.ray_tracing.name";
    constexpr static uint32_t inputImageNum = 0;
    constexpr static uint32_t outputImageNum = 16;

    // The output images as the pack's passes name them, in output order: an image's index among the outputs is also
    // its binding in descriptor set 3.
    constexpr static std::string_view TARGET_RADIANCE = "out:radiance";
    constexpr static std::string_view TARGET_DIFFUSE_ALBEDO_METALLIC = "out:diffuse_albedo_metallic";
    constexpr static std::string_view TARGET_SPECULAR_ALBEDO = "out:specular_albedo";
    constexpr static std::string_view TARGET_NORMAL_ROUGHNESS = "out:normal_roughness";
    constexpr static std::string_view TARGET_MOTION_VECTOR = "out:motion_vector";
    constexpr static std::string_view TARGET_LINEAR_DEPTH = "out:linear_depth";
    constexpr static std::string_view TARGET_SPECULAR_HIT_DEPTH = "out:specular_hit_depth";
    constexpr static std::string_view TARGET_FIRST_HIT_DEPTH = "out:first_hit_depth";
    constexpr static std::string_view TARGET_FIRST_HIT_DIFFUSE_DIRECT_LIGHT = "out:first_hit_diffuse_direct_light";
    constexpr static std::string_view TARGET_FIRST_HIT_DIFFUSE_INDIRECT_LIGHT = "out:first_hit_diffuse_indirect_light";
    constexpr static std::string_view TARGET_FIRST_HIT_SPECULAR = "out:first_hit_specular";
    constexpr static std::string_view TARGET_FIRST_HIT_CLEAR = "out:first_hit_clear";
    constexpr static std::string_view TARGET_FIRST_HIT_BASE_EMISSION = "out:first_hit_base_emission";
    constexpr static std::string_view TARGET_FOG_IMAGE = "out:fog_image";
    constexpr static std::string_view TARGET_FIRST_HIT_REFRACTION = "out:first_hit_refraction";
    constexpr static std::string_view TARGET_FOG_TRANSMISSION = "out:fog_transmission";

    RayTracingModule();

    void init(std::shared_ptr<Framework> framework, std::shared_ptr<WorldPipeline> worldPipeline);

    bool setOrCreateInputImages(std::vector<std::shared_ptr<gpu::DeviceLocalImage>> &images,
                                std::vector<VkFormat> &formats,
                                uint32_t frameIndex) override;
    // Takes the output images the later stages made and makes the others, all of the size of the given ones.
    bool setOrCreateOutputImages(std::vector<std::shared_ptr<gpu::DeviceLocalImage>> &images,
                                 std::vector<VkFormat> &formats,
                                 uint32_t frameIndex) override;

    // The attributes of the shader pack the given attributes point at (the built-in pack when they name none) with
    // their translations, as JSON for the settings screen. Reads the pack from disk and needs no built module.
    static std::string getAttributes(const std::vector<std::string> &attributes, const std::string &language);

    void setAttributes(int attributeCount, std::vector<std::string> &attributeKVs) override;

    // The background compile (see ShaderPrewarm) of what this module, built again with the attributes
    // `attributeKVs`, will need: its passes' shaders with those settings' defines, and their ray tracing pipelines.
    // Copies what it needs of the module now, so the work reads nothing the frames use; call it while no frame is
    // being recorded (under the framework's frame lock). Empty when it cannot apply: the module is not built, or the
    // new attributes name another shader pack or switch SHaRC (the pipeline layout would differ).
    ShaderPrewarm::Work preparePrewarm(const std::vector<std::string> &attributeKVs);

    void build() override;

    std::vector<std::shared_ptr<WorldModuleContext>> &contexts() override;

    void bindTexture(std::shared_ptr<gpu::Sampler> sampler,
                     std::shared_ptr<gpu::DeviceLocalImage> image,
                     int index) override;

    void preClose() override;

  private:
    // SHaRC's hash grid entries, and the threads per group of its resolve pass.
    constexpr static uint32_t sharcCapacity = 1u << 23;
    constexpr static uint32_t sharcResolveWorkgroupSize = 64;
    // How often a loop of the execution script may repeat before it counts as endless.
    constexpr static uint32_t executionLoopLimit = 1u << 16;

    // The SHaRC parameters the shaders read (set 4, binding 0), laid out as the shaders declare them.
    struct SharcConfigData {
        uint32_t hashEntriesAddress[2];
        uint32_t lockAddress[2];
        uint32_t accumulationAddress[2];
        uint32_t resolvedAddress[2];
        glm::vec4 cameraPosition;
        glm::vec4 cameraPositionPrev;
        float sceneScale;
        float radianceScale;
        uint32_t accumulationFrameNum;
        uint32_t staleFrameNumMax;
        uint32_t capacity;
        uint32_t frameIndex;
        uint32_t enableAntiFireflyFilter;
        uint32_t useLockBuffer;
        uint32_t debugMode;
        uint32_t updateDownsampleFactor;
    };
    static_assert(sizeof(SharcConfigData) == 104, "SharcConfigData must keep the layout the shaders read");

    using ExecutionVariables = ShaderPack::ExecutionVariables;
    using PassVariant = std::variant<std::shared_ptr<FullScreenPass>,
                                     std::shared_ptr<RayTracingPass>,
                                     std::shared_ptr<ComputePass>>;
    using TargetImages = std::array<std::shared_ptr<gpu::DeviceLocalImage>, outputImageNum>;
    using ShaderList = std::span<const std::shared_ptr<gpu::Shader>>;

    // An image a pass names: one of this module's outputs, or a texture of the shader pack. Imported textures are
    // files the pack loads, only ever read.
    struct ImageResource {
        std::string name;
        std::optional<uint32_t> target;
        bool imported = false;
        // The layout the pass uses it in: read through a sampler, as storage, or drawn to.
        VkImageLayout layout = VK_IMAGE_LAYOUT_GENERAL;
    };

    struct BufferResource {
        std::string name;
        bool writable = false;
    };

    // Everything a pass reads and writes, each named once; an output overrides an input of the same name.
    struct Resources {
        std::vector<ImageResource> images;
        std::vector<BufferResource> buffers;
    };

    struct Pass {
        PassVariant pass;
        std::string name;
        std::string profilerMark;
        // Worked out the first time the pass runs, which is also when a pass naming something the pack lacks fails:
        // a pass the execution script never runs never does.
        std::optional<Resources> resources;
        // A full-screen pass's target, with the size and layer count it has in frame slot 0, which its pipelines are
        // made for.
        ImageResource target;
        VkExtent2D targetExtent{};
        uint32_t targetLayers = 0;
        // The pass's shaders in the batch the build compiles.
        size_t firstShader = 0;
        size_t shaderCount = 0;
    };

    // What the passes of one frame record with.
    struct Frame {
        RayTracingModuleContext &context;
        const std::shared_ptr<gpu::CommandBuffer> &commandBuffer;
        uint32_t index;
        uint32_t queueIndex;
    };

    // The module's attributes, and their values when an attribute is not given.
    struct Settings {
        // The shader_pack_path attribute as given (empty for the built-in pack).
        std::string shaderPackPath;
        bool jitter = true;
        bool sharc = true;
        uint32_t sharcDebugMode = 0;
        float sharcSceneScale = 64.0f;
        uint32_t sharcAccumulationFrameNum = 64;
        uint32_t sharcStaleFrameNumMax = 256;
        uint32_t sharcUpdateDownsampleFactor = 5;
    };

    void loadShaderPack();
    void initDescriptorTables();
    void initRuntimeResources();
    void initSharc();
    void initPipelines();
    void initContexts();

    void addPass(const ShaderPackLoader::PassConfig &config, size_t executionBufferSize);
    void selectSharcUpdatePass();
    void makeRenderTargets(Pass &entry, FullScreenPass &pass);

    void collectShaders(Pass &entry, FullScreenPass &pass, std::vector<ShaderPack::ShaderCreateInfo> &requests);
    void collectShaders(Pass &entry, RayTracingPass &pass, std::vector<ShaderPack::ShaderCreateInfo> &requests);
    void collectShaders(Pass &entry, ComputePass &pass, std::vector<ShaderPack::ShaderCreateInfo> &requests);
    void buildPass(Pass &entry, FullScreenPass &pass, ShaderList shaders);
    void buildPass(Pass &entry, RayTracingPass &pass, ShaderList shaders);
    void buildPass(Pass &entry, ComputePass &pass, ShaderList shaders);
    // Hands a ray tracing pass the shaders compiled for it, in the order collectShaders asked for them.
    static void takeShaders(RayTracingPass &pass, ShaderList shaders);
    // The pass's pipeline with this ray generation shader, laid out as its shader binding tables expect.
    static std::shared_ptr<gpu::RayTracingPipeline>
    buildRayTracingPipeline(const RayTracingPass &pass,
                            const std::shared_ptr<gpu::Shader> &rayGenShader,
                            const std::shared_ptr<gpu::DescriptorTable> &layout,
                            const std::shared_ptr<gpu::Device> &device);
    void uploadStaticSbts();

    void render(RayTracingModuleContext &context);
    const std::vector<ExpressionEvaluator::Variable> &updateExpressionVariables();
    void bindScene(RayTracingModuleContext &context);
    ExecutionVariables frameVariables() const;
    void uploadExecution(const std::shared_ptr<gpu::DeviceLocalBuffer> &buffer,
                         const ExecutionVariables &variables,
                         const Frame &frame);
    void updateSharcConfig(uint32_t frameIndex);

    std::shared_ptr<gpu::DeviceLocalImage> findImage(const ImageResource &resource, uint32_t frameIndex);
    Resources passResources(const ShaderPackLoader::ResourceList &inputs,
                            const ShaderPackLoader::ResourceList &outputs);
    void addPassBarriers(Pass &entry,
                         const ShaderPackLoader::ResourceList &inputs,
                         const ShaderPackLoader::ResourceList &outputs,
                         const Frame &frame);
    void recordBarriers(const Frame &frame);
    uint32_t dispatchSize(const std::string &expression,
                          const ExecutionVariables &variables,
                          std::string_view what,
                          const std::string &passName);

    void renderPass(Pass &entry, FullScreenPass &pass, const Frame &frame, const ExecutionVariables &variables);
    void renderPass(Pass &entry, RayTracingPass &pass, const Frame &frame, const ExecutionVariables &variables);
    void renderPass(Pass &entry, ComputePass &pass, const Frame &frame, const ExecutionVariables &variables);
    void renderSharcUpdate(Pass &entry, RayTracingPass &pass, const Frame &frame, const ExecutionVariables &variables);

    // Per frame slot.
    std::vector<TargetImages> targets_;
    std::vector<std::shared_ptr<gpu::DescriptorTable>> descriptorTables_;

    std::shared_ptr<gpu::Shader> fullScreenVertexShader_;

    std::shared_ptr<ShaderPack> shaderPack_;
    bool hasSharcRuntime_ = false;
    std::unordered_map<std::string, ShaderPackLoader::VariableConfig> executionVariableConfigs_;
    std::unordered_map<std::string, std::string> globalVariables_;
    // The values the pack's expressions may name besides its variables, as of the frame being recorded.
    std::vector<ExpressionEvaluator::Variable> expressionVariables_;

    std::vector<Pass> passes_;
    std::unordered_map<std::string, size_t> passIndices_;
    std::optional<size_t> sharcUpdatePass_;

    // The barriers of the pass being recorded; kept to reuse their memory.
    std::vector<gpu::CommandBuffer::BufferMemoryBarrier> bufferBarriers_;
    std::vector<gpu::CommandBuffer::ImageMemoryBarrier> imageBarriers_;

    std::shared_ptr<gpu::DeviceLocalBuffer> sharcHashEntriesBuffer_;
    std::shared_ptr<gpu::DeviceLocalBuffer> sharcLockBuffer_;
    std::shared_ptr<gpu::DeviceLocalBuffer> sharcAccumulationBuffer_;
    std::shared_ptr<gpu::DeviceLocalBuffer> sharcResolvedBuffer_;
    std::vector<std::shared_ptr<gpu::HostVisibleBuffer>> sharcConfigBuffers_;
    glm::dvec3 sharcPreviousCamera_ = glm::dvec3(0.0);
    bool sharcCameraKnown_ = false;
    uint32_t sharcFrameIndex_ = 0;
    // The downsample factor this frame's update traces with (renderSharcUpdate), which the raygen reads too.
    uint32_t sharcDownsample_ = 5;

    Settings settings_;

    std::shared_ptr<WorldPrepare> worldPrepare_;

    std::vector<std::shared_ptr<WorldModuleContext>> contexts_;
};

// The module's work for one frame slot.
struct RayTracingModuleContext : public WorldModuleContext, SharedObject<RayTracingModuleContext> {
    std::weak_ptr<RayTracingModule> rayTracingModule;

    std::shared_ptr<gpu::DescriptorTable> rayTracingDescriptorTable;
    std::shared_ptr<WorldPrepareContext> worldPrepareContext;

    RayTracingModuleContext(std::shared_ptr<FrameworkContext> frameworkContext,
                            std::shared_ptr<WorldPipelineContext> worldPipelineContext,
                            std::shared_ptr<RayTracingModule> rayTracingModule);

    void render() override;
};
