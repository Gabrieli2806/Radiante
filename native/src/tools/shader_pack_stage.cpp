// shader_pack_stage: puts together a pack that extends radiante-pt the way the renderer's loader does, for checking
// it without the game (compile its shaders with glslangValidator afterwards, see examples/shaderpacks/README.md).
//
//   shader_pack_stage <base pack folder> <pack folder> <output folder>
//   shader_pack_stage --self-test
//   shader_pack_stage --extract <zip> <output folder>   (the loader's checked zip extraction; exit 1 when refused)
//
// The base pack folder is radiante-pt as the native build stages it (build/.../ray_tracing_internal_zip/radiante-pt)
// or an extracted radiante-pt.zip.

#include "prism/render/modules/world/shader_pack/shader_pack_extends.hpp"

#include <cstdio>
#include <exception>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>

using json = nlohmann::json;

namespace {

int failures = 0;

void expect(bool condition, const std::string &what) {
    if (!condition) {
        failures++;
        std::cerr << "FAIL: " << what << "\n";
    } else {
        std::cout << "ok: " << what << "\n";
    }
}

void expectThrows(const std::function<void()> &action, const std::string &needle, const std::string &what) {
    try {
        action();
    } catch (const std::exception &exception) {
        const std::string message = exception.what();
        expect(message.find(needle) != std::string::npos, what + " (\"" + message + "\")");
        return;
    }
    expect(false, what + " (did not throw)");
}

const json *named(const json &list, const std::string &name) {
    for (const json &entry : list) {
        if (entry.value("name", std::string()) == name) return &entry;
    }
    return nullptr;
}

int selfTest() {
    using namespace shader_pack;
    const json base = json::parse(R"({
        "radiance": {"shader_pack": true, "display_name": "Base"},
        "requires_emission": false,
        "sharc": {"update_pass": "world", "resolve_comp": "sharc.comp"},
        "attributes": [
            {"name": "a.one", "type": "bool", "default_value": "render_pipeline.true", "define": "ONE"},
            {"name": "a.two", "type": "float_range:0.0-1.0", "default_value": "0.5", "define": "TWO"}
        ],
        "textures": [{"name": "tex", "type": "intermediate", "format": "R8G8B8A8_UNORM", "width": 4, "height": 4}],
        "execution": {"global_variables": [{"READY": false}],
                      "commands": [{"pass": "sky"}, {"if_else": {"condition": "READY", "then": [{"pass": "world"}]}}]},
        "execution_post": {"global_variables": [{"HOOK": false}],
                           "commands": [{"pass": "post_a"}, {"if_else": {"condition": "HOOK", "then": [{"pass": "post_b"}]}}]},
        "passes": [
            {"type": "full_screen", "name": "sky", "fragment": "sky.frag"},
            {"type": "ray_tracing", "name": "world", "rgen": "world.rgen"},
            {"type": "full_screen", "stage": "post_render", "name": "post_a", "fragment": "a.frag"},
            {"type": "full_screen", "stage": "post_render", "name": "post_b", "fragment": "b.frag"}
        ]
    })");

    // extends
    expect(!readExtends(base).has_value(), "no extends in a pack without it");
    {
        auto parsed = readExtends(json::parse(R"({"radiante": {"extends": "Radiante-PT@1"}})"));
        expect(parsed && parsed->id == "radiante-pt" && parsed->major == 1, "extends id@major read, id lowercased");
        checkExtendsBase(*parsed);
        expect(true, "radiante-pt@1 accepted");
    }
    expectThrows([] { readExtends(json::parse(R"({"radiante": {"extends": 3}})")); }, "must be a pack id",
                 "extends that is not a string");
    expectThrows([] { readExtends(json::parse(R"({"radiante": {"extends": "radiante-pt@x"}})")); }, "major version",
                 "extends with a bad version");
    expectThrows([] { checkExtendsBase({"other-pack", std::nullopt}); }, "unknown base pack", "unknown base refused");
    expectThrows([] { checkExtendsBase({"radiante-pt", 2}); }, "radiante-pt@1", "newer base major refused");

    // attributes, textures, manifest, other keys
    const json derived = json::parse(R"({
        "radiante": {"display_name": "Derived", "extends": "radiante-pt"},
        "requires_emission": true,
        "attributes": [
            {"name": "a.two", "default_value": "0.8"},
            {"name": "d.three", "type": "int", "default_value": "3", "define": "THREE"}
        ],
        "textures": [{"name": "tex2", "type": "intermediate", "format": "R8G8B8A8_UNORM", "width": 2, "height": 2}],
        "execution_post": {"global_variables": [{"HOOK": true}, {"MINE": 1}]},
        "passes": [
            {"type": "full_screen", "name": "pre_world", "fragment": "pre.frag", "before": "world"},
            {"type": "full_screen", "stage": "post_render", "name": "post_tail", "fragment": "tail.frag"},
            {"type": "full_screen", "stage": "post_render", "name": "post_mid", "fragment": "mid.frag", "after": "post_a"},
            {"name": "post_b", "fragment": "b2.frag"}
        ]
    })");
    const json merged = mergeConfigs(base, derived);
    expect(!merged.contains("radiance") && merged["radiante"]["display_name"] == "Derived",
           "manifest is the pack's own");
    expect(merged["requires_emission"] == true, "plain keys: the pack's value wins");
    expect(merged["attributes"].size() == 3, "attributes appended");
    expect((*named(merged["attributes"], "a.two"))["default_value"] == "0.8" &&
               (*named(merged["attributes"], "a.two"))["define"] == "TWO",
           "attribute of the same name merged field by field");
    expect(merged["textures"].size() == 2, "textures appended");
    expect(merged["sharc"]["update_pass"] == "world", "sharc kept from the base");

    const json &passes = merged["passes"];
    expect(passes.size() == 7, "passes added");
    expect(passes[1]["name"] == "pre_world" && passes[2]["name"] == "world", "before: placed in passes");
    expect(!named(passes, "pre_world")->contains("before"), "placement keys removed");
    expect((*named(passes, "post_b"))["fragment"] == "b2.frag" && (*named(passes, "post_b"))["stage"] == "post_render",
           "pass of the same name merged field by field");
    const json &rt = merged["execution"]["commands"];
    expect(rt[1]["if_else"]["then"][0]["pass"] == "pre_world" && rt[1]["if_else"]["then"][1]["pass"] == "world",
           "before: placed in the script, inside if_else");
    const json &post = merged["execution_post"]["commands"];
    expect(post[0]["pass"] == "post_a" && post[1]["pass"] == "post_mid", "after: placed in the post script");
    expect(post.back()["pass"] == "post_tail", "unplaced new pass run at the end of its stage");
    expect(merged["execution_post"]["global_variables"].size() == 2 &&
               merged["execution_post"]["global_variables"][0]["HOOK"] == true &&
               merged["execution_post"]["global_variables"][1]["MINE"] == 1,
           "global variables merged by name");

    // debug views: merged by name like attributes, so a pack keeps radiante-pt's and adds its own
    const json withViews = mergeConfigs(json::parse(R"({"passes": [], "debug_views": [
        {"name": "albedo", "define": "RADIANTE_DEBUG_VIEW", "value": 1},
        {"name": "normal", "define": "RADIANTE_DEBUG_VIEW", "value": 2}]})"),
                                        json::parse(R"({"debug_views": [{"name": "mine", "define": "MINE_VIEW"},
        {"name": "normal", "value": 9}]})"));
    expect(withViews["debug_views"].size() == 3 && withViews["debug_views"][2]["name"] == "mine" &&
               (*named(withViews["debug_views"], "normal"))["value"] == 9 &&
               (*named(withViews["debug_views"], "normal"))["define"] == "RADIANTE_DEBUG_VIEW",
           "debug views merged by name");

    // base step
    const json withBase = mergeConfigs(base, json::parse(R"({
        "execution": {"commands": [{"pass": "mine"}, {"base": true}]},
        "passes": [{"type": "full_screen", "name": "mine", "fragment": "m.frag"}]
    })"));
    const json &spliced = withBase["execution"]["commands"];
    expect(spliced.size() == 3 && spliced[0]["pass"] == "mine" && spliced[1]["pass"] == "sky",
           "{\"base\": true} replaced by the base's commands; the pack's script runs its own pass once");

    // errors
    expectThrows([&] { mergeConfigs(base, json::parse(R"({"passes": [{"type": "full_screen", "name": "x", "fragment": "x", "after": "nope"}]})")); },
                 "no pass named nope", "unknown anchor");
    expectThrows([&] { mergeConfigs(base, json::parse(R"({"passes": [{"name": "world", "after": "sky"}]})")); },
                 "keeps its place", "before/after on a replaced pass");
    expectThrows([&] { mergeConfigs(base, json::parse(R"({"passes": [{"type": "full_screen", "stage": "post_render", "name": "x", "fragment": "x", "after": "sky"}]})")); },
                 "different stages", "anchor in another stage");
    expectThrows([&] { mergeConfigs(base, json::parse(R"({"attributes": [{"name": "a"}, {"name": "a"}]})")); },
                 "listed twice", "duplicate names in the pack");
    expectThrows([&] { mergeConfigs(base, json::parse(R"({"attributes": [{"type": "bool"}]})")); },
                 "name must be", "entry without a name");

    // paths
    expect(safePackPath("hooks/sky.glsl") && safePackPath("lang/en_us.lang"), "ordinary paths are safe");
    expect(!safePackPath("../x") && !safePackPath("a/../../x") && !safePackPath("/etc/x") &&
               !safePackPath("C:\\x") && !safePackPath("a\\..\\..\\x") && !safePackPath(""),
           "escaping paths are refused");

    std::cout << (failures == 0 ? "all checks passed\n" : "checks failed\n");
    return failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char **argv) {
    if (argc == 2 && std::string(argv[1]) == "--self-test") return selfTest();
    if (argc == 4 && std::string(argv[1]) == "--extract") {
        std::string error;
        if (!shader_pack::extractPackZip(argv[2], argv[3], error)) {
            std::cout << "refused: " << error << "\n";
            return 1;
        }
        std::cout << "extracted " << argv[2] << "\n";
        return 0;
    }
    if (argc != 4) {
        std::cerr << "usage: shader_pack_stage <base pack folder> <pack folder> <output folder>\n"
                     "       shader_pack_stage --self-test\n";
        return 2;
    }
    try {
        const std::filesystem::path packRoot(argv[2]);
        std::ifstream stream(packRoot / "configs.json");
        const json config = json::parse(stream);
        const auto base = shader_pack::readExtends(config);
        if (!base.has_value()) {
            std::cerr << "the pack does not extend another\n";
            return 1;
        }
        shader_pack::checkExtendsBase(*base);
        shader_pack::stageExtendedPack(argv[1], packRoot, argv[3]);
        std::cout << "staged " << packRoot.string() << " on " << base->id << " in " << argv[3] << "\n";
    } catch (const std::exception &exception) {
        std::cerr << "error: " << exception.what() << "\n";
        return 1;
    }
    return 0;
}
