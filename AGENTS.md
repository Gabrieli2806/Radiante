# AGENTS.md

Notes for AI coding agents (and new contributors) working on Radiante. Read this before changing anything.
For how the renderer works, see [docs/dev/architecture.md](docs/dev/architecture.md); the code wins when the two disagree.
The contributor guide for people is [CONTRIBUTING.md](CONTRIBUTING.md) (Spanish: `CONTRIBUTING.es.md`), with more in
[docs/dev/](docs/dev/README.md) (building, testing, architecture); keep them consistent with this file.

## What this is

Radiante is a ray tracing mod for Minecraft Java. The Java side (`common/`, plus the `fabric/`, `neoforge/` and
`forge/` loaders) collects the world and the settings; PrismRT, the native C++/Vulkan renderer in `native/src/prism`,
traces it on Minecraft's own Vulkan device. The path tracer itself is PrismRT's built-in shader pack, `radiante-pt`,
in `native/src/shader/world/ray_tracing/internal/radiante-pt/`.

## Map of the code

| Where | What |
|---|---|
| `common/src/main/java/com/g2806/radiante/client/pipeline/Pipeline.java` | The pipeline: its modules and their values, what the settings read and set, which preset to use, building it, and the `native` methods |
| `common/.../client/pipeline/` beside it | `PresetAssembly` (each preset's modules and connections), `PipelineStore` (`pipeline.yaml`, each pack's settings file, their migrations), `ModuleAttributes` (a module's attributes as the shader pack declares them), `NativePipelineParams` (the layout the renderer reads), `RtAttributes` (the ray tracing attributes' names) |
| `common/.../client/bedrock/` | Bedrock packs (converter, fog, lighting) and `BedrockRtxValues`, the Bedrock RTX values that are radiante-pt's and tone mapping's defaults (a test checks they agree) |
| `common/.../client/pipeline/ShaderPack*.java` | Shader packs in `radiante/shaderpacks/`: manifest, library, settings page keys, validation |
| `common/.../client/gui/` | Settings screen (`RadianteOptionsScreen`, on top of `SettingsChoices`, what is chosen until Apply and applying it, and `SettingsRows`, each section's rows; `SettingsLayout`, `AdvancedOptions`, `Tunable`), Shader Packs screen and a pack's settings page (`ShaderPacksScreen`, `PackSettingsPage`), toasts, background rebuild, start-up warm-up |
| `common/.../client/render/` | Chunk and entity collection, point lights (`BedrockPointLights`), emission (`EmissionTiles`), held light, biome ambiance |
| `common/.../client/benchmark/` | `/radiante benchmark`: the scene it builds (`BenchmarkScene`), the camera's shots (`BenchmarkPath`), the run and its report (`Benchmark`) |
| `common/.../client/option/Options.java` | Plain options saved in `radiante/options.properties` |
| `common/src/main/resources/assets/radiante/lang/` | Texts: `en_us`, `es_es`, `zh_cn` |
| `native/src/prism/` | Renderer: `gpu/`, `render/` (modules, framework, pipeline), `middleware/` (JNI, one file per Java class) |
| `native/src/shader/world/ray_tracing/internal/radiante-pt/` | The path tracer: `configs.json` (attributes, textures, passes), `world/`, `pt/`, `common/`, and `hooks/`, the hook points shader packs replace |
| `native/src/shader/include/radiante/v1/` | The public headers shader packs include (`#include <radiante/v1/surface.glsl>`; `uniforms.glsl`, the per-frame uniforms under names of their own), the same for every pack on this Radiante. Written from scratch, nothing copied, so packs on them can keep their own licence (`LICENSE-EXCEPTION.md`); within v1 functions are only added, never renamed, removed or changed |
| `examples/shaderpacks/` | Example packs built on radiante-pt (`./gradlew shaderPackExamples`); the design is `docs/dev/shader-packs.md`, the author guide `docs/en/shader-packs.md`, the roadmap issue #55 |
| `docs/en`, `docs/es` | Player documentation (same pages in both languages), one page per settings section in `settings/` (`opciones/`) |
| `docs/dev` | Developer documentation, English only: building, testing, architecture, shader pack design |
| `changelog.md` | What changed per version (open work is in GitHub Issues) |

## Building

- Java: `./gradlew build` (JDK 25). `./gradlew :fabric:runClient` starts a dev client in `run-fabric/`; NeoForge and
  Forge run in `run-neoforge/` and `run-forge/`. Worlds, resource and shader packs, settings and servers live once in
  `run-shared/` (linked or copied into each by `shareRun` in the root `build.gradle`); mods and configs stay per loader.
  Their screenshots land in the repo's `screenshots/`, ignored by git; only its subfolders (`art/`, `promo/`...) are tracked.
- Native: `native/build-windows.bat` (Linux: `native/build-linux.sh`). It installs `prismrt.dll` and the shader pack
  zip into `common/src/main/resources/radiante-native/`, which is gitignored.
- **Any change under `native/src/shader` or `native/src/prism` needs the native build**, and so does every branch
  switch, since the native output is not in git. Check the DLL's and `radiante-pt.zip`'s modification time afterwards:
  if the clock jumped back, MSBuild skips the edits and a clean build is needed.
- From a git worktree the path gets too long for the FidelityFX shader compiler: map the worktree to a short path
  (`subst W: <worktree>`, or a junction under `C:\rtb\`) and set `RADIANTE_BUILD_DIR` to a separate build folder.
- `./gradlew build` regenerates headers in `native/include/`; revert them before committing unless they are part of
  the change.

## Testing

- Never touch the user's worlds. Test only in worlds whose name starts with `Radiante` (for example
  `RadianteParityA`, `RadianteRealisticTerrain`); copies get a distinct level name; never delete a copy while a client
  is running.
- Dev scripts drive the client through `DevAutomation` (`RADIANTE_DEV_PRESCRIPT`); join with `testworld=`, not
  `join=` (the experimental-features prompt hangs `join=`).
- Settings tests write to the dev `radiante/` files (`options.properties`, `pipeline.yaml`, `radiante-pt.zip.txt`) and
  `options.txt`: back them up first and restore them after.
- Parity and visual runs need SHaRC on (`use_sharc`), the player's default; without it indirect light reads low.
- Do not send keys or commands to a client the user is playing in. Ask before launching a client while one is
  running, unless told extra dev clients are fine.
- Bedrock Edition: input only in worlds named `RadianteRef*`, through a chat verified open on a screenshot (the first
  key after a focus change is dropped; never press Enter blindly).
- Reproduce a visual bug before fixing it, and compare before/after screenshots of the same view. Say plainly what
  was not checked in game.

## Conventions

- Commit messages: a short lowercase subject `area: what changed`, a body saying why when it is not obvious.
  **No `Co-Authored-By` or any other AI attribution lines.**
- Comments explain why, in full sentences, in the style of the surrounding code.
- Every player-facing text goes in all three lang files with the same keys, the same `%s` placeholders and the same
  `\n` line breaks. Tooltips stay short: what it does, and what raising or lowering it does.
- A new setting usually needs: the attribute in `configs.json` (`"runtime": true` when it can change without a
  rebuild), a `Tunable` or a row in `RadianteOptionsScreen`, `OptionImpact`, the three lang files, the settings page in
  `docs/en` and `docs/es`, and a `changelog.md` line.
- radiante-pt's defaults are the Bedrock RTX look: a change to one of them goes in `configs.json` (or
  `tone_mapping.yaml`) and in `BedrockRtxValues`. Other looks are shader packs, not settings.
- The hook files and `common/pack_*.glsl` in radiante-pt are the shader pack interface (`radiante-pt@1`): keep their
  signatures, and with radiante-pt's own empty hooks it must compile to the same shaders. Check a pack change offline
  with `shader_pack_stage` and `native/src/tools/compile_pack.py` (`examples/shaderpacks/README.md`).
- `WorldUBO` and `SkyUBO` (`common/shared.hpp`) only grow at their end: `radiante/v1/uniforms.glsl` describes them to
  packs, and the native build stops when the two lay out differently (`native/src/tools/check_v1_layout.py`). A
  member packs should read gets its twin at the end of `RadianteWorld` or `RadianteSky`.

## Bugs and ideas

- Bugs and feature ideas live in GitHub Issues (`gh issue list`, `gh issue view <n>`), with labels (`bug`, `enhancement`,
  `visual`, `lighting`, `noise`, `performance`, `stability`, `compatibility`, `needs-repro`, `roadmap`). The big
  directions are the issues labelled `roadmap`; there is no roadmap file. Check the open issues before starting on a bug, and put what you found in the issue.
- Say `fixes #<n>` in a commit message only when the fix is verified; leave the issue open otherwise and comment what
  was tried.

## Branches and releases

- Every Minecraft version has its own branch, named after it: `mc/26.2`, `mc/26.3` (the default branch for now) and
  `mc/26.4`. When 26.4 is stable, `mc/26.4` becomes the default branch. Work on `mc/26.3` first, then port: merge it into
  the other version branches,
  adapt the API differences, check the mixins against that version's jar (wrong mixin targets do not fail the
  compile), rebuild native, build.
- **Changing `mod_version` in `gradle.properties` and pushing publishes a release** to GitHub, Modrinth and
  CurseForge through CI (tag `v<mod_version>+<minecraft_version>`). Only do that when asked.
