#include "prism/render/modules/world/shader_pack/shader_pack_loader_internal.hpp"

#include <algorithm>
#include <unordered_set>
#include <utility>
#include <vector>

namespace shader_pack::loader {

namespace {

using Loader = ShaderPackLoader;

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

} // namespace

// world.rchit, world.rahit and world.rint make the shaders of hit group "world" unless the pack names others.
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

} // namespace shader_pack::loader
