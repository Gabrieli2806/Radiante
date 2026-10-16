#pragma once

// What the shader pack's source files share. Not for use outside this folder.

#include "prism/render/modules/world/shader_pack/shader_pack.hpp"
#include "prism/util/parallel.hpp"

#include <cstddef>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace shader_pack {

// fn(0) .. fn(count - 1) on the shader pack build threads; the first exception is rethrown once all have run.
// The one call into util/parallel.hpp, under the namespace name it has in this tree.
template <typename Fn>
void parallelEach(size_t count, Fn &&fn) {
    prism::parallelFor(count, std::forward<Fn>(fn));
}

// ---- text

// Without leading and trailing whitespace (std::isspace, like all of configs.json's handling).
std::string_view trimView(std::string_view text);
std::string trimmed(std::string_view text);

// ---- the "type" of an attribute or execution variable in configs.json

inline bool isEnumType(std::string_view type) {
    return type.starts_with("enum:");
}
inline bool isIntType(std::string_view type) {
    return type == "int" || type.starts_with("int_range:");
}
inline bool isFloatType(std::string_view type) {
    return type == "float" || type.starts_with("float_range:");
}

// How a bool is written in configs.json and in the game's settings.
bool isTrueValue(std::string_view value);
// The position of `value` among the '-' separated choices of an "enum:" type.
std::optional<int> enumIndex(std::string_view type, std::string_view value);
// An attribute's value as its defines are written with it: a bool as 1 or 0, a choice as its index, others as they
// are.
std::string defineValue(const std::string &type, const std::string &value);
// `expression` with each X that stands alone (not part of a longer name) replaced by `value`.
std::string substitutePlaceholder(std::string_view expression, std::string_view value);

// What a runtime attribute's WorldUBO.packParams slot holds. Only these fit a vec4.
enum class RuntimeValueKind {
    Unsupported,
    Float,
    Int, // int, int_range, bool and enum: the shaders read them as an int
    Vec3,
};
RuntimeValueKind runtimeValueKind(std::string_view type);

// The attribute defines the expressions in configs.json can read as numbers: a bool or numeric attribute's direct
// define, and each define template that is plain arithmetic once the value is in. In attribute order; a name can come
// more than once, and the first counts. Throws what std::stod throws for a numeric attribute whose value is no number.
std::vector<std::pair<std::string, double>>
attributeNumbers(const std::vector<ShaderPackLoader::AttributeConfig> &attributes,
                 const std::unordered_map<std::string, std::string> &values);

// An execution variable's value as expressions read it, or nothing when it is no number. Throws what std::stod throws
// for a choice that is neither one of its options nor a number.
std::optional<double> executionVariableNumber(const std::string &type, const std::string &value);

// ---- expressions

struct ExpressionStorage;

// The variables one expression evaluation reads. tinyexpr takes the first variable of a name, so what should win is
// added first. A variable given by address is read where it lives, and it and every name must outlast the evaluation;
// a variable given by value is kept by the scope. The storage is the thread's own and reused: the execution scripts
// evaluate expressions every frame.
class ExpressionScope {
  public:
    ExpressionScope();
    ~ExpressionScope();
    ExpressionScope(const ExpressionScope &) = delete;
    ExpressionScope &operator=(const ExpressionScope &) = delete;

    void add(const std::string &name, const double *value);
    void add(const std::string &name, double value);
    size_t size() const;
    // Whether a variable added at a position in [begin, end) has this name.
    bool contains(std::string_view name, size_t begin = 0, size_t end = std::numeric_limits<size_t>::max()) const;
    // Checks the names in the order they were added, then compiles and evaluates. Throws std::runtime_error for an
    // invalid or reserved name and for an expression that does not parse, as ExpressionEvaluator::evaluate does.
    double evaluate(const std::string &expression) const;

  private:
    // Only made when an evaluation is already under way on this thread.
    std::unique_ptr<ExpressionStorage> nested_;
    ExpressionStorage *storage_ = nullptr;
};

// Adds the attribute numbers (by address: they must outlast the evaluation) after what the scope has, a name it has
// already hiding a number. When the numbers could not be worked out, throws why (`error`) instead.
void addAttributeNumbers(ExpressionScope &scope,
                         const std::vector<ExpressionEvaluator::Variable> &numbers,
                         const std::exception_ptr &error);

} // namespace shader_pack
