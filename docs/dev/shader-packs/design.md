# Shader packs: design

How shader packs work in Radiante today, and the contract they are moving to. "Today" is `mc/26.3` with the
`feature/shaderpacks` work; "planned" items are in the [roadmap](roadmap.md). The code wins where this page disagrees.

## 1. Moving parts

| Side | Where | Role |
| --- | --- | --- |
| Java | `client/pipeline/ShaderPackLibrary.java` | Finds packs in `radiante/shaderpacks/` (zips and folders), reads their manifest, checks the layout, crash marker (`.trying`) |
| Java | `client/pipeline/ShaderPackManifest.java` | Manifest checks without loading: `extends`, `api_version`, `requires.radiante` |
| Java | `client/gui/ShaderPacksScreen.java` | The screen: list, Use, Open Folder, Refresh, Copy Error |
| Java | `client/gui/ShaderPackTrial.java` | Watches a switch: rejected config, compile errors, failed rebuild, ten seconds of frames before a pack counts as good |
| Java | `client/gui/ShaderPackHotReload.java`, `client/pipeline/ShaderPackValidation.java` | Hot reload of folder packs; a pack checked (loaded and compiled) without being used (section 10) |
| Java | `client/command/RadianteCommands.java` | `/radiante pack validate`, `/radiante pack reload`, `/radiante debug view`; registered by each loader |
| Java | `client/pipeline/Pipeline.java` | The selected pack (`shader_pack_path` attribute), per-pack values, `getModuleAttributes` (values checked against the pack's types), runtime attributes |
| Native | `prism/render/modules/world/shader_pack/shader_pack_loader.cpp` | Reads `configs.json` into `ShaderPack`, rejects bad configs (with the reason in the log) |
| Native | `shader_pack/shader_pack_extends.cpp` | `extends`: stages the base and the pack, merges `configs.json` and `lang` (section 5); path checks for zips |
| Native | `shader_pack/shader_compile.cpp` | Compiles the pack's GLSL with shaderc; include paths, attribute defines |
| Native | `shader_pack/shader_pack.cpp`, `execution_script.cpp`, `runtime_resources.cpp` | Passes, execution scripts, textures and buffers, runtime attributes (`packParams`) |
| Native | `shader_pack/shader_pack_check.cpp` | Loads and compiles a pack apart from the running pipeline (section 10) |
| Build | `native/src/shader/CMakeLists.txt` | Zips Radiante PT with `shader/util/`, `src/common/` and the SHaRC headers into `radiante-pt.zip` |
| Build | `build.gradle`, `shaderPackExamples` | Zips the example packs (packs without `extends` are staged on Radiante PT) |
| Tools | `native/src/tools/` | `shader_pack_stage` (stage a pack offline, `--self-test`), `compile_pack.py` (compile a staged pack's shaders) |

A switch: the screen calls `Pipeline.selectShaderPack`, the background rebuild (`BackgroundRebuild`) builds the
pipeline, `ShaderPackTrial` reads the renderer's log; on failure the built-in pack is put back and the lines are shown.

## 2. Anatomy of a pack (today)

A zip or folder with `configs.json` at its top. A pack that extends Radiante PT holds only `configs.json`, `pack.png`,
`lang/` and the files it adds or replaces (usually `hooks/*.glsl`), section 5. A whole pack:

```
my-pack/
  configs.json          manifest, attributes, textures, passes, execution scripts
  pack.png              icon (optional)
  lang/en_us.lang ...   names of the attributes
  world/ pt/ common/    the pack's shaders (GLSL)
  hooks/                Radiante PT's hook points (section 5)
  util/ common/ extern/ renderer headers the pack must carry today (vertex fetch, uniforms, SHaRC)
```

Shaders are compiled with the pack's root as the include path. Today a pack carries its own copy of the renderer's
headers (`common/shared.hpp`, `util/vertex.glsl`, SHaRC): this is the coupling [phase 2](roadmap.md) removes.

## 3. Manifest

In `configs.json`, under `"radiante"`:

| Field | Today | Meaning |
| --- | --- | --- |
| `display_name` | yes | Name in the list |
| `version` | yes | The pack's own version |
| `author` | yes | Shown in the list |
| `license` | yes | SPDX id, shown in the list |
| `description` | yes | One line |
| `icon` | yes | Image in the pack, shown in the list |
| `api_version` | checked | The `radiante/vN` interface the pack is written for; 0 = built on Radiante PT's internals (every pack today). Radiante offers 0 until phase 2 |
| `extends` | yes | The pack this one builds on: `"radiante-pt"` or `"radiante-pt@1"` (section 5) |
| `requires` | `radiante` checked | `{"radiante": ">=1.4", "features": ["omm?", "ser?"]}`. `radiante`: `>=`, `>`, `=`, `<=`, `<` or a bare version (meaning `>=`) against the mod version. `features` (`?` = optional, the shader gets a define) is planned |
| `id` | planned | Stable id (lowercase), for settings files and future pack lists |
| `homepage` | planned | Link shown in the list |

A pack that extends an unknown base or another major of radiante-pt, asks for an `api_version` newer than Radiante
offers, or needs another Radiante version, is listed with that reason and cannot be chosen (`ShaderPackManifest`,
unit tested). The native loader refuses the same `extends` cases on its own, so a pack set by hand in `pipeline.yaml`
fails like any other bad pack. A required feature the GPU lacks: planned.

## 4. Versioning and compatibility

- **Public interface**: the `radiante/vN/` include headers, the manifest, the `configs.json` schema, the surface flags,
  the uniforms exposed through the headers, the named bindings. Everything else (`src/common/shared.hpp`,
  `shader/util/*`, pass internals of Radiante PT) is private and may change in any release.
- **Within a major version, append-only**: new accessors, flags, uniforms or `configs.json` keys may be added; nothing
  is removed, renamed or re-laid-out. Old fields are read through accessors, so the renderer can change their storage.
- **A breaking change is a new major version** (`radiante/v2`). Radiante keeps compiling `v1` packs for at least one
  minor release after `v2` ships, and the changelog says what packs must change.
- **`api_version: 0`** (today's packs, built on copies of the internals) is best effort: it works until the internals
  change. The Shader Packs screen says so.

## 5. Inheritance and hook points

Implemented in phase 1. The first example pack showed the problem: of its 113 files, 103 were verbatim copies of
Radiante PT and the renderer headers, and the four shading files it overrode each changed one to a few lines of a file
of hundreds. A copy breaks at the next change to Radiante PT.

### `extends`

A pack names its base in the manifest, `"radiante": {"extends": "radiante-pt@1"}` (`@1` optional), and holds only its
own files. Only radiante-pt can be a base today. `@N` is the major version of radiante-pt's pack interface (its hook
points; `BUILT_IN_BASE_MAJOR` in `shader_pack_extends.hpp`, `ShaderPackManifest.BASE_MAJOR` in Java), raised when a
hook's signature or meaning changes.

The loader (`shader_pack_loader.cpp`, `stageIfExtending`) extracts the pack and radiante-pt as usual, builds a staged
pack in `temp/shaders/world/ray_tracing/<pack>_<hash>_extended` (`shader_pack_extends.cpp`, `stageExtendedPack`) and
parses that like any pack. The folder name is stable, so the SPIR-V cache keeps its shaders.

| Part | Rule |
| --- | --- |
| Files | radiante-pt's, then the pack's over them; the same relative path is the pack's file. Symbolic links are skipped |
| `attributes`, `textures`, `buffers`, `passes`, `debug_views` | Merged by `name`. A pack entry with a base entry's name is merged into it field by field, the pack's fields winning (`{"name": "...wind", "default_value": "render_pipeline.false"}` changes only the default); a new name is appended. A name listed twice in the pack is an error |
| New passes | `"before": "<pass>"` or `"after": "<pass>"` (one of them, only on new passes, same stage as the anchor): placed there in `passes` and next to every run of the anchor in the stage's script, inside `if_else` and `while` too; an anchor the script never runs is an error. Without either: appended to `passes` and run at the end of the stage's script, unless the pack's own script already runs it. The two keys are removed before parsing |
| `execution`, `execution_post` | `global_variables` merged by variable name (the pack's start value wins). The pack's `commands`, when it gives any, replace the base's; a `{"base": true}` command in them, at any depth, stands for the base's commands. Scripts are merged before new passes are placed |
| `sharc` | Merged field by field |
| Manifest (`radiante`, `radiance`) | The pack's own; the base's is dropped |
| Any other key (`requires_emission`...) | The pack's value replaces the base's |
| `lang/*.lang` | The base's lines, then the pack's, per file name (case, `-` and `_` do not matter); the last line for a key wins, so the pack's names win |

Errors (unknown base, newer major, a bad anchor, an entry without a name, a pack over the limits) are thrown as
`extends: <where>: <what>` and reported like any other load error: the built-in pack is used and the Shader Packs
screen shows the line. Limits: 20000 files and 512 MB per pack, zipped or in a folder. Every pack's zip, not only
one that extends another, is extracted entry by entry (`extractPackZip`): an entry that leaves the pack's folder
(`..`, absolute, a drive letter, or a resolved path outside it), a symbolic link, an unreadable header, more bytes
actually written than the limit or than the entry's header says, or a bad CRC, and the zip is refused and what was
written removed.

### Hook points

Radiante PT calls a function from a small file under `hooks/` at each point; its own files are empty. A pack replaces
the file, keeping the include guard (`RADIANTE_HOOK_<NAME>_GLSL`). The signatures are part of `radiante-pt@1`.

| File | Function | Called | Notes |
| --- | --- | --- | --- |
| `hooks/surface.glsl` | `void hookSurface(inout SampledSurface surface, uint surfaceFlags, uint bounce)` | Default closest-hit shader, once per hit, after the material is sampled, before the denoiser guides, the mirror test and the lights read it | `surfaceFlags` is the packed vertex data: `isLeavesSurface(surfaceFlags)`, `isPlantSurface`, `isWaterSurface`... (`util/vertex.glsl`). Globals declared here are visible to `direct_light.glsl`, included right after it. Change `albedoValue` and `mat.albedo` together: `albedoValue` is the denoiser's albedo guide `hookTexelCentre(surface.uv, surface.worldPos)` gives the centre of the texture texel hit, on the face; moving `surface.worldPos` moves where shadow, light, bounce and reflection rays start; `g_hookViewDir` is the view the surface is shaded with (Pixel Light points it at the texel centre) |
| `hooks/direct_light.glsl` | `void hookDirectLight(SampledSurface surface, vec3 viewDir, vec3 lightDir, float NoL, inout vec3 lightBRDF)` | Every sun or moon sample at a surface, before its shadow ray | `lightBRDF` is the BRDF times the cosine. Behind an opaque surface it arrives as zero; left zero, no shadow ray is traced, as before; made non-zero, the surface is lit from behind (light through leaves, keeps opacity micromaps) |
| `hooks/water.glsl` | `void hookWaterAbsorption(inout vec3 extinction)` | `waterMediumExtinction`, for the water a ray crosses | Extinction per colour channel per block, before the density settings |
| `hooks/sky.glsl` | `void hookSky(inout vec3 radiance, vec3 direction)` | Overworld miss shader: the clear sky, before rain greys it and before the sun, moon, clouds and stars | Rays that bounce to the sky see it too, so it also colours sky light |
| `hooks/stars.glsl` | `float hookStars(inout vec3 radiance, vec3 direction)` | Overworld miss shader, sky the camera sees only (also through glass), not under water: after the rain grey, the sun and moon and their halos, before the cloud layer | Add the pack's night sky to `radiance`; return how much of Radiante PT's own stars (drawn after the frame) to keep, 0 to hide them where the pack draws its own |
| `hooks/clouds.glsl` | `vec4 hookCloudLayer(vec3 direction)` | Overworld miss shader, every ray that reaches the sky: after the rain grey, the sun and moon discs, halos and `hookStars` | Returns in-scattered light (rgb) and transmittance (a): the sky becomes `sky * a + rgb`, so the clouds cover the sun and moon and stay in rain; Radiante PT's stars are dimmed by `a` too |
| `hooks/cloud_shadow.glsl` | `float hookCloudShadow(vec3 worldPos, vec3 lightDir)` | Every sun or moon sample at a surface, after `hookDirectLight`, before its shadow ray | The light is multiplied by the result (0 to 1); `worldPos` is camera-relative (`worldUBO.cameraPos` makes it absolute). A sample left at zero traces no shadow ray |
| `hooks/fog.glsl` | `void hookFog(float cosTheta, inout vec3 scattering, inout vec3 extinction)` | `volumetricAirMedium`, the air the volumetric light marches through | Only with Volumetric Light on |
| `hooks/post_grade.glsl` | `void hookPostGrade(inout vec3 color, vec2 uv)` | Pass `post_hook_grade` (`post_render/post_hook_grade.frag`), after tone mapping, motion blur and depth of field, before the HUD | Runs only when the pack sets the `execution_post` global `RADIANTE_HOOK_POST_GRADE` to true; `post_hook_copy` then puts the picture in `post_motion` first. The picture is the sampler `hookPostGradeInput`. `common/pack_post.glsl` is included before it: live settings (`worldUBO`), weather (`skyUBO`) and the bloom (`radianteBloom`, below) |

**Radiante PT unchanged.** With its default hooks Radiante PT compiles to the same SPIR-V as before the hooks
existed (checked offline at its default settings with glslangValidator and `spirv-opt -O`, IDs canonicalized: every
ray tracing, compute, sky and froxel shader identical). The default `direct_light.glsl` defines
`RADIANTE_HOOK_DIRECT_LIGHT_DEFAULT`, which keeps `pt_direct.glsl`'s original early exit for light behind a surface;
a pack's file must not define it. In the same way the default `stars.glsl`, `clouds.glsl` and `cloud_shadow.glsl`
define `RADIANTE_HOOK_STARS_DEFAULT`, `RADIANTE_HOOK_CLOUDS_DEFAULT` and `RADIANTE_HOOK_CLOUD_SHADOW_DEFAULT`, and
their calls are compiled out; checked the same way when they were added (every shader's optimized SPIR-V identical
with and without them). The post shaders differ only by the new execution variable in their buffer, which
they do not read. The debug views (section 10) were checked the same way: with no view chosen every shader of
Radiante PT compiles to the same optimized SPIR-V as before them (the surface flags write in `pt_shade_default.glsl`
is compiled out); only `post_hook_grade.frag`, which runs for packs alone, gained the bloom sampler's declaration.

The set grows with requests; within `radiante-pt@1` hooks are only added (section 4). With `extends` and hooks the
example pack is a manifest with five attributes, five hook files, three lang files and an icon.

### World data for packs

`common/pack_world.glsl` gives hooks the weather and the biome at the camera. A hook includes it
(`#include "common/pack_world.glsl"`); radiante-pt never does, so it renders the same. It reads `skyUBO`, which every
hook point sees: the miss shader's sky, stars and clouds, the closest hit's surface, direct light and cloud shadow,
the fog and the water. `post_render/post_hook_grade.frag` declares the world and sky uniforms (set 1, bindings 0 and 1,
which the post stage always binds), so `hooks/post_grade.glsl` can use it too. Within `radiante-pt@1` functions are
only added.

| Function | Value | Iris equivalent | Java source |
| --- | --- | --- | --- |
| `float radianteRain()` | Rain, 0 to 1 | `rainStrength` | `1 - SkyRenderState.rainBrightness` (`skyUBO.rainGradient`) |
| `float radianteThunder()` | Thunder, 0 to 1 | `thunderStrength` | `ClientLevel.getThunderLevel(partialTick)` (`skyUBO.thunderGradient`) |
| `float radianteBiomeTemperature()` | The biome's base temperature, eased | `temperature` | `Biome.getBaseTemperature()` (`biomeClimate.x`) |
| `float radianteBiomeDownfall()` | The biome's downfall, eased | `rainfall` | the biome's climate `downfall`, read from `Biome.NETWORK_CODEC` once per biome (`biomeClimate.y`) |
| `float radianteBiomeSnowy()` | 0 to 1, 1 where snow falls at the camera's height | `biome_precipitation == 2` | `Biome.getPrecipitationAt(pos, seaLevel) == SNOW` (`biomeClimate.z`) |
| `float radianteBiomeDry()` | 0 to 1, 1 where nothing falls | `biome_precipitation == 0` | `getPrecipitationAt == NONE` (`biomeClimate.w`) |
| `float radianteBiomeRainy()` | `1 - snowy - dry` | `biome_precipitation == 1` | |
| `float radianteBiomePrecipitation()` | 0 nothing, 1 rain, 2 snow, eased | `smooth(biome_precipitation, ...)` | |
| `uint radianteBiomeId()` | Hash of the biome's id, 0 without a world | `biome` | `skyUBO.biomeId` |
| `float radianteBiomeIs(uint biome)` | How much the camera is in that biome, 0 to 1, crossfading on a change | `biome == BIOME_...` | `biomeId`, `previousBiomeId`, `biomeIdBlend` |

All of it is for the camera's position, not the point shaded. `BiomeClimate` (Java) samples the biome at the camera
each frame and eases the climate towards it with the biome haze's time constant (most of the way in 2.5 seconds), so
an effect tied to it fades across a border. A biome's id is the FNV-1a hash (offset basis `0x811C9DC5`, prime
`0x01000193`) of the UTF-8 bytes of its id, `minecraft:snowy_plains`; `pack_world.glsl` defines
`RADIANTE_BIOME_<NAME>` for the vanilla biomes, and `radianteBiomeHash` with `RADIANTE_BIOME_HASH_START` works one
out in GLSL for a modded biome (or by hand: `h = 0x811C9DC5; for b in id.encode(): h = ((h ^ b) * 0x01000193) &
0xFFFFFFFF`). For example `radianteBiomeIs(RADIANTE_BIOME_PALE_GARDEN)` fades from 0 to 1 as the camera walks into a
pale garden.

The values sit at the end of `SkyUBO` (`common/shared.hpp`, bytes 304 to 335): `biomeClimate` (vec4), then
`thunderGradient`, `biomeId`, `previousBiomeId`, `biomeIdBlend`. Radiante PT reads none of them; its optimized SPIR-V
differs from before only in the uniform block's declaration.

**Post render bindings**, for a pack's own post passes: set 1 holds the world (0) and sky (1) uniforms; set 3 holds
the frame's images at fixed bindings whatever a pass lists as inputs: 0 `out:ldr`, 1 `out:first_hit_depth`, 2 the HDR
image, 3 `out:motion_vector`, 4 `out:normal_roughness`, 5 the bloom. A pack's textures are in set 4 at their
`sampled_binding` (`post_motion`: 4). A pass that writes `out:ldr` must not sample set 3 binding 0: copy the picture to
`post_motion` first (`post_render/post_copy.frag` reads set 3 binding 0), as `post_hook_copy` and Inkwell's
`inkwell_copy` do. Bindings 120 and 121 of the runtime resources' set are Radiante PT's debug view image: a pack that
extends it must not use them.

### Post passes: live settings and bloom

`common/pack_post.glsl` is for a pack's post passes and `hooks/post_grade.glsl` (which `post_hook_grade.frag`
includes it for). It declares the world and sky uniforms, unless the pass defined `RADIANTE_POST_UNIFORMS_DECLARED`
after declaring them itself, so a runtime attribute's define (a read of `worldUBO.packParams`) works in a post pass as
in the ray tracing shaders. A runtime value is only known when the shader runs: it is tested with `if`, never `#if`.

It also gives the bloom, set 3 binding 5: the tone mapping module's own bloom pyramid (`bloom_down.frag`,
`bloom_up.frag`: the exposed HDR picture, five levels down to 1/32 and four back up), the finest combined level at
half the picture's size. The pyramid runs with Radiante's Bloom setting, or for a pack that sets the `execution_post`
global `RADIANTE_POST_BLOOM` to true (`ToneMappingModule::runsBloom`, read at each build); only the setting adds it
to the picture itself. Without either, binding 5 holds one black texel and nothing more is drawn, so Radiante PT costs
nothing extra. Its threshold, scatter and level weights are Radiante's bloom settings.

| Function | Value |
| --- | --- |
| `vec3 radianteBloom(vec2 uv)` | The bloom, linear, on the exposed scale before tone mapping, never negative |
| `vec3 radianteBloomDisplay(vec2 uv, float strength)` | `strength` of it tone mapped (`x / (1 + x)`) and encoded for the screen |
| `vec3 radianteAddBloom(vec3 color, vec2 uv, float strength)` | A display colour with that bloom added as light |

Lens flares read it at mirrored or stretched positions; chromatic aberration reads the picture
(`hookPostGradeInput`) per channel. The example pack does both, with live strengths.

### Checking a pack offline

`native/src/tools/shader_pack_stage` (a CMake target outside the default build) stages a pack as the loader does;
`--self-test` runs the merge rules' checks and `--extract` the zip extraction (`zip_safety_test.py` feeds it crafted
zips). `native/src/tools/compile_pack.py` compiles a staged pack's shaders with
glslangValidator and the defines the loader gives at default settings (attributes, runtime attributes as
`worldUBO.packParams` reads, debug view defines, pass defines, execution variables, SHaRC variants; `--debug-view`
compiles with a view chosen). In game, `/radiante pack validate` does the same with the real settings. See
[examples](../../../examples/shaderpacks/README.md#check-a-pack-without-the-game).

## 6. Public headers (planned, phase 2)

`#include <radiante/v1/...>`, from an include root the loader adds to every pack compile (`shader_compile.cpp`):

| Header | Contents |
| --- | --- |
| `bindings.glsl` | Every built-in set/binding as named declarations |
| `scene.glsl` | TLAS, instance and geometry lookup |
| `vertex.glsl` | `radianteFetchTriangle(...)`: positions, normal, UVs, colour, light, surface flags, over both the full and the compact chunk layouts |
| `surface.glsl` | Surface flag accessors: water, lava, glass, pane, leaves, plants, layer overlays, held item, rain, unlit, blend… |
| `uniforms.glsl` | Frozen v1 views of `WorldUBO`/`SkyUBO` and accessors (sun, moon and its phase, weather, wind, brightness, night vision) |
| `material.glsl` | `radianteSampleMaterial(...)`: albedo, normal, roughness, metal, emission, subsurface from LabPBR and converted Bedrock RTX packs |
| `payload.glsl`, `sharc.glsl`, `restir.glsl`, `omm.glsl`, `lod.glsl`, `debug.glsl` | Shared payloads, radiance cache, reservoirs, micromap state, Distant Horizons coverage, debug output |

Radiante PT moves onto these headers first; a header is only published once Radiante PT uses it.

## 7. Settings

Today every attribute of a pack is a setting, named through its `lang` files; `"runtime": true` attributes change
live through `worldUBO.packParams` (256 slots, `PRISM_PACK_PARAM_COUNT` in `common/shared.hpp`; Radiante PT takes
about 60, in the order it lists them, then a pack's own), the rest rebuild. They work in every stage: ray tracing,
compute and full-screen passes, the post passes included (section 5, post passes). Planned (phase 4), as extra
attribute keys:

- `group` (section), `order`, `advanced`, `description` (tooltip), `performance_impact` (`low`/`medium`/`high`),
  `step`, `visible_when` (another attribute's value), `requires_feature`.
- `presets` at the top: named sets of values shown as a Quality row.
- The settings screen builds a page per pack from these, with Radiante's own rows staying for Radiante PT.
- Renderer profiles (`RendererProfile`, `BedrockRtxProfile`) write Radiante PT's attribute names. With another pack
  they only touch attributes the pack declares as shared (`"profile_keys"`); today the type check in
  `Pipeline.getModuleAttributes` is the only guard.

## 8. Materials and opacity micromaps

- Per-material ids (planned, phase 5): a `materials` block maps block/entity tags or ids to the pack's own material
  ids (`"#minecraft:leaves": "foliage"`), carried per geometry so shaders can branch on them; the surface flags cover
  the common cases today (`isLeavesSurface`, `isPlantSurface`).
- **Foliage translucency and micromaps**: with opacity micromaps the GPU answers cut-out hits without the any-hit
  shader, so light cannot be tinted while passing through leaves. Two ways for a pack:
  1. **Light at the leaf** (works today, keeps micromaps): add the light arriving from behind when shading the leaf;
     the example pack does this in `pt/pt_direct.glsl`. Loses only coloured shadows on the ground.
  2. **Micromap opt-out per material** (planned): `"omm": "off"` for a material moves those quads to geometry without
     micromaps (as `bakeSection` already does for glass and animated sprites), so the pack's any-hit shader runs.
     Costs frame rate in forests; the setting says so.

## 9. Resources, passes and renderer features

- Today: `textures`, `buffers`, `passes` (`ray_tracing`, `compute`, `full_screen`, `render`) and `execution`
  scripts; SHaRC through `sharc.update_pass`/`resolve_comp`.
- Planned (phase 6): built-in resources by name; custom resources with generated declarations
  (`radiante/pack_resources.glsl`); declared outputs for the denoiser (the pack states which guide images it writes,
  Radiante picks NRD or DLSS-RR); ReSTIR reservoir helpers; optional extra outputs (segmentation, separate
  direct/indirect) created only when a pack asks; Distant Horizons LOD and entity/particle data.

## 10. Development loop

The Shader Packs screen shows a failed pack's errors with file and line under the list (Copy Error). Besides:

**Checking a pack** (`shader_pack_check.cpp`, `Pipeline.validateShaderPackNative`, `ShaderPackValidation`): the pack
is loaded and every shader a build of it would compile is compiled, for both stages, with the ray tracing module's
current attributes and this device's features (micromaps, SER, SHaRC), on a Java thread of its own while frames are
drawn. It never touches the running pipeline: no fallback to Radiante PT, zips extracted and packs staged under
`temp/shaders/world/ray_tracing/check/` (a build's folders are never rewritten under it), no shader modules, and
compile errors are not written to the error log (`gpu::Shader::QuietErrors`), where `ShaderPackTrial` would take them
for the running pack's. The SPIR-V goes to the shared cache: a folder pack that does not extend another reads its
files in place, so the build that follows finds them all; one that extends Radiante PT is staged elsewhere for the
check, so the build compiles again what changed. The report: the loader's reason, or each failing shader's path and
compiler lines.

**Hot reload** (`ShaderPackHotReload`): the active pack, when it is a folder in `radiante/shaderpacks/`, is looked at
once a second (names, sizes and modification times; folders starting with a dot skipped; at most 20000 files). A
change that has settled for a second is checked; a pack that passes is built through the background rebuild
(`Pipeline.requestRebuild`, `BackgroundRebuild.apply`: prewarm while drawing, then a short build), so the picture
changes without a wait screen. A pack that fails is not built: the running pipeline stays, a popup gives the first
error, and the report goes to the Shader Packs screen (`ShaderPackTrial.reportCheckFailure`) and the game log.

**Commands** (`RadianteCommands`, registered as client commands by each loader): `/radiante pack validate [name]`
checks a pack in the folder (the active one without a name) without switching and prints the report in the chat;
`/radiante pack reload` checks and reloads the active folder pack now; `/radiante debug view [view|off]`.

**Debug views**: a pack lists them in `configs.json`:

```json
"debug_views": [{"name": "wetness", "define": "MY_DEBUG_WET", "value": 1}]
```

Every view's define is in the shaders' defines as 0, and the chosen view's as its `value` (1 when left out), and in
the numbers the execution scripts and texture sizes read, so a pack can run passes or size images only while a view
is chosen. The view is an attribute of the ray tracing module that Java adds to the build only
(`render_pipeline.module.ray_tracing.attribute.debug_view`), never saved. `debug_views` merge by name under
`extends`, so a pack keeps Radiante PT's views and adds its own.

Radiante PT's views share `RADIANTE_DEBUG_VIEW` (`common/debug_view.glsl`): `albedo` 1, `normal` 2, `roughness` 3,
`metallic` 4, `emission` 5, `depth` 6, `motion` 7, `flags` 8. While one is chosen, the compute pass
`debug_view_capture` (after the world pass) turns the guide images the ray generation shader wrote into display
colours in `radiante_debug_view` (render resolution; one texel while no view is chosen, sized by
`if(RADIANTE_DEBUG_VIEW, RENDER_WIDTH, 1)`), the closest-hit shader writes the surface flags there itself, and
`post_debug_view` draws it last in the post stage, after any grade. Planned: SHaRC heat map (SHaRC's own
`sharc_debug_mode` meanwhile) and micromap coverage.

## 11. Safety

Packs are code that runs on the GPU. Radiante does not sandbox shaders; it limits the damage:

- A pack that fails to load or compile is replaced by Radiante PT before the player sees a broken frame.
- A pack that hangs or crashes the GPU (`VK_ERROR_DEVICE_LOST`) ends the game, so a marker file
  (`radiante/shaderpacks/.trying`) makes the next start go back to Radiante PT and say which pack it was.
- Packs are read from `radiante/shaderpacks/` only. Zips are extracted entry by entry with their own checks (section
  5); a folder pack that extends another has the same limits and its symbolic links are
  skipped. Shaders get no file or network access (GLSL has none).
- Players should get packs from places they trust, as with any mod; the screen shows author and licence.
