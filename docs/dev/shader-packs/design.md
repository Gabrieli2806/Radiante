# Shader packs: design

How shader packs work in Radiante today, and the contract they are moving to. "Today" is `mc/26.3` with the
`feature/shaderpacks` work; "planned" items are in the [roadmap](roadmap.md). The code wins where this page disagrees.

## 1. Moving parts

| Side | Where | Role |
| --- | --- | --- |
| Java | `client/pipeline/ShaderPackLibrary.java` | Finds packs in `radiante/shaderpacks/` (zips and folders), reads their manifest, checks the layout, crash marker (`.trying`) |
| Java | `client/gui/ShaderPacksScreen.java` | The screen: list, Use, Open Folder, Refresh, Copy Error |
| Java | `client/gui/ShaderPackTrial.java` | Watches a switch: rejected config, compile errors, failed rebuild, ten seconds of frames before a pack counts as good |
| Java | `client/pipeline/Pipeline.java` | The selected pack (`shader_pack_path` attribute), per-pack values, `getModuleAttributes` (values checked against the pack's types), runtime attributes |
| Native | `prism/render/modules/world/shader_pack/shader_pack_loader.cpp` | Reads `configs.json` into `ShaderPack`, rejects bad configs (with the reason in the log) |
| Native | `shader_pack/shader_compile.cpp` | Compiles the pack's GLSL with shaderc; include paths, attribute defines |
| Native | `shader_pack/shader_pack.cpp`, `execution_script.cpp`, `runtime_resources.cpp` | Passes, execution scripts, textures and buffers, runtime attributes (`packParams`) |
| Build | `native/src/shader/CMakeLists.txt` | Zips Radiante PT with `shader/util/`, `src/common/` and the SHaRC headers into `radiante-pt.zip` |
| Build | `build.gradle`, `shaderPackExamples` | Builds the example packs on top of Radiante PT |

A switch: the screen calls `Pipeline.selectShaderPack`, the background rebuild (`BackgroundRebuild`) builds the
pipeline, `ShaderPackTrial` reads the renderer's log; on failure the built-in pack is put back and the lines are shown.

## 2. Anatomy of a pack (today)

A zip or folder with `configs.json` at its top:

```
my-pack/
  configs.json          manifest, attributes, textures, passes, execution scripts
  pack.png              icon (optional)
  lang/en_us.lang ...   names of the attributes
  world/ pt/ common/    the pack's shaders (GLSL)
  util/ common/ extern/ renderer headers the pack must carry today (vertex fetch, uniforms, SHaRC)
```

Shaders are compiled with the pack's root as the include path. Today a pack carries its own copy of the renderer's
headers (`common/shared.hpp`, `util/vertex.glsl`, SHaRC): this is the coupling [phase 2](roadmap.md) removes.

## 3. Manifest

In `configs.json`, under `"radiante"` (Radiance's `"radiance"` is read too):

| Field | Today | Meaning |
| --- | --- | --- |
| `display_name` | yes | Name in the list |
| `version` | yes | The pack's own version |
| `author` | yes | Shown in the list |
| `license` | yes | SPDX id, shown in the list |
| `description` | yes | One line |
| `icon` | yes | Image in the pack, shown in the list |
| `api_version` | read, not enforced | The `radiante/vN` interface the pack is written for; 0 = built on Radiante PT's internals (today's packs) |
| `id` | planned | Stable id (lowercase), for settings files and future pack lists |
| `extends` | planned | The pack this one builds on (section 5) |
| `requires` | planned | `{"radiante": ">=1.4", "features": ["omm?", "ser?"]}`; `?` = optional, the shader gets a define |
| `homepage` | planned | Link shown in the list |

A pack asking for an `api_version` newer than Radiante offers, or a required feature the GPU lacks, is listed with
that reason and not loaded.

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
- **Radiance packs** have no `api_version` and are ported, not emulated ([decisions](decisions.md#d-002)).

## 5. Inheritance and hook points (planned, phase 1)

The example pack showed the problem: of its 113 files, 103 were verbatim copies of Radiante PT and the renderer
headers, and only 10 were its own. A copy breaks at the next change to Radiante PT. And of its own 10, the four
shading files it overrides (`pt_direct.glsl`, `pt_shade_default.glsl`, `water_medium.glsl`,
`world_miss_eval_impl.glsl`) each change one to a few lines of a file of hundreds: the rest still has to follow Radiante
PT by hand.

**`extends`**: a pack names a base (`"extends": "radiante-pt"`) and holds only its own files.

- Files: the loader stages the base pack, then the pack's files over it (same rule as `shaderPackExamples` today).
- `configs.json`: merged, not replaced. `attributes` are appended (same name = override of the base's default or
  range), `passes` can be added and inserted by name (`"before": "post_render_tonemap"`), `textures`/`buffers`
  appended, `execution` scripts can call the base's (`"base"` step). The manifest is the derived pack's own.
- `lang/*.lang`: merged, the derived pack's keys win.
- The base is pinned by version (`"extends": "radiante-pt@1"`); a base major change flags the derived pack.

**Hook points**: overriding whole shading files (`pt/pt_direct.glsl`) still drifts. Radiante PT gets small, stable
hook files a derived pack replaces instead, each a function with a do-nothing default, called at the right place:

| Hook (file `hooks/*.glsl`) | Called | Example use |
| --- | --- | --- |
| `hookSurface(inout SurfaceSample)` | After material sampling | Wet look, snow cover, custom PBR maps |
| `hookDirectLight(...)` | Per light sample at a hit | Foliage translucency, cel shading |
| `hookWaterAbsorption(inout vec3)` | Water medium | Water colour |
| `hookSky(inout vec3, vec3 dir)` | Sky radiance | Sky tint, stylised skies |
| `hookFog(...)` | Volumetric media | Fog density and colour |
| `hookPostGrade(inout vec3)` | After tone mapping | Colour grade, LUT |

The set grows with requests; each hook is part of the public interface (section 4). With `extends` and hooks, the
example pack is a manifest, six attributes, four hook files and a grade pass.

Until the loader supports `extends`, `./gradlew shaderPackExamples` does the staging at build time
([examples](../../../examples/shaderpacks/)).

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
live through `worldUBO.packParams` (64 slots), the rest rebuild. Planned (phase 4), as extra attribute keys
(Radiance Advanced already uses some of these names):

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
  scripts as Radiance defined them; SHaRC through `sharc.update_pass`/`resolve_comp`.
- Planned (phase 6): built-in resources by name; custom resources with generated declarations
  (`radiante/pack_resources.glsl`); declared outputs for the denoiser (the pack states which guide images it writes,
  Radiante picks NRD or DLSS-RR); ReSTIR reservoir helpers; optional extra outputs (segmentation, separate
  direct/indirect) created only when a pack asks; Distant Horizons LOD and entity/particle data.

## 10. Development loop

Today: Shader Packs screen, errors with file and line under the list, Copy Error. Planned (phase 4):

- Hot reload of folder packs: on change, compile in the background and swap on success, keeping the running pipeline
  on failure.
- `/radiante pack validate`: load and compile a pack without switching, print the report.
- Debug views: normals, albedo, roughness, surface flags, motion vectors, SHaRC heat map, micromap coverage, plus the
  pack's own (`debug_views` in the manifest).

## 11. Safety

Packs are code that runs on the GPU. Radiante does not sandbox shaders; it limits the damage:

- A pack that fails to load or compile is replaced by Radiante PT before the player sees a broken frame.
- A pack that hangs or crashes the GPU (`VK_ERROR_DEVICE_LOST`) ends the game, so a marker file
  (`radiante/shaderpacks/.trying`) makes the next start go back to Radiante PT and say which pack it was.
- Packs are read from `radiante/shaderpacks/` only; zips are read without extracting outside the pack; shaders get
  no file or network access (GLSL has none).
- Players should get packs from places they trust, as with any mod; the screen shows author and licence.
