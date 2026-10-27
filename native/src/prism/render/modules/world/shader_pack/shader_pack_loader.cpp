#include "prism/render/modules/world/shader_pack/shader_pack_internal.hpp"

#include "prism/render/renderer.hpp"
#include "prism/util/logging.hpp"

#include "mz.h"
#include "mz_strm.h"
#include "mz_zip.h"
#include "mz_zip_rw.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

using json = nlohmann::json;

namespace {

using Loader = ShaderPackLoader;
using PackConfig = ShaderPackLoader::ShaderPack;

// ---- reading configs.json. Errors name the place in the file, as "root.passes[2].hit_groups.shadow".

[[noreturn]] void fail(const std::string &message) {
    throw std::runtime_error(message);
}

std::string at(const std::string &context, std::string_view key) {
    std::string path;
    path.reserve(context.size() + 1 + key.size());
    return path.append(context).append(1, '.').append(key);
}

std::string at(const std::string &context, size_t index) {
    return context + "[" + std::to_string(index) + "]";
}

const json *field(const json &object, std::string_view key) {
    const auto found = object.find(key);
    return found == object.end() ? nullptr : &*found;
}

const json &requireObject(const json &object, std::string_view key, const std::string &context) {
    const json *value = field(object, key);
    if (value == nullptr || !value->is_object()) fail(at(context, key) + " must be an object");
    return *value;
}

const json &requireArray(const json &object, std::string_view key, const std::string &context) {
    const json *value = field(object, key);
    if (value == nullptr || !value->is_array()) fail(at(context, key) + " must be an array");
    return *value;
}

std::string requireString(const json &object, std::string_view key, const std::string &context) {
    const json *value = field(object, key);
    if (value == nullptr || !value->is_string()) fail(at(context, key) + " must be a string");
    return value->get<std::string>();
}

std::optional<std::string> optionalString(const json &object, std::string_view key, const std::string &context) {
    const json *value = field(object, key);
    if (value == nullptr) return std::nullopt;
    if (!value->is_string()) fail(at(context, key) + " must be a string");
    return value->get<std::string>();
}

std::optional<bool> optionalFlag(const json &object, std::string_view key, const std::string &context) {
    const json *value = field(object, key);
    if (value == nullptr) return std::nullopt;
    if (!value->is_boolean()) fail(at(context, key) + " must be a boolean");
    return value->get<bool>();
}

bool optionalBool(const json &object, std::string_view key, bool fallback, const std::string &context) {
    return optionalFlag(object, key, context).value_or(fallback);
}

std::optional<uint32_t> optionalUnsigned(const json &object, std::string_view key, const std::string &context) {
    const json *value = field(object, key);
    if (value == nullptr) return std::nullopt;
    if (!value->is_number_unsigned()) fail(at(context, key) + " must be an unsigned integer");
    return value->get<uint32_t>();
}

std::vector<std::string> stringArray(const json &object, std::string_view key, const std::string &context) {
    std::vector<std::string> strings;
    const json *value = field(object, key);
    if (value == nullptr) return strings;
    if (!value->is_array()) fail(at(context, key) + " must be an array");
    strings.reserve(value->size());
    for (size_t i = 0; i < value->size(); i++) {
        const json &entry = (*value)[i];
        if (!entry.is_string()) fail(at(at(context, key), i) + " must be a string");
        strings.push_back(entry.get<std::string>());
    }
    return strings;
}

// A number, or an expression over RENDER_WIDTH and the like.
std::string expressionText(const json &value, const std::string &context) {
    if (value.is_string()) return value.get<std::string>();
    if (value.is_number()) return value.dump();
    fail(context + " must be a string or number");
}

void readExpression(const json &object, std::string_view key, const std::string &context, std::string &expression) {
    if (const json *value = field(object, key)) expression = expressionText(*value, at(context, key));
}

std::string scalarText(const json &value, const std::string &context) {
    if (value.is_string()) return value.get<std::string>();
    if (value.is_boolean()) return value.get<bool>() ? "true" : "false";
    if (value.is_number()) return value.dump();
    fail(context + " must be a string, boolean, or number");
}

std::optional<std::string> inferScalarType(const json &value, bool allowString) {
    if (value.is_boolean()) return "bool";
    if (value.is_number_integer() || value.is_number_unsigned()) return "int";
    if (value.is_number_float()) return "float";
    if (allowString && value.is_string()) return "string";
    return std::nullopt;
}

// A file named relative to the pack's root, which must exist.
fs::path packFile(const fs::path &root, const std::string &reference, const std::string &context) {
    if (reference.empty()) fail(context + " cannot be empty");
    const fs::path relative(reference);
    if (relative.is_absolute()) fail(context + " must be relative to shader pack root");
    fs::path resolved = root / relative;
    if (!fs::exists(resolved)) fail(context + " references missing file: " + reference);
    return resolved;
}

fs::path requireFile(const json &object, std::string_view key, const fs::path &root, const std::string &context) {
    return packFile(root, requireString(object, key, context), at(context, key));
}

std::optional<fs::path>
optionalFile(const json &object, std::string_view key, const fs::path &root, const std::string &context) {
    auto reference = optionalString(object, key, context);
    if (!reference.has_value()) return std::nullopt;
    return packFile(root, *reference, at(context, key));
}

// ---- value tables

VkFormat parseFormat(const std::string &value) {
    static const std::unordered_map<std::string, VkFormat> formats = {
        {"r8_unorm", VK_FORMAT_R8_UNORM},
        {"r8g8_unorm", VK_FORMAT_R8G8_UNORM},
        {"r8g8b8a8_unorm", VK_FORMAT_R8G8B8A8_UNORM},
        {"r16_sfloat", VK_FORMAT_R16_SFLOAT},
        {"r16g16_sfloat", VK_FORMAT_R16G16_SFLOAT},
        {"r16g16b16a16_sfloat", VK_FORMAT_R16G16B16A16_SFLOAT},
        {"r32_sfloat", VK_FORMAT_R32_SFLOAT},
        {"r32g32_sfloat", VK_FORMAT_R32G32_SFLOAT},
        {"r32g32_uint", VK_FORMAT_R32G32_UINT},
        {"r32g32b32a32_sfloat", VK_FORMAT_R32G32B32A32_SFLOAT},
        {"r32g32b32a32_uint", VK_FORMAT_R32G32B32A32_UINT},
    };
    const auto found = formats.find(Loader::toLower(value));
    if (found == formats.end()) fail("unsupported format: " + value);
    return found->second;
}

VkFilter parseFilter(const std::string &value) {
    const std::string lower = Loader::toLower(value);
    if (lower == "nearest") return VK_FILTER_NEAREST;
    if (lower == "linear") return VK_FILTER_LINEAR;
    fail("unsupported filter: " + value);
}

VkSamplerAddressMode parseAddressMode(const std::string &value) {
    const std::string lower = Loader::toLower(value);
    if (lower == "repeat") return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    if (lower == "clamp_to_edge") return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (lower == "mirrored_repeat") return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    fail("unsupported address mode: " + value);
}

Loader::TextureDimension parseTextureDimension(const std::string &value) {
    const std::string lower = Loader::toLower(value);
    if (lower == Loader::VALUE_2D) return Loader::TextureDimension::Texture2D;
    if (lower == Loader::VALUE_2D_ARRAY) return Loader::TextureDimension::Texture2DArray;
    if (lower == Loader::VALUE_3D) return Loader::TextureDimension::Texture3D;
    if (lower == Loader::VALUE_CUBE) return Loader::TextureDimension::Cube;
    fail("unsupported texture dimension: " + value);
}

bool isLayered(Loader::TextureDimension dimension) {
    return dimension == Loader::TextureDimension::Texture3D || dimension == Loader::TextureDimension::Texture2DArray;
}

Loader::Stage parseStage(const json &value, const std::string &context) {
    if (!value.is_string()) fail(context + " must be a string");
    const std::string stage = Loader::toLower(value.get<std::string>());
    if (stage == Loader::KEY_RAY_TRACING) return Loader::Stage::RayTracing;
    if (stage == Loader::KEY_POST_RENDER) return Loader::Stage::PostRender;
    fail("unsupported stage: " + value.get<std::string>());
}

// ---- shader pack location

uint64_t fnv1a64(std::string_view data) {
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned char ch : data) hash = (hash ^ ch) * 1099511628211ULL;
    return hash;
}

// The folder a zip is extracted to: the same for the same zip on every run.
std::string extractionFolderName(const fs::path &zipPath) {
    std::error_code error;
    fs::path normalized = fs::weakly_canonical(zipPath, error);
    if (error) normalized = zipPath.lexically_normal();
    std::ostringstream name;
    name << normalized.stem().string() << "_" << std::hex << static_cast<size_t>(fnv1a64(normalized.string()));
    return name.str();
}

struct ZipReaderDeleter {
    void operator()(void *reader) const {
        mz_zip_reader_delete(&reader);
    }
};

bool extractZip(const fs::path &zipPath, const fs::path &destination, std::string &error) {
    // Extracted afresh every time: what an earlier extraction left could be from another version of the zip.
    std::error_code fileError;
    fs::remove_all(destination, fileError);
    fs::create_directories(destination, fileError);
    if (fileError) {
        error = "failed to prepare temp dir: " + destination.string();
        return false;
    }

    std::unique_ptr<void, ZipReaderDeleter> reader(mz_zip_reader_create());
    if (reader == nullptr) {
        error = "failed to create zip reader";
        return false;
    }
    if (mz_zip_reader_open_file(reader.get(), zipPath.string().c_str()) != MZ_OK) {
        error = "failed to open zip file";
        return false;
    }
    const int32_t extracted = mz_zip_reader_save_all(reader.get(), destination.string().c_str());
    const int32_t closed = mz_zip_reader_close(reader.get());
    if (extracted != MZ_OK) {
        error = "failed to extract zip file " + zipPath.string() + " (minizip error " + std::to_string(extracted) +
                (extracted == MZ_SUPPORT_ERROR ? ", compression method not built in" : "") + ")";
        return false;
    }
    if (closed != MZ_OK) {
        error = "failed to close zip file";
        return false;
    }
    return true;
}

// The folder the pack's files are in: the pack itself, or where its zip was extracted. Empty on failure.
fs::path packRoot(const fs::path &path, std::string &error) {
    if (path.empty()) {
        error = "shader pack path is empty";
        return {};
    }
    std::error_code fileError;
    if (!fs::exists(path, fileError)) {
        error = "shader pack path does not exist: " + path.string();
        return {};
    }
    if (fs::is_directory(path, fileError)) return fs::weakly_canonical(path, fileError);
    if (Loader::toLower(path.extension().string()) != ".zip") {
        error = "shader pack must be a directory or zip: " + path.string();
        return {};
    }
    const fs::path destination = Renderer::folderPath / "temp/shaders/world/ray_tracing" / extractionFolderName(path);
    if (!extractZip(path, destination, error)) return {};
    return destination;
}

// ---- translations

std::unordered_map<std::string, std::string> readTranslations(const fs::path &path) {
    std::unordered_map<std::string, std::string> translations;
    std::ifstream stream(path);
    if (!stream.is_open()) return translations;

    std::string line;
    while (std::getline(stream, line)) {
        const std::string_view text = shader_pack::trimView(line);
        if (text.empty() || text.front() == '#') continue;
        const size_t separator = text.find('=');
        if (separator == std::string_view::npos) continue;
        std::string key = shader_pack::trimmed(text.substr(0, separator));
        if (!key.empty()) translations[std::move(key)] = shader_pack::trimmed(text.substr(separator + 1));
    }
    return translations;
}

// "zh-CN", "zh_cn.lang", "lang/zh_CN" and the like all as "zh_cn".
std::string languageCode(std::string code) {
    code = shader_pack::trimmed(Loader::toLower(std::move(code)));
    std::replace(code.begin(), code.end(), '\\', '/');
    if (const size_t slash = code.find_last_of('/'); slash != std::string::npos) code = code.substr(slash + 1);
    auto stripSuffix = [&](std::string_view suffix) {
        if (!std::string_view(code).ends_with(suffix)) return false;
        code.resize(code.size() - suffix.size());
        return true;
    };
    while (stripSuffix(".json") || stripSuffix(".lang")) {}

    std::string normalized;
    normalized.reserve(code.size());
    bool previousUnderscore = false;
    for (char ch : code) {
        const auto byte = static_cast<unsigned char>(ch);
        if (std::isalnum(byte) != 0) {
            normalized.push_back(static_cast<char>(std::tolower(byte)));
            previousUnderscore = false;
            continue;
        }
        if (ch == '@' || ch == '#') break;
        const bool separator = ch == '-' || ch == '_' || ch == '.' || std::isspace(byte) != 0;
        if (separator && !normalized.empty() && !previousUnderscore) {
            normalized.push_back('_');
            previousUnderscore = true;
        }
    }
    while (!normalized.empty() && normalized.back() == '_') normalized.pop_back();
    return normalized;
}

// en_us under everything, then the language asked for and the languages it narrows ("zh_hans_cn", "zh_hans", "zh"),
// each overriding what came before it.
std::unordered_map<std::string, std::string> loadLanguage(const fs::path &root, const std::string &language) {
    std::unordered_map<std::string, std::string> translations;
    const fs::path languageFolder = root / Loader::KEY_LANG;
    if (!fs::exists(languageFolder) || !fs::is_directory(languageFolder)) return translations;

    std::unordered_map<std::string, fs::path> files;
    for (const auto &entry : fs::directory_iterator(languageFolder)) {
        if (!entry.is_regular_file()) continue;
        if (Loader::toLower(entry.path().extension().string()) != ".lang") continue;
        std::string code = languageCode(entry.path().stem().string());
        if (!code.empty()) files.emplace(std::move(code), entry.path());
    }

    auto mergeFrom = [&](const std::string &code) {
        const std::string normalized = languageCode(code);
        if (normalized.empty()) return;
        const auto file = files.find(normalized);
        if (file == files.end()) return;
        for (auto &[key, value] : readTranslations(file->second)) translations.insert_or_assign(key, std::move(value));
    };

    mergeFrom("en_us");
    if (language.empty()) return translations;

    std::vector<std::string> candidates;
    auto addCandidate = [&](const std::string &candidate) {
        if (candidate.empty() || std::find(candidates.begin(), candidates.end(), candidate) != candidates.end()) return;
        candidates.push_back(candidate);
    };
    std::string narrowed = languageCode(language);
    addCandidate(narrowed);
    for (size_t underscore = narrowed.rfind('_'); underscore != std::string::npos; underscore = narrowed.rfind('_')) {
        narrowed.resize(underscore);
        addCandidate(narrowed);
    }
    for (const std::string &candidate : candidates) {
        if (candidate != "en_us") mergeFrom(candidate);
    }
    return translations;
}

// ---- hit shaders found by name

// world.rchit, world.rahit and world.rint make the shaders of hit group "world" unless the pack names others.
struct HitShaderFiles {
    std::optional<fs::path> anyHit;
    std::optional<fs::path> closestHit;
    std::optional<fs::path> intersection;
};
using HitShadersByGroup = std::unordered_map<std::string, HitShaderFiles>;

HitShadersByGroup findHitShaders(const fs::path &root) {
    std::vector<fs::path> files;
    for (const auto &entry : fs::recursive_directory_iterator(root)) {
        if (entry.is_regular_file()) files.push_back(entry.path());
    }
    // In path order, so the first of two same-named shaders is always the same one.
    std::sort(files.begin(), files.end(),
              [](const fs::path &lhs, const fs::path &rhs) { return lhs.generic_string() < rhs.generic_string(); });

    HitShadersByGroup groups;
    for (const auto &file : files) {
        const std::string extension = Loader::toLower(file.extension().string());
        std::optional<fs::path> HitShaderFiles::*slot = nullptr;
        if (extension == ".rahit") {
            slot = &HitShaderFiles::anyHit;
        } else if (extension == ".rchit") {
            slot = &HitShaderFiles::closestHit;
        } else if (extension == ".rint") {
            slot = &HitShaderFiles::intersection;
        } else {
            continue;
        }
        auto &shader = groups[file.stem().string()].*slot;
        if (!shader.has_value()) shader = file;
    }
    return groups;
}

// ---- attributes and variables

std::unordered_map<std::string, std::string> parseDefinitions(const json &object, const std::string &context) {
    std::unordered_map<std::string, std::string> definitions;
    const json *found = nullptr;
    int present = 0;
    for (std::string_view key : {Loader::KEY_DEFINITIONS, Loader::KEY_DEFINES, Loader::KEY_DEFINE}) {
        if (const json *value = field(object, key)) {
            found = value;
            present++;
        }
    }
    if (present > 1) fail(context + " cannot define multiple define objects");
    if (found == nullptr) return definitions;

    const std::string definitionsContext = at(context, Loader::KEY_DEFINITIONS);
    if (!found->is_object()) fail(definitionsContext + " must be an object");
    for (auto entry = found->begin(); entry != found->end(); ++entry) {
        if (!entry.value().is_string()) fail(at(definitionsContext, entry.key()) + " must be a string");
        definitions[entry.key()] = entry.value().get<std::string>();
    }
    return definitions;
}

Loader::DefineConfig parseAttributeDefine(const json &attribute, const std::string &context) {
    Loader::DefineConfig define;
    const json *node = field(attribute, Loader::KEY_DEFINE);
    if (node == nullptr) return define;
    if (node->is_string()) {
        define.direct = node->get<std::string>();
        return define;
    }

    const std::string defineContext = at(context, Loader::KEY_DEFINE);
    if (!node->is_object()) fail(defineContext + " must be a string or object");
    bool hasStrings = false;
    bool hasObjects = false;
    for (auto entry = node->begin(); entry != node->end(); ++entry) {
        hasStrings |= entry.value().is_string();
        hasObjects |= entry.value().is_object();
        if (!entry.value().is_string() && !entry.value().is_object()) {
            fail(at(defineContext, entry.key()) + " must be a string or object");
        }
    }
    if (hasStrings && hasObjects) fail(defineContext + " cannot mix string and object entries");

    if (hasObjects) {
        // Per value: the defines written when the attribute has that value.
        for (auto entry = node->begin(); entry != node->end(); ++entry) {
            auto &variant = define.variants[entry.key()];
            for (auto inner = entry.value().begin(); inner != entry.value().end(); ++inner) {
                if (!inner.value().is_string()) {
                    fail(at(at(defineContext, entry.key()), inner.key()) + " must be a string");
                }
                variant[inner.key()] = inner.value().get<std::string>();
            }
        }
        return define;
    }
    for (auto entry = node->begin(); entry != node->end(); ++entry) {
        define.expressions[entry.key()] = entry.value().get<std::string>();
    }
    return define;
}

Loader::AttributeConfig parseAttribute(const json &object, const std::string &context) {
    if (!object.is_object()) fail(context + " must be an object");

    Loader::AttributeConfig attribute;
    attribute.name = requireString(object, Loader::KEY_NAME, context);
    attribute.type = requireString(object, Loader::KEY_TYPE, context);
    attribute.defaultValue = requireString(object, Loader::KEY_DEFAULT_VALUE, context);
    attribute.define = parseAttributeDefine(object, context);
    attribute.runtime = optionalBool(object, Loader::KEY_RUNTIME, false, context);
    if (attribute.runtime) {
        // A live value has to fit a vec4 slot, and every value has to compile to the same shader: per-value define
        // variants pick different code, so they stay compiled in.
        if (shader_pack::runtimeValueKind(attribute.type) == shader_pack::RuntimeValueKind::Unsupported) {
            fail(at(context, Loader::KEY_RUNTIME) + " is not supported for type " + attribute.type);
        }
        if (!attribute.define.variants.empty()) {
            fail(at(context, Loader::KEY_RUNTIME) + " cannot be used with per-value define variants");
        }
    }
    return attribute;
}

// The runtime attributes take the WorldUBO.packParams slots in the order the pack lists them.
void assignRuntimeSlots(std::vector<Loader::AttributeConfig> &attributes) {
    uint32_t slots = 0;
    for (auto &attribute : attributes) {
        if (!attribute.runtime) continue;
        if (slots == PRISM_PACK_PARAM_COUNT) {
            fail("root.attributes has more than " + std::to_string(PRISM_PACK_PARAM_COUNT) + " runtime attributes");
        }
        attribute.runtimeSlot = slots++;
    }
}

Loader::VariableConfig parseVariable(const json &object, const std::string &context) {
    if (!object.is_object() || object.size() != 1) fail(context + " must be an object with exactly one variable");

    const auto entry = object.begin();
    Loader::VariableConfig variable;
    variable.name = entry.key();
    const std::string variableContext = at(context, variable.name);
    if (!ExpressionEvaluator::isValidIdentifier(variable.name)) fail(variableContext + " has an invalid variable name");
    if (ExpressionEvaluator::isReservedIdentifier(variable.name)) {
        fail(variableContext + " uses a reserved variable name");
    }

    const json &value = entry.value();
    if (!value.is_object()) {
        auto type = inferScalarType(value, true);
        if (!type.has_value()) fail(variableContext + " must be an object or scalar");
        variable.type = *type;
        variable.defaultValue = scalarText(value, variableContext);
        return variable;
    }

    const json *defaultValue = field(value, Loader::KEY_DEFAULT_VALUE);
    if (defaultValue == nullptr) fail(at(variableContext, Loader::KEY_DEFAULT_VALUE) + " must be present");
    if (const json *type = field(value, Loader::KEY_TYPE)) {
        if (!type->is_string()) fail(at(variableContext, Loader::KEY_TYPE) + " must be a string");
        variable.type = type->get<std::string>();
    } else {
        auto inferred = inferScalarType(*defaultValue, true);
        if (!inferred.has_value()) fail(at(variableContext, Loader::KEY_TYPE) + " could not be inferred");
        variable.type = *inferred;
    }
    variable.defaultValue = scalarText(*defaultValue, at(variableContext, Loader::KEY_DEFAULT_VALUE));
    return variable;
}

// ---- textures and buffers

Loader::TextureConfig parseTexture(const json &object, const fs::path &root, const std::string &context) {
    if (!object.is_object()) fail(context + " must be an object");

    Loader::TextureConfig texture;
    texture.name = requireString(object, Loader::KEY_NAME, context);
    const std::string type = requireString(object, Loader::KEY_TYPE, context);
    const std::string loweredType = Loader::toLower(type);
    if (loweredType == Loader::VALUE_FILE) {
        texture.imported = true;
    } else if (loweredType == Loader::VALUE_INTERMEDIATE) {
        texture.imported = false;
    } else {
        fail("unsupported texture type: " + type);
    }
    texture.dimension =
        parseTextureDimension(optionalString(object, Loader::KEY_DIMENSION, context).value_or(Loader::VALUE_2D));
    texture.filter = parseFilter(optionalString(object, Loader::KEY_FILTER, context).value_or("linear"));
    texture.addressMode =
        parseAddressMode(optionalString(object, Loader::KEY_ADDRESS_MODE, context).value_or("repeat"));
    texture.shared = optionalBool(object, Loader::KEY_SHARED, false, context);
    texture.sampledBinding = optionalUnsigned(object, Loader::KEY_SAMPLED_BINDING, context);
    texture.storageBinding = optionalUnsigned(object, Loader::KEY_STORAGE_BINDING, context);

    if (texture.imported) {
        texture.sourcePath = requireFile(object, Loader::KEY_PATH, root, context);
        // Files outside the pack taken in place of the bundled one when present: absolute paths, or relative to the
        // game directory. They are the player's own (a Bedrock install's textures, which cannot ship in the pack);
        // the bundled file stands in when none is there.
        const json *externalPaths = field(object, Loader::KEY_EXTERNAL_PATHS);
        if (externalPaths != nullptr && externalPaths->is_array()) {
            for (const json &candidate : *externalPaths) {
                if (!candidate.is_string()) continue;
                fs::path path(candidate.get<std::string>());
                if (!path.is_absolute()) path = fs::current_path() / path;
                std::error_code error;
                if (fs::is_regular_file(path, error)) {
                    texture.sourcePath = path;
                    break;
                }
            }
        }
        texture.format = parseFormat(optionalString(object, Loader::KEY_FORMAT, context).value_or("R8G8B8A8_UNORM"));
        if (!texture.sampledBinding.has_value()) fail(context + " requires sampled_binding");

        if (isLayered(texture.dimension)) {
            auto extent = [&](std::string_view key) {
                const json *value = field(object, key);
                if (value == nullptr || !value->is_number_unsigned()) {
                    fail(at(context, key) + " must be an unsigned integer for imported layered textures");
                }
                return value->get<uint32_t>();
            };
            texture.importedWidth = extent(Loader::KEY_WIDTH);
            texture.importedHeight = extent(Loader::KEY_HEIGHT);
            texture.importedDepth = extent(Loader::KEY_DEPTH);
            if (texture.importedWidth == 0 || texture.importedHeight == 0 || texture.importedDepth <= 1) {
                fail(context + " imported layered textures require positive width/height and depth > 1");
            }
        } else if (texture.dimension != Loader::TextureDimension::Texture2D) {
            fail(context + " currently only supports imported 2d, 2d_array, or 3d textures");
        }
        return texture;
    }

    texture.format = parseFormat(requireString(object, Loader::KEY_FORMAT, context));
    const json *width = field(object, Loader::KEY_WIDTH);
    const json *height = field(object, Loader::KEY_HEIGHT);
    const json *depth = field(object, Loader::KEY_DEPTH);
    const json *scale = field(object, Loader::KEY_SCALE);
    if (width != nullptr && height != nullptr) {
        texture.widthExpression = expressionText(*width, at(context, Loader::KEY_WIDTH));
        texture.heightExpression = expressionText(*height, at(context, Loader::KEY_HEIGHT));
        if (isLayered(texture.dimension)) {
            if (depth == nullptr) {
                fail(at(context, Loader::KEY_DEPTH) + " must be present for intermediate layered textures");
            }
            texture.depthExpression = expressionText(*depth, at(context, Loader::KEY_DEPTH));
        } else if (depth != nullptr) {
            fail(at(context, Loader::KEY_DEPTH) + " is only supported for intermediate 2d_array or 3d textures");
        }
    } else if (scale != nullptr) {
        if (isLayered(texture.dimension)) {
            fail(context + " intermediate 2d_array and 3d textures require explicit width/height/depth");
        }
        if (!scale->is_number()) fail(at(context, Loader::KEY_SCALE) + " must be numeric");
        texture.scale = scale->get<float>();
    } else {
        fail(context + " requires width/height or scale");
    }
    return texture;
}

Loader::BufferConfig parseBuffer(const json &object, const std::string &context) {
    if (!object.is_object()) fail(context + " must be an object");

    Loader::BufferConfig buffer;
    buffer.name = requireString(object, Loader::KEY_NAME, context);
    const std::string type = requireString(object, Loader::KEY_TYPE, context);
    if (Loader::toLower(type) != Loader::VALUE_INTERMEDIATE) fail("unsupported buffer type: " + type);
    buffer.shared = optionalBool(object, Loader::KEY_SHARED, false, context);

    const json *size = field(object, Loader::KEY_SIZE);
    if (size == nullptr) fail(at(context, Loader::KEY_SIZE) + " must be present");
    buffer.sizeExpression = expressionText(*size, at(context, Loader::KEY_SIZE));

    const json *binding = field(object, Loader::KEY_BINDING);
    if (binding == nullptr || !binding->is_number_unsigned()) {
        fail(at(context, Loader::KEY_BINDING) + " must be an unsigned integer");
    }
    buffer.binding = binding->get<uint32_t>();
    return buffer;
}

// ---- passes

Loader::ResourceList parseResources(const json &pass, std::string_view key, const std::string &context) {
    Loader::ResourceList resources;
    const json *list = field(pass, key);
    if (list == nullptr) return resources;

    const std::string listContext = at(context, key);
    if (!list->is_object()) fail(listContext + " must be an object");
    for (auto entry = list->begin(); entry != list->end(); ++entry) {
        if (entry.key() != Loader::KEY_IMAGES && entry.key() != Loader::KEY_BUFFERS) {
            fail(listContext + " only supports " + Loader::KEY_IMAGES + " and " + Loader::KEY_BUFFERS);
        }
    }
    resources.images = stringArray(*list, Loader::KEY_IMAGES, listContext);
    resources.buffers = stringArray(*list, Loader::KEY_BUFFERS, listContext);
    return resources;
}

Loader::RayTracingPassConfig parseRayTracingPass(const json &pass,
                                                 const fs::path &root,
                                                 const HitShadersByGroup &found,
                                                 const std::string &context) {
    Loader::RayTracingPassConfig config;
    config.name = requireString(pass, Loader::KEY_NAME, context);
    config.querySharc = optionalBool(pass, Loader::KEY_QUERY_SHARC, false, context);
    config.inputs = parseResources(pass, Loader::KEY_INPUTS, context);
    config.outputs = parseResources(pass, Loader::KEY_OUTPUTS, context);
    readExpression(pass, Loader::KEY_WIDTH, context, config.width);
    readExpression(pass, Loader::KEY_HEIGHT, context, config.height);
    readExpression(pass, Loader::KEY_DEPTH, context, config.depth);
    config.definitions = parseDefinitions(pass, context);
    config.rayGenShaderPath = requireFile(pass, Loader::KEY_RGEN, root, context);
    config.defaultHitGroupName = requireString(pass, Loader::KEY_DEFAULT_HIT_GROUP, context);

    const json &groups = requireObject(pass, Loader::KEY_HIT_GROUPS, context);
    const std::string groupsContext = at(context, Loader::KEY_HIT_GROUPS);
    if (groups.empty()) fail(groupsContext + " must not be empty");
    if (!groups.contains(config.defaultHitGroupName)) {
        fail(at(context, Loader::KEY_DEFAULT_HIT_GROUP) + " is missing in hit_groups");
    }

    // The default group first (the fallback for every other), then the rest by name.
    std::vector<std::string> names{config.defaultHitGroupName};
    for (auto entry = groups.begin(); entry != groups.end(); ++entry) {
        if (entry.key() != config.defaultHitGroupName) names.push_back(entry.key());
    }
    std::sort(names.begin() + 1, names.end());

    auto parseGroup = [&](const std::string &name, const std::optional<fs::path> &fallbackClosestHit) {
        const std::string groupContext = at(groupsContext, name);
        const json &group = groups.at(name);
        if (!group.is_object()) fail(groupContext + " must be an object");
        const json &shaders = requireObject(group, Loader::KEY_SHADERS, groupContext);
        const std::string shadersContext = at(groupContext, Loader::KEY_SHADERS);
        const auto byName = found.find(name);
        const HitShaderFiles *named = byName == found.end() ? nullptr : &byName->second;

        // A shader the pack names, else the one found by the group's name; a group without its own closest hit or
        // intersection shares the default group's closest hit.
        Loader::HitGroupConfig hitGroup;
        hitGroup.name = name;
        hitGroup.closestHit = optionalFile(shaders, Loader::KEY_RCHIT, root, shadersContext);
        if (!hitGroup.closestHit.has_value() && named != nullptr) hitGroup.closestHit = named->closestHit;
        hitGroup.anyHit = optionalFile(shaders, Loader::KEY_RAHIT, root, shadersContext);
        if (!hitGroup.anyHit.has_value() && named != nullptr) hitGroup.anyHit = named->anyHit;
        hitGroup.intersection = optionalFile(shaders, Loader::KEY_RINT, root, shadersContext);
        if (!hitGroup.intersection.has_value() && named != nullptr) hitGroup.intersection = named->intersection;
        if (!hitGroup.closestHit.has_value() && !hitGroup.intersection.has_value()) {
            hitGroup.closestHit = fallbackClosestHit;
        }

        const std::string type = requireString(group, Loader::KEY_TYPE, groupContext);
        if (type != Loader::VALUE_TRIANGLE && type != Loader::VALUE_AABB) fail("unsupported hit group type: " + type);
        if (hitGroup.intersection.has_value()) {
            hitGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
        } else if (type == Loader::VALUE_AABB) {
            fail("aabb hit group requires rint: " + name);
        } else {
            hitGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        }
        if (!hitGroup.closestHit.has_value() && !hitGroup.intersection.has_value()) {
            fail("hit group must provide rchit or rint: " + name);
        }
        return hitGroup;
    };

    config.hitGroups.push_back(parseGroup(names[0], std::nullopt));
    const std::optional<fs::path> defaultClosestHit = config.hitGroups[0].closestHit;
    for (size_t i = 1; i < names.size(); i++) config.hitGroups.push_back(parseGroup(names[i], defaultClosestHit));

    const json &misses = requireArray(pass, Loader::KEY_MISS, context);
    const std::string missesContext = at(context, Loader::KEY_MISS);
    if (misses.empty()) fail(missesContext + " must not be empty");
    for (size_t i = 0; i < misses.size(); i++) {
        const std::string missContext = at(missesContext, i);
        const json &miss = misses[i];
        if (!miss.is_object()) fail(missContext + " must be an object");
        Loader::MissShaderConfig shader;
        shader.name = requireString(miss, Loader::KEY_NAME, missContext);
        shader.shaderPath = requireFile(miss, Loader::KEY_SHADER, root, missContext);
        config.missShaders.push_back(std::move(shader));
    }
    return config;
}

Loader::FullScreenPassConfig parseFullScreenPass(const json &pass, const fs::path &root, const std::string &context) {
    Loader::FullScreenPassConfig config;
    config.name = requireString(pass, Loader::KEY_NAME, context);
    config.fragmentShaderPath = requireFile(pass, Loader::KEY_FRAGMENT, root, context);
    config.target = requireString(pass, Loader::KEY_TARGET, context);
    config.inputs = parseResources(pass, Loader::KEY_INPUTS, context);
    config.outputs = parseResources(pass, Loader::KEY_OUTPUTS, context);
    auto &outputImages = config.outputs.images;
    if (std::find(outputImages.begin(), outputImages.end(), config.target) == outputImages.end()) {
        outputImages.push_back(config.target);
    }

    if (const json *face = field(pass, Loader::KEY_FACE)) {
        if (!face->is_number_integer()) fail(at(context, Loader::KEY_FACE) + " must be an integer");
        const int value = face->get<int>();
        if (value < 0 || value > 5) fail(at(context, Loader::KEY_FACE) + " must be between 0 and 5");
        config.face = static_cast<uint32_t>(value);
    }
    if (const json *layer = field(pass, Loader::KEY_LAYER)) {
        if (!layer->is_number_unsigned()) fail(at(context, Loader::KEY_LAYER) + " must be an unsigned integer");
        config.layer = layer->get<uint32_t>();
    }
    if (config.face.has_value() && config.layer.has_value()) fail(context + " cannot specify both face and layer");
    config.definitions = parseDefinitions(pass, context);
    return config;
}

Loader::RenderShaderConfig parseRenderShader(const json &shader, const fs::path &root, const std::string &context) {
    if (!shader.is_object()) fail(context + " must be an object");
    Loader::RenderShaderConfig config;
    config.vertexShaderPath = requireFile(shader, Loader::KEY_VERTEX, root, context);
    config.fragmentShaderPath = requireFile(shader, Loader::KEY_FRAGMENT, root, context);
    return config;
}

Loader::RenderPassConfig parseRenderPass(const json &pass, const fs::path &root, const std::string &context) {
    Loader::RenderPassConfig config;
    config.name = requireString(pass, Loader::KEY_NAME, context);
    if (const json *shaders = field(pass, Loader::KEY_SHADER)) {
        // One vertex and fragment shader per kind of content drawn, "default" for the rest.
        const std::string shadersContext = at(context, Loader::KEY_SHADER);
        if (!shaders->is_object() || shaders->empty()) fail(shadersContext + " must be a non-empty object");
        for (auto entry = shaders->begin(); entry != shaders->end(); ++entry) {
            config.shaderConfigs.emplace(entry.key(),
                                         parseRenderShader(entry.value(), root, at(shadersContext, entry.key())));
        }
    } else {
        config.vertexShaderPath = requireFile(pass, Loader::KEY_VERTEX, root, context);
        config.fragmentShaderPath = requireFile(pass, Loader::KEY_FRAGMENT, root, context);
        config.shaderConfigs.emplace("default", Loader::RenderShaderConfig{
                                                    .vertexShaderPath = config.vertexShaderPath,
                                                    .fragmentShaderPath = config.fragmentShaderPath,
                                                });
    }
    config.content = requireString(pass, Loader::KEY_CONTENT, context);
    config.inputs = parseResources(pass, Loader::KEY_INPUTS, context);
    config.outputs = parseResources(pass, Loader::KEY_OUTPUTS, context);
    if (config.outputs.images.empty()) config.outputs.images.push_back("out:ldr");
    config.depthTest = optionalFlag(pass, Loader::KEY_DEPTH_TEST, context);
    config.depthWrite = optionalFlag(pass, Loader::KEY_DEPTH_WRITE, context);
    config.depthCompare = optionalString(pass, Loader::KEY_DEPTH_COMPARE, context);
    config.colorBlend = optionalBool(pass, Loader::KEY_COLOR_BLEND, true, context);
    config.definitions = parseDefinitions(pass, context);
    return config;
}

Loader::ComputePassConfig parseComputePass(const json &pass, const fs::path &root, const std::string &context) {
    Loader::ComputePassConfig config;
    config.name = requireString(pass, Loader::KEY_NAME, context);
    config.computeShaderPath = requireFile(pass, Loader::KEY_COMPUTE, root, context);
    config.inputs = parseResources(pass, Loader::KEY_INPUTS, context);
    config.outputs = parseResources(pass, Loader::KEY_OUTPUTS, context);
    readExpression(pass, Loader::KEY_GX, context, config.gx);
    readExpression(pass, Loader::KEY_GY, context, config.gy);
    readExpression(pass, Loader::KEY_GZ, context, config.gz);
    config.definitions = parseDefinitions(pass, context);
    return config;
}

Loader::PassConfig
parsePass(const json &pass, const fs::path &root, const HitShadersByGroup &hitShaders, const std::string &context) {
    if (!pass.is_object()) fail(context + " must be an object");

    using Type = Loader::PassConfig::Type;
    const std::string type = requireString(pass, Loader::KEY_TYPE, context);
    Loader::PassConfig config;
    if (type == Loader::VALUE_FULL_SCREEN) {
        config.type = Type::FullScreen;
    } else if (type == Loader::VALUE_RENDER) {
        config.type = Type::Render;
    } else if (type == Loader::VALUE_RAY_TRACING) {
        config.type = Type::RayTracing;
    } else if (type == Loader::VALUE_COMPUTE) {
        config.type = Type::Compute;
    } else {
        fail("unsupported pass type: " + type);
    }

    if (const json *stage = field(pass, Loader::KEY_STAGE)) {
        config.stage = parseStage(*stage, at(context, Loader::KEY_STAGE));
    } else {
        config.stage = Loader::Stage::RayTracing;
    }
    if (config.type == Type::RayTracing && config.stage != Loader::Stage::RayTracing) {
        fail("ray_tracing pass must use ray_tracing stage: " + context);
    }
    if (config.type == Type::Render && config.stage != Loader::Stage::PostRender) {
        fail("render pass must use post_render stage: " + context);
    }

    switch (config.type) {
        case Type::FullScreen: config.fullScreen = parseFullScreenPass(pass, root, context); break;
        case Type::Render: config.render = parseRenderPass(pass, root, context); break;
        case Type::RayTracing: config.rayTracing = parseRayTracingPass(pass, root, hitShaders, context); break;
        case Type::Compute: config.compute = parseComputePass(pass, root, context); break;
    }
    return config;
}

const std::string &passName(const Loader::PassConfig &pass) {
    switch (pass.type) {
        case Loader::PassConfig::Type::FullScreen: return pass.fullScreen.name;
        case Loader::PassConfig::Type::Render: return pass.render.name;
        case Loader::PassConfig::Type::RayTracing: return pass.rayTracing.name;
        case Loader::PassConfig::Type::Compute: break;
    }
    return pass.compute.name;
}

// ---- execution scripts

std::vector<Loader::ExecutionCommand> parseCommands(const json &commands, const std::string &context);

Loader::ExecutionCommand parseCommand(const json &object, const std::string &context) {
    if (!object.is_object() || object.size() != 1) fail(context + " must be an object with exactly one command");

    using Type = Loader::ExecutionCommand::Type;
    Loader::ExecutionCommand command;
    if (const json *assign = field(object, Loader::KEY_ASSIGN)) {
        const std::string assignContext = at(context, Loader::KEY_ASSIGN);
        if (!assign->is_object() || assign->empty()) fail(assignContext + " must be a non-empty object");
        command.type = Type::Assign;
        for (auto entry = assign->begin(); entry != assign->end(); ++entry) {
            const json &value = entry.value();
            Loader::ExecutionCommand::Assignment assignment;
            if (value.is_object()) {
                const std::string entryContext = at(assignContext, entry.key());
                if (const json *type = field(value, Loader::KEY_TYPE)) {
                    if (!type->is_string()) fail(at(entryContext, Loader::KEY_TYPE) + " must be a string");
                    assignment.type = type->get<std::string>();
                    assignment.explicitType = true;
                }
                const json *assigned = field(value, Loader::KEY_VALUE);
                if (assigned == nullptr) fail(at(entryContext, Loader::KEY_VALUE) + " must be present");
                for (auto part = value.begin(); part != value.end(); ++part) {
                    if (part.key() != Loader::KEY_TYPE && part.key() != Loader::KEY_VALUE) {
                        fail(entryContext + " only supports " + Loader::KEY_TYPE + " and " + Loader::KEY_VALUE);
                    }
                }
                assignment.value = *assigned;
            } else {
                assignment.type = inferScalarType(value, false);
                assignment.value = value;
            }
            command.assignments[entry.key()] = std::move(assignment);
        }
        return command;
    }

    if (const json *pass = field(object, Loader::KEY_PASS)) {
        if (!pass->is_string()) fail(at(context, Loader::KEY_PASS) + " must be a string");
        command.type = Type::Pass;
        command.passName = pass->get<std::string>();
        return command;
    }

    const bool isIfElse = field(object, Loader::KEY_IF_ELSE) != nullptr;
    const std::string_view key = isIfElse ? Loader::KEY_IF_ELSE : Loader::KEY_WHILE;
    const json *block = field(object, key);
    if (block == nullptr) fail(context + " must contain assign, pass, if_else, or while");

    const std::string blockContext = at(context, key);
    if (!block->is_object()) fail(blockContext + " must be an object");
    command.type = isIfElse ? Type::IfElse : Type::While;
    const json *condition = field(*block, Loader::KEY_CONDITION);
    if (condition == nullptr) fail(at(blockContext, Loader::KEY_CONDITION) + " must be present");
    command.condition = *condition;
    command.thenCommands =
        parseCommands(requireArray(*block, Loader::KEY_THEN, blockContext), at(blockContext, Loader::KEY_THEN));
    if (isIfElse && field(*block, Loader::KEY_ELSE) != nullptr) {
        command.elseCommands =
            parseCommands(requireArray(*block, Loader::KEY_ELSE, blockContext), at(blockContext, Loader::KEY_ELSE));
    }
    return command;
}

std::vector<Loader::ExecutionCommand> parseCommands(const json &commands, const std::string &context) {
    if (!commands.is_array()) fail(context + " must be an array");
    std::vector<Loader::ExecutionCommand> parsed;
    parsed.reserve(commands.size());
    for (size_t i = 0; i < commands.size(); i++) parsed.push_back(parseCommand(commands[i], at(context, i)));
    return parsed;
}

std::optional<std::string> assignmentType(const Loader::ExecutionCommand::Assignment &assignment) {
    if (assignment.type.has_value()) return assignment.type;
    return inferScalarType(assignment.value, false);
}

std::string defaultValueForType(const std::string &type) {
    if (type == "bool") return "false";
    if (shader_pack::isIntType(type) || shader_pack::isEnumType(type)) return "0";
    return "0.0";
}

// Every variable the script assigns, after the globals, in the order the script first assigns it. A variable first
// assigned an expression gets its type from a later typed assignment, or stays a float.
struct VariableCollector {
    std::vector<Loader::VariableConfig> &variables;
    std::unordered_map<std::string, size_t> indices;
    std::unordered_set<std::string> untyped;

    void collect(const std::vector<Loader::ExecutionCommand> &commands) {
        for (const auto &command : commands) {
            switch (command.type) {
                case Loader::ExecutionCommand::Type::Assign:
                    for (const auto &[name, assignment] : command.assignments) add(name, assignmentType(assignment));
                    break;
                case Loader::ExecutionCommand::Type::IfElse:
                    collect(command.thenCommands);
                    collect(command.elseCommands);
                    break;
                case Loader::ExecutionCommand::Type::While: collect(command.thenCommands); break;
                case Loader::ExecutionCommand::Type::Pass: break;
            }
        }
    }

    void add(const std::string &name, const std::optional<std::string> &type) {
        const auto found = indices.find(name);
        if (found == indices.end()) {
            const std::string variableType = type.value_or("float");
            indices.emplace(name, variables.size());
            variables.push_back(
                {.name = name, .type = variableType, .defaultValue = defaultValueForType(variableType)});
            if (!type.has_value()) untyped.insert(name);
            return;
        }
        if (!type.has_value()) return;

        Loader::VariableConfig &variable = variables[found->second];
        if (untyped.erase(name) > 0) {
            variable.type = *type;
            variable.defaultValue = defaultValueForType(*type);
            return;
        }
        if (variable.type != *type) {
            fail("execution variable type mismatch for " + name + ": " + variable.type + " and " + *type);
        }
    }
};

Loader::ExecutionConfig parseExecution(const json &object, const std::string &context) {
    Loader::ExecutionConfig execution;
    if (!object.is_object()) fail(context + " must be an object");

    if (const json *globals = field(object, Loader::KEY_GLOBAL_VARIABLES)) {
        const std::string globalsContext = at(context, Loader::KEY_GLOBAL_VARIABLES);
        if (!globals->is_array()) fail(globalsContext + " must be an array");
        execution.globalVariables.reserve(globals->size());
        for (size_t i = 0; i < globals->size(); i++) {
            execution.globalVariables.push_back(parseVariable((*globals)[i], at(globalsContext, i)));
        }
    }
    if (const json *commands = field(object, Loader::KEY_COMMANDS)) {
        execution.commands = parseCommands(*commands, at(context, Loader::KEY_COMMANDS));
    }

    std::unordered_set<std::string> globalNames;
    for (const auto &variable : execution.globalVariables) {
        if (!globalNames.insert(variable.name).second) {
            fail("duplicate execution variable " + variable.name + " in " + Loader::KEY_GLOBAL_VARIABLES + " and " +
                 Loader::KEY_GLOBAL_VARIABLES);
        }
    }

    execution.variables = execution.globalVariables;
    VariableCollector collector{execution.variables};
    for (size_t i = 0; i < execution.variables.size(); i++) collector.indices[execution.variables[i].name] = i;
    collector.collect(execution.commands);
    return execution;
}

// ---- configs.json

template <typename Config, typename Parse>
std::vector<Config> parseList(const json &config, std::string_view key, Parse &&parse) {
    std::vector<Config> list;
    const json *entries = field(config, key);
    if (entries == nullptr) return list;
    const std::string context = at("root", key);
    if (!entries->is_array()) fail(context + " must be an array");
    list.resize(entries->size());
    shader_pack::parallelEach(entries->size(), [&](size_t i) { list[i] = parse((*entries)[i], at(context, i)); });
    return list;
}

PackConfig parseConfigFile(const fs::path &root, const std::string &language) {
    const fs::path configPath = root / "configs.json";
    if (!fs::exists(configPath)) fail("missing shader config: " + configPath.string());
    std::ifstream stream(configPath);
    if (!stream.is_open()) fail("failed to open shader config: " + configPath.string());
    json config;
    stream >> config;
    if (!config.is_object()) fail("shader config root must be an object");

    PackConfig pack;
    pack.rootPath = root;
    pack.requiresEmission = optionalBool(config, Loader::KEY_REQUIRES_EMISSION, false, "root");
    pack.includeDirectories = {root.string()};
    pack.translations = loadLanguage(root, language);
    const HitShadersByGroup hitShaders = findHitShaders(root);

    pack.attributes = parseList<Loader::AttributeConfig>(config, Loader::KEY_ATTRIBUTES, parseAttribute);
    assignRuntimeSlots(pack.attributes);
    pack.textures = parseList<Loader::TextureConfig>(
        config, Loader::KEY_TEXTURES,
        [&](const json &texture, const std::string &context) { return parseTexture(texture, root, context); });
    pack.buffers = parseList<Loader::BufferConfig>(config, Loader::KEY_BUFFERS, parseBuffer);

    const json *passes = field(config, Loader::KEY_PASSES);
    if (passes == nullptr || !passes->is_array() || passes->empty()) fail("root.passes must be a non-empty array");
    pack.passes = parseList<Loader::PassConfig>(config, Loader::KEY_PASSES,
                                                [&](const json &pass, const std::string &context) {
                                                    return parsePass(pass, root, hitShaders, context);
                                                });

    if (const json *sharc = field(config, Loader::KEY_SHARC)) {
        const std::string context = at("root", Loader::KEY_SHARC);
        if (!sharc->is_object()) fail(context + " must be an object");
        Loader::SharcConfig settings;
        settings.updatePassName = requireString(*sharc, Loader::KEY_UPDATE_PASS, context);
        settings.resolveCompShaderPath = requireFile(*sharc, Loader::KEY_RESOLVE_COMP, root, context);
        const bool updatesRayTracingPass = std::any_of(pack.passes.begin(), pack.passes.end(), [&](const auto &pass) {
            return pass.type == Loader::PassConfig::Type::RayTracing && pass.rayTracing.name == settings.updatePassName;
        });
        if (!updatesRayTracingPass) fail(at(context, Loader::KEY_UPDATE_PASS) + " must reference a ray_tracing pass");
        pack.sharc = std::move(settings);
    }

    if (const json *execution = field(config, Loader::KEY_EXECUTION)) {
        if (!execution->is_object()) fail(at("root", Loader::KEY_EXECUTION) + " must be an object");
        // The ray tracing script, either under "ray_tracing" or as the whole object.
        if (const json *rayTracing = field(*execution, Loader::KEY_RAY_TRACING)) {
            pack.rayTracingExecution = parseExecution(*rayTracing, "root.execution.ray_tracing");
        } else {
            pack.rayTracingExecution = parseExecution(*execution, "root.execution");
        }
    }
    if (const json *execution = field(config, Loader::KEY_EXECUTION_POST)) {
        if (!execution->is_object()) fail(at("root", Loader::KEY_EXECUTION_POST) + " must be an object");
        pack.postRenderExecution = parseExecution(*execution, "root.execution_post");
    }

    // Without a script a stage runs its passes in the order the pack lists them.
    auto addDefaultCommands = [&](Loader::ExecutionConfig &execution, Loader::Stage stage) {
        if (!execution.commands.empty()) return;
        for (const auto &pass : pack.passes) {
            if (pass.stage != stage) continue;
            Loader::ExecutionCommand command;
            command.type = Loader::ExecutionCommand::Type::Pass;
            command.passName = passName(pass);
            execution.commands.push_back(std::move(command));
        }
    };
    addDefaultCommands(pack.rayTracingExecution, Loader::Stage::RayTracing);
    addDefaultCommands(pack.postRenderExecution, Loader::Stage::PostRender);

    // The textures and buffers share one descriptor set.
    std::unordered_map<uint32_t, std::string> bindingOwners;
    auto claimBinding = [&](uint32_t binding, const std::string &owner) {
        const auto [existing, inserted] = bindingOwners.emplace(binding, owner);
        if (!inserted) {
            fail("duplicate runtime binding " + std::to_string(binding) + ": " + existing->second + " and " + owner);
        }
    };
    for (const auto &texture : pack.textures) {
        if (texture.sampledBinding.has_value()) {
            claimBinding(*texture.sampledBinding, "texture(sampled):" + texture.name);
        }
        if (texture.storageBinding.has_value()) {
            claimBinding(*texture.storageBinding, "texture(storage):" + texture.name);
        }
    }
    for (const auto &buffer : pack.buffers) claimBinding(buffer.binding, "buffer:" + buffer.name);

    return pack;
}

} // namespace

std::string ShaderPackLoader::toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

ShaderPackLoader::LoadResult
ShaderPackLoader::load(const fs::path &path, const fs::path &builtInPath, const std::string &language) {
    LoadResult result;
    auto tryLoad = [&](const fs::path &packPath) {
        std::string error;
        const fs::path root = packRoot(packPath, error);
        if (root.empty()) {
            result.error = error;
            return false;
        }
        try {
            result.shaderPack = parseConfigFile(root, language);
        } catch (const std::exception &exception) {
            result.error = exception.what();
            return false;
        }
        result.success = true;
        return true;
    };
    auto underRendererFolder = [](const fs::path &packPath) {
        return packPath.empty() || packPath.is_absolute() ? packPath : Renderer::folderPath / packPath;
    };

    const fs::path fallbackPath = underRendererFolder(builtInPath);
    const fs::path requestedPath = underRendererFolder(path.empty() ? builtInPath : path);
    if (tryLoad(requestedPath)) return result;
    if (!path.empty() && requestedPath != fallbackPath) {
        // Said where Java reads it (RendererProxy.drainNativeErrors): without it a pack whose configs.json is wrong
        // ran as the built-in one without a word, and the shader pack screen could not say why.
        radiante::err() << "[ShaderPack] " << requestedPath.string() << " could not be loaded: " << result.error
                        << "; using the built-in pack" << std::endl;
        tryLoad(fallbackPath);
    }
    return result;
}

std::string ShaderPackLoader::buildExecutionSource(const std::vector<VariableConfig> &variables,
                                                   uint32_t executionSet) {
    if (variables.empty()) return "";

    // One float per variable; each name becomes a define reading it back as the type the script gives it.
    std::string source = "layout(set = " + std::to_string(executionSet) +
                         ", binding = " + std::to_string(EXECUTION_BINDING) +
                         ", std430) readonly buffer RtExecutionVariables {\n";
    for (const auto &variable : variables) source += "    float " + variable.name + "_;\n";
    source += "} rtExecutionVariables;\n";
    for (const auto &variable : variables) {
        const std::string stored = "rtExecutionVariables." + variable.name + "_";
        std::string read = stored;
        if (variable.type == "bool") {
            read = "(" + stored + " != 0.0)";
        } else if (shader_pack::isIntType(variable.type) || shader_pack::isEnumType(variable.type)) {
            read = "int(" + stored + ")";
        }
        source += "#define " + variable.name + " " + read + "\n";
    }
    return source;
}
