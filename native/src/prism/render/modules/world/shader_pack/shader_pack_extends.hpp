#pragma once

// Packs that build on another one ("extends" in the manifest): the base pack's files staged first, the pack's own
// over them, configs.json and the lang files merged; and the checked extraction of every pack's zip. Kept free of the
// renderer (no Vulkan, no Renderer folder; minizip and nlohmann json only) so the offline tool in src/tools compiles
// it on its own; the loader calls extractPackZip and stageExtendedPack before parsing.
//
// The rules (docs/dev/shader-packs/design.md, section 5, says the same for pack authors):
// - files: the base's, then the pack's over them, same relative path = the pack's file.
// - configs.json: merged key by key. "attributes", "textures", "buffers" and "passes" are lists merged by "name": an
//   entry with a base entry's name is merged into it field by field (the pack's fields win), a new one is appended.
//   A new pass may say "before" or "after" another pass: it goes there in "passes" and next to every run of that
//   pass in its stage's script; a new pass with neither is appended, and run at the end of its stage's script unless
//   the pack's script already runs it. "execution" and "execution_post": "global_variables" merged by name, the
//   pack's "commands" (when it gives any) replace the base's, and a {"base": true} command in them stands for the
//   base's commands. "sharc" merged field by field. The manifest ("radiante", "radiance") is the pack's own. Any
//   other key: the pack's value replaces the base's.
// - lang/*.lang: the base's lines then the pack's, by file name (case and '-' or '_' do not matter); the last line
//   for a key wins, so the pack's names win.

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace shader_pack {

// The one base a pack can extend today, and the major version of its interface (its hook points). A pack that asks
// for "radiante-pt@2" on a Radiante that offers @1 is refused with that reason.
inline constexpr std::string_view BUILT_IN_BASE_ID = "radiante-pt";
inline constexpr int BUILT_IN_BASE_MAJOR = 1;

// Limits for a pack's own files, zipped or in a folder: what a shader pack needs is far below them, and a pack over
// them is refused instead of filling the disk on extraction.
inline constexpr uint64_t MAX_PACK_FILES = 20000;
inline constexpr uint64_t MAX_PACK_BYTES = 512ull * 1024 * 1024;

struct ExtendsBase {
    std::string id;           // "radiante-pt"
    std::optional<int> major; // 1 for "radiante-pt@1"; empty when no version is named
};

// The base the manifest ("radiante", else "radiance") names in "extends"; empty when it names none. Throws
// std::runtime_error when "extends" is not a string of the form id or id@major.
std::optional<ExtendsBase> readExtends(const nlohmann::json &config);

// Throws std::runtime_error, naming the base Radiante has, when the pack cannot extend the base asked for.
void checkExtendsBase(const ExtendsBase &base);

// configs.json of a pack built on `base`, by the rules above. Throws std::runtime_error naming the place in the
// pack's configs.json ("extends: root.passes[2].after ...").
nlohmann::json mergeConfigs(const nlohmann::json &base, const nlohmann::json &derived);

// Whether a path inside a pack (as a zip names it) stays inside it: relative, no "..", no drive or root.
bool safePackPath(std::string_view path);

// Builds `stage` (removed first) from the base pack's folder and the pack's folder: files, configs.json and lang
// files as above. Throws std::runtime_error on a bad merge, a pack over the limits, or a file that cannot be copied.
void stageExtendedPack(const std::filesystem::path &baseRoot,
                       const std::filesystem::path &packRoot,
                       const std::filesystem::path &stage);

// Extracts a pack's zip into `destination` (removed first), entry by entry: an entry whose header cannot be read, a
// name that leaves the folder, a symbolic link, more than MAX_PACK_FILES entries, more than MAX_PACK_BYTES actually
// written, an entry longer than its header says or failing its CRC, and the zip is refused, `destination` removed and
// `error` set. Every pack's zip goes through here, not only packs that extend another.
bool extractPackZip(const std::filesystem::path &zipPath, const std::filesystem::path &destination, std::string &error);

} // namespace shader_pack
