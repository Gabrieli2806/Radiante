#include "prism/render/modules/world/shader_pack/shader_pack_extends.hpp"

#include "mz.h"
#include "mz_strm.h"
#include "mz_zip.h"
#include "mz_zip_rw.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace shader_pack {

namespace {

[[noreturn]] void fail(const std::string &message) {
    throw std::runtime_error("extends: " + message);
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return text;
}

const json *manifestOf(const json &config) {
    for (const char *key : {"radiante", "radiance"}) {
        const auto found = config.find(key);
        if (found != config.end() && found->is_object()) return &*found;
    }
    return nullptr;
}

// ---- lists merged by name

std::string entryName(const json &entry, const std::string &context) {
    if (!entry.is_object()) fail(context + " must be an object");
    const auto name = entry.find("name");
    if (name == entry.end() || !name->is_string() || name->get<std::string>().empty()) {
        fail(context + ".name must be a non-empty string");
    }
    return name->get<std::string>();
}

// The base's entry of the same name merged field by field (the pack's fields win), or the pack's entry appended.
// Returns the names of the entries the pack added, in its order.
std::vector<std::string> mergeNamedList(json &merged, const json &derived, const std::string &key) {
    std::vector<std::string> added;
    const auto entries = derived.find(key);
    if (entries == derived.end()) return added;
    const std::string context = "root." + key;
    if (!entries->is_array()) fail(context + " must be an array");
    json &target = merged[key];
    if (target.is_null()) target = json::array();
    if (!target.is_array()) fail("the base's root." + key + " is not an array");

    std::unordered_set<std::string> seen;
    for (size_t i = 0; i < entries->size(); i++) {
        const std::string entryContext = context + "[" + std::to_string(i) + "]";
        const json &entry = (*entries)[i];
        const std::string name = entryName(entry, entryContext);
        if (!seen.insert(name).second) fail(entryContext + ": " + name + " is listed twice");
        auto existing = std::find_if(target.begin(), target.end(), [&](const json &candidate) {
            return candidate.is_object() && candidate.value("name", std::string()) == name;
        });
        if (existing != target.end()) {
            for (auto field = entry.begin(); field != entry.end(); ++field) (*existing)[field.key()] = field.value();
        } else {
            target.push_back(entry);
            added.push_back(name);
        }
    }
    return added;
}

// ---- execution scripts

// The object holding the script of a stage: "execution" ("execution.ray_tracing" when written that way) or
// "execution_post". Null when the config has none.
json *scriptOf(json &config, bool postRender) {
    const char *key = postRender ? "execution_post" : "execution";
    const auto found = config.find(key);
    if (found == config.end() || !found->is_object()) return nullptr;
    if (!postRender) {
        const auto rayTracing = found->find("ray_tracing");
        if (rayTracing != found->end() && rayTracing->is_object()) return &*rayTracing;
    }
    return &*found;
}

const json *scriptOf(const json &config, bool postRender) {
    return scriptOf(const_cast<json &>(config), postRender);
}

bool isBaseStep(const json &command) {
    if (!command.is_object() || command.size() != 1) return false;
    const auto base = command.find("base");
    return base != command.end() && base->is_boolean() && base->get<bool>();
}

// The pack's commands with each {"base": true} replaced by the base's commands, in then/else blocks too.
json spliceBase(const json &commands, const json &baseCommands, bool &used) {
    json result = json::array();
    for (const json &command : commands) {
        if (isBaseStep(command)) {
            used = true;
            for (const json &baseCommand : baseCommands) result.push_back(baseCommand);
            continue;
        }
        json copy = command;
        for (const char *block : {"if_else", "while"}) {
            const auto found = copy.find(block);
            if (found == copy.end() || !found->is_object()) continue;
            for (const char *branch : {"then", "else"}) {
                const auto list = found->find(branch);
                if (list != found->end() && list->is_array()) *list = spliceBase(*list, baseCommands, used);
            }
        }
        result.push_back(std::move(copy));
    }
    return result;
}

void mergeScript(json &merged, const json &base, const json &derived, bool postRender) {
    const char *key = postRender ? "execution_post" : "execution";
    const auto found = derived.find(key);
    if (found == derived.end()) return;
    const std::string context = std::string("root.") + key;
    if (!found->is_object()) fail(context + " must be an object");
    const json *derivedScript = scriptOf(derived, postRender);

    json *target = scriptOf(merged, postRender);
    if (target == nullptr) {
        merged[key] = json::object();
        target = &merged[key];
    }
    const json *baseScript = scriptOf(base, postRender);
    const json baseCommands =
        baseScript != nullptr && baseScript->contains("commands") ? (*baseScript)["commands"] : json::array();

    if (const auto globals = derivedScript->find("global_variables"); globals != derivedScript->end()) {
        if (!globals->is_array()) fail(context + ".global_variables must be an array");
        json &targetGlobals = (*target)["global_variables"];
        if (!targetGlobals.is_array()) targetGlobals = json::array();
        for (size_t i = 0; i < globals->size(); i++) {
            const json &variable = (*globals)[i];
            if (!variable.is_object() || variable.size() != 1) {
                fail(context + ".global_variables[" + std::to_string(i) + "] must be an object with one variable");
            }
            const std::string &name = variable.begin().key();
            auto existing = std::find_if(targetGlobals.begin(), targetGlobals.end(), [&](const json &candidate) {
                return candidate.is_object() && candidate.contains(name);
            });
            if (existing != targetGlobals.end()) {
                *existing = variable;
            } else {
                targetGlobals.push_back(variable);
            }
        }
    }

    if (const auto commands = derivedScript->find("commands"); commands != derivedScript->end()) {
        if (!commands->is_array()) fail(context + ".commands must be an array");
        bool used = false;
        (*target)["commands"] = spliceBase(*commands, baseCommands, used);
    }
}

// ---- passes

bool passIsPostRender(const json &pass) {
    const auto stage = pass.find("stage");
    return stage != pass.end() && stage->is_string() && lower(stage->get<std::string>()) == "post_render";
}

bool scriptRuns(const json &commands, const std::string &pass) {
    for (const json &command : commands) {
        if (!command.is_object()) continue;
        if (const auto name = command.find("pass"); name != command.end() && name->is_string() && *name == pass) {
            return true;
        }
        for (const char *block : {"if_else", "while"}) {
            const auto found = command.find(block);
            if (found == command.end() || !found->is_object()) continue;
            for (const char *branch : {"then", "else"}) {
                const auto list = found->find(branch);
                if (list != found->end() && list->is_array() && scriptRuns(*list, pass)) return true;
            }
        }
    }
    return false;
}

// Puts {"pass": added} before or after every {"pass": anchor}; returns how many it found.
size_t insertNextTo(json &commands, const std::string &anchor, const std::string &added, bool after) {
    size_t found = 0;
    json result = json::array();
    for (json &command : commands) {
        const bool isAnchor = command.is_object() && command.contains("pass") && command["pass"] == anchor;
        if (isAnchor && !after) result.push_back(json{{"pass", added}});
        if (command.is_object()) {
            for (const char *block : {"if_else", "while"}) {
                const auto blockIt = command.find(block);
                if (blockIt == command.end() || !blockIt->is_object()) continue;
                for (const char *branch : {"then", "else"}) {
                    const auto list = blockIt->find(branch);
                    if (list != blockIt->end() && list->is_array()) found += insertNextTo(*list, anchor, added, after);
                }
            }
        }
        result.push_back(std::move(command));
        if (isAnchor && after) result.push_back(json{{"pass", added}});
        if (isAnchor) found++;
    }
    commands = std::move(result);
    return found;
}

void placePasses(json &merged, const json &derived, const std::vector<std::string> &addedPasses) {
    const auto derivedPasses = derived.find("passes");
    if (derivedPasses == derived.end()) return;
    json &passes = merged["passes"];
    const std::unordered_set<std::string> added(addedPasses.begin(), addedPasses.end());

    for (size_t i = 0; i < derivedPasses->size(); i++) {
        const json &entry = (*derivedPasses)[i];
        const std::string context = "root.passes[" + std::to_string(i) + "]";
        const std::string name = entry["name"].get<std::string>();
        const bool hasBefore = entry.contains("before");
        const bool hasAfter = entry.contains("after");
        if (hasBefore && hasAfter) fail(context + " cannot have both before and after");
        if ((hasBefore || hasAfter) && !added.contains(name)) {
            fail(context + ": " + name + " replaces a base pass, which keeps its place; before/after are for new "
                                          "passes");
        }

        // The merged entry, without the placement keys the pass parser does not know.
        auto self = std::find_if(passes.begin(), passes.end(),
                                 [&](const json &pass) { return pass.value("name", std::string()) == name; });
        std::optional<std::string> anchor;
        bool after = false;
        if (hasBefore || hasAfter) {
            const json &value = entry[hasBefore ? "before" : "after"];
            if (!value.is_string()) fail(context + "." + (hasBefore ? "before" : "after") + " must be a pass name");
            anchor = value.get<std::string>();
            after = hasAfter;
        }
        self->erase("before");
        self->erase("after");
        if (!added.contains(name)) continue;

        const bool postRender = passIsPostRender(*self);
        json *script = scriptOf(merged, postRender);
        json *commands = script != nullptr && script->contains("commands") && (*script)["commands"].is_array() &&
                                 !(*script)["commands"].empty()
                             ? &(*script)["commands"]
                             : nullptr;

        if (anchor.has_value()) {
            if (*anchor == name) fail(context + " cannot be placed next to itself");
            auto anchorIt = std::find_if(passes.begin(), passes.end(),
                                         [&](const json &pass) { return pass.value("name", std::string()) == *anchor; });
            if (anchorIt == passes.end()) fail(context + ": no pass named " + *anchor + " to place " + name + " by");
            if (passIsPostRender(*anchorIt) != postRender) {
                fail(context + ": " + name + " and " + *anchor + " run in different stages");
            }
            // In "passes" too, which is the order a stage without a script runs them in.
            json moved = *self;
            passes.erase(self);
            anchorIt = std::find_if(passes.begin(), passes.end(),
                                    [&](const json &pass) { return pass.value("name", std::string()) == *anchor; });
            passes.insert(after ? anchorIt + 1 : anchorIt, std::move(moved));
            if (commands != nullptr && !scriptRuns(*commands, name)) {
                if (insertNextTo(*commands, *anchor, name, after) == 0) {
                    fail(context + ": the " + std::string(postRender ? "execution_post" : "execution") +
                         " script never runs " + *anchor + ", so " + name + " cannot go next to it");
                }
            }
        } else if (commands != nullptr && !scriptRuns(*commands, name)) {
            commands->push_back(json{{"pass", name}});
        }
    }
}

// ---- files

std::string languageKey(const fs::path &file) {
    std::string stem = lower(file.stem().string());
    std::replace(stem.begin(), stem.end(), '-', '_');
    return stem;
}

std::string readFile(const fs::path &path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open()) fail("cannot read " + path.string());
    std::ostringstream text;
    text << stream.rdbuf();
    return text.str();
}

void writeFile(const fs::path &path, const std::string &text) {
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream.is_open()) fail("cannot write " + path.string());
    stream << text;
}

bool isLangFile(const fs::path &relative) {
    auto part = relative.begin();
    if (part == relative.end() || lower(part->string()) != "lang") return false;
    return std::next(part) != relative.end() && std::next(std::next(part)) == relative.end() &&
           lower(relative.extension().string()) == ".lang";
}

// Every regular file under root, relative to it, in path order; symbolic links are not followed or copied.
std::vector<fs::path> packFiles(const fs::path &root, bool checkLimits) {
    std::vector<fs::path> files;
    uint64_t bytes = 0;
    std::error_code error;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::none, error);
         !error && it != fs::recursive_directory_iterator(); it.increment(error)) {
        const fs::file_status status = it->symlink_status();
        if (!fs::is_regular_file(status)) continue;
        const fs::path relative = it->path().lexically_relative(root);
        if (relative.empty() || !safePackPath(relative.generic_string())) continue;
        files.push_back(relative);
        if (checkLimits) {
            bytes += it->file_size();
            if (files.size() > MAX_PACK_FILES) fail("the pack has more than " + std::to_string(MAX_PACK_FILES) + " files");
            if (bytes > MAX_PACK_BYTES) fail("the pack is larger than " + std::to_string(MAX_PACK_BYTES >> 20) + " MB");
        }
    }
    if (error) fail("cannot list " + root.string() + ": " + error.message());
    std::sort(files.begin(), files.end(),
              [](const fs::path &a, const fs::path &b) { return a.generic_string() < b.generic_string(); });
    return files;
}

struct ZipReaderDeleter {
    void operator()(void *reader) const {
        mz_zip_reader_delete(&reader);
    }
};

json readConfig(const fs::path &root, const char *which) {
    const fs::path path = root / "configs.json";
    if (!fs::exists(path)) fail(std::string("the ") + which + " has no configs.json");
    try {
        json config = json::parse(readFile(path));
        if (!config.is_object()) fail(std::string("the ") + which + "'s configs.json is not an object");
        return config;
    } catch (const json::exception &exception) {
        fail(std::string("the ") + which + "'s configs.json does not parse: " + exception.what());
    }
}

} // namespace

std::optional<ExtendsBase> readExtends(const json &config) {
    const json *manifest = manifestOf(config);
    if (manifest == nullptr) return std::nullopt;
    const auto value = manifest->find("extends");
    if (value == manifest->end() || value->is_null()) return std::nullopt;
    if (!value->is_string()) fail("\"extends\" must be a pack id such as \"radiante-pt\" or \"radiante-pt@1\"");
    const std::string text = value->get<std::string>();
    ExtendsBase base;
    const size_t at = text.find('@');
    base.id = lower(text.substr(0, at));
    if (base.id.empty()) fail("\"extends\" names no pack");
    if (at != std::string::npos) {
        const std::string major = text.substr(at + 1);
        if (major.empty() || major.size() > 4 ||
            !std::all_of(major.begin(), major.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
            fail("\"extends\": the version after @ must be a major version number (\"" + text + "\")");
        }
        base.major = std::stoi(major);
    }
    return base;
}

void checkExtendsBase(const ExtendsBase &base) {
    if (base.id != BUILT_IN_BASE_ID) {
        fail("unknown base pack \"" + base.id + "\"; packs can extend " + std::string(BUILT_IN_BASE_ID));
    }
    if (base.major.has_value() && *base.major != BUILT_IN_BASE_MAJOR) {
        fail("the pack is written for " + std::string(BUILT_IN_BASE_ID) + "@" + std::to_string(*base.major) +
             "; this Radiante has " + std::string(BUILT_IN_BASE_ID) + "@" + std::to_string(BUILT_IN_BASE_MAJOR));
    }
}

json mergeConfigs(const json &base, const json &derived) {
    if (!base.is_object()) fail("the base's configs.json is not an object");
    if (!derived.is_object()) fail("configs.json is not an object");
    json merged = base;
    // The manifest is the pack's own: the base's name, version and icon are not the pack's.
    merged.erase("radiante");
    merged.erase("radiance");

    for (const char *key : {"attributes", "textures", "buffers"}) mergeNamedList(merged, derived, key);
    // Scripts before passes: a new pass is placed in the merged script.
    mergeScript(merged, base, derived, false);
    mergeScript(merged, base, derived, true);
    const std::vector<std::string> addedPasses = mergeNamedList(merged, derived, "passes");
    placePasses(merged, derived, addedPasses);

    for (auto field = derived.begin(); field != derived.end(); ++field) {
        const std::string &key = field.key();
        if (key == "attributes" || key == "textures" || key == "buffers" || key == "passes" || key == "execution" ||
            key == "execution_post") {
            continue;
        }
        if (key == "sharc" && field->is_object() && merged.contains("sharc") && merged["sharc"].is_object()) {
            for (auto entry = field->begin(); entry != field->end(); ++entry) merged["sharc"][entry.key()] = *entry;
            continue;
        }
        merged[key] = *field;
    }
    return merged;
}

bool safePackPath(std::string_view path) {
    if (path.empty() || path.size() > 512) return false;
    if (path.front() == '/' || path.front() == '\\') return false;
    if (path.find(':') != std::string_view::npos) return false; // C:\..., and alternate data streams
    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find_first_of("/\\", start);
        if (end == std::string_view::npos) end = path.size();
        if (path.substr(start, end - start) == "..") return false;
        start = end + 1;
    }
    return true;
}

void stageExtendedPack(const fs::path &baseRoot, const fs::path &packRoot, const fs::path &stage) {
    const json baseConfig = readConfig(baseRoot, "base pack");
    const json packConfig = readConfig(packRoot, "pack");
    const json merged = mergeConfigs(baseConfig, packConfig);

    std::error_code error;
    fs::remove_all(stage, error);
    fs::create_directories(stage, error);
    if (error) fail("cannot prepare " + stage.string() + ": " + error.message());

    // lang files by language, base first; written once both packs are read.
    std::map<std::string, std::pair<fs::path, std::string>> languages;
    auto copyPack = [&](const fs::path &root, bool own) {
        for (const fs::path &relative : packFiles(root, own)) {
            if (relative == "configs.json") continue;
            if (isLangFile(relative)) {
                auto &[name, text] = languages[languageKey(relative)];
                if (name.empty()) name = relative;
                std::string lines = readFile(root / relative);
                if (!text.empty() && text.back() != '\n') text.push_back('\n');
                text += lines;
                continue;
            }
            const fs::path target = stage / relative;
            fs::create_directories(target.parent_path(), error);
            fs::copy_file(root / relative, target, fs::copy_options::overwrite_existing, error);
            if (error) fail("cannot copy " + relative.generic_string() + ": " + error.message());
        }
    };
    copyPack(baseRoot, false);
    copyPack(packRoot, true);
    for (const auto &[key, file] : languages) writeFile(stage / file.first, file.second);
    writeFile(stage / "configs.json", merged.dump(4));
}

bool extractPackZip(const fs::path &zipPath, const fs::path &destination, std::string &error) {
    // Extracted afresh every time: what an earlier extraction left could be from another version of the zip.
    std::error_code fileError;
    fs::remove_all(destination, fileError);
    fs::create_directories(destination, fileError);
    if (fileError) {
        error = "failed to prepare temp dir: " + destination.string();
        return false;
    }
    const fs::path root = fs::weakly_canonical(destination, fileError);
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

    // Entry by entry, each file written here: a name that leaves the pack's folder, a symbolic link, an entry whose
    // header cannot be read, more files than a shader pack needs, or more bytes than it needs (counted as they are
    // written, not as the headers claim, so a zip that lies about its sizes stops there too), and the zip is refused
    // and what was written is removed. minizip's save_all was not used for this: it trusts the headers' sizes and
    // recreates symbolic links, and a later entry under a link's path would be written outside the folder.
    auto refuse = [&](const std::string &reason) {
        mz_zip_reader_close(reader.get());
        std::error_code ignored;
        fs::remove_all(destination, ignored);
        error = "zip refused (" + zipPath.filename().string() + "): " + reason;
        return false;
    };
    uint64_t entries = 0;
    uint64_t written = 0;
    std::vector<char> buffer(64 * 1024);
    int32_t status = mz_zip_reader_goto_first_entry(reader.get());
    if (status == MZ_END_OF_LIST) return refuse("it is empty");
    for (; status == MZ_OK; status = mz_zip_reader_goto_next_entry(reader.get())) {
        mz_zip_file *info = nullptr;
        if (mz_zip_reader_entry_get_info(reader.get(), &info) != MZ_OK || info == nullptr ||
            info->filename == nullptr) {
            return refuse("an entry's header cannot be read");
        }
        const std::string name(info->filename);
        if (!shader_pack::safePackPath(name)) return refuse("entry leaves the pack's folder: " + name);
        if (mz_zip_attrib_is_symlink(info->external_fa, info->version_madeby) == MZ_OK) {
            return refuse("entry is a symbolic link: " + name);
        }
        if (++entries > shader_pack::MAX_PACK_FILES) {
            return refuse("more than " + std::to_string(shader_pack::MAX_PACK_FILES) + " files");
        }

        // The path built here, and checked to stay under the folder once resolved (a folder entry of the same name
        // as a link, a junction left behind: nothing written may land outside).
        const fs::path target = fs::weakly_canonical(root / fs::path(name).lexically_normal(), fileError);
        if (fileError || std::mismatch(root.begin(), root.end(), target.begin(), target.end()).first != root.end()) {
            return refuse("entry leaves the pack's folder: " + name);
        }
        const bool directory = mz_zip_reader_entry_is_dir(reader.get()) == MZ_OK || name.back() == '/' ||
                               name.back() == '\\';
        if (directory) {
            fs::create_directories(target, fileError);
            if (fileError) return refuse("cannot create " + name);
            continue;
        }
        fs::create_directories(target.parent_path(), fileError);
        if (fileError) return refuse("cannot create the folder of " + name);
        // The resolved parent must still be inside: a link planted under the folder by another process would move it.
        const fs::path parent = fs::weakly_canonical(target.parent_path(), fileError);
        if (fileError || std::mismatch(root.begin(), root.end(), parent.begin(), parent.end()).first != root.end()) {
            return refuse("entry leaves the pack's folder: " + name);
        }

        // Refused before writing when the header already says it is too much; an entry longer than its header is
        // refused as it is read, so the bytes written never pass the limit either way.
        const uint64_t declared = static_cast<uint64_t>(std::max<int64_t>(info->uncompressed_size, 0));
        if (declared > shader_pack::MAX_PACK_BYTES - written) {
            return refuse("more than " + std::to_string(shader_pack::MAX_PACK_BYTES >> 20) + " MB");
        }
        if (mz_zip_reader_entry_open(reader.get()) != MZ_OK) return refuse("cannot read " + name);
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            mz_zip_reader_entry_close(reader.get());
            return refuse("cannot write " + name);
        }
        // Any stop leaves through the end of the loop, so the file is closed before the folder is removed (an open
        // file cannot be deleted on Windows).
        uint64_t entryBytes = 0;
        std::string problem;
        for (;;) {
            const int32_t read =
                mz_zip_reader_entry_read(reader.get(), buffer.data(), static_cast<int32_t>(buffer.size()));
            if (read < 0) {
                problem = "cannot read " + name + " (minizip error " + std::to_string(read) +
                          (read == MZ_SUPPORT_ERROR ? ", compression method not built in" : "") + ")";
                break;
            }
            if (read == 0) break;
            entryBytes += static_cast<uint64_t>(read);
            written += static_cast<uint64_t>(read);
            if (entryBytes > declared) {
                problem = name + " is larger than its header says";
                break;
            }
            if (written > shader_pack::MAX_PACK_BYTES) {
                problem = "more than " + std::to_string(shader_pack::MAX_PACK_BYTES >> 20) + " MB";
                break;
            }
            out.write(buffer.data(), read);
            if (!out) {
                problem = "cannot write " + name;
                break;
            }
        }
        out.close();
        // Closing checks the entry's CRC: a damaged or tampered entry is refused here.
        const int32_t closed = mz_zip_reader_entry_close(reader.get());
        if (!problem.empty()) return refuse(problem);
        if (closed != MZ_OK) return refuse(name + " is damaged (CRC)");
    }
    if (status != MZ_END_OF_LIST) {
        return refuse("its directory cannot be read (minizip error " + std::to_string(status) + ")");
    }
    if (mz_zip_reader_close(reader.get()) != MZ_OK) {
        error = "failed to close zip file";
        return false;
    }
    return true;
}


} // namespace shader_pack
