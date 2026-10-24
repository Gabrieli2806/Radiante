#pragma once

#include "common/shared.hpp"
#include "prism/all_extern.hpp"
#include "prism/gpu/gpu.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <atomic>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

// Arithmetic over named numbers: the dispatch sizes, texture extents, conditions and assignments a shader pack's
// configs.json writes as text. Besides tinyexpr's operators and functions it knows true, false and
// if(condition, then, else).
class ExpressionEvaluator {
  public:
    struct Variable {
        std::string name;
        double value = 0.0;
    };

    // A letter, then letters, digits and underscores.
    static bool isValidIdentifier(std::string_view name);
    // The names the evaluator defines itself.
    static bool isReservedIdentifier(std::string_view name);
    // Throws std::runtime_error for an invalid or reserved variable name and for an expression that does not parse.
    static double evaluate(const std::string &expression, const std::vector<Variable> &variables);
};

// Reads a shader pack (a folder, or a zip extracted next to the game) and its configs.json: the settings it offers,
// the textures and buffers it keeps between passes, its passes, and the scripts that order them each frame.
class ShaderPackLoader {
  public:
    enum class Stage {
        RayTracing,
        PostRender,
    };

    // What an attribute writes into the shaders' defines: a define holding its value, defines whose text has the
    // value in place of X, or defines chosen by the value.
    struct DefineConfig {
        std::optional<std::string> direct;
        std::unordered_map<std::string, std::string> expressions;
        std::unordered_map<std::string, std::unordered_map<std::string, std::string>> variants;
    };

    struct AttributeConfig {
        std::string name;
        std::string type;
        std::string defaultValue;
        DefineConfig define;
        // "runtime": true in configs.json: the shaders read the value from WorldUBO.packParams[runtimeSlot] instead
        // of having it compiled in, so changing it needs no rebuild. Slots follow the order the pack lists them.
        bool runtime = false;
        uint32_t runtimeSlot = 0;
    };

    struct VariableConfig {
        std::string name;
        std::string type;
        std::string defaultValue;
    };

    enum class TextureDimension {
        Texture2D,
        Texture2DArray,
        Texture3D,
        Cube,
    };

    struct TextureConfig {
        std::string name;
        bool imported = false;
        bool shared = false;
        TextureDimension dimension = TextureDimension::Texture2D;
        fs::path sourcePath;
        VkFormat format = VK_FORMAT_UNDEFINED;
        std::string widthExpression;
        std::string heightExpression;
        std::string depthExpression;
        uint32_t importedWidth = 0;
        uint32_t importedHeight = 0;
        uint32_t importedDepth = 1;
        float scale = 0.0f;
        std::optional<uint32_t> sampledBinding;
        std::optional<uint32_t> storageBinding;
        VkFilter filter = VK_FILTER_LINEAR;
        VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    };

    struct BufferConfig {
        std::string name;
        bool shared = false;
        std::string sizeExpression;
        uint32_t binding = 0;
    };

    struct ResourceList {
        std::vector<std::string> images;
        std::vector<std::string> buffers;
    };

    struct HitGroupConfig {
        std::string name;
        std::optional<fs::path> anyHit;
        std::optional<fs::path> closestHit;
        std::optional<fs::path> intersection;
        VkRayTracingShaderGroupTypeKHR type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
    };

    struct MissShaderConfig {
        std::string name;
        fs::path shaderPath;
    };

    struct FullScreenPassConfig {
        std::string name;
        fs::path fragmentShaderPath;
        std::string target;
        ResourceList inputs;
        ResourceList outputs;
        std::optional<uint32_t> face;
        std::optional<uint32_t> layer;
        std::unordered_map<std::string, std::string> definitions;
    };

    struct RenderShaderConfig {
        fs::path vertexShaderPath;
        fs::path fragmentShaderPath;
    };

    struct RenderPassConfig {
        std::string name;
        fs::path vertexShaderPath;
        fs::path fragmentShaderPath;
        std::unordered_map<std::string, RenderShaderConfig> shaderConfigs;
        std::string content;
        ResourceList inputs;
        ResourceList outputs;
        std::optional<bool> depthTest;
        std::optional<bool> depthWrite;
        std::optional<std::string> depthCompare;
        bool colorBlend = true;
        std::unordered_map<std::string, std::string> definitions;
    };

    struct RayTracingPassConfig {
        std::string name;
        bool querySharc = false;
        fs::path rayGenShaderPath;
        std::string defaultHitGroupName;
        ResourceList inputs;
        ResourceList outputs;
        std::string width = "RENDER_WIDTH";
        std::string height = "RENDER_HEIGHT";
        std::string depth = "1";
        std::vector<HitGroupConfig> hitGroups;
        std::vector<MissShaderConfig> missShaders;
        std::unordered_map<std::string, std::string> definitions;
    };

    struct ComputePassConfig {
        std::string name;
        fs::path computeShaderPath;
        ResourceList inputs;
        ResourceList outputs;
        std::string gx = "1";
        std::string gy = "1";
        std::string gz = "1";
        std::unordered_map<std::string, std::string> definitions;
    };

    struct PassConfig {
        enum class Type {
            FullScreen,
            Render,
            RayTracing,
            Compute,
        };

        Type type = Type::RayTracing;
        Stage stage = Stage::RayTracing;
        FullScreenPassConfig fullScreen;
        RenderPassConfig render;
        RayTracingPassConfig rayTracing;
        ComputePassConfig compute;
    };

    struct ExecutionCommand {
        struct Assignment {
            std::optional<std::string> type;
            bool explicitType = false;
            nlohmann::json value;
        };

        enum class Type {
            Assign,
            Pass,
            IfElse,
            While,
        };

        Type type = Type::Pass;
        // Run in this map's order. The execution buffer's layout follows the order new names are met in, so the
        // container stays as it is.
        std::unordered_map<std::string, Assignment> assignments;
        std::string passName;
        nlohmann::json condition;
        std::vector<ExecutionCommand> thenCommands;
        std::vector<ExecutionCommand> elseCommands;
    };

    struct ExecutionConfig {
        std::vector<VariableConfig> globalVariables;
        // Every variable the script uses, globals first: one float each in the execution buffer, in this order.
        std::vector<VariableConfig> variables;
        std::vector<ExecutionCommand> commands;
    };

    struct SharcConfig {
        std::string updatePassName;
        fs::path resolveCompShaderPath;
    };

    struct ShaderPack {
        fs::path rootPath;
        bool requiresEmission = false;
        std::vector<std::string> includeDirectories;
        std::vector<AttributeConfig> attributes;
        std::unordered_map<std::string, std::string> translations;
        std::vector<TextureConfig> textures;
        std::vector<BufferConfig> buffers;
        std::vector<PassConfig> passes;
        std::optional<SharcConfig> sharc;
        ExecutionConfig rayTracingExecution;
        ExecutionConfig postRenderExecution;
    };

    struct LoadResult {
        bool success = false;
        ShaderPack shaderPack;
        std::string error;
    };

    static constexpr char KEY_ATTRIBUTES[] = "attributes";
    static constexpr char KEY_REQUIRES_EMISSION[] = "requires_emission";
    static constexpr char KEY_TEXTURES[] = "textures";
    static constexpr char KEY_BUFFERS[] = "buffers";
    static constexpr char KEY_PASSES[] = "passes";
    static constexpr char KEY_NAME[] = "name";
    static constexpr char KEY_TYPE[] = "type";
    static constexpr char KEY_DEFAULT_VALUE[] = "default_value";
    static constexpr char KEY_DEFINE[] = "define";
    static constexpr char KEY_RUNTIME[] = "runtime";
    static constexpr char KEY_DEFINES[] = "defines";
    static constexpr char KEY_DEFINITIONS[] = "definitions";
    static constexpr char KEY_DIMENSION[] = "dimension";
    static constexpr char KEY_PATH[] = "path";
    static constexpr char KEY_EXTERNAL_PATHS[] = "external_paths";
    static constexpr char KEY_FORMAT[] = "format";
    static constexpr char KEY_WIDTH[] = "width";
    static constexpr char KEY_HEIGHT[] = "height";
    static constexpr char KEY_DEPTH[] = "depth";
    static constexpr char KEY_SIZE[] = "size";
    static constexpr char KEY_SCALE[] = "scale";
    static constexpr char KEY_SHARED[] = "shared";
    static constexpr char KEY_FILTER[] = "filter";
    static constexpr char KEY_ADDRESS_MODE[] = "address_mode";
    static constexpr char KEY_BINDING[] = "binding";
    static constexpr char KEY_SAMPLED_BINDING[] = "sampled_binding";
    static constexpr char KEY_STORAGE_BINDING[] = "storage_binding";
    static constexpr char KEY_VERTEX[] = "vertex";
    static constexpr char KEY_FRAGMENT[] = "fragment";
    static constexpr char KEY_COMPUTE[] = "compute";
    static constexpr char KEY_TARGET[] = "target";
    static constexpr char KEY_CONTENT[] = "content";
    static constexpr char KEY_INPUTS[] = "inputs";
    static constexpr char KEY_OUTPUTS[] = "outputs";
    static constexpr char KEY_IMAGES[] = "images";
    static constexpr char KEY_FACE[] = "face";
    static constexpr char KEY_LAYER[] = "layer";
    static constexpr char KEY_GX[] = "gx";
    static constexpr char KEY_GY[] = "gy";
    static constexpr char KEY_GZ[] = "gz";
    static constexpr char KEY_SHARC[] = "sharc";
    static constexpr char KEY_UPDATE_PASS[] = "update_pass";
    static constexpr char KEY_RESOLVE_COMP[] = "resolve_comp";
    static constexpr char KEY_QUERY_SHARC[] = "query_sharc";
    static constexpr char KEY_RGEN[] = "rgen";
    static constexpr char KEY_DEFAULT_HIT_GROUP[] = "default_hit_group";
    static constexpr char KEY_HIT_GROUPS[] = "hit_groups";
    static constexpr char KEY_SHADERS[] = "shaders";
    static constexpr char KEY_RCHIT[] = "rchit";
    static constexpr char KEY_RAHIT[] = "rahit";
    static constexpr char KEY_RINT[] = "rint";
    static constexpr char KEY_MISS[] = "miss";
    static constexpr char KEY_SHADER[] = "shader";
    static constexpr char KEY_LANG[] = "lang";
    static constexpr char KEY_EXECUTION[] = "execution";
    static constexpr char KEY_EXECUTION_POST[] = "execution_post";
    static constexpr char KEY_GLOBAL_VARIABLES[] = "global_variables";
    static constexpr char KEY_COMMANDS[] = "commands";
    static constexpr char KEY_ASSIGN[] = "assign";
    static constexpr char KEY_PASS[] = "pass";
    static constexpr char KEY_IF_ELSE[] = "if_else";
    static constexpr char KEY_WHILE[] = "while";
    static constexpr char KEY_CONDITION[] = "condition";
    static constexpr char KEY_THEN[] = "then";
    static constexpr char KEY_ELSE[] = "else";
    static constexpr char KEY_VALUE[] = "value";
    static constexpr char KEY_STAGE[] = "stage";
    static constexpr char KEY_DEPTH_TEST[] = "depth_test";
    static constexpr char KEY_DEPTH_WRITE[] = "depth_write";
    static constexpr char KEY_DEPTH_COMPARE[] = "depth_compare";
    static constexpr char KEY_COLOR_BLEND[] = "color_blend";
    static constexpr char KEY_RAY_TRACING[] = "ray_tracing";
    static constexpr char KEY_POST_RENDER[] = "post_render";

    static constexpr char VALUE_FULL_SCREEN[] = "full_screen";
    static constexpr char VALUE_RENDER[] = "render";
    static constexpr char VALUE_RAY_TRACING[] = "ray_tracing";
    static constexpr char VALUE_COMPUTE[] = "compute";
    static constexpr char VALUE_FILE[] = "file";
    static constexpr char VALUE_INTERMEDIATE[] = "intermediate";
    static constexpr char VALUE_2D[] = "2d";
    static constexpr char VALUE_2D_ARRAY[] = "2d_array";
    static constexpr char VALUE_3D[] = "3d";
    static constexpr char VALUE_CUBE[] = "cube";
    static constexpr char VALUE_TRIANGLE[] = "triangle";
    static constexpr char VALUE_AABB[] = "aabb";
    static constexpr uint32_t EXECUTION_BINDING = 0;

    // Loads the pack at `path` (a relative path is under Renderer::folderPath), or the built-in one when the path is
    // empty or that pack cannot be loaded. On failure `error` says why the last attempt failed.
    static LoadResult load(const fs::path &path, const fs::path &builtInPath, const std::string &language);

    // The defines the attributes write with these values (an attribute missing from them has its default).
    static std::unordered_map<std::string, std::string>
    buildAttributeDefinitions(const std::vector<AttributeConfig> &attributes,
                              const std::unordered_map<std::string, std::string> &currentValues);
    // GLSL declaring the execution buffer and a define reading each variable from it; empty without variables.
    static std::string buildExecutionSource(const std::vector<VariableConfig> &variables, uint32_t executionSet);
    static std::string toLower(std::string value);
    // A runtime attribute's value as its WorldUBO.packParams slot holds it, by the rules the defines use: a bool as 0
    // or 1, a choice as its index, a number as itself, a vec3 in xyz. Empty when it cannot be read that way.
    static std::optional<glm::vec4> parseRuntimeValue(const std::string &type, const std::string &value);
};

class Framework;

struct HitShaderGroup {
    std::string name;
    std::shared_ptr<gpu::Shader> closestHitShader;
    std::shared_ptr<gpu::Shader> anyHitShader;
    std::shared_ptr<gpu::Shader> intersectionShader;
    VkRayTracingShaderGroupTypeKHR type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
};

struct MissShader {
    std::string name;
    uint32_t index = 0;
    std::shared_ptr<gpu::Shader> shader;
};

struct FullScreenPass {
    ShaderPackLoader::FullScreenPassConfig config;
    std::shared_ptr<gpu::RenderPass> renderPass;
    std::vector<std::vector<std::shared_ptr<gpu::Framebuffer>>> framebuffers;
    std::vector<std::shared_ptr<gpu::Shader>> fragmentShaders;
    std::vector<std::shared_ptr<gpu::GraphicsPipeline>> pipelines;
    std::shared_ptr<gpu::DeviceLocalBuffer> executionBuffer;
};

struct RenderPass {
    enum class Target {
        Weather,
        Particle,
        Text,
        NameTag,
        Star,
    };

    struct ShaderVariant {
        std::shared_ptr<gpu::Shader> vertexShader;
        std::shared_ptr<gpu::Shader> fragmentShader;
        std::shared_ptr<gpu::GraphicsPipeline> pipeline;
    };

    ShaderPackLoader::RenderPassConfig config;
    Target target = Target::Particle;
    bool writesFirstHitDepth = false;
    std::shared_ptr<gpu::RenderPass> renderPass;
    std::vector<std::shared_ptr<gpu::Framebuffer>> framebuffers;
    std::shared_ptr<gpu::Shader> vertexShader;
    std::shared_ptr<gpu::Shader> fragmentShader;
    std::shared_ptr<gpu::GraphicsPipeline> pipeline;
    std::unordered_map<std::string, ShaderVariant> shaderVariants;
    std::shared_ptr<gpu::DeviceLocalBuffer> executionBuffer;
};

struct RayTracingPass {
    ShaderPackLoader::RayTracingPassConfig config;
    bool querySharcEnabled = false;
    bool isSharcUpdatePass = false;
    std::vector<MissShader> missShaders;
    std::vector<HitShaderGroup> hitShaderGroups;
    std::unordered_map<std::string, uint32_t> hitGroupNameToIndex;
    uint32_t shadowHitGroupIndex = 0;
    uint32_t fallbackHitGroupIndex = 0;
    uint32_t missGroupCount = 0;
    uint32_t hitGroupCount = 0;
    std::shared_ptr<gpu::Shader> rayGenUpdateShader;
    std::shared_ptr<gpu::Shader> rayGenQueryShader;
    std::shared_ptr<gpu::RayTracingPipeline> updatePipeline;
    std::shared_ptr<gpu::RayTracingPipeline> queryPipeline;
    std::vector<std::shared_ptr<gpu::SBT>> updateSbts;
    std::vector<std::shared_ptr<gpu::SBT>> querySbts;
    std::shared_ptr<gpu::Shader> sharcResolveCompShader;
    std::shared_ptr<gpu::ComputePipeline> sharcResolvePipeline;
    std::shared_ptr<gpu::DeviceLocalBuffer> executionBuffer;

};

struct ComputePass {
    ShaderPackLoader::ComputePassConfig config;
    std::shared_ptr<gpu::Shader> computeShader;
    std::shared_ptr<gpu::ComputePipeline> pipeline;
    std::shared_ptr<gpu::DeviceLocalBuffer> executionBuffer;
};

// A loaded shader pack as one world pipeline build uses it, shared by the ray tracing and post render modules: the
// settings it was built with, its textures and buffers, its execution scripts and the compile of its shaders.
class ShaderPack {
  public:
    struct BuildConfig {
        std::string shaderPackPath;
        bool shouldUseSharc = true;
        std::unordered_map<std::string, std::string> staticAttributes;
        std::string language = "en_us";
    };

    struct RuntimeTexture {
        ShaderPackLoader::TextureConfig config;
        std::shared_ptr<gpu::Sampler> sampler;
        std::shared_ptr<gpu::DeviceLocalImage> importedImage;
        std::vector<std::shared_ptr<gpu::DeviceLocalImage>> frameImages;
        uint32_t sampledViewIndex = 0;
    };

    struct RuntimeBuffer {
        ShaderPackLoader::BufferConfig config;
        std::vector<std::shared_ptr<gpu::DeviceLocalBuffer>> frameBuffers;
    };

    struct StageRuntime {
        std::unordered_map<std::string, ShaderPackLoader::VariableConfig> executionVariableConfigs;
        std::unordered_map<std::string, std::string> globalVariables;
    };

    struct ExecutionVariable {
        std::string name;
        std::string value;
        std::string type;
    };

    struct ShaderCreateInfo {
        fs::path path;
        VkShaderStageFlagBits stage;
        std::unordered_map<std::string, std::string> definitions;
        ShaderPackLoader::Stage executionStage = ShaderPackLoader::Stage::RayTracing;
        uint32_t executionSet = 0;
    };

#ifdef DEBUG
    struct ShaderBatchStats {
        size_t requestCount = 0;
        size_t uniqueShaderCount = 0;
        size_t cacheHitCount = 0;
        size_t cacheMissCount = 0;
        size_t cacheReadFailureCount = 0;
    };
#endif

    using ExecutionVariables = std::unordered_map<std::string, ExecutionVariable>;
    using ExecutePassCallback = std::function<void(const std::string &passName, ExecutionVariables &variables)>;

    explicit ShaderPack(std::shared_ptr<Framework> framework);

    static std::filesystem::path builtInShaderPackPath();
    // The ray tracing module's attribute key/value pairs: the pack, whether SHaRC may be used, and the settings.
    static BuildConfig buildConfigFromRayTracingAttributes(const std::vector<std::string> &attributeKVs);
    // A pass's execution buffer (one float per execution variable, filled by uploadExecutionBuffer); null for size 0.
    static std::shared_ptr<gpu::DeviceLocalBuffer>
    createPassExecutionBuffer(std::shared_ptr<gpu::Device> device,
                              std::shared_ptr<gpu::VMA> vma,
                              size_t executionBufferSize);

    // Loads the pack (the built-in one in its place when it needs block emission the chunks do not collect) and
    // works out its defines for these settings. False with the reason in `error` when no pack could be loaded.
    bool initialize(const BuildConfig &config, std::string &error);
    // Makes the pack's textures and buffers for this render size, or only resizes buffers when it has them already.
    void ensureRuntimeResources(uint32_t referenceWidth, uint32_t referenceHeight);
    // Remakes the buffers whose size expression now comes to another size.
    void refreshRuntimeBuffers();
    // RENDER_WIDTH and the like, for the texture and buffer size expressions.
    void setRuntimeResourceExpressionVariables(const std::vector<ExpressionEvaluator::Variable> &variables);
    void bindRuntimeResources(const std::shared_ptr<gpu::DescriptorTable> &descriptorTable,
                              uint32_t setIndex,
                              uint32_t frameIndex);
    void defineRuntimeResourceDescriptorSet(gpu::DescriptorTableBuilder &builder,
                                            VkShaderStageFlags sampledImageStageFlags,
                                            VkShaderStageFlags storageImageStageFlags,
                                            VkShaderStageFlags storageBufferStageFlags) const;
    void defineExecutionDescriptorSet(gpu::DescriptorTableBuilder &builder,
                                      ShaderPackLoader::Stage stage,
                                      VkShaderStageFlags stageFlags) const;
    void copyStageExecutionState(ShaderPackLoader::Stage stage,
                                 std::unordered_map<std::string, ShaderPackLoader::VariableConfig> &variableConfigs,
                                 std::unordered_map<std::string, std::string> &globalVariables) const;
    // The execution buffer's descriptor set comes right after the runtime resources' set, when the pack has one.
    uint32_t executionSet(uint32_t runtimeResourceSet) const;
    bool hasRuntimeResources() const;
    void preClose();

    // Compiles the requests together, each distinct shader once, and returns one shader per request in their order.
#ifdef DEBUG
    std::vector<std::shared_ptr<gpu::Shader>> createShaders(std::shared_ptr<gpu::Device> device,
                                                           const std::vector<ShaderCreateInfo> &requests,
                                                           ShaderBatchStats *stats = nullptr) const;
#else
    std::vector<std::shared_ptr<gpu::Shader>> createShaders(std::shared_ptr<gpu::Device> device,
                                                           const std::vector<ShaderCreateInfo> &requests) const;
#endif

    // Where a batch compiled outside a rebuild counts its progress (in place of the rebuild's counters), and the
    // flag that stops it. Any of them may be null.
    struct CompileProgress {
        std::atomic<uint32_t> *done = nullptr;
        std::atomic<uint32_t> *total = nullptr;
        const std::atomic<bool> *cancel = nullptr;
    };
    // As createShaders, with `attributeDefinitions` (ShaderPackLoader::buildAttributeDefinitions of other settings)
    // in place of the defines this pack was built with: compiles what a build with those settings will look up in the
    // SPIR-V cache. Thread-safe against frames, which never change what it reads. Empty once cancelled.
    std::vector<std::shared_ptr<gpu::Shader>>
    createShaders(std::shared_ptr<gpu::Device> device,
                  const std::vector<ShaderCreateInfo> &requests,
                  const std::unordered_map<std::string, std::string> &attributeDefinitions,
                  const CompileProgress &progress) const;

    const ShaderPackLoader::ShaderPack &shaderPack() const;
    const StageRuntime &stageRuntime(ShaderPackLoader::Stage stage) const;
    const ShaderPackLoader::ExecutionConfig &execution(ShaderPackLoader::Stage stage) const;
    std::string executionSource(ShaderPackLoader::Stage stage, uint32_t executionSet) const;
    // The execution variables come first, then the attribute defines (when included), then the additional
    // variables; a name already present hides a later one.
    double evaluateNumericExpression(ShaderPackLoader::Stage stage,
                                     const std::string &expression,
                                     const ExecutionVariables &variables,
                                     const std::vector<ExpressionEvaluator::Variable> &additionalVariables,
                                     bool includeAttributeDefines) const;
    bool evaluateCondition(ShaderPackLoader::Stage stage,
                           const nlohmann::json &condition,
                           const ExecutionVariables &variables,
                           const std::vector<ExpressionEvaluator::Variable> &additionalVariables,
                           bool includeAttributeDefines) const;
    // The value `assignment` gives `variable`, as text; also settles the variable's type.
    std::string evaluateAssignment(ShaderPackLoader::Stage stage,
                                   ExecutionVariable &variable,
                                   const ShaderPackLoader::ExecutionCommand::Assignment &assignment,
                                   const ExecutionVariables &variables,
                                   const std::vector<ExpressionEvaluator::Variable> &additionalVariables,
                                   bool includeAttributeDefines) const;
    // Records the variables' values into a pass's execution buffer (in the command buffer itself, so each run of a pass
    // reads the values of that run) and binds it, ready for `dstStageMask`. Outside a render pass.
    void uploadExecutionBuffer(ShaderPackLoader::Stage stage,
                               const std::shared_ptr<gpu::DeviceLocalBuffer> &executionBuffer,
                               const ExecutionVariables &variables,
                               const std::shared_ptr<gpu::CommandBuffer> &commandBuffer,
                               const std::shared_ptr<gpu::DescriptorTable> &descriptorTable,
                               uint32_t executionSet,
                               uint32_t queueIndex,
                               VkPipelineStageFlags2 dstStageMask) const;
    // Runs an execution script; a while loop running more than loopLimit times throws.
    void executeCommands(ShaderPackLoader::Stage stage,
                         const std::vector<ShaderPackLoader::ExecutionCommand> &commands,
                         ExecutionVariables &variables,
                         const std::vector<ExpressionEvaluator::Variable> &additionalVariables,
                         bool includeAttributeDefines,
                         uint32_t loopLimit,
                         const ExecutePassCallback &executePass) const;

    bool hasSharcRuntime() const;
    bool runtimeResourcesReady() const;

    // The runtime attributes' values by WorldUBO.packParams slot, as this pack was built with them or last set.
    const std::array<glm::vec4, PRISM_PACK_PARAM_COUNT> &runtimeValues() const;
    std::optional<uint32_t> runtimeSlot(const std::string &name) const;
    // Changes a runtime attribute without a rebuild. The value is kept for the execution script and the dispatch
    // sizes (read next frame); slot and packed are what WorldUBO.packParams needs. False when the pack has no such
    // attribute, compiles it in, or cannot read the value.
    bool setRuntimeAttribute(const std::string &name, const std::string &value, uint32_t &slot, glm::vec4 &packed);

    std::optional<std::reference_wrapper<RuntimeTexture>> findRuntimeTexture(std::string_view name);
    std::optional<std::reference_wrapper<const RuntimeTexture>> findRuntimeTexture(std::string_view name) const;
    std::optional<std::reference_wrapper<RuntimeBuffer>> findRuntimeBuffer(std::string_view name);
    std::optional<std::reference_wrapper<const RuntimeBuffer>> findRuntimeBuffer(std::string_view name) const;

    std::shared_ptr<gpu::DeviceLocalImage> findRuntimeVKTexture(RuntimeTexture &runtimeTexture, uint32_t frameIndex);
    std::shared_ptr<gpu::DeviceLocalImage>
    findRuntimeVKTexture(const RuntimeTexture &runtimeTexture, uint32_t frameIndex) const;
    std::shared_ptr<gpu::DeviceLocalBuffer> findRuntimeVKBuffer(RuntimeBuffer &runtimeBuffer, uint32_t frameIndex);
    std::shared_ptr<gpu::DeviceLocalBuffer>
    findRuntimeVKBuffer(const RuntimeBuffer &runtimeBuffer, uint32_t frameIndex) const;

  private:
    // Lookups by std::string_view without making a std::string.
    struct NameHash {
        using is_transparent = void;
        size_t operator()(std::string_view name) const noexcept {
            return std::hash<std::string_view>{}(name);
        }
    };
    template <typename T>
    using NameMap = std::unordered_map<std::string, T, NameHash, std::equal_to<>>;

    // The attribute defines as numbers only change with an attribute's value, so they are worked out then rather than
    // for every expression of every frame.
    void rebuildAttributeNumbers();
    // A texture or buffer size expression: RENDER_WIDTH and the like, then the attribute defines.
    double evaluateResourceExpression(const std::string &expression) const;
    VkExtent3D runtimeTextureExtent(const ShaderPackLoader::TextureConfig &config) const;
    size_t runtimeBufferSize(const ShaderPackLoader::BufferConfig &config) const;
    ExecutionVariable &
    findOrCreateExecutionVariable(ShaderPackLoader::Stage stage, ExecutionVariables &variables, const std::string &name)
        const;
    static void initStageRuntime(StageRuntime &stageRuntime, const ShaderPackLoader::ExecutionConfig &execution);
    void initRuntimeTextures();
    void initRuntimeBuffers();
    // Reads the imported textures' files into new images, ready to upload.
    void loadImportedTextures();
    // Clears the new textures and buffers to zero and uploads the imported textures, then waits for the GPU.
    void prepareRuntimeResources();

    std::weak_ptr<Framework> framework_;

    ShaderPackLoader::ShaderPack shaderPack_;
    std::unordered_map<std::string, std::string> shaderAttributes_;
    StageRuntime rayTracingStageRuntime_;
    StageRuntime postRenderStageRuntime_;
    std::unordered_map<std::string, std::string> staticAttributeValues_;
    std::array<glm::vec4, PRISM_PACK_PARAM_COUNT> runtimeValues_{};

    // The attribute defines the expressions read, as numbers: in attribute order, each name once. When an attribute's
    // value is no number, those expressions fail, as they did when the numbers were worked out for each of them: the
    // failure is kept here instead and thrown again.
    std::vector<ExpressionEvaluator::Variable> attributeNumbers_;
    std::exception_ptr attributeNumbersError_;

    std::vector<RuntimeTexture> runtimeTextures_;
    NameMap<size_t> runtimeTextureIndices_;
    std::vector<RuntimeBuffer> runtimeBuffers_;
    NameMap<size_t> runtimeBufferIndices_;
    std::vector<ExpressionEvaluator::Variable> runtimeResourceExpressionVariables_;

    uint32_t referenceWidth_ = 0;
    uint32_t referenceHeight_ = 0;
    bool runtimeResourcesReady_ = false;
    bool hasSharcRuntime_ = false;
};
