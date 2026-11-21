#include "prism/render/modules/world/shader_pack/shader_pack_loader_internal.hpp"
#include "prism/render/modules/world/shader_pack/shader_pack_extends.hpp"
#include "prism/render/modules/world/shader_pack/shader_pack_features.hpp"

#include "prism/render/renderer.hpp"
#include "prism/util/logging.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace shader_pack::loader {

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

Loader::Stage parseStage(const json &value, const std::string &context) {
    if (!value.is_string()) fail(context + " must be a string");
    const std::string stage = Loader::toLower(value.get<std::string>());
    if (stage == Loader::KEY_RAY_TRACING) return Loader::Stage::RayTracing;
    if (stage == Loader::KEY_POST_RENDER) return Loader::Stage::PostRender;
    fail("unsupported stage: " + value.get<std::string>());
}

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

namespace {

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
        {"r32_uint", VK_FORMAT_R32_UINT},
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

// Where zips are extracted and packs that extend another are staged; a check's folder below it is its own.
fs::path tempRoot(const std::string &folder) {
    const fs::path root = Renderer::folderPath / "temp/shaders/world/ray_tracing";
    return folder.empty() ? root : root / folder;
}

// The folder the pack's files are in: the pack itself, or where its zip was extracted. Empty on failure.
fs::path packRoot(const fs::path &path, std::string &error, const std::string &tempFolder = {}) {
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
    const fs::path destination = tempRoot(tempFolder) / extractionFolderName(path);
    if (!shader_pack::extractPackZip(path, destination, error)) return {};
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

// ---- attributes, textures and buffers

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
        if (shader_pack::runtimeValueKind(attribute.type) == shader_pack::RuntimeValueKind::Unsupported) {
            fail(at(context, Loader::KEY_RUNTIME) + " is not supported for type " + attribute.type);
        }
        if (!attribute.define.variants.empty()) {
            fail(at(context, Loader::KEY_RUNTIME) + " cannot be used with per-value define variants");
        }
    }
    return attribute;
}

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
    pack.includeDirectories = {(Renderer::folderPath / "shaders/include").string(), root.string()};
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

    if (const json *views = field(config, Loader::KEY_DEBUG_VIEWS)) {
        const std::string context = at("root", Loader::KEY_DEBUG_VIEWS);
        if (!views->is_array()) fail(context + " must be an array");
        std::unordered_set<std::string> names;
        for (size_t i = 0; i < views->size(); i++) {
            const json &entry = (*views)[i];
            const std::string entryContext = at(context, i);
            if (!entry.is_object()) fail(entryContext + " must be an object");
            Loader::DebugViewConfig view;
            view.name = requireString(entry, Loader::KEY_NAME, entryContext);
            view.define = requireString(entry, Loader::KEY_DEFINE, entryContext);
            if (const json *value = field(entry, Loader::KEY_VALUE)) {
                view.value = scalarText(*value, at(entryContext, Loader::KEY_VALUE));
            }
            if (view.name.empty() || view.name == "off" || view.name.find_first_of(" \t") != std::string::npos) {
                fail(at(entryContext, Loader::KEY_NAME) + " must be one word other than off");
            }
            if (!ExpressionEvaluator::isValidIdentifier(view.define)) {
                fail(at(entryContext, Loader::KEY_DEFINE) + " must be a define name");
            }
            if (!names.insert(view.name).second) fail(entryContext + ": " + view.name + " is listed twice");
            pack.debugViews.push_back(std::move(view));
        }
    }

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

    auto checkPassNames = [&](const Loader::ExecutionConfig &execution, Loader::Stage stage, const std::string &where) {
        std::unordered_set<std::string> known;
        for (const auto &pass : pack.passes) {
            if (pass.stage == stage) known.insert(passName(pass));
        }
        std::function<void(const std::vector<Loader::ExecutionCommand> &)> walk =
            [&](const std::vector<Loader::ExecutionCommand> &commands) {
                for (const auto &command : commands) {
                    if (command.type == Loader::ExecutionCommand::Type::Pass && !known.contains(command.passName)) {
                        std::string names;
                        for (const auto &name : known) names += (names.empty() ? "" : ", ") + name;
                        fail(where + " runs a pass named " + command.passName + ", which this stage does not have (" +
                             (names.empty() ? std::string("it has none") : "it has: " + names) + ")");
                    }
                    walk(command.thenCommands);
                    walk(command.elseCommands);
                }
            };
        walk(execution.commands);
    };
    checkPassNames(pack.rayTracingExecution, Loader::Stage::RayTracing, "root.execution");
    checkPassNames(pack.postRenderExecution, Loader::Stage::PostRender, "root.execution_post");

    std::unordered_map<uint32_t, std::string> bindingOwners;
    auto claimBinding = [&](uint32_t binding, const std::string &owner) {
        const auto [existing, inserted] = bindingOwners.emplace(binding, owner);
        if (!inserted) {
            fail("duplicate runtime binding " + std::to_string(binding) + ": " + existing->second + " and " + owner +
                 " (a pack that extends Radiante PT puts its own textures and buffers on 64 to 119)");
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
    uint32_t nextFree = Loader::PACK_BINDING_FIRST;
    for (auto &texture : pack.textures) {
        if (texture.sampledBinding.has_value() || texture.storageBinding.has_value()) continue;
        while (nextFree <= Loader::PACK_BINDING_LAST && bindingOwners.contains(nextFree)) nextFree++;
        if (nextFree > Loader::PACK_BINDING_LAST) {
            fail("texture " + texture.name + " has no binding left: a pack's own go on " +
                 std::to_string(Loader::PACK_BINDING_FIRST) + " to " + std::to_string(Loader::PACK_BINDING_LAST));
        }
        texture.sampledBinding = nextFree;
        claimBinding(nextFree, "texture(sampled):" + texture.name);
    }

    return pack;
}

} // namespace

} // namespace shader_pack::loader

using namespace shader_pack::loader;

std::string ShaderPackLoader::toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

ShaderPackLoader::LoadResult
ShaderPackLoader::load(const fs::path &path, const fs::path &builtInPath, const std::string &language) {
    return load(path, builtInPath, language, LoadOptions{});
}

ShaderPackLoader::LoadResult ShaderPackLoader::load(const fs::path &path,
                                                    const fs::path &builtInPath,
                                                    const std::string &language,
                                                    const LoadOptions &options) {
    LoadResult result;
    auto underRendererFolder = [](const fs::path &packPath) {
        return packPath.empty() || packPath.is_absolute() ? packPath : Renderer::folderPath / packPath;
    };
    const fs::path fallbackPath = underRendererFolder(builtInPath);
    auto stageIfExtending = [&](const fs::path &root, const fs::path &packPath) -> fs::path {
        const fs::path configPath = root / "configs.json";
        if (!fs::exists(configPath)) return root;
        std::ifstream stream(configPath);
        const json config = json::parse(stream, nullptr, false);
        if (config.is_discarded() || !config.is_object()) return root;
        const auto base = shader_pack::readExtends(config);
        if (!base.has_value()) return root;
        shader_pack::checkExtendsBase(*base);
        if (fs::weakly_canonical(packPath) == fs::weakly_canonical(fallbackPath)) {
            fail("the built-in pack cannot extend itself");
        }
        std::string error;
        const fs::path baseRoot = packRoot(fallbackPath, error, options.tempFolder);
        if (baseRoot.empty()) fail("extends: the base pack could not be read: " + error);
        const fs::path stage = tempRoot(options.tempFolder) / (extractionFolderName(packPath) + "_extended");
        shader_pack::stageExtendedPack(baseRoot, root, stage);
        radiante::out() << "[ShaderPack] " << packPath.filename().string() << " extends " << base->id
                        << ", staged in " << stage.string() << std::endl;
        return stage;
    };

    auto tryLoad = [&](const fs::path &packPath) {
        std::string error;
        const fs::path root = packRoot(packPath, error, options.tempFolder);
        if (root.empty()) {
            result.error = error;
            return false;
        }
        try {
            result.shaderPack = parseConfigFile(stageIfExtending(root, packPath), language);
        } catch (const std::exception &exception) {
            result.error = exception.what();
            return false;
        }
        result.success = true;
        return true;
    };
    const fs::path requestedPath = underRendererFolder(path.empty() ? builtInPath : path);
    if (tryLoad(requestedPath)) return result;
    if (options.fallBack && !path.empty() && requestedPath != fallbackPath) {
        radiante::err() << "[ShaderPack] " << requestedPath.string() << " could not be loaded: " << result.error
                        << "; using the built-in pack" << std::endl;
        tryLoad(fallbackPath);
    }
    return result;
}

void ShaderPackLoader::addResourceDefinitions(const std::vector<TextureConfig> &textures,
                                              const std::vector<BufferConfig> &buffers,
                                              std::unordered_map<std::string, std::string> &definitions) {
    const auto macro = [](std::string_view prefix, const std::string &name) {
        std::string text(prefix);
        for (const char c : name) {
            text += std::isalnum(static_cast<unsigned char>(c)) != 0
                        ? static_cast<char>(std::toupper(static_cast<unsigned char>(c)))
                        : '_';
        }
        return text;
    };
    definitions.try_emplace("RADIANTE_PACK_SET_RAY_TRACING", "5");
    definitions.try_emplace("RADIANTE_PACK_SET_POST", "4");
    // What this Radiante offers, for a pack that uses a feature only where it is there ("glass_frost?").
    for (const std::string_view feature : kShaderPackFeatures) {
        definitions.try_emplace(macro("RADIANTE_FEATURE_", std::string(feature)), "1");
    }
    for (const auto &texture : textures) {
        if (texture.sampledBinding.has_value()) {
            definitions.try_emplace(macro("RADIANTE_TEXTURE_", texture.name), std::to_string(*texture.sampledBinding));
        }
        if (texture.storageBinding.has_value()) {
            definitions.try_emplace(macro("RADIANTE_IMAGE_", texture.name), std::to_string(*texture.storageBinding));
        }
    }
    for (const auto &buffer : buffers) {
        definitions.try_emplace(macro("RADIANTE_BUFFER_", buffer.name), std::to_string(buffer.binding));
    }
}

bool ShaderPackLoader::addDebugViewDefinitions(const std::vector<DebugViewConfig> &views,
                                               const std::string &view,
                                               std::unordered_map<std::string, std::string> &definitions) {
    for (const auto &candidate : views) definitions.try_emplace(candidate.define, "0");
    if (view.empty() || view == "off") return true;
    for (const auto &candidate : views) {
        if (candidate.name != view) continue;
        definitions[candidate.define] = candidate.value;
        return true;
    }
    return false;
}

std::string ShaderPackLoader::buildExecutionSource(const std::vector<VariableConfig> &variables,
                                                   uint32_t executionSet) {
    if (variables.empty()) return "";

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
