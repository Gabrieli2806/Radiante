#include "prism/render/modules/world/shader_pack/shader_pack_internal.hpp"

#include <tinyexpr.h>

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace shader_pack {

struct ExpressionStorage {
    struct Variable {
        std::string_view name; // the whole of a std::string, so NUL-terminated as tinyexpr needs
        const double *address; // null: the value is values[valueIndex]
        size_t valueIndex;
    };

    std::vector<Variable> variables;
    std::vector<double> values;
    std::vector<te_variable> table;
    bool busy = false;
};

namespace {

constexpr double kTrue = 1.0;
constexpr double kFalse = 0.0;

double ifThenElse(double condition, double then, double otherwise) {
    return condition != 0.0 ? then : otherwise;
}

struct CompiledExpressionDeleter {
    void operator()(te_expr *expression) const {
        te_free(expression);
    }
};

ExpressionStorage &threadStorage() {
    thread_local ExpressionStorage storage;
    return storage;
}

} // namespace

ExpressionScope::ExpressionScope() {
    ExpressionStorage &shared = threadStorage();
    if (shared.busy) {
        nested_ = std::make_unique<ExpressionStorage>();
        storage_ = nested_.get();
    } else {
        storage_ = &shared;
    }
    storage_->busy = true;
    storage_->variables.clear();
    storage_->values.clear();
}

ExpressionScope::~ExpressionScope() {
    storage_->busy = false;
}

void ExpressionScope::add(const std::string &name, const double *value) {
    storage_->variables.push_back({name, value, 0});
}

void ExpressionScope::add(const std::string &name, double value) {
    storage_->values.push_back(value);
    storage_->variables.push_back({name, nullptr, storage_->values.size() - 1});
}

size_t ExpressionScope::size() const {
    return storage_->variables.size();
}

bool ExpressionScope::contains(std::string_view name, size_t begin, size_t end) const {
    end = std::min(end, storage_->variables.size());
    for (size_t i = begin; i < end; i++) {
        if (storage_->variables[i].name == name) return true;
    }
    return false;
}

double ExpressionScope::evaluate(const std::string &expression) const {
    for (const auto &variable : storage_->variables) {
        if (!ExpressionEvaluator::isValidIdentifier(variable.name)) {
            throw std::runtime_error("invalid execution variable name: " + std::string(variable.name));
        }
        if (ExpressionEvaluator::isReservedIdentifier(variable.name)) {
            throw std::runtime_error("reserved execution variable name: " + std::string(variable.name));
        }
    }

    // tinyexpr searches the table in order, so true and false come first and the if function last.
    std::vector<te_variable> &table = storage_->table;
    table.clear();
    table.reserve(storage_->variables.size() + 3);
    table.push_back({"true", &kTrue, TE_VARIABLE, nullptr});
    table.push_back({"false", &kFalse, TE_VARIABLE, nullptr});
    for (const auto &variable : storage_->variables) {
        const double *address =
            variable.address != nullptr ? variable.address : &storage_->values[variable.valueIndex];
        table.push_back({variable.name.data(), address, TE_VARIABLE, nullptr});
    }
    table.push_back({"if", reinterpret_cast<const void *>(&ifThenElse), TE_FUNCTION3 | TE_FLAG_PURE, nullptr});

    int error = 0;
    std::unique_ptr<te_expr, CompiledExpressionDeleter> compiled(
        te_compile(expression.c_str(), table.data(), static_cast<int>(table.size()), &error));
    if (compiled == nullptr) {
        throw std::runtime_error("invalid execution expression: " + expression + " at " + std::to_string(error));
    }
    return te_eval(compiled.get());
}

} // namespace shader_pack

bool ExpressionEvaluator::isValidIdentifier(std::string_view name) {
    if (name.empty() || std::isalpha(static_cast<unsigned char>(name.front())) == 0) return false;
    return std::all_of(name.begin(), name.end(), [](char ch) {
        return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_';
    });
}

bool ExpressionEvaluator::isReservedIdentifier(std::string_view name) {
    return name == "if" || name == "true" || name == "false";
}

double ExpressionEvaluator::evaluate(const std::string &expression, const std::vector<Variable> &variables) {
    shader_pack::ExpressionScope scope;
    for (const Variable &variable : variables) scope.add(variable.name, &variable.value);
    return scope.evaluate(expression);
}
