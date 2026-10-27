# Shader packs: state, findings and plan

For contributors. What Radiante's shader pack support is today, what happened when the original Radiance packs were
tried on it, and a proposed Radiante Pack API with a phased roadmap. File and function names are from `mc/26.3` at
the time of writing; the code wins where this page disagrees.

## 1. Where things stand

### What Radiance's pack format offered

PrismRT inherited Radiance's pack format, documented in Radiance's pack manual
(`Minecraft-Radiance/awsome-radiance-shaderpacks`, `doc/manual_en.md`). A pack is a zip or a folder with
`configs.json` at its top:

- **Manifest**: `"radiance": { "shader_pack": true, "display_name": ... }`. Nothing else: no version, author,
  licence, icon or API version.
- **Attributes**: `bool`, `int`, `float`, `string`, `vec3`, `int_range`, `float_range`, `enum`, turned into GLSL
  macros through `define` (a name, a template such as `uint(X)`, or per-value variants). Names double as translation
  keys; `lang/*.lang` files give the texts. Values are kept per pack in `<pack>.txt` next to it.
- **Resources**: `textures` (`file` images, `intermediate` render targets with sizes as expressions) and `buffers`,
  bound by number in a "custom" descriptor set (set 5 in the ray tracing stage, set 4 in post).
- **Passes**: `ray_tracing` (raygen, miss, hit groups with closest/any hit and AABB intersection), `compute`,
  `full_screen` and `render`, with input/output image lists; `execution` / `execution_post` scripts (variables,
  `assign`, `pass`, `if_else`, `while`) decide which passes run each frame.
- **SHaRC**: `sharc.update_pass` and `resolve_comp`; passes opt into queries with `query_sharc`.
- **Fallback**: a pack that fails to load falls back to the built-in one.

What it lacked:

- **No stable interface.** Shaders are compiled with the pack root as the only include path, so every pack carries
  its own copy of the renderer's internals: `common/shared.hpp` (vertex formats, `WorldUBO`, `SkyUBO`), `util/*.glsl`
  (vertex fetch, payloads), SHaRC headers. Any change to the renderer's data silently breaks every pack built
  against the old copy, with no version to check.
- **Fixed descriptor layout by convention.** Sets and bindings (set 1 geometry, set 2 uniforms, set 3 outputs, set 4
  SHaRC) are documented, not declared, and are whatever the renderer happens to bind.
- **No error reporting.** A rejected `configs.json` fell back to the built-in pack silently; compiler messages only
  reached the log.
- **No metadata, no grouping or presets in settings, no hot reload, no debug views, no per-material hooks.**

### What PrismRT changed since (why old packs break)

- **Geometry is packed.** Material vertices are 40-byte `PackedMaterialVertex` (was the 80-byte `MaterialVertex`),
  and chunk geometry uses a compact layout flagged by the lowest bit of its buffer addresses (16-bit indices,
  half-float positions, per-geometry material words): `native/src/common/shared.hpp`,
  `native/src/shader/util/vertex.glsl` (`isCompactAddress`, `unpackMaterialVertex`), written by
  `packChunkGeometry` in `native/src/prism/render/chunks.cpp`. A pack that dereferences those addresses with the old
  layout reads garbage (or faults: the tagged address is misaligned).
- **Surface bits** in `packedData`: `WATER`, `RAIN`, `HELD`, `GLASS`, `PLANT`, `PANE` (+ caps), `BLEND`, `UNLIT`,
  `LAVA`, `LAYER`, `LEAVES` (bits 17-30, `util/vertex.glsl`); Radiance had only `NO_HEIGHT_SURFACE_BIT` (bit 16).
- **Uniforms grew, append-only.** `WorldUBO` gained held/off-hand lights, brightness sliders, wind, LOD coverage and
  entity light addresses, and `packParams[64]` (runtime pack settings, `PRISM_PACK_PARAM_COUNT`); `SkyUBO` gained
  night vision, sun/moon UV rectangles, biome fog, celestial axis. The prefix Radiance knew is unchanged, so old
  uniform reads still work.
- **Runtime attributes**: `"runtime": true` makes an attribute read `worldUBO.packParams[N]` instead of a compiled
  define (`ShaderPack::setRuntimeAttribute`, `Pipeline.setRuntimeAttribute`), so changing it needs no rebuild.
- **New renderer features** that packs reach only by copying radiante-pt's code: opacity micromaps for cut-out
  terrain (`render/opacity_micromaps.*`, `VPT_OMM_SUPPORTED` set by `ray_tracing_module.cpp`), Shader Execution
  Reordering (`VPT_SER_SUPPORTED`), ReSTIR reservoirs (`VPT_RESTIR`), NRD signal mode (`VPT_NRD_SIGNALS`), wind
  refit (`PositionVertex::pad0`), block and entity light lists, Distant Horizons coverage, Bedrock material
  attributes.
- **Built-in pack build**: `native/src/shader/CMakeLists.txt` zips `radiante-pt` together with `shader/util/`,
  `common/` and the SHaRC headers, so the built-in pack is always in step; external packs are not.
- **Outputs**: the ray tracing module's images are set 3 bindings 0-15 (`modules/ray_tracing.yaml`,
  `pt/pt_bindings.glsl`). Later Radiance versions (the Advanced pack) write more (set 3 up to 23: segmentation,
  separate direct radiance, guides) and read set 1 binding 10 (`TlasInstances`), which PrismRT does not provide.

### What this branch adds

- **Shader Packs screen** (`client/gui/ShaderPacksScreen.java`), opened from a button beside Presets
  (`SettingsLayout.init`, `RadianteOptionsScreen.init`). Lists the built-in pack and `radiante/shaderpacks/`
  (`client/pipeline/ShaderPackLibrary.java`), reading name/version/author/licence/description/icon from a
  `"radiante"` (or Radiance's `"radiance"`) manifest, flags zips that wrap the pack in a folder, opens the folder,
  and switches through `Pipeline.selectShaderPack` and `BackgroundRebuild.open`.
- **Failure handling** (`client/gui/ShaderPackTrial.java`): watches the renderer's log after a switch; a rejected
  config, a shader that does not compile or a failed rebuild puts the built-in pack back and shows the renderer's
  lines under the list (Copy Error copies them). A crash during the trial is caught at the next start through a
  marker file.
- **Value check** (`Pipeline.getModuleAttributes`): packs share attribute names, so a value is reset to the pack's
  default when the pack's type does not allow it.
- **Native**: `ShaderPackLoader::load` now logs why a requested pack was rejected before falling back
  (`[ShaderPack] <path> could not be loaded: <reason>`); before, the fallback was silent.
- The folder is `radiante/shaderpacks/`, not the game's `shaderpacks/`: Iris and OptiFine read that one and their
  packs are a different format.

## 2. The original Radiance packs on Radiante today

Tested on this branch (RTX 5070, NVIDIA driver of 2026-10, world `RadianteShaderPacksTerrain`); packs downloaded
from GitHub as data, repacked with `configs.json` at the zip's top.

| Pack | Result |
| --- | --- |
| `Shaderpack-Vanilla-PT` as GitHub's "Download ZIP" | Listed with "configs.json is inside the folder `Shaderpack-Vanilla-PT-HEAD`": the zip wraps the pack in a folder. Not loadable as downloaded. |
| Radiance **Advanced** (`Shaderpack-Advanced`, GPL-3.0) | Rejected by the loader: `root.passes[1].inputs.images must be an array`. Built-in pack put back, message shown. |
| Radiance **Vanilla PT** (`Shaderpack-Vanilla-PT`, GPL-3.0), first try | 3 shaders fail to compile (`world/default.rchit:860`, `world/no_height.rchit:860`, `world/shadow.rahit:84`: `'render_pipeline' : undeclared identifier`). Cause on our side, not the pack's: the Bedrock RTX renderer profile wrote radiante-pt's `water_surface_mode.bedrock` into the attribute of the same name, Vanilla PT's enum has no such value, and the loader compiled the value's text into `VPT_WATER_SURFACE_MODE`. Fixed in this branch (`Pipeline.getModuleAttributes` resets values a pack does not offer to its default). |
| Radiance **Vanilla PT**, after that fix | `configs.json` loads, all 34 ray tracing shaders and 10 post shaders compile, the pipeline builds (`[Rebuild] total 5946 ms`), and the GPU is lost within the first two seconds of frames (`VK_ERROR_DEVICE_LOST: Failed to wait for semaphore`), crashing the game. Reproduced twice. No frame could be captured. |

Why, from reading both sides (not confirmed with a debugger):

- **Vanilla PT crash**: its `util/vertex.glsl` reads every geometry's buffers as Radiance laid them out: 80-byte
  `MaterialVertex`, 32-bit indices, float positions. PrismRT writes 40-byte `PackedMaterialVertex` and, for chunk
  geometry, a compact layout marked by the low bit of the buffer address (`isCompactAddress` in our
  `util/vertex.glsl`). Read with the old layout, indices and addresses are garbage, the hit shaders dereference wild
  device addresses, and the device is lost. The uniforms are not the problem: our `WorldUBO`/`SkyUBO` start with
  Radiance's fields unchanged.
- Vanilla PT's post passes for particles, weather, text and name tags are skipped (`[PostRender] pass
  'post_render_particle' draws particle, which is traced in the world: skipped`): PrismRT traces those in the world.
  Harmless, but the pack's look would differ.
- **Advanced** targets a newer Radiance than PrismRT forked from. Its `configs.json` uses things PrismRT's loader does
  not know: pass `inputs`/`outputs` as maps of access modes (`{"atmosphere_transmittance": "read"}`) instead of
  arrays, pass keys `indirect_args`, `async_group`, `clear_outputs`, `pipeline`, `max_ray_recursion_depth`, root keys
  `segmentation_mask`, `nrd_mode`, `acceleration_structure_build_mode`, attribute keys `group`, `description`,
  `performance_impact`, `preset`, `visible_when`. Its shaders bind set 3 images up to binding 23 (segmentation,
  separate direct radiance, guide images) and set 1 binding 10 (`TlasInstances`), which PrismRT's ray tracing module
  does not provide. Past the first schema error there would be more of each.

Fixes made while testing (this branch): enum/bool/range values checked against the pack's own attribute types;
a marker file (`radiante/shaderpacks/.trying`) so a pack that crashes the game while being tried is replaced by the
built-in one at the next start (`ShaderPackLibrary.recoverFromCrashedTrial`), and a trial only counts as good after
ten seconds of frames, not at "pipeline built".

Not tried: fixing either pack. A port of Vanilla PT would start by replacing its `common/` and `util/vertex.glsl`
with Radiante's (section 4).

## 3. Proposed Radiante Pack API

Goal: a pack written against API version N keeps working while Radiante changes underneath, and a pack can use what
radiante-pt uses (OMM, SER, SHaRC, ReSTIR, NRD/DLSS-RR signals, LOD data) without copying renderer internals.

### 3.1 Versioned include headers

- Ship the interface as headers the renderer provides, not files packs copy: an include root added to every pack
  compile (beside the pack root, in `ShaderPack` include directories, `shader_pack_loader.cpp`), e.g.
  `#include <radiante/v1/scene.glsl>`.
- `radiante/v1/`: `bindings.glsl` (every built-in set/binding as named declarations), `scene.glsl` (TLAS, instance
  and geometry lookup), `vertex.glsl` (`radianteFetchTriangle(...)` returning positions, normal, UVs, colour, light,
  surface flags, hiding compact vs. full layouts), `uniforms.glsl` (`WorldUBO`, `SkyUBO` as frozen v1 views plus
  accessor functions for later fields), `surface.glsl` (flag accessors), `payload.glsl`, `sharc.glsl`, `omm.glsl`,
  `restir.glsl`, `lod.glsl`, `debug.glsl`.
- Rules: v1 never changes layout; new data is added through accessors or `v2/`. The renderer keeps compiling v1
  packs for at least one major Radiante version after v2 ships. Internals (`shared.hpp`, `util/vertex.glsl`) stay
  private and may change freely; radiante-pt moves onto the public headers itself (dogfooding is the test that the
  API is enough).
- The copy step in `native/src/shader/CMakeLists.txt` stays only for radiante-pt's private helpers.

### 3.2 Manifest

```json
"radiante": {
  "api_version": 1,
  "id": "examplepack",
  "display_name": "Example PT",
  "version": "1.2.0",
  "authors": ["Name"],
  "license": "MIT",
  "description": "One line",
  "icon": "pack.png",
  "homepage": "https://...",
  "requires": { "radiante": ">=1.4", "features": ["ray_query", "omm?"] }
}
```

`api_version` missing means a Radiance pack (compatibility path, 4). A pack asking for a newer API than the renderer
offers is listed with that message instead of being loaded. `features` with `?` are optional (the define says
whether they are there).

### 3.3 Surface flags and materials

- Public flags (v1): solid, cutout, translucent, emissive, water, lava, glass, pane, ice/blend, leaves, plants
  (grass/flowers), layer overlays, held item, rain, entity, particle, text, LOD terrain, Bedrock-material surface.
- Per-material hooks: a `materials` block in `configs.json` mapping block/entity tags or ids to pack-defined material
  ids (e.g. `"#minecraft:leaves": "foliage"`), carried per geometry so a hit shader can branch on them.
- **Foliage translucency vs. opacity micromaps**: OMM makes cut-out shadow/bounce rays skip the any-hit shader
  (`opacity_micromaps.hpp`), which is exactly what a pack that lets light through leaves does not want. Proposal:
  `"omm": "auto" | "off"` per material (default `auto`), so a pack opts leaves (or any material) out and gets its
  any-hit shader called; the renderer moves those quads to their own geometry as `bakeSection` already does for
  glass and animated sprites. Document the cost (any-hit per leaf per ray) next to the switch.
- Bedrock RTX materials: expose the MER/height/normal lookups radiante-pt uses (`pt_material.glsl`) as
  `radiante/v1/material.glsl` (`radianteSampleMaterial(...)` returning albedo, metal, emissive, roughness, normal,
  subsurface), so packs read Bedrock and LabPBR resource packs the same way.

### 3.4 Uniforms and runtime values

- Frozen v1 views of `WorldUBO`/`SkyUBO`; anything newer through accessors.
- Keep `"runtime": true` and `packParams` (64 vec4 slots), and add `float2`/`color` types and slider steps.

### 3.5 Resources, bindings and passes

- Built-in resources by name in `bindings.glsl`, not by number. Custom resources keep their set but get names and
  generated declarations (`#include <radiante/pack_resources.glsl>`, generated by the loader from `configs.json`),
  so moving a binding is not a silent break.
- Passes as today, plus declared access to: SHaRC (update/query, as now), NRD signals vs. DLSS-RR inputs (the pack
  states which guide images it writes; the renderer picks the denoiser path), ReSTIR reservoir buffers (shared
  helper), Distant Horizons LOD (coverage buffer and LOD geometry flag), entity and particle data (instance kind,
  entity id, light lists).
- Optional extra outputs (segmentation, separate direct/indirect) declared by the pack, created only when asked, so
  packs like Radiance Advanced have somewhere to write.

### 3.6 Settings UI

- Attribute metadata: `group` (section), `order`, `description`, `performance_impact` (`low`/`medium`/`high`),
  `slider_step`, `visible_if`, `advanced`. Radiance Advanced already writes `group`, `description`,
  `performance_impact` and `preset`: adopting those names keeps its files readable.
- Pack presets: named sets of attribute values (`presets: { "Low": {...}, "Ultra": {...} }`), shown as the Quality
  row.
- The settings screen builds a section per group from the pack's attributes (sliders, toggles, enums), so a pack
  other than radiante-pt gets a usable settings page; Radiante's own hand-made rows stay for radiante-pt.
- Renderer profiles (`RendererProfile.enforce`, `BedrockRtxProfile`) write radiante-pt's attribute names on every
  build. With another pack they should do nothing, or only touch settings the pack declares it shares (a
  `"profile_keys"` list); today the value check above is the only guard.

### 3.7 Development loop

- **Hot reload**: a folder pack is watched; on change, a prewarm compiles the new shaders in the background
  (`ShaderPrewarm`) and swaps on success, keeping the old pipeline on failure. A "Reload pack" key in dev.
- **Compile errors in game**: done in this branch for the first failure; next, a per-file view with line numbers and
  the failing line, and errors kept while the old pipeline keeps running.
- **Debug views**: a `debug_views` list in the manifest (name, define), shown in F3 or a settings enum, plus
  renderer-provided ones (normals, albedo, roughness, motion vectors, SHaRC heatmap, OMM coverage, surface flags).
- A validator command (`/radiante pack validate`) that loads `configs.json` and compiles every shader without
  switching, printing the report.

## 4. Radiance packs: shim or port

- **Shim** (renderer side): when `api_version` is missing, compile with a compatibility header set that recreates
  Radiance's `MaterialVertex` fetch over the packed and compact layouts, rebinds set 3/set 1 extras as dummy images,
  and maps Radiance-only `configs.json` keys. Feasible for Vanilla PT-era packs (the uniforms are a prefix of ours),
  but the shim has to override files the pack ships (`util/vertex.glsl`, `common/shared.hpp`) by include-path order,
  which only works where the pack includes them by those names. Advanced needs outputs and bindings PrismRT lacks.
- **Port** (pack side): replace the pack's copies of `common/`, `util/vertex.glsl` and the SHaRC headers with
  Radiante's, then fix what does not compile. For Vanilla PT this is small (its shaders are radiante-pt's ancestors);
  for Advanced it needs the extra outputs of 3.5 or rewrites of its denoiser feed.
- Recommendation: port, not shim. Offer the port of Radiance Vanilla PT as the reference external pack (with the
  GPL-3.0 licence it carries), and design v1 so the next renderer change does not need another port. A shim costs
  renderer complexity forever for two packs.

## 5. Licensing (owner's decision)

- radiante-pt is GPL-3.0 (inherited from Radiance's Vanilla PT); so are Radiance's packs. Today a pack must copy
  renderer headers to work, so in practice every pack is a derivative of GPL code.
- If the v1 headers are published under a permissive licence (MIT, Apache-2.0 or a GPL linking exception for them
  alone), packs of any licence can exist without copying GPL code; radiante-pt itself stays GPL. This needs the
  owner's decision and checking that the header contents are Radiante's own (not Radiance's) or relicensed by their
  authors. **Not decided here.**

## 6. Distribution

- Packs are zips: Modrinth's "Shader" project type with a loader tag (Modrinth lists Iris/OptiFine/Canvas; ask for a
  "Radiante" loader category or publish as a resource-pack-like project with a `radiante` tag), and CurseForge's
  Shaders class with a clear "Radiante" in the title and summary until there is a category.
- A pack id and `api_version` in the manifest let a future in-game browser filter compatible packs.
- Radiante's docs get a pack author guide built from Radiance's manual plus the v1 headers.

## 7. Roadmap

| Phase | What | Effort |
| --- | --- | --- |
| 0 (this branch) | Screen, folder, metadata, switch, fallback with compile errors, native rejection log | done |
| 1 | `radiante/v1` headers (bindings, scene, vertex fetch, surface flags, uniforms); include root for packs; radiante-pt moved onto them; manifest `api_version` check | 1-2 weeks |
| 2 | Port Radiance Vanilla PT to v1 as the reference pack; pack author guide (en/es) | 3-5 days |
| 3 | Settings UI from attribute metadata (groups, sliders, enums, presets, performance impact); per-pack settings page | 1 week |
| 4 | Dev loop: folder hot reload with prewarm, validator command, per-file error view, debug views | 1 week |
| 5 | Material hooks: `materials` block, per-material OMM opt-out, Bedrock/LabPBR material helpers | 1-2 weeks |
| 6 | Feature access: SHaRC/ReSTIR helpers, declared NRD vs. DLSS-RR inputs, optional extra outputs, LOD and entity/particle data | 2-3 weeks |
| 7 | Distribution: Modrinth/CurseForge listing, in-game list filtered by `api_version` | 2-3 days plus platform talks |

Phases 1 and 2 are the ones that make external packs possible at all; 3 and 4 make them pleasant to write; 5 and 6
are where packs can do things radiante-pt does not.
