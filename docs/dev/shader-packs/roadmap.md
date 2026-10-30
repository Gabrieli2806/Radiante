# Shader packs: roadmap

Phases in order; each one is useful on its own. Efforts are rough, for one developer. Each phase gets a GitHub issue
(label `shader-packs`) that links back here; mark a phase done with the commit or release it shipped in.

| Phase | Goal | Effort | Status |
| --- | --- | --- | --- |
| 0 | Choose packs in game, survive bad packs | — | Done |
| 1 | Small packs: `extends` and hook points | 1 week | Done; example packs being polished |
| 2 | Stable interface: `radiante/v1` headers, `api_version` | 1-2 weeks | Groundwork: `api_version` and `requires.radiante` checked in the list |
| 3 | Reference packs, more hooks and the author guide | 1-2 weeks | Guide started |
| 4 | Settings pages, presets and the development loop | 1-2 weeks | Built: settings pages, presets, the development loop, live post settings and bloom; a few items open |
| 5 | Materials and micromap opt-out | 1-2 weeks | Planned |
| 6 | Renderer features for every pack | 2-3 weeks | Planned |
| 7 | Distribution | 2-3 days + platforms | Planned |

## Phase 0: choose packs, survive bad ones (done)

Shader Packs screen, `radiante/shaderpacks/`, metadata, switch through the background rebuild, fallback to Radiante PT
with the compiler's errors, crash recovery marker, per-pack values checked against the pack's types, native log of
why a pack was rejected, example pack built as an overlay (`./gradlew shaderPackExamples`).

## Phase 1: small packs (done)

- Loader: `"extends"` in the manifest; stage the base, then the pack's files; merge `configs.json` (attributes,
  passes with `before`/`after`, textures, buffers, execution) and `lang` files ([design §5](design.md#5-inheritance-and-hook-points)).
- Radiante PT: the first hook files (`hookSurface`, `hookDirectLight`, `hookWaterAbsorption`, `hookSky`, `hookFog`,
  `hookPostGrade`) with do-nothing defaults, called from the right places, no visible change.
- The example pack rewritten as manifest + attributes + hook files; `shaderPackExamples` kept for packs that override
  whole files.

Done when: the example pack is under 10 files with no copied Radiante PT shader, loads through `extends` with the same
picture as today's (side-by-side screenshots), and Radiante PT renders identically with its default hooks.

Status:

- Done: `extends` in the loader (`shader_pack_extends.cpp`, merge rules in [design §5](design.md#extends)); the six
  hooks; radiante-example rewritten (10 files: manifest, five hook files, three lang files, icon; none copied);
  Inkwell, a second example built only on `extends`, hooks and passes of its own; Pixel Light (now the built-in
  Pixelated Lighting), shadows and reflections per texture texel (`hookTexelCentre`,
  `hooks/ray_origins.glsl`); checked zip extraction for every
  pack (path escapes, symbolic links, file count, bytes actually written, CRC); offline tools (`shader_pack_stage`
  with `--self-test` and `--extract`, `compile_pack.py`, `zip_safety_test.py`).
- Added later in `radiante-pt@1`: `hookCloudLayer` (`hooks/clouds.glsl`), drawn after the rain and over the sun
  and moon; `hookCloudShadow` (`hooks/cloud_shadow.glsl`) on the sun and moon light; `hookStars` (`hooks/stars.glsl`)
  for a night sky that can hide Radiante PT's stars. Asked for by the Mellow port, which uses all three.
- Checked offline: native and Java builds; the merge's self-test; crafted zips; every shader of Radiante PT, the
  example and Inkwell compiles with glslangValidator (Inkwell and the example also with all settings off their
  defaults); Radiante PT's optimized SPIR-V with the default hooks is identical to before for every ray tracing,
  compute and sky shader.
- Tried in game: the Shader Packs screen, switching packs, Inkwell, Pixel Light.
- Open: Inkwell's outlines shimmer (drawn after the upscaler on a jittered picture; draw them before it, from depth
  and normals); Pixelated Lighting's shadow and reflection origins (`hooks/ray_origins.glsl`) not yet
  compared in game against BetterRTX's preset; side-by-side
  screenshots of Radiante PT before and after the hooks.
- Not done: `"extends"` of a pack other than radiante-pt.

## Phase 2: stable interface

- `radiante/v1/` headers (bindings, scene, vertex fetch over both chunk layouts, surface flags, uniforms, material
  sampling) on an include root added to every pack compile.
- Radiante PT moved onto them (dogfooding).
- `api_version` enforced; packs with 0 marked "built on internals" in the list; `requires` features checked.
  (Already in: a pack asking for an `api_version` above 0, or for another Radiante version in
  `requires.radiante`, is listed with the reason and cannot be chosen.)
- A CI step compiles the example packs against the current renderer, so a change that breaks the interface fails the
  build.

Done when: Radiante PT and the example pack include no private header; a change to `src/common/shared.hpp` that keeps
the v1 accessors needs no pack change.

## Phase 3: reference packs and guide

- Reference packs written only on v1 (Pixel Light, a reworked Inkwell), proving the interface is enough for looks
  far from Radiante PT's.
- More hook points: clouds (shape, density, colour), water waves (height, speed, foam), weather and time of day, so a
  pack changes them with one small file instead of replacing Radiante PT's own. Done: a pack's cloud layer and its
  shadow, stars, the sun and moon discs, water waves and surface (foam), the view under water, the Nether's and the
  End's sky and fog, block light colour, and the weather, biome and time of day in `common/pack_world.glsl`. Open:
  the shape of radiante-pt's own clouds, and wind per kind of plant (its pass is the renderer's, design §5).
- Pack author guide in `docs/en` and `docs/es`: quick start from the example, manifest, attributes, hooks, headers,
  debugging, publishing.

Done when: a person who never read PrismRT's source can make a pack that changes the sky colour by following the guide.

## Phase 4: settings and development loop

- A page per pack in the settings, with the pack's settings grouped, instead of rows among Radiante's own.
- Live settings in post passes too (pack parameters reach full-screen passes), so a strength slider in a grade or
  palette moves without a rebuild.
- Attribute metadata (`group`, `order`, `advanced`, `description`, `performance_impact`, `step`, `visible_when`) and
  `presets`; a generated settings page per pack.
- Hot reload of folder packs, `/radiante pack validate`, per-file error view, debug views.

Done when: editing a shader in a folder pack shows the result in game without a restart, and a failed edit keeps the
running picture with the error on screen.

Status:

- Done: the pack's page in Radiante's settings, named after the pack with its icon, with its own attributes grouped
  and ordered, advanced ones behind Advanced, `visible_when`, descriptions and cost; `presets` as a Pack Preset row
  ([design §7](design.md#7-settings)). The example packs
  use the keys, with names in three languages. Unit tested (`ShaderPackSettingsTest`, the example packs included).
- Done ([design §10](design.md#10-development-loop)): hot reload of folder packs (checked in the background first;
  a pack that does not compile is not built, the running picture stays, the error shows in a popup and on the Shader
  Packs screen); `/radiante pack validate [name]` and `/radiante pack reload`; debug views (`/radiante debug view`:
  albedo, normal, roughness, metallic, emission, depth, motion vectors, surface flags) and `debug_views` for a pack's
  own.
- Done: live settings in post passes, and room for them: `worldUBO.packParams` has 256 slots (was 64, of which
  Radiante PT took 62); `common/pack_post.glsl` for post passes; Pixel Light's palette and the Mellow port's grade are
  live now.
- Done: bloom for packs (`RADIANTE_POST_BLOOM`, `radianteBloom`): Radiante's own bloom pyramid, drawn only for a pack
  that asks; the example pack adds bloom, a lens flare and chromatic aberration with it.
- Checked offline: native and Java builds; Radiante PT's optimized SPIR-V unchanged with no debug view chosen (the
  slot count changes only the uniform block's array length); every example pack and every debug view compiles.
- Not tried in game yet: the page, presets, `visible_when` (built and unit tested only); the
  hot reload, the commands, the debug views and the bloom.
- Open: `requires_feature`, rows for `vec3` attributes, a per-file error view, SHaRC heat map and micromap coverage
  debug views.

## Phase 5: materials and micromaps

- `materials` block (tags/ids to pack material ids) carried per geometry.
- Per-material `"omm": "off"`, built on `bakeSection`'s sibling geometry; cost shown in the setting.
- `material.glsl`: LabPBR and converted Bedrock RTX materials through one call.

Done when: a pack lets coloured sunlight through leaves with micromaps off for leaves only, and the frame-rate cost is
measured and documented.

## Phase 6: renderer features for every pack

- Named built-in resources and generated declarations for custom ones.
- Denoiser contract: packs declare their guide outputs; Radiante drives NRD or DLSS-RR.
- ReSTIR, SHaRC, Distant Horizons LOD and entity/particle data through v1 helpers.
- Optional extra outputs (segmentation, separate direct/indirect) on request.

Done when: a pack other than Radiante PT runs with DLSS-RR, SHaRC and Distant Horizons with no copied renderer code.

## Phase 7: distribution

- Modrinth and CurseForge: a "Radiante" category or tag for packs; `id` and `api_version` used to filter.
- The Shader Packs screen links to them.

## Open questions

- Licence of the `radiante/v1` headers (owner's call, blocks phase 2's release): GPL-3.0 like Radiante PT, or a
  permissive licence for the headers alone so packs of any licence can include them.
- Whether packs may ship native-side resources (custom images in formats the renderer does not know): not planned.
