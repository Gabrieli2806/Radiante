#include "prism/render/modules/world/shader_pack/shader_pack_internal.hpp"
#include <stdexcept>
#include <charconv>

#include <cctype>
#include <cmath>
#include <initializer_list>
#include <string>

namespace shader_pack {

namespace {

bool isSpace(char ch) {
    return std::isspace(static_cast<unsigned char>(ch)) != 0;
}

bool isSymbolChar(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

// "uint(x)" as "x": a scalar cast around a whole define template, which the expression evaluator has no use for.
std::optional<std::string_view> unwrapScalarCast(std::string_view text, std::string_view cast) {
    text = trimView(text);
    if (text.size() <= cast.size() + 2 || !text.starts_with(cast)) return std::nullopt;
    text.remove_prefix(cast.size());
    while (!text.empty() && isSpace(text.front())) text.remove_prefix(1);
    if (text.empty() || text.front() != '(' || text.back() != ')') return std::nullopt;

    // The parenthesis opened first must be the one that closes last.
    int depth = 0;
    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] == '(') {
            depth++;
        } else if (text[i] == ')') {
            depth--;
            if (depth == 0 && i + 1 != text.size()) return std::nullopt;
            if (depth < 0) return std::nullopt;
        }
    }
    if (depth != 0) return std::nullopt;
    return trimView(text.substr(1, text.size() - 2));
}

// The number a define template comes to with this attribute value, when it is plain arithmetic (scalar casts aside).
std::optional<double> defineTemplateNumber(const ShaderPackLoader::AttributeConfig &attribute,
                                           const std::string &value,
                                           const std::string &text) {
    std::string resolved = substitutePlaceholder(text, defineValue(attribute.type, value));
    for (bool stripped = true; stripped;) {
        stripped = false;
        for (std::string_view cast : {"uint", "int", "float", "double"}) {
            if (auto inner = unwrapScalarCast(resolved, cast)) {
                resolved = std::string(*inner);
                stripped = true;
                break;
            }
        }
    }
    try {
        return ExpressionEvaluator::evaluate(resolved, {});
    } catch (...) { return std::nullopt; }
}

// A number as written in a define: always with a decimal point, whatever the C locale (std::stod follows it, and the
// JVM sets it on Linux, where a decimal comma would break every value). Leading spaces and a '+' are allowed, as
// std::stod allows them; parsed counts the characters read.
std::optional<double> parseLocaleFree(std::string_view text, size_t *parsed = nullptr) {
    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) start++;
    if (start < text.size() && text[start] == '+') start++;
    double value = 0.0;
    auto [end, error] = std::from_chars(text.data() + start, text.data() + text.size(), value);
    if (error != std::errc()) return std::nullopt;
    if (parsed != nullptr) *parsed = static_cast<size_t>(end - text.data());
    return value;
}

// A whole string read as one finite number, the way it reads as a literal in a define (where a float may end in f).
std::optional<float> parseRuntimeNumber(std::string_view text) {
    std::string number = trimmed(text);
    if (!number.empty() && (number.back() == 'f' || number.back() == 'F')) number.pop_back();
    if (number.empty()) return std::nullopt;
    size_t parsed = 0;
    auto value = parseLocaleFree(number, &parsed);
    if (!value || parsed != number.size() || !std::isfinite(*value)) return std::nullopt;
    return static_cast<float>(*value);
}

// A choice as its index, anything else as the number it starts with; throws std::invalid_argument for none, as
// std::stod did.
double indexOrNumber(const std::string &type, const std::string &value) {
    if (auto index = enumIndex(type, value)) return static_cast<double>(*index);
    if (auto number = parseLocaleFree(value)) return *number;
    throw std::invalid_argument("not a number: " + value);
}

} // namespace

std::string_view trimView(std::string_view text) {
    while (!text.empty() && isSpace(text.front())) text.remove_prefix(1);
    while (!text.empty() && isSpace(text.back())) text.remove_suffix(1);
    return text;
}

std::string trimmed(std::string_view text) {
    return std::string(trimView(text));
}

bool isTrueValue(std::string_view value) {
    return value == "render_pipeline.true" || value == "true" || value == "1";
}

std::optional<int> enumIndex(std::string_view type, std::string_view value) {
    if (!isEnumType(type)) return std::nullopt;
    // Split as std::getline splits on '-': empty choices count, an empty last one does not.
    const std::string_view choices = type.substr(5);
    int index = 0;
    for (size_t start = 0; start < choices.size(); index++) {
        const size_t dash = choices.find('-', start);
        const std::string_view choice = choices.substr(start, dash == std::string_view::npos ? dash : dash - start);
        if (choice == value) return index;
        if (dash == std::string_view::npos) break;
        start = dash + 1;
    }
    return std::nullopt;
}

std::string defineValue(const std::string &type, const std::string &value) {
    if (type == "bool") return isTrueValue(value) ? "1" : "0";
    if (auto index = enumIndex(type, value)) return std::to_string(*index);
    return value;
}

std::string substitutePlaceholder(std::string_view expression, std::string_view value) {
    std::string resolved;
    resolved.reserve(expression.size() + value.size());
    for (size_t i = 0; i < expression.size(); i++) {
        const bool alone = expression[i] == 'X' && (i == 0 || !isSymbolChar(expression[i - 1])) &&
                           (i + 1 == expression.size() || !isSymbolChar(expression[i + 1]));
        if (alone) {
            resolved += value;
        } else {
            resolved += expression[i];
        }
    }
    return resolved;
}

RuntimeValueKind runtimeValueKind(std::string_view type) {
    if (type == "vec3") return RuntimeValueKind::Vec3;
    if (type == "bool" || isIntType(type) || isEnumType(type)) return RuntimeValueKind::Int;
    if (isFloatType(type)) return RuntimeValueKind::Float;
    return RuntimeValueKind::Unsupported;
}

std::vector<std::pair<std::string, double>>
attributeNumbers(const std::vector<ShaderPackLoader::AttributeConfig> &attributes,
                 const std::unordered_map<std::string, std::string> &values) {
    std::vector<std::pair<std::string, double>> numbers;
    for (const auto &attribute : attributes) {
        const auto found = values.find(attribute.name);
        const std::string &value = found == values.end() ? attribute.defaultValue : found->second;
        const std::string &type = attribute.type;

        // A plain "float" has no direct define number: only the ranged types do.
        if (attribute.define.direct.has_value()) {
            if (type == "bool") {
                numbers.emplace_back(*attribute.define.direct, isTrueValue(value) ? 1.0 : 0.0);
            } else if (isIntType(type) || type.starts_with("float_range:") || isEnumType(type)) {
                numbers.emplace_back(*attribute.define.direct, indexOrNumber(type, value));
            }
        }
        for (const auto &[name, text] : attribute.define.expressions) {
            if (auto number = defineTemplateNumber(attribute, value, text)) numbers.emplace_back(name, *number);
        }
    }
    return numbers;
}

std::optional<double> executionVariableNumber(const std::string &type, const std::string &value) {
    if (type == "bool") return isTrueValue(value) ? 1.0 : 0.0;
    if (isEnumType(type)) return indexOrNumber(type, value);
    size_t parsed = 0;
    auto number = parseLocaleFree(value, &parsed);
    if (number && parsed == value.size()) return number;
    return std::nullopt;
}

void addAttributeNumbers(ExpressionScope &scope,
                         const std::vector<ExpressionEvaluator::Variable> &numbers,
                         const std::exception_ptr &error) {
    if (error != nullptr) std::rethrow_exception(error);
    const size_t before = scope.size();
    for (const auto &number : numbers) {
        if (!scope.contains(number.name, 0, before)) scope.add(number.name, &number.value);
    }
}

} // namespace shader_pack

std::unordered_map<std::string, std::string>
ShaderPackLoader::buildAttributeDefinitions(const std::vector<AttributeConfig> &attributes,
                                            const std::unordered_map<std::string, std::string> &currentValues) {
    std::unordered_map<std::string, std::string> definitions;
    auto writeTemplates = [&](const std::unordered_map<std::string, std::string> &templates, const std::string &value) {
        for (const auto &[name, text] : templates) definitions[name] = shader_pack::substitutePlaceholder(text, value);
    };

    for (const auto &attribute : attributes) {
        if (attribute.runtime) {
            // A read of the world uniform in place of the value: the define text stays the same whatever the setting
            // is, so changing it rebuilds nothing, and the compiled shaders stay in the cache. A template sees the
            // value as the type it had compiled in, so (X / 2) stays an integer division.
            const std::string slot = "worldUBO.packParams[" + std::to_string(attribute.runtimeSlot) + "]";
            std::string direct;
            std::string templateValue;
            switch (shader_pack::runtimeValueKind(attribute.type)) {
                case shader_pack::RuntimeValueKind::Vec3:
                    direct = "vec3(" + slot + ".xyz)";
                    templateValue = slot + ".xyz";
                    break;
                case shader_pack::RuntimeValueKind::Int:
                    direct = "int(" + slot + ".x)";
                    templateValue = direct;
                    break;
                default:
                    direct = "(" + slot + ".x)";
                    templateValue = slot + ".x";
                    break;
            }
            if (attribute.define.direct.has_value()) definitions[*attribute.define.direct] = direct;
            writeTemplates(attribute.define.expressions, templateValue);
            continue;
        }

        const auto found = currentValues.find(attribute.name);
        const std::string &currentValue = found == currentValues.end() ? attribute.defaultValue : found->second;
        const std::string value = shader_pack::defineValue(attribute.type, currentValue);
        if (attribute.define.direct.has_value()) definitions[*attribute.define.direct] = value;
        writeTemplates(attribute.define.expressions, value);
        // Variants are chosen by the value as the settings write it, not as the defines do.
        if (auto variant = attribute.define.variants.find(currentValue); variant != attribute.define.variants.end()) {
            writeTemplates(variant->second, value);
        }
    }
    return definitions;
}

std::optional<glm::vec4> ShaderPackLoader::parseRuntimeValue(const std::string &type, const std::string &value) {
    switch (shader_pack::runtimeValueKind(type)) {
        case shader_pack::RuntimeValueKind::Float:
        case shader_pack::RuntimeValueKind::Int: {
            auto number = shader_pack::parseRuntimeNumber(shader_pack::defineValue(type, value));
            if (!number.has_value()) return std::nullopt;
            return glm::vec4(*number, 0.0f, 0.0f, 0.0f);
        }
        case shader_pack::RuntimeValueKind::Vec3: {
            // "x,y,z", or one number for all three, the way vec3(X) reads it.
            std::vector<float> components;
            const std::string_view text = value;
            for (size_t start = 0;;) {
                const size_t comma = text.find(',', start);
                auto number = shader_pack::parseRuntimeNumber(
                    text.substr(start, comma == std::string_view::npos ? comma : comma - start));
                if (!number.has_value()) return std::nullopt;
                components.push_back(*number);
                if (comma == std::string_view::npos) break;
                start = comma + 1;
            }
            if (components.size() == 1) return glm::vec4(glm::vec3(components[0]), 0.0f);
            if (components.size() == 3) return glm::vec4(components[0], components[1], components[2], 0.0f);
            return std::nullopt;
        }
        case shader_pack::RuntimeValueKind::Unsupported: break;
    }
    return std::nullopt;
}
