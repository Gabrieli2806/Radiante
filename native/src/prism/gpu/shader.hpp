#pragma once

#include "prism/all_extern.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <shaderc/shaderc.hpp>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace gpu {
class Device;

// The shaderc kind of a Vulkan shader stage. Throws std::invalid_argument for a stage the renderer does not compile.
shaderc_shader_kind shaderKindFromStage(VkShaderStageFlagBits stage);

// The on-disk cache of compiled shaders. A file is named after a hash of everything its compile reads (the source
// and every file it includes, the stage, the definitions, the include directories and the injected source), so a
// change to any of them misses the cache. Past 1500 files, the ones used longest ago are removed.
class ShaderSpirvCache {
  public:
    // A hash of a source file and, in turn, of each file it includes with #include "..." (found next to the including
    // file, else in the include directories). Empty when a file cannot be read or an include is not found: such a
    // compile is not cached.
    static std::optional<uint64_t> computeDependencyHash(const std::string &sourcePath,
                                                         const std::vector<std::string> &includeDirectories);

    // The name, without its .spv extension, of the cache file of one compile.
    static std::string computeCompiledHash(const std::string &sourcePath,
                                           VkShaderStageFlagBits stage,
                                           const std::unordered_map<std::string, std::string> &definitions,
                                           const std::vector<std::string> &includeDirectories,
                                           const std::string &injectedSource,
                                           uint64_t dependencyHash);

    // The SPIR-V of a cache file; empty when the file is missing or does not hold SPIR-V.
    static std::vector<uint32_t> readCachedSpirv(const std::filesystem::path &path);

    // Stores a cache file, then trims the cache.
    static void writeCachedSpirv(const std::filesystem::path &path, const std::vector<uint32_t> &spirv);
};

// Resolves the #include directives of a compile for shaderc: #include "file" next to the including file first, then
// both forms in the include directories, in order.
class ShaderIncluder : public shaderc::CompileOptions::IncluderInterface {
  public:
    explicit ShaderIncluder(std::vector<std::filesystem::path> includeDirectories);

    shaderc_include_result *GetInclude(const char *requestedSource,
                                       shaderc_include_type type,
                                       const char *requestingSource,
                                       size_t includeDepth) override;
    void ReleaseInclude(shaderc_include_result *data) override;

  private:
    struct IncludeData;

    std::vector<std::filesystem::path> includeDirectories_;
};

// A VkShaderModule, made from a SPIR-V file or compiled from GLSL.
class Shader : public SharedObject<Shader> {
  public:
    struct CompileResult {
        std::string sourcePath;
        VkShaderStageFlagBits stage{};
        std::vector<uint32_t> spirv;
        // Served from the cache.
        bool cacheHit = false;
        // A cache file was there but held no usable SPIR-V, so the shader was compiled again.
        bool cacheReadFailed = false;
        // Empty when the compile could not be cached.
        std::string cacheFilePath;

        CompileResult clone() const { return *this; }
    };

    // From a SPIR-V file.
    Shader(std::shared_ptr<Device> device, std::string path);
    // Compiled from GLSL, without the cache (see compileGlslToSpv).
    Shader(std::shared_ptr<Device> device,
           std::string sourcePath,
           VkShaderStageFlagBits stage,
           std::unordered_map<std::string, std::string> definitions = {},
           std::vector<std::string> includeDirectories = {},
           std::string injectedSource = "");
    Shader(std::shared_ptr<Device> device, CompileResult compileResult);
    ~Shader();

    Shader(const Shader &) = delete;
    Shader &operator=(const Shader &) = delete;

    // Compiles GLSL to SPIR-V for Vulkan 1.4, optimised for performance. `definitions` become macros, includes are
    // resolved by a ShaderIncluder searching the source's folder and then `includeDirectories`, and `injectedSource` is
    // placed after the #version line and the #extension lines that follow it. With a cacheDir, the result comes from
    // the cache when it is there and is stored there when it is not. Throws when the source cannot be read or does not
    // compile.
    static CompileResult compileGlslToSpv(std::string sourcePath,
                                          VkShaderStageFlagBits stage,
                                          std::unordered_map<std::string, std::string> definitions = {},
                                          std::vector<std::string> includeDirectories = {},
                                          std::string injectedSource = "",
                                          const std::filesystem::path &cacheDir = {});

    VkShaderModule &vkShaderModule();

  private:
    void createModule(std::span<const uint32_t> spirv, const std::string &name);

    std::shared_ptr<Device> device_;
    VkShaderModule module_ = VK_NULL_HANDLE;
};
} // namespace gpu
