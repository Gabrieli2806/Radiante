#include "prism/render/modules/world/shader_pack/shader_pack_internal.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace {

// The most vkCmdUpdateBuffer writes in one command.
constexpr VkDeviceSize kMaxInlineUpdate = 65536;

// What static_cast<int64_t> gives on x86-64 for every double (the lowest value for NaN and anything out of range),
// without the undefined behaviour of that cast out of range.
int64_t truncateToInt64(double value) {
    constexpr double limit = 9223372036854775808.0; // 2^63
    if (value >= -limit && value < limit) return static_cast<int64_t>(value);
    return std::numeric_limits<int64_t>::min();
}

std::string formatNumber(double value) {
    std::ostringstream text;
    text << value;
    return text.str();
}

// The type a value gives a variable that has none: an expression that evaluates is a float, other text a string.
template <typename Evaluate>
std::string typeOfValue(const nlohmann::json &value, Evaluate &&evaluate) {
    if (value.is_boolean()) return "bool";
    if (value.is_number_integer() || value.is_number_unsigned()) return "int";
    if (value.is_number_float()) return "float";
    if (value.is_string()) {
        try {
            evaluate(value.get_ref<const std::string &>());
            return "float";
        } catch (...) { return "string"; }
    }
    return "string";
}

// A variable's float in the execution buffer: a bool as 0 or 1, a choice as its index; what does not read as a number
// is 0.
float bufferValue(const std::string &type, const std::string &value) {
    if (type == "bool") return shader_pack::isTrueValue(value) ? 1.0f : 0.0f;
    try {
        if (shader_pack::isIntType(type)) return static_cast<float>(std::stoi(value));
        if (shader_pack::isEnumType(type)) {
            if (auto index = shader_pack::enumIndex(type, value)) return static_cast<float>(*index);
            return static_cast<float>(std::stoi(value));
        }
        return std::stof(value);
    } catch (...) { return 0.0f; }
}

} // namespace

std::shared_ptr<gpu::DeviceLocalBuffer> ShaderPack::createPassExecutionBuffer(std::shared_ptr<gpu::Device> device,
                                                                              std::shared_ptr<gpu::VMA> vma,
                                                                              size_t executionBufferSize) {
    if (executionBufferSize == 0) return nullptr;
    // Without a staging buffer: uploadExecutionBuffer writes the values through the command buffer.
    return gpu::DeviceLocalBuffer::create(vma, device, false, executionBufferSize,
                                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0, VMA_MEMORY_USAGE_GPU_ONLY);
}

void ShaderPack::defineExecutionDescriptorSet(gpu::DescriptorTableBuilder &builder,
                                              ShaderPackLoader::Stage stage,
                                              VkShaderStageFlags stageFlags) const {
    if (execution(stage).variables.empty()) return;

    builder.beginDescriptorLayoutSet()
        .beginDescriptorLayoutSetBinding()
        .defineDescriptorLayoutSetBinding({
            .binding = ShaderPackLoader::EXECUTION_BINDING,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = stageFlags,
        })
        .endDescriptorLayoutSetBinding()
        .endDescriptorLayoutSet();
}

double ShaderPack::evaluateNumericExpression(ShaderPackLoader::Stage,
                                             const std::string &expression,
                                             const ExecutionVariables &variables,
                                             const std::vector<ExpressionEvaluator::Variable> &additionalVariables,
                                             bool includeAttributeDefines) const {
    shader_pack::ExpressionScope scope;
    for (const auto &entry : variables) {
        const ExecutionVariable &variable = entry.second;
        const auto number = shader_pack::executionVariableNumber(variable.type, variable.value);
        if (number.has_value() && !scope.contains(variable.name)) scope.add(variable.name, *number);
    }
    if (includeAttributeDefines) shader_pack::addAttributeNumbers(scope, attributeNumbers_, attributeNumbersError_);
    for (const auto &variable : additionalVariables) {
        if (!scope.contains(variable.name)) scope.add(variable.name, &variable.value);
    }
    return scope.evaluate(expression);
}

bool ShaderPack::evaluateCondition(ShaderPackLoader::Stage stage,
                                   const nlohmann::json &condition,
                                   const ExecutionVariables &variables,
                                   const std::vector<ExpressionEvaluator::Variable> &additionalVariables,
                                   bool includeAttributeDefines) const {
    if (condition.is_boolean()) return condition.get<bool>();
    if (condition.is_number()) return condition.get<double>() != 0.0;
    if (condition.is_string()) {
        return evaluateNumericExpression(stage, condition.get_ref<const std::string &>(), variables,
                                         additionalVariables, includeAttributeDefines) != 0.0;
    }
    throw std::runtime_error("execution condition must be a string, boolean, or number");
}

std::string ShaderPack::evaluateAssignment(ShaderPackLoader::Stage stage,
                                           ExecutionVariable &variable,
                                           const ShaderPackLoader::ExecutionCommand::Assignment &assignment,
                                           const ExecutionVariables &variables,
                                           const std::vector<ExpressionEvaluator::Variable> &additionalVariables,
                                           bool includeAttributeDefines) const {
    const nlohmann::json &value = assignment.value;
    const auto evaluate = [&](const std::string &expression) {
        return evaluateNumericExpression(stage, expression, variables, additionalVariables, includeAttributeDefines);
    };

    // A variable the script declares keeps its type. Any other takes the assignment's type, else keeps the one it has,
    // else gets one from the value.
    const auto &configs = stageRuntime(stage).executionVariableConfigs;
    const auto config = configs.find(variable.name);
    if (config != configs.end()) {
        if (assignment.explicitType && assignment.type.has_value() && *assignment.type != config->second.type) {
            throw std::runtime_error("execution assignment type for " + variable.name +
                                     " does not match global variable type");
        }
        variable.type = config->second.type;
    } else if (assignment.type.has_value()) {
        variable.type = *assignment.type;
    } else if (variable.type.empty()) {
        variable.type = typeOfValue(value, evaluate);
    }

    const std::string &type = variable.type;
    const bool isBool = type == "bool";
    const bool isInt = shader_pack::isIntType(type);
    if (value.is_string()) {
        const std::string &text = value.get_ref<const std::string &>();
        if (isBool) return evaluate(text) != 0.0 ? "true" : "false";
        if (isInt) return std::to_string(truncateToInt64(evaluate(text)));
        if (shader_pack::isFloatType(type)) return formatNumber(evaluate(text));
        return text;
    }
    if (value.is_boolean()) return value.get<bool>() ? "true" : "false";
    if (value.is_number()) {
        const double number = value.get<double>();
        if (isBool) return number != 0.0 ? "true" : "false";
        if (isInt) return std::to_string(truncateToInt64(number));
        return formatNumber(number);
    }
    throw std::runtime_error("execution assignment for " + variable.name + " must be a string, boolean, or number");
}

void ShaderPack::uploadExecutionBuffer(ShaderPackLoader::Stage stage,
                                       const std::shared_ptr<gpu::DeviceLocalBuffer> &executionBuffer,
                                       const ExecutionVariables &variables,
                                       const std::shared_ptr<gpu::CommandBuffer> &commandBuffer,
                                       const std::shared_ptr<gpu::DescriptorTable> &descriptorTable,
                                       uint32_t executionSet,
                                       uint32_t queueIndex,
                                       VkPipelineStageFlags2 dstStageMask) const {
    const auto &configs = execution(stage).variables;
    if (executionBuffer == nullptr || configs.empty()) return;

    // Reused: this runs before every pass of every frame.
    thread_local std::vector<float> values;
    values.clear();
    for (const auto &config : configs) {
        const auto found = variables.find(config.name);
        const std::string &value = found != variables.end() ? found->second.value : config.defaultValue;
        values.push_back(bufferValue(config.type, value));
    }

    // Written into the command buffer rather than into a staging buffer the CPU would rewrite for the next frame
    // before the GPU had copied it for this one: each run of a pass reads the values the script gave it.
    // The previous run of this pass (this frame or the last) may still read the buffer: the write waits for it.
    commandBuffer->barriersBufferImage({{
                                           .srcStageMask = dstStageMask,
                                           .srcAccessMask = 0,
                                           .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                           .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                           .srcQueueFamilyIndex = queueIndex,
                                           .dstQueueFamilyIndex = queueIndex,
                                           .buffer = executionBuffer,
                                       }},
                                       {});
    const auto *bytes = reinterpret_cast<const std::byte *>(values.data());
    const VkDeviceSize size =
        std::min<VkDeviceSize>(values.size() * sizeof(float), executionBuffer->size()) & ~VkDeviceSize{3};
    for (VkDeviceSize offset = 0; offset < size; offset += kMaxInlineUpdate) {
        vkCmdUpdateBuffer(commandBuffer->vkCommandBuffer(), executionBuffer->vkBuffer(), offset,
                          std::min(kMaxInlineUpdate, size - offset), bytes + offset);
    }
    descriptorTable->bindBuffer(executionBuffer, executionSet, ShaderPackLoader::EXECUTION_BINDING);
    commandBuffer->barriersBufferImage({{
                                           .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                           .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                           .dstStageMask = dstStageMask,
                                           .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
                                           .srcQueueFamilyIndex = queueIndex,
                                           .dstQueueFamilyIndex = queueIndex,
                                           .buffer = executionBuffer,
                                       }},
                                       {});
}

ShaderPack::ExecutionVariable &ShaderPack::findOrCreateExecutionVariable(ShaderPackLoader::Stage stage,
                                                                         ExecutionVariables &variables,
                                                                         const std::string &name) const {
    if (auto found = variables.find(name); found != variables.end()) return found->second;

    // First assigned now: it starts as the script declares it, or untyped and empty.
    ExecutionVariable variable{.name = name};
    const auto &configs = stageRuntime(stage).executionVariableConfigs;
    if (auto config = configs.find(name); config != configs.end()) {
        variable.value = config->second.defaultValue;
        variable.type = config->second.type;
    }
    return variables.emplace(name, std::move(variable)).first->second;
}

void ShaderPack::executeCommands(ShaderPackLoader::Stage stage,
                                 const std::vector<ShaderPackLoader::ExecutionCommand> &commands,
                                 ExecutionVariables &variables,
                                 const std::vector<ExpressionEvaluator::Variable> &additionalVariables,
                                 bool includeAttributeDefines,
                                 uint32_t loopLimit,
                                 const ExecutePassCallback &executePass) const {
    using Type = ShaderPackLoader::ExecutionCommand::Type;
    const auto condition = [&](const nlohmann::json &expression) {
        return evaluateCondition(stage, expression, variables, additionalVariables, includeAttributeDefines);
    };
    const auto run = [&](const std::vector<ShaderPackLoader::ExecutionCommand> &block) {
        executeCommands(stage, block, variables, additionalVariables, includeAttributeDefines, loopLimit, executePass);
    };

    for (const auto &command : commands) {
        switch (command.type) {
            case Type::Assign:
                for (const auto &[name, assignment] : command.assignments) {
                    ExecutionVariable &variable = findOrCreateExecutionVariable(stage, variables, name);
                    variable.value = evaluateAssignment(stage, variable, assignment, variables, additionalVariables,
                                                        includeAttributeDefines);
                }
                break;
            case Type::Pass: executePass(command.passName, variables); break;
            case Type::IfElse: run(condition(command.condition) ? command.thenCommands : command.elseCommands); break;
            case Type::While:
                for (uint32_t iteration = 0; condition(command.condition); iteration++) {
                    if (iteration >= loopLimit) throw std::runtime_error("execution while exceeded iteration limit");
                    run(command.thenCommands);
                }
                break;
        }
    }
}
