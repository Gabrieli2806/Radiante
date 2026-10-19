#include "prism/gpu/shader.hpp"

#include "prism/gpu/device.hpp"
#include "prism/util/logging.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <chrono>
#include <fstream>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_set>

namespace gpu {
namespace {

namespace fs = std::filesystem;

constexpr uint32_t kSpirvMagic = 0x07230203u;
constexpr size_t kMaxCachedShaders = 1500;

std::ostream &shaderOut() {
    return radiante::out() << "[Shader] ";
}

std::ostream &shaderErr() {
    return radiante::err() << "[Shader] ";
}

// ---- Files

// Everything left in a file opened in binary mode.
std::string readAll(std::ifstream &file) {
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    file.seekg(0, std::ios::beg);
    if (size <= 0) { return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()); }
    std::string text(static_cast<size_t>(size), '\0');
    file.read(text.data(), size);
    text.resize(static_cast<size_t>(file.gcount()));
    return text;
}

// The 32-bit words of a file opened in binary mode at its end; empty when its size is not a whole number of words.
std::vector<uint32_t> readWords(std::ifstream &file) {
    const std::streamoff size = file.tellg();
    if (size <= 0 || size % static_cast<std::streamoff>(sizeof(uint32_t)) != 0) { return {}; }
    std::vector<uint32_t> words(static_cast<size_t>(size) / sizeof(uint32_t));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char *>(words.data()), size)) { return {}; }
    return words;
}

fs::path makeCanonical(const fs::path &path) {
    std::error_code error;
    fs::path canonical = fs::weakly_canonical(path, error);
    return error ? path.lexically_normal() : canonical;
}

// ---- Hashes. Cache files carry these hashes in their names: computing them any other way orphans every cached shader.

// 64-bit FNV-1a.
class Fnv1a {
  public:
    Fnv1a() = default;
    explicit Fnv1a(uint64_t state) : state_(state) {}

    void bytes(const void *data, size_t size) {
        const auto *byte = static_cast<const unsigned char *>(data);
        for (size_t i = 0; i < size; i++) { state_ = (state_ ^ byte[i]) * 1099511628211ull; }
    }

    // Followed by a zero, so that "ab" then "c" and "a" then "bc" hash differently.
    void string(std::string_view text) {
        bytes(text.data(), text.size());
        const char terminator = '\0';
        bytes(&terminator, 1);
    }

    template <typename T>
        requires std::is_trivially_copyable_v<T>
    void value(const T &item) {
        bytes(&item, sizeof(item));
    }

    uint64_t state() const {
        return state_;
    }

  private:
    uint64_t state_ = 14695981039346656037ull;
};

std::string toHex(uint64_t value) {
    char text[16];
    const auto converted = std::to_chars(std::begin(text), std::end(text), value, 16);
    return std::string(text, converted.ptr);
}

// Bumped whenever decorateNonUniformResources (below) changes what it writes.
constexpr std::string_view kSpirvPostProcessVersion = "nonuniform-1";

// computeCompiledHash for a source whose canonical path is known.
std::string compiledHash(std::string_view canonicalSource,
                         VkShaderStageFlagBits stage,
                         const std::unordered_map<std::string, std::string> &definitions,
                         const std::vector<std::string> &includeDirectories,
                         std::string_view injectedSource,
                         uint64_t dependencyHash) {
    Fnv1a hash;
    // What compileSource does to the SPIR-V after the compiler; changed when that changes, so cached results go.
    hash.string(kSpirvPostProcessVersion);
    hash.string(canonicalSource);
    hash.value(stage);
    hash.value(dependencyHash);
    // By name, as the map's own order is not stable.
    std::vector<const std::pair<const std::string, std::string> *> sorted;
    sorted.reserve(definitions.size());
    for (const auto &definition : definitions) { sorted.push_back(&definition); }
    std::ranges::sort(sorted, {}, [](const auto *definition) -> const std::string & { return definition->first; });
    for (const auto *definition : sorted) {
        hash.string(definition->first);
        hash.string(definition->second);
    }
    for (const std::string &directory : includeDirectories) { hash.string(directory); }
    hash.string(injectedSource);
    return toHex(hash.state());
}

// ---- The includes of a source, as the dependency hash sees them

bool isSpace(char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

bool isIdentifier(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

// The code of a line without its comments, in `code`. `inBlockComment` carries a /* */ comment from line to line.
void stripComments(std::string_view line, bool &inBlockComment, std::string &code) {
    code.clear();
    size_t i = 0;
    while (i < line.size()) {
        if (inBlockComment) {
            const size_t end = line.find("*/", i);
            if (end == std::string_view::npos) { return; }
            inBlockComment = false;
            i = end + 2;
            continue;
        }
        const size_t slash = line.find('/', i);
        if (slash == std::string_view::npos || slash + 1 == line.size()) {
            code.append(line.substr(i));
            return;
        }
        if (line[slash + 1] == '*') {
            code.append(line.substr(i, slash - i));
            inBlockComment = true;
            i = slash + 2;
        } else if (line[slash + 1] == '/') {
            code.append(line.substr(i, slash - i));
            return;
        } else {
            code.append(line.substr(i, slash + 1 - i));
            i = slash + 1;
        }
    }
}

// The file of an #include "file" line. #include <file> is not followed, so it does not count towards the hash.
std::optional<std::string_view> quotedInclude(std::string_view line) {
    size_t i = 0;
    const auto skipSpace = [&] {
        while (i < line.size() && isSpace(line[i])) { i++; }
    };
    skipSpace();
    if (i == line.size() || line[i] != '#') { return std::nullopt; }
    i++;
    skipSpace();
    const size_t directive = i;
    while (i < line.size() && isIdentifier(line[i])) { i++; }
    if (line.substr(directive, i - directive) != "include") { return std::nullopt; }
    skipSpace();
    if (i == line.size() || line[i] != '"') { return std::nullopt; }
    const size_t start = i + 1;
    const size_t end = line.find('"', start);
    if (end == std::string_view::npos) { return std::nullopt; }
    return line.substr(start, end - start);
}

// One source file of a SourceIndex. Read on first use, then never changed.
struct SourceFile {
    std::string key; // the canonical path, as hashed
    fs::path path;
    std::once_flag loaded;
    bool readable = false;
    uint64_t ownHash = 0; // of the key and the file's bytes
    uint64_t size = 0;
    std::vector<SourceFile *> includes; // each #include "...", in order; null where it is not found
};

// The source files read for the dependency hashes of a set of shaders that share their include directories. Each
// file is read, hashed and searched for includes once, however many of the shaders include it. Thread-safe.
class SourceIndex {
  public:
    explicit SourceIndex(const std::vector<std::string> &includeDirectories)
        : includeDirectories_(includeDirectories) {}

    SourceFile &file(const fs::path &canonicalPath) {
        return loaded(entry(canonicalPath));
    }

    SourceFile &loaded(SourceFile &file) {
        std::call_once(file.loaded, [&] { load(file); });
        return file;
    }

  private:
    // The file at a canonical path, not read yet when it is new.
    SourceFile &entry(const fs::path &canonicalPath) {
        std::string key = canonicalPath.string();
        std::scoped_lock lock(mutex_);
        auto [it, inserted] = files_.try_emplace(std::move(key));
        if (inserted) {
            it->second = std::make_unique<SourceFile>();
            it->second->key = it->first;
            it->second->path = canonicalPath;
        }
        return *it->second;
    }

    void load(SourceFile &file) {
        std::ifstream stream(file.path, std::ios::binary);
        if (!stream) { return; }
        const std::string content = readAll(stream);
        Fnv1a hash;
        hash.string(file.key);
        hash.string(content);
        file.ownHash = hash.state();
        file.size = content.size();
        file.readable = true;

        const fs::path directory = file.path.parent_path();
        const std::string_view text(content);
        std::string code;
        bool inBlockComment = false;
        for (size_t start = 0; start < text.size();) {
            const size_t end = std::min(text.find('\n', start), text.size());
            stripComments(text.substr(start, end - start), inBlockComment, code);
            if (const auto include = quotedInclude(code)) { file.includes.push_back(resolve(directory, *include)); }
            start = end + 1;
        }
    }

    SourceFile *resolve(const fs::path &directory, std::string_view name) {
        std::error_code error;
        const fs::path beside = directory / fs::path(name);
        if (fs::is_regular_file(beside, error)) { return &entry(makeCanonical(beside)); }
        for (const std::string &includeDirectory : includeDirectories_) {
            const fs::path candidate = fs::path(includeDirectory) / fs::path(name);
            if (fs::is_regular_file(candidate, error)) { return &entry(makeCanonical(candidate)); }
        }
        return nullptr;
    }

    const std::vector<std::string> includeDirectories_;
    std::mutex mutex_;
    std::unordered_map<std::string, std::unique_ptr<SourceFile>> files_;
};

// The dependency hash of one shader: a file's hash covers its own bytes, then the hash of each file it includes, in
// order. A file met again while its own includes are still being hashed (an include cycle) counts as 0 there, which
// makes the hashes in a cycle depend on where the walk began. That is why each shader walks with its own memo and
// only what is read from the files is shared.
class DependencyWalk {
  public:
    explicit DependencyWalk(SourceIndex &index) : index_(index) {}

    std::optional<uint64_t> hash(SourceFile &file) {
        if (const auto done = done_.find(&file); done != done_.end()) { return done->second; }
        if (!open_.insert(&file).second) { return 0; }
        const std::optional<uint64_t> result = combine(index_.loaded(file));
        open_.erase(&file);
        done_.emplace(&file, result);
        return result;
    }

    // The size of every file read so far, each counted once.
    uint64_t bytes() const {
        return bytes_;
    }

  private:
    std::optional<uint64_t> combine(SourceFile &file) {
        if (!file.readable) { return std::nullopt; }
        bytes_ += file.size;
        Fnv1a combined(file.ownHash);
        for (SourceFile *include : file.includes) {
            if (include == nullptr) { return std::nullopt; }
            const std::optional<uint64_t> includeHash = hash(*include);
            if (!includeHash) { return std::nullopt; }
            combined.value(*includeHash);
        }
        return combined.state();
    }

    SourceIndex &index_;
    std::unordered_map<const SourceFile *, std::optional<uint64_t>> done_;
    std::unordered_set<const SourceFile *> open_;
    uint64_t bytes_ = 0;
};

// ---- The cache folder

// Unique among the temporary files of every game that may share the cache folder.
std::string temporaryTag() {
    static const uint64_t process =
        static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()) ^
        (static_cast<uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id())) << 1);
    static std::atomic<uint64_t> counter{0};
    return toHex(process) + "-" + toHex(counter.fetch_add(1, std::memory_order_relaxed));
}

// Writes a cache file under a temporary name and renames it into place, so that no reader (another thread, another
// game sharing the folder, the next start after a crash) can find half a file. The temporary name ends in .spv as
// well, so one a crash leaves behind is trimmed with the rest. False when the file could not be stored.
bool storeSpirv(const fs::path &path, std::span<const uint32_t> spirv) {
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    if (error) { return false; }
    fs::path temporary = path;
    temporary.replace_extension("." + temporaryTag() + ".spv");
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) { return false; }
        file.write(reinterpret_cast<const char *>(spirv.data()), static_cast<std::streamsize>(spirv.size_bytes()));
        file.close();
        if (!file) {
            fs::remove(temporary, error);
            return false;
        }
    }
    fs::rename(temporary, path, error);
    if (error) {
        fs::remove(temporary, error);
        return false;
    }
    return true;
}

// Removes the files used longest ago (a cache hit renews a file's write time) beyond kMaxCachedShaders.
void trimCacheDirectory(const fs::path &directory) {
    std::vector<std::pair<fs::file_time_type, fs::path>> files;
    std::error_code error;
    for (fs::directory_iterator it(directory, error), end; !error && it != end; it.increment(error)) {
        std::error_code entryError;
        if (it->path().extension() == ".spv" && it->is_regular_file(entryError)) {
            files.emplace_back(it->last_write_time(entryError), it->path());
        }
    }
    if (files.size() <= kMaxCachedShaders) { return; }
    const auto excess = static_cast<std::ptrdiff_t>(files.size() - kMaxCachedShaders);
    std::nth_element(files.begin(), files.begin() + excess, files.end());
    for (auto it = files.begin(); it != files.begin() + excess; ++it) { fs::remove(it->second, error); }
}

// ---- Compiling

// The source with `injected` on its own line after the #version line and the blank and #extension lines that follow
// it; at the very start when the source does not begin with #version.
std::string injectAfterVersion(std::string source, std::string_view injected) {
    if (injected.empty()) { return source; }
    const auto nextLine = [&](size_t from) {
        const size_t end = source.find('\n', from);
        return end == std::string::npos ? source.size() : end + 1;
    };
    size_t position = 0;
    if (source.starts_with("#version")) {
        position = nextLine(0);
        while (position < source.size()) {
            const size_t next = nextLine(position);
            std::string_view line = std::string_view(source).substr(position, next - position);
            if (line.ends_with('\n')) { line.remove_suffix(1); }
            const size_t content = line.find_first_not_of(" \t\r");
            if (content != std::string_view::npos && !line.substr(content).starts_with("#extension")) { break; }
            position = next;
        }
    }
    source.insert(position, std::string(injected) + "\n");
    return source;
}

// Marks every load of an element of a descriptor array (textures[], the image and sampler tables) NonUniform, and the
// access chain it loads through.
//
// The shaders index those arrays with nonuniformEXT, but almost always hand the element to a helper function
// (sampleTexture(textures[nonuniformEXT(id)], ...)). glslang then passes a pointer to the element, and the load inside
// the helper carries no NonUniform decoration: only the index does. Vulkan wants the decoration on what the image
// instruction reads, and RADV goes by it. Without it, lanes of one wave that hit different textures all sampled the
// first lane's: in an any-hit shader the alpha test read the wrong texture, kept see-through texels of entity skins,
// and the pig's, cow's and villager's sides came out black. NVIDIA's drivers do not need the decoration.
// A load of a uniform index costs RADV nothing more than a check.
void decorateNonUniformResources(std::vector<uint32_t> &spirv) {
    constexpr uint32_t kHeaderWords = 5;
    if (spirv.size() <= kHeaderWords || spirv[0] != 0x07230203u) return;
    enum : uint32_t {
        OpCapability = 17,
        OpTypeVoid = 19,
        OpTypeImage = 25,
        OpTypeSampler = 26,
        OpTypeSampledImage = 27,
        OpTypeArray = 28,
        OpTypeRuntimeArray = 29,
        OpTypePointer = 32,
        OpTypeForwardPointer = 39,
        OpVariable = 59,
        OpLoad = 61,
        OpAccessChain = 65,
        OpInBoundsAccessChain = 66,
        OpDecorate = 71,
        OpCopyObject = 83,
        DecorationNonUniform = 5300,
        CapabilityShaderNonUniform = 5301,
    };

    std::unordered_set<uint32_t> resourceTypes;  // images, samplers, sampled images
    std::unordered_set<uint32_t> resourceArrays; // arrays of them
    std::unordered_set<uint32_t> arrayPointers;  // pointers to those arrays
    std::unordered_set<uint32_t> arrayVariables; // descriptor arrays
    std::unordered_set<uint32_t> elementPointers; // access chains into them
    std::unordered_set<uint32_t> alreadyNonUniform;
    std::vector<uint32_t> toDecorate;
    bool hasCapability = false;
    size_t firstType = 0;

    for (size_t at = kHeaderWords; at < spirv.size();) {
        const uint32_t count = spirv[at] >> 16;
        const uint32_t opcode = spirv[at] & 0xFFFFu;
        if (count == 0 || at + count > spirv.size()) return; // malformed: leave it alone
        const uint32_t *w = spirv.data() + at;
        switch (opcode) {
            case OpCapability:
                if (count >= 2 && w[1] == CapabilityShaderNonUniform) hasCapability = true;
                break;
            case OpDecorate:
                if (count >= 3 && w[2] == DecorationNonUniform) alreadyNonUniform.insert(w[1]);
                break;
            case OpTypeImage:
            case OpTypeSampler:
            case OpTypeSampledImage:
                if (count >= 2) resourceTypes.insert(w[1]);
                break;
            case OpTypeArray:
            case OpTypeRuntimeArray:
                if (count >= 3 && resourceTypes.contains(w[2])) resourceArrays.insert(w[1]);
                break;
            case OpTypePointer:
                if (count >= 4 && resourceArrays.contains(w[3])) arrayPointers.insert(w[1]);
                break;
            case OpVariable:
                if (count >= 3 && arrayPointers.contains(w[1])) arrayVariables.insert(w[2]);
                break;
            case OpAccessChain:
            case OpInBoundsAccessChain:
                if (count >= 4 && (arrayVariables.contains(w[3]) || elementPointers.contains(w[3]))) {
                    elementPointers.insert(w[2]);
                    toDecorate.push_back(w[2]);
                }
                break;
            case OpCopyObject:
                if (count >= 4 && elementPointers.contains(w[3])) {
                    elementPointers.insert(w[2]);
                    toDecorate.push_back(w[2]);
                }
                break;
            case OpLoad:
                if (count >= 4 && elementPointers.contains(w[3])) toDecorate.push_back(w[2]);
                break;
            default: break;
        }
        // Decorations go before the first type declaration (the annotations section ends there).
        if (firstType == 0 && opcode >= OpTypeVoid && opcode <= OpTypeForwardPointer) {
            firstType = at;
        }
        at += count;
    }

    std::vector<uint32_t> decorations;
    std::unordered_set<uint32_t> added;
    for (uint32_t id : toDecorate) {
        if (alreadyNonUniform.contains(id) || !added.insert(id).second) continue;
        decorations.insert(decorations.end(), {(3u << 16) | OpDecorate, id, DecorationNonUniform});
    }
    if (decorations.empty() || firstType == 0) return;
    spirv.insert(spirv.begin() + static_cast<ptrdiff_t>(firstType), decorations.begin(), decorations.end());
    if (!hasCapability) {
        // Capabilities come first, right after the header.
        const uint32_t capability[] = {(2u << 16) | OpCapability, CapabilityShaderNonUniform};
        spirv.insert(spirv.begin() + kHeaderWords, std::begin(capability), std::end(capability));
    }
}

std::vector<uint32_t> compileSource(const std::string &sourcePath,
                                    VkShaderStageFlagBits stage,
                                    const std::unordered_map<std::string, std::string> &definitions,
                                    const std::vector<std::string> &includeDirectories,
                                    std::string_view injectedSource) {
    std::ifstream file(sourcePath, std::ios::binary);
    if (!file) {
        shaderErr() << "Cannot open source file: " << sourcePath << std::endl;
        throw std::runtime_error("cannot open shader source " + sourcePath);
    }
    const std::string source = injectAfterVersion(readAll(file), injectedSource);

    std::vector<fs::path> searchPath;
    searchPath.reserve(includeDirectories.size() + 1);
    searchPath.push_back(fs::path(sourcePath).parent_path());
    for (const std::string &directory : includeDirectories) { searchPath.emplace_back(directory); }

    shaderc::CompileOptions options;
    for (const auto &[name, value] : definitions) { options.AddMacroDefinition(name, value); }
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_4);
    options.SetSourceLanguage(shaderc_source_language_glsl);
    options.SetOptimizationLevel(shaderc_optimization_level_performance);
    options.SetIncluder(std::make_unique<ShaderIncluder>(std::move(searchPath)));

    const shaderc::Compiler compiler;
    const shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(
        source, shaderKindFromStage(stage), sourcePath.c_str(), options);
    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        shaderErr() << "failed to compile shader source " << sourcePath << "\n"
                    << result.GetErrorMessage() << std::endl;
        throw std::runtime_error("shader " + sourcePath + " does not compile");
    }
    std::vector<uint32_t> spirv(result.cbegin(), result.cend());
    decorateNonUniformResources(spirv);
    return spirv;
}

} // namespace

shaderc_shader_kind shaderKindFromStage(VkShaderStageFlagBits stage) {
    switch (stage) {
        case VK_SHADER_STAGE_VERTEX_BIT: return shaderc_glsl_vertex_shader;
        case VK_SHADER_STAGE_FRAGMENT_BIT: return shaderc_glsl_fragment_shader;
        case VK_SHADER_STAGE_COMPUTE_BIT: return shaderc_glsl_compute_shader;
        case VK_SHADER_STAGE_RAYGEN_BIT_KHR: return shaderc_glsl_raygen_shader;
        case VK_SHADER_STAGE_MISS_BIT_KHR: return shaderc_glsl_miss_shader;
        case VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR: return shaderc_glsl_closesthit_shader;
        case VK_SHADER_STAGE_ANY_HIT_BIT_KHR: return shaderc_glsl_anyhit_shader;
        case VK_SHADER_STAGE_INTERSECTION_BIT_KHR: return shaderc_glsl_intersection_shader;
        case VK_SHADER_STAGE_CALLABLE_BIT_KHR: return shaderc_glsl_callable_shader;
        default: break;
    }
    shaderErr() << "unsupported shader stage " << stage << std::endl;
    throw std::invalid_argument("unsupported shader stage " + std::to_string(stage));
}

// ---- ShaderSpirvCache

std::optional<uint64_t> ShaderSpirvCache::computeDependencyHash(const std::string &sourcePath,
                                                                const std::vector<std::string> &includeDirectories) {
    SourceIndex index(includeDirectories);
    return DependencyWalk(index).hash(index.file(makeCanonical(sourcePath)));
}

std::string ShaderSpirvCache::computeCompiledHash(const std::string &sourcePath,
                                                  VkShaderStageFlagBits stage,
                                                  const std::unordered_map<std::string, std::string> &definitions,
                                                  const std::vector<std::string> &includeDirectories,
                                                  const std::string &injectedSource,
                                                  uint64_t dependencyHash) {
    return compiledHash(makeCanonical(sourcePath).string(), stage, definitions, includeDirectories, injectedSource,
                        dependencyHash);
}

std::vector<uint32_t> ShaderSpirvCache::readCachedSpirv(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) { return {}; }
    std::vector<uint32_t> spirv = readWords(file);
    if (spirv.empty() || spirv.front() != kSpirvMagic) { return {}; }
    return spirv;
}

void ShaderSpirvCache::writeCachedSpirv(const std::filesystem::path &path, const std::vector<uint32_t> &spirv) {
    if (storeSpirv(path, spirv)) { trimCacheDirectory(path.parent_path()); }
}

// ---- ShaderIncluder

struct ShaderIncluder::IncludeData {
    shaderc_include_result result{};
    std::string sourceName;
    std::string content;
};

ShaderIncluder::ShaderIncluder(std::vector<std::filesystem::path> includeDirectories)
    : includeDirectories_(std::move(includeDirectories)) {}

shaderc_include_result *ShaderIncluder::GetInclude(const char *requestedSource,
                                                   shaderc_include_type type,
                                                   const char *requestingSource,
                                                   size_t) {
    // Nothing may be thrown back through shaderc's C interface.
    try {
        const fs::path requested(requestedSource);
        fs::path resolved;
        std::error_code error;
        if (type == shaderc_include_type_relative && requestingSource != nullptr && requestingSource[0] != '\0') {
            const fs::path beside = fs::path(requestingSource).parent_path() / requested;
            if (fs::is_regular_file(beside, error)) { resolved = makeCanonical(beside); }
        }
        for (size_t i = 0; resolved.empty() && i < includeDirectories_.size(); i++) {
            const fs::path candidate = includeDirectories_[i] / requested;
            if (fs::is_regular_file(candidate, error)) { resolved = makeCanonical(candidate); }
        }

        // An empty source name tells shaderc the include failed, and the content says why.
        auto data = std::make_unique<IncludeData>();
        if (resolved.empty()) {
            data->content = "Failed to resolve include: " + std::string(requestedSource);
        } else if (std::ifstream file(resolved, std::ios::binary); file) {
            data->sourceName = resolved.string();
            data->content = readAll(file);
        } else {
            data->content = "Failed to open include: " + resolved.string();
        }
        data->result = {data->sourceName.c_str(), data->sourceName.size(), data->content.c_str(),
                        data->content.size(), data.get()};
        return &data.release()->result;
    } catch (...) {
        // Never written, so it can be handed to every thread; no user data, so ReleaseInclude leaves it alone.
        static constexpr std::string_view reason = "Failed to include a file: out of memory or an unusable path";
        static shaderc_include_result failed{"", 0, reason.data(), reason.size(), nullptr};
        return &failed;
    }
}

void ShaderIncluder::ReleaseInclude(shaderc_include_result *data) {
    if (data != nullptr) { delete static_cast<IncludeData *>(data->user_data); }
}

// ---- Shader

Shader::Shader(std::shared_ptr<Device> device, std::string path) : device_(std::move(device)) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        shaderErr() << "Cannot open file: " << path << std::endl;
        throw std::runtime_error("cannot open shader " + path);
    }
    createModule(readWords(file), path);
#ifdef DEBUG
    shaderOut() << "created shader module for " << path << std::endl;
#endif
}

Shader::Shader(std::shared_ptr<Device> device,
               std::string sourcePath,
               VkShaderStageFlagBits stage,
               std::unordered_map<std::string, std::string> definitions,
               std::vector<std::string> includeDirectories,
               std::string injectedSource)
    : Shader(std::move(device),
             compileGlslToSpv(std::move(sourcePath), stage, std::move(definitions), std::move(includeDirectories),
                              std::move(injectedSource))) {}

Shader::Shader(std::shared_ptr<Device> device, CompileResult compileResult) : device_(std::move(device)) {
    createModule(compileResult.spirv, compileResult.sourcePath);
#ifdef DEBUG
    shaderOut() << "created runtime shader module for " << compileResult.sourcePath << std::endl;
#endif
}

Shader::~Shader() {
    vkDestroyShaderModule(device_->vkDevice(), module_, nullptr);
}

Shader::CompileResult Shader::compileGlslToSpv(std::string sourcePath,
                                               VkShaderStageFlagBits stage,
                                               std::unordered_map<std::string, std::string> definitions,
                                               std::vector<std::string> includeDirectories,
                                               std::string injectedSource,
                                               const std::filesystem::path &cacheDir) {
    CompileResult result;
    result.stage = stage;

    // The cache file is named after everything the compile reads; without a cache there is nothing to name.
    fs::path cacheFile;
    if (!cacheDir.empty()) {
        SourceIndex index(includeDirectories);
        SourceFile &source = index.file(makeCanonical(sourcePath));
        if (const std::optional<uint64_t> dependencyHash = DependencyWalk(index).hash(source)) {
            cacheFile = cacheDir / (compiledHash(source.key, stage, definitions, includeDirectories, injectedSource,
                                                 *dependencyHash) +
                                    ".spv");
            result.cacheFilePath = cacheFile.string();
            std::error_code error;
            if (fs::is_regular_file(cacheFile, error)) {
                std::vector<uint32_t> spirv = ShaderSpirvCache::readCachedSpirv(cacheFile);
                if (!spirv.empty()) {
                    // Used now: the trim removes what has gone longest unused, not what was written longest ago.
                    fs::last_write_time(cacheFile, fs::file_time_type::clock::now(), error);
                    result.sourcePath = std::move(sourcePath);
                    result.spirv = std::move(spirv);
                    result.cacheHit = true;
                    return result;
                }
                result.cacheReadFailed = true;
            }
        }
    }

    result.spirv = compileSource(sourcePath, stage, definitions, includeDirectories, injectedSource);
    if (!cacheFile.empty()) { ShaderSpirvCache::writeCachedSpirv(cacheFile, result.spirv); }
    result.sourcePath = std::move(sourcePath);
    return result;
}

VkShaderModule &Shader::vkShaderModule() {
    return module_;
}

void Shader::createModule(std::span<const uint32_t> spirv, const std::string &name) {
    if (spirv.empty() || spirv.front() != kSpirvMagic) {
        shaderErr() << name << " is not SPIR-V" << std::endl;
        throw std::runtime_error("shader " + name + " is not SPIR-V");
    }
    VkShaderModuleCreateInfo createInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    createInfo.codeSize = spirv.size_bytes();
    createInfo.pCode = spirv.data();
    if (vkCreateShaderModule(device_->vkDevice(), &createInfo, nullptr, &module_) != VK_SUCCESS) {
        shaderErr() << "failed to create shader module for " << name << std::endl;
        throw std::runtime_error("could not create the shader module of " + name);
    }
}

} // namespace gpu
