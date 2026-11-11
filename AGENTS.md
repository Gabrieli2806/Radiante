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
| `common/src/main/java/com/g2806/radiante/client/pipeline/Pipeline.java` | The pipeline itself: its modules and connections, which preset to use, storing it, and the `native` methods (they stay here: their JNI names carry the class, and `pipeline.yaml` names its nested storage classes) |
| `common/.../client/pipeline/Pipeline*.java` | `PipelineSettings` (what the settings read and set: DLSS mode, clouds, fog, any attribute of the pack or of a module), `PipelineBuild` (building it, the warm-up, the values pushed live, falling back to a pipeline this machine can run), `PipelineShaderPack` (the pack the ray tracing module uses, and where its settings file is) |
| `common/.../client/pipeline/` beside it | `PresetAssembly` (each preset's modules and connections), `PipelineStore` (`pipeline.yaml`, each pack's settings file, their migrations), `ModuleAttributes` (a module's attributes as the shader pack declares them), `NativePipelineParams` (the layout the renderer reads), `RtAttributes` (the ray tracing attributes' names) |
| `common/.../client/bedrock/` | Bedrock packs (converter, fog, lighting) and `BedrockRtxValues`, the Bedrock RTX values that are radiante-pt's and tone mapping's defaults (a test checks they agree) |
| `common/.../client/pipeline/ShaderPack*.java` | Shader packs in `radiante/shaderpacks/`: manifest, library, settings page keys, validation |
| `common/.../client/gui/` | Settings screen (`RadianteOptionsScreen`, on top of `SettingsChoices`, what is chosen until Apply and applying it, and `SettingsRows`, each section's rows; `SettingsLayout`, `AdvancedOptions`, `Tunable`), Shader Packs and Share Settings (`ShaderPacksScreen`, `ShareSettingsScreen`, both a `PanelScreen`: sidebar, list, panel), a pack's settings page (`PackSettingsPage`), the shared colours and measures (`Theme`), toasts, background rebuild, start-up warm-up |
| `common/.../client/render/` | Chunk collection (`ChunkManager`, with what kind of block a block is and how it sways in `BlockKinds`); the haze and water of each biome (`BiomeAmbiance`, `BiomeWater`); what is traced as entities: `EntityManager` (a frame's collection), `EntityBlockEntities`, `EntityWeather` (rain, snow, vanilla clouds), `EntityHands` (first person and VR hands, held lights), `EntityUpload` (handing them to the renderer); point lights (`BedrockPointLights`), emission (`EmissionTiles`), held light, biome ambiance |
| `common/.../client/option/Options.java` | Plain options saved in `radiante/options.properties` |
| `common/.../client/option/SettingsFile.java` | How every settings file is written (whole, then moved over the old one) and how one that cannot be read is kept as `<name>.bak` |
| `common/.../compat/VersionCompat.java` | Where Minecraft's API differs between the versions Radiante is built for: one small static method per differing call, with the same signatures on every version branch and each branch's own bodies. Not `client/compat/`, which is for other mods |
| `common/src/main/resources/assets/radiante/lang/` | Texts: `en_us`, `es_es`, `zh_cn` |
| `native/src/prism/` | Renderer: `gpu/`, `render/` (modules, framework, pipeline), `middleware/` (JNI, one file per Java class) |
| `native/src/shader/world/ray_tracing/internal/radiante-pt/` | The path tracer: `configs.json` (attributes, textures, passes), `world/` (the ray generation shader's `main()` is one file a stage in `world/main/`), `pt/`, `common/`, and `hooks/`, the hook points shader packs replace |
| `native/src/prism/render/chunks*.cpp`, `chunk_*.cpp` | The sections: `chunks.cpp` (the grid and its slots), `chunk_geometry.cpp` (packing a section for the GPU), `chunk_build_scheduler.cpp` (building structures in batches), `chunks_wind.cpp` (the wind pass); what they share is in `chunks_internal.hpp` |
| `native/src/prism/render/modules/world/ray_tracing/` | The ray tracing module: `ray_tracing_module.cpp` (set-up and build steps), `ray_tracing_passes.cpp`, `ray_tracing_frames.cpp`; what they share is in `ray_tracing_internal.hpp` |
| `native/src/prism/render/modules/world/upscaler_common.hpp` | What the upscaler modules do the same way: when to drop history, taking or making their images at the sizes they adopt or choose, the helper pass, the three guide upscales, the stretched copy. A new upscaler module uses these rather than writing its own |
| `native/src/prism/render/entities.cpp`, `entity_geometry.cpp` | Entities: reading a task as Java sends it and turning it into triangles (`entity_geometry.cpp`), and the structures built from them (`entities.cpp`) |
| `native/src/prism/render/modules/world/post_render/` | The post render module: `post_render_module.cpp` (set-up and its own resources), `post_render_passes.cpp` (the shader pack's passes), `post_render_frames.cpp` (a frame's recording); what they share is in `post_render_internal.hpp` |
| `native/include/` | The JNI headers `javac` generates from the Java classes with `native` methods; each middleware file includes its own, so a signature that differs between Java and C++ does not compile |
| `native/extern/` | Third-party libraries, only what the build reads; `native/extern/README.md` lists versions, what was left out and what was changed here |
| `native/src/tools/` | Offline checks: `check_shaders.py`, `check_example_packs.py`, `check_v1_layout.py`, `compile_pack.py`, `shader_pack_stage` and the small C++ tests (`native_tests` target, `ctest`) |
| `native/src/shader/include/radiante/v1/` | The public headers shader packs include (`#include <radiante/v1/surface.glsl>`; `uniforms.glsl`, the per-frame uniforms under names of their own), the same for every pack on this Radiante. Written from scratch, nothing copied, so packs on them can keep their own licence (`LICENSE-EXCEPTION.md`); within v1 functions are only added, never renamed, removed or changed |
| `examples/shaderpacks/` | Example packs built on radiante-pt (`./gradlew shaderPackExamples`); the design is `docs/dev/shader-packs.md`, the author guide `docs/en/shader-packs.md`, the roadmap issue #55 |
| `docs/en`, `docs/es` | Player documentation (same pages in both languages), one page per settings section in `settings/` (`opciones/`) |
| `docs/dev` | Developer documentation, English only: building, testing, architecture, shader pack design |
| `dev/smoke/` | The visual smoke test (`smoke.py`, `scenes.json`), the measurement of what settings cost on the GPU (`bench.py`, `bench.json`) and the settings sweep, which finds settings that change nothing in the picture (`settings.py`, `settings.json`) |
| `changelog.md` | What changed per version (open work is in GitHub Issues) |

## Building

- Java: `./gradlew build` (JDK 25). `./gradlew :fabric:runClient` starts a dev client in `run-fabric/`; NeoForge and
  Forge run in `run-neoforge/` and `run-forge/`. Worlds, resource and shader packs, settings and servers live once in
  `run-shared/` (linked or copied into each by `shareRun` in the root `build.gradle`); mods and configs stay per loader.
  Their screenshots land in the repo's `screenshots/`, ignored by git; only its subfolders (`art/`, `promo/`...) are tracked.
- `native/setup-windows.ps1` (Linux: `native/setup-linux.sh`) says what the machine lacks for the build; with
  `-Install` the Windows one fetches it.
- Native: `native/build-windows.bat` (Linux: `native/build-linux.sh`). It installs `prismrt.dll` and the shader pack
  zip into `common/src/main/resources/radiante-native/`, which is gitignored.
- **Any change under `native/src/shader` or `native/src/prism` needs the native build**, and so does every branch
  switch, since the native output is not in git. Check the DLL's and `radiante-pt.zip`'s modification time afterwards:
  if the clock jumped back, MSBuild skips the edits and a clean build is needed.
- From a git worktree the path gets too long for the FidelityFX shader compiler: map the worktree to a short path
  (`subst W: <worktree>`, or a junction under `C:\rtb\`) and set `RADIANTE_BUILD_DIR` to a separate build folder.
- `./gradlew build` regenerates the headers in `native/include/`, which the middleware includes. A change to a
  `native` method changes its header: commit the header with it and change the C++ function to match, or the native
  build stops. Revert a header that changed without such a change.
- `-DPRISM_ENABLE_NRD=OFF` builds the renderer without the NRD module (its pipelines are then not offered). DLSS is
  not optional: NGX is part of the device set-up and of frame generation. Its libraries are not in the repository;
  the setup scripts and the CI fetch the pinned version, and CMake fetches them itself when `native/extern/DLSS/lib`
  is missing.

## Testing

- After any change under `native/src/shader`, run `python native/src/tools/check_shaders.py`: it compiles Radiante PT
  under every compiled-in setting and debug view without the game (under a minute; the CI runs it too). A shader that
  compiles at the defaults can still fail with one setting off.
- After a change to a hook, to a file in radiante-pt's `common/` or to an example pack, run
  `python native/src/tools/check_example_packs.py --stage <build dir>/src/tools/Release/shader_pack_stage.exe`
  (the `shader_pack_stage` target of the native build; no `Release/` on Linux): it stages every pack in
  `examples/shaderpacks` on radiante-pt as the loader does and compiles it. The CI runs it.
- `./gradlew :common:test` runs the Java tests, which need no game. Besides the pure logic they keep hand-kept lists
  in step: `MixinTargetsTest` (every method, field and class a mixin names exists in this Minecraft),
  `ModuleLayoutTest` (a module's YAML image lists against its C++ `inputImageNum`/`outputImageNum`),
  `PresetAssemblyTest` (every preset assembled from the modules' YAML holds together),
  `TunableDefaultsTest` (a slider's default and range against its attribute), `StagedOptionsTest` (option names
  the settings screen stages as text), `LangFilesTest`, `BedrockRtxValuesTest` (the `BedrockRtxValues` table
  against radiante-pt's defaults in `configs.json` and `tone_mapping.yaml`). `SettingsCodeTest` covers the shared
  settings codes. When one fails after your change, the change is incomplete: fix the other side, do not loosen
  the test.
- The native tests: `cmake --build <build dir> --config Release --target native_tests`, then
  `ctest --test-dir <build dir> -C Release --output-on-failure`.
- `python dev/smoke/smoke.py` is the visual smoke test: a client walked through the showcase gallery, with
  automatic checks and a report of pictures and frame times in `build/smoke/` (`docs/dev/testing.md`). Run it
  before a release and after a port; add a scene to `dev/smoke/scenes.json` for a visual bug once it is fixed.
- Measure a performance change with `python dev/smoke/bench.py`, with nothing else using the GPU (check
  `nvidia-smi`): its last variant repeats the default, and a run where that differs from the first is not a
  measurement. Say the numbers, and what the change does to the picture.
- `python dev/smoke/settings.py` is the settings sweep: for each setting listed per view in
  `dev/smoke/settings.json` it compares the picture with the setting changed against the unchanged view before and
  after, and reports the settings that do nothing there (`build/settings-sweep/<time>/report.md`; about an hour,
  `--only <view>` for less). Run it after removing or rewiring settings; it reports, it does not fail.
- A scripted run switches pipelines with `pipe=preset:<key>`, the key being the translation key
  (`render_pipeline.preset.rt_nrd_fsr`, `rt_nrd_xess`, `rt_nrd`, `rt_dlss`, `rt_nrd_dlss`), not the enum's
  name: an unknown key falls back to the best pipeline without saying so. The GPU times of `fps=` name the
  passes that ran (`Nrd`, `XessSr`, `FSRUpscaler`, `DLSS`): check them. XeSS needs its runtime, which a
  development folder gets with `download=xess` and a restart.
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
- An API difference between Minecraft versions goes in `VersionCompat`, not inline: a static method named for what
  it does, with a line saying what differs, added to every version branch (`mc/26.2`, `mc/26.3`, `mc/26.4`) with
  the same signature and that version's body. The class that calls it then stays the same file on every branch and
  merges without a conflict. Mixins and accessors are a version's own and stay as they are. A method called per
  vertex, entity or frame allocates nothing and uses no boxing, lambda or reflection; what cannot be written that
  way stays inline.
- Every player-facing text goes in all three lang files with the same keys, the same `%s` placeholders and the same
  `\n` line breaks. Tooltips stay short: what it does, and what raising or lowering it does.
- A new setting of the shader pack needs: the attribute in `configs.json` (`"runtime": true` when it can change
  without a rebuild); for a slider a `Tunable` (default and range in steps, checked against the attribute by
  `TunableDefaultsTest`) and its row in `SettingsRows`; for a switch its name in `RtAttributes`, then in
  `SettingsChoices` the `pending*` field, its line in `carryOver`, its default in `resetToDefaults` and its write
  in the apply method, and the row in `SettingsRows`. Either kind: `OptionImpact`, the dependency map and the
  blocked-reason `switch` in `RadianteOptionsScreen` when another setting turns it off, the three lang files
  (label and `.tooltip`), the settings page in `docs/en` and `docs/es`, and a `changelog.md` line.
- A new plain option needs: the field in `Options` and its row in `Options.ENTRIES`, its default in
  `SettingsChoices.resetToDefaults`, the row in `SettingsRows` (staged under the field's exact name, which
  `StagedOptionsTest` looks up), `SettingsCode.OPTIONS` when a shared settings code should carry it, and the same
  texts and docs.
- A new render module needs: the C++ class (`NAME`, `inputImageNum`, `outputImageNum`), its `registerWorldModule`
  line in `pipeline.cpp`, its YAML in `common/src/main/resources/modules/` with the same name and as many images
  (`ModuleLayoutTest`), its name constant in `Pipeline.java` and its place in `PresetAssembly`.
- The fields of `Options` are `volatile`, and the native `Options` (`renderer.hpp`) is made of atomics: the
  settings screen writes them while the render thread and the builders read them. A new option field is declared
  the same way. Two values that must change together do not belong in two such fields.
- Radiante's screens take their shared colours and measures from `gui/Theme`; a screen keeps only its own.
- A shader helper with no bindings that more than one file needs goes in `native/src/shader/util/` once
  (`util/numeric.glsl` has `finiteFloat`, `finiteVec3` and `luminance709`), not in each file.
- What assembling a pipeline asks of the renderer goes through `Pipeline.host` (`Pipeline.Host`), not a `native`
  method called directly: a test puts its own answers there, which is how the presets are checked without a game.
- Another mod is asked for with `OtherMods.has(<one of its classes>)` before anything of it is touched.
- A file that passes about a thousand lines is usually two things: give the second a file of its own (as
  `PipelineSettings`, `EntityWeather`, `chunk_geometry.cpp` were), with what both need in an `_internal.hpp`
  on the native side. Two files that carry the same code get one copy both call (`PanelScreen`,
  `upscaler_common.hpp`, `util/numeric.glsl`).
- Settings files are written with `SettingsFile.write`, never with `Files.writeString`: a crash in the middle of a
  save must leave the old file. Code that gives up on a file it cannot read calls `SettingsFile.keepDamaged` first.
- A command or tool that changes the world or moves the player (`/radiante showcase`, `/radiante biome`) checks that
  the world allows commands.
- `PBRVertex` (`common/shared.hpp`) is written by Java at fixed offsets (`PBRVertexWriter.OFF_*`): move a member in
  both, and in the `static_assert`s under the struct.
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
  adapt the API differences (in that branch's `VersionCompat`, the one file expected to conflict), run
  `./gradlew :common:test` (`MixinTargetsTest` looks the mixins' targets up in that version's jar, which the
  compile does not; it checks names, not descriptors, so a client start is still the last word), rebuild native,
  build.
- **Changing `mod_version` in `gradle.properties` and pushing publishes a release** to GitHub, Modrinth and
  CurseForge through CI (tag `v<mod_version>+<minecraft_version>`). Only do that when asked.
