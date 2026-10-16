#include "prism/render/modules/world/shader_pack/shader_pack_internal.hpp"

#include "prism/render/pipeline.hpp"
#include "prism/render/renderer.hpp"
#include "prism/util/logging.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <numeric>
#include <sstream>
#include <unordered_set>

namespace {

using Definitions = std::unordered_map<std::string, std::string>;

struct NameHash {
    using is_transparent = void;
    size_t operator()(std::string_view name) const noexcept {
        return std::hash<std::string_view>{}(name);
    }
};
using NameSet = std::unordered_set<std::string, NameHash, std::equal_to<>>;

fs::path shaderCacheFolder() {
    return Renderer::folderPath / "cache/shaders";
}

// ---- which attribute defines a shader names

bool isSpace(char ch) {
    return std::isspace(static_cast<unsigned char>(ch)) != 0;
}

bool isIdentifierStart(char ch) {
    return std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

bool isIdentifierChar(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

// `line` without its comments, into `code`. A block comment may have begun on an earlier line and run on past this one.
void stripComments(std::string_view line, bool &inBlockComment, std::string &code) {
    code.clear();
    for (size_t i = 0; i < line.size();) {
        const bool twoLeft = i + 1 < line.size();
        if (inBlockComment) {
            if (twoLeft && line[i] == '*' && line[i + 1] == '/') {
                inBlockComment = false;
                i += 2;
            } else {
                i++;
            }
            continue;
        }
        if (twoLeft && line[i] == '/' && line[i + 1] == '*') {
            inBlockComment = true;
            i += 2;
            continue;
        }
        if (twoLeft && line[i] == '/' && line[i + 1] == '/') break;
        code.push_back(line[i++]);
    }
}

// The file an #include "file" line names. Includes in angle brackets are not followed.
std::optional<std::string_view> quotedInclude(std::string_view line) {
    size_t i = 0;
    const auto skipSpaces = [&] {
        while (i < line.size() && isSpace(line[i])) i++;
    };
    skipSpaces();
    if (i >= line.size() || line[i] != '#') return std::nullopt;
    i++;
    skipSpaces();
    const size_t directive = i;
    while (i < line.size() && isIdentifierChar(line[i])) i++;
    if (line.substr(directive, i - directive) != "include") return std::nullopt;
    skipSpaces();
    if (i >= line.size() || line[i] != '"') return std::nullopt;
    const size_t start = i + 1;
    const size_t end = line.find('"', start);
    if (end == std::string_view::npos) return std::nullopt;
    return line.substr(start, end - start);
}

// An included file next to the file including it, else in the first include folder that has it.
std::optional<fs::path> resolveInclude(const fs::path &includingFile,
                                       std::string_view include,
                                       const std::vector<fs::path> &includeDirectories) {
    std::error_code error;
    const auto isFile = [&](const fs::path &candidate) {
        return fs::exists(candidate, error) && fs::is_regular_file(candidate, error);
    };
    if (fs::path candidate = includingFile.parent_path() / fs::path(include); isFile(candidate)) {
        return fs::weakly_canonical(candidate, error);
    }
    for (const auto &directory : includeDirectories) {
        if (fs::path candidate = directory / fs::path(include); isFile(candidate)) {
            return fs::weakly_canonical(candidate, error);
        }
    }
    return std::nullopt;
}

// The attribute defines a source file and the files it includes name, and how much source that is.
struct SourceUse {
    // Nothing when a file could not be read or an include not found: such a shader is given every define.
    std::optional<std::unordered_set<std::string>> attributes;
    uint64_t bytes = 0;
};

// Reads the shaders of one batch, each file once.
class SourceScanner {
  public:
    SourceScanner(const NameSet &attributeNames, std::vector<fs::path> includeDirectories)
        : attributeNames_(attributeNames), includeDirectories_(std::move(includeDirectories)) {}

    const SourceUse &scan(const fs::path &shader) {
        std::unordered_set<std::string> open;
        return scanFile(shader, open);
    }

  private:
    // `open` holds the files being read further up, so an include cycle stops where it closes.
    const SourceUse &scanFile(const fs::path &file, std::unordered_set<std::string> &open) {
        static const SourceUse closesCycle{std::unordered_set<std::string>{}, 0};
        std::error_code error;
        fs::path canonical = fs::weakly_canonical(file, error);
        if (error) canonical = file.lexically_normal();
        std::string key = canonical.string();
        if (const auto known = files_.find(key); known != files_.end()) return known->second;
        if (!open.insert(key).second) return closesCycle;

        SourceUse use = read(canonical, open);
        open.erase(key);
        return files_.insert_or_assign(std::move(key), std::move(use)).first->second;
    }

    SourceUse read(const fs::path &file, std::unordered_set<std::string> &open) {
        std::ifstream stream(file);
        if (!stream.is_open()) return {};
        std::ostringstream text;
        text << stream.rdbuf();
        const std::string source = std::move(text).str();

        SourceUse use{std::unordered_set<std::string>{}, source.size()};
        std::string code;
        bool inBlockComment = false;
        for (size_t start = 0; start <= source.size();) {
            const size_t newline = source.find('\n', start);
            const size_t end = newline == std::string::npos ? source.size() : newline;
            stripComments(std::string_view(source).substr(start, end - start), inBlockComment, code);
            start = end + 1;

            for (size_t i = 0; i < code.size();) {
                if (!isIdentifierStart(code[i])) {
                    i++;
                    continue;
                }
                const size_t begin = i++;
                while (i < code.size() && isIdentifierChar(code[i])) i++;
                const std::string_view identifier = std::string_view(code).substr(begin, i - begin);
                if (attributeNames_.contains(identifier)) use.attributes->emplace(identifier);
            }

            const auto include = quotedInclude(code);
            if (!include.has_value()) continue;
            const auto resolved = resolveInclude(file, *include, includeDirectories_);
            if (!resolved.has_value()) {
                use.attributes.reset();
                return use;
            }
            const SourceUse &included = scanFile(*resolved, open);
            use.bytes += included.bytes;
            if (!included.attributes.has_value()) {
                use.attributes.reset();
                return use;
            }
            use.attributes->insert(included.attributes->begin(), included.attributes->end());
        }
        return use;
    }

    const NameSet &attributeNames_;
    std::vector<fs::path> includeDirectories_;
    // By canonical path.
    std::unordered_map<std::string, SourceUse> files_;
};

// ---- compiles

// Requests with the same key compile to the same SPIR-V.
std::string compileKey(const ShaderPack::ShaderCreateInfo &request,
                       const Definitions &definitions,
                       const std::string &executionSource) {
    std::vector<std::pair<std::string_view, std::string_view>> sorted(definitions.begin(), definitions.end());
    std::sort(sorted.begin(), sorted.end());
    std::string key = request.path.string();
    key.append(1, '|').append(std::to_string(static_cast<int>(request.stage))).append(1, '|');
    for (const auto &[name, value] : sorted) key.append(name).append(1, '=').append(value).append(1, ';');
    key.append(1, '|').append(std::to_string(static_cast<int>(request.executionStage)));
    key.append(1, '|').append(std::to_string(request.executionSet));
    key.append(1, '|').append(executionSource);
    return key;
}

} // namespace

std::shared_ptr<gpu::Shader> ShaderPack::createShader(std::shared_ptr<gpu::Device> device,
                                                      const std::filesystem::path &path,
                                                      VkShaderStageFlagBits stage,
                                                      const std::unordered_map<std::string, std::string> &definitions,
                                                      ShaderPackLoader::Stage executionStage,
                                                      uint32_t executionSet) const {
    Definitions merged = shaderAttributes_;
    for (const auto &[name, value] : definitions) merged.insert_or_assign(name, value);
    return gpu::Shader::create(device, gpu::Shader::compileGlslToSpv(path.string(), stage, std::move(merged),
                                                                     shaderPack_.includeDirectories,
                                                                     executionSource(executionStage, executionSet),
                                                                     shaderCacheFolder()));
}

namespace {

// How a batch went: requests, distinct compiles, and how many of those the SPIR-V cache had.
struct BatchCounts {
    size_t requests = 0;
    size_t unique = 0;
    size_t cacheHits = 0;
    size_t cacheMisses = 0;
    size_t cacheReadFailures = 0;
};

// Compiles the requests together, each distinct shader once, with `attributeDefinitions` as the attribute defines.
// Empty when progress.cancel was set before the batch finished.
std::vector<std::shared_ptr<gpu::Shader>> compileBatch(const ShaderPack &pack,
                                                       const std::shared_ptr<gpu::Device> &device,
                                                       const std::vector<ShaderPack::ShaderCreateInfo> &requests,
                                                       const Definitions &attributeDefinitions,
                                                       const ShaderPack::CompileProgress &progress,
                                                       BatchCounts &counts) {
    counts.requests = requests.size();
    if (requests.empty()) return {};
    const auto cancelled = [&] { return progress.cancel != nullptr && progress.cancel->load(); };
    const std::vector<std::string> &includeDirectories = pack.shaderPack().includeDirectories;

    // A shader is given only the attribute defines it names, so that a setting recompiles only the shaders that read
    // it (the cache key is everything a compile gets). A request's own definitions do not replace attribute defines.
    NameSet attributeNames;
    attributeNames.reserve(attributeDefinitions.size());
    for (const auto &entry : attributeDefinitions) attributeNames.insert(entry.first);
    SourceScanner scanner(attributeNames, std::vector<fs::path>(includeDirectories.begin(), includeDirectories.end()));

    struct Compile {
        size_t request; // the first request it serves
        Definitions definitions;
        std::string executionSource;
        uint64_t sourceBytes;
    };
    std::vector<Compile> compiles;
    std::vector<size_t> compileOfRequest(requests.size());
    std::unordered_map<std::string, size_t> compileByKey;
    for (size_t i = 0; i < requests.size(); i++) {
        const ShaderPack::ShaderCreateInfo &request = requests[i];
        const SourceUse &use = scanner.scan(request.path);
        Definitions definitions;
        if (use.attributes.has_value()) {
            for (const std::string &name : *use.attributes) definitions.emplace(name, attributeDefinitions.at(name));
        } else {
            definitions = attributeDefinitions;
        }
        for (const auto &[name, value] : request.definitions) {
            if (!attributeNames.contains(name)) definitions.insert_or_assign(name, value);
        }
        std::string source = pack.executionSource(request.executionStage, request.executionSet);

        const auto [found, added] = compileByKey.try_emplace(compileKey(request, definitions, source), compiles.size());
        if (added) compiles.push_back({i, std::move(definitions), std::move(source), use.bytes});
        compileOfRequest[i] = found->second;
    }
    counts.unique = compiles.size();

    if (progress.total != nullptr) progress.total->fetch_add(static_cast<uint32_t>(compiles.size()));
    const auto started = std::chrono::steady_clock::now();
    // The largest first: started last, one long compile would keep a single thread busy after the others had finished.
    std::vector<size_t> order(compiles.size());
    std::iota(order.begin(), order.end(), size_t{0});
    std::stable_sort(order.begin(), order.end(),
                     [&](size_t a, size_t b) { return compiles[a].sourceBytes > compiles[b].sourceBytes; });
    const fs::path cacheFolder = shaderCacheFolder();
    std::vector<gpu::Shader::CompileResult> results(compiles.size());
    shader_pack::parallelEach(order.size(), [&](size_t k) {
        // A cancelled batch skips what it has not started; the compiles under way finish (shaderc cannot be stopped).
        if (cancelled()) return;
        Compile &compile = compiles[order[k]];
        const ShaderPack::ShaderCreateInfo &request = requests[compile.request];
        results[order[k]] = gpu::Shader::compileGlslToSpv(request.path.string(), request.stage,
                                                           std::move(compile.definitions), includeDirectories,
                                                           std::move(compile.executionSource), cacheFolder);
        if (progress.done != nullptr) progress.done->fetch_add(1);
    });
    if (cancelled()) return {};
    radiante::out() << "[ShaderPack] " << compiles.size() << " shaders to SPIR-V in "
                    << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                               started)
                           .count()
                    << " ms" << std::endl;

    for (const auto &result : results) {
        if (result.cacheHit) {
            counts.cacheHits++;
        } else {
            counts.cacheMisses++;
        }
        if (result.cacheReadFailed) counts.cacheReadFailures++;
    }

    std::vector<std::shared_ptr<gpu::Shader>> shaders(requests.size());
    shader_pack::parallelEach(requests.size(), [&](size_t i) {
        shaders[i] = gpu::Shader::create(device, results[compileOfRequest[i]].clone());
    });
    return shaders;
}

} // namespace

std::vector<std::shared_ptr<gpu::Shader>> ShaderPack::createShaders(std::shared_ptr<gpu::Device> device,
                                                                   const std::vector<ShaderCreateInfo> &requests
#ifdef DEBUG
                                                                   ,
                                                                   ShaderBatchStats *stats
#endif
                                                                   ) const {
    BatchCounts counts;
    auto shaders = compileBatch(*this, device, requests, shaderAttributes_,
                                {.done = &Pipeline::rebuildShadersDone, .total = &Pipeline::rebuildShadersTotal},
                                counts);
#ifdef DEBUG
    if (stats != nullptr) {
        stats->requestCount = counts.requests;
        stats->uniqueShaderCount = counts.unique;
        stats->cacheHitCount = counts.cacheHits;
        stats->cacheMissCount = counts.cacheMisses;
        stats->cacheReadFailureCount = counts.cacheReadFailures;
    }
#endif
    return shaders;
}

std::vector<std::shared_ptr<gpu::Shader>>
ShaderPack::createShaders(std::shared_ptr<gpu::Device> device,
                          const std::vector<ShaderCreateInfo> &requests,
                          const std::unordered_map<std::string, std::string> &attributeDefinitions,
                          const CompileProgress &progress) const {
    BatchCounts counts;
    auto shaders = compileBatch(*this, device, requests, attributeDefinitions, progress, counts);
    if (!shaders.empty()) {
        radiante::out() << "[ShaderPack] prewarm: " << counts.unique << " shaders, " << counts.cacheHits
                        << " already cached" << std::endl;
    }
    return shaders;
}
