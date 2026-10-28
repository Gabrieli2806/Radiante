# Shader packs: roadmap

Phases in order; each one is useful on its own. Efforts are rough, for one developer. Each phase gets a GitHub issue
(label `shader-packs`) that links back here; mark a phase done with the commit or release it shipped in.

| Phase | Goal | Effort | Status |
| --- | --- | --- | --- |
| 0 | Choose packs in game, survive bad packs | — | Done (`feature/shaderpacks`) |
| 1 | Small packs: `extends` and hook points | 1 week | Built, not yet tried in game (`feature/shaderpacks-api`) |
| 2 | Stable interface: `radiante/v1` headers, `api_version` | 1-2 weeks | Groundwork: `api_version` and `requires.radiante` checked in the list |
| 3 | Reference packs and the author guide | 3-5 days | Planned |
| 4 | Settings pages, presets and the development loop | 1-2 weeks | Planned |
| 5 | Materials and micromap opt-out | 1-2 weeks | Planned |
| 6 | Renderer features for every pack | 2-3 weeks | Planned |
| 7 | Distribution | 2-3 days + platforms | Planned |

## Phase 0: choose packs, survive bad ones (done)

Shader Packs screen, `radiante/shaderpacks/`, metadata, switch through the background rebuild, fallback to Radiante PT
with the compiler's errors, crash recovery marker, per-pack values checked against the pack's types, native log of
why a pack was rejected, example pack built as an overlay (`./gradlew shaderPackExamples`).

## Phase 1: small packs (built, not yet tried in game)

- Loader: `"extends"` in the manifest; stage the base, then the pack's files; merge `configs.json` (attributes,
  passes with `before`/`after`, textures, buffers, execution) and `lang` files ([design §5](design.md#5-inheritance-and-hook-points)).
- Radiante PT: the first hook files (`hookSurface`, `hookDirectLight`, `hookWaterAbsorption`, `hookSky`, `hookFog`,
  `hookPostGrade`) with do-nothing defaults, called from the right places, no visible change.
- The example pack rewritten as manifest + attributes + hook files; `shaderPackExamples` kept for packs that override
  whole files.

Done when: the example pack is under 10 files with no copied Radiante PT shader, loads through `extends` with the same
picture as today's (side-by-side screenshots), and Radiante PT renders identically with its default hooks.

Status (`feature/shaderpacks-api`):

- Done: `extends` in the loader (`shader_pack_extends.cpp`, merge rules in [design §5](design.md#extends)); the six
  hooks; radiante-example rewritten (10 files: manifest, five hook files, three lang files, icon; none copied);
  Inkwell, a second example built only on `extends`, hooks and passes of its own; checked zip extraction for every
  pack (path escapes, symbolic links, file count, bytes actually written, CRC); offline tools (`shader_pack_stage`
  with `--self-test` and `--extract`, `compile_pack.py`, `zip_safety_test.py`).
- Checked offline: native and Java builds; the merge's self-test; crafted zips; every shader of Radiante PT, the
  example and Inkwell compiles with glslangValidator (Inkwell and the example also with all settings off their
  defaults); Radiante PT's optimized SPIR-V with the default hooks is identical to before for every ray tracing,
  compute and sky shader.
- Not yet checked: in game. The side-by-side screenshots of the example (old overlay build against the `extends`
  build) and of Radiante PT, Inkwell's look, and loading a pack through the Shader Packs screen.
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

- Port Radiance Vanilla PT onto v1 as a second reference pack (GPL-3.0, credited), proving the interface fits a pack
  Radiante did not write ([findings](radiance-packs.md)).
- Pack author guide in `docs/en` and `docs/es`: quick start from the example, manifest, attributes, hooks, headers,
  debugging, publishing.

Done when: a person who never read PrismRT's source can make a pack that changes the sky colour by following the guide.

## Phase 4: settings and development loop

- Attribute metadata (`group`, `order`, `advanced`, `description`, `performance_impact`, `step`, `visible_when`) and
  `presets`; a generated settings page per pack.
- Renderer profiles touch only `profile_keys` a pack declares.
- Hot reload of folder packs, `/radiante pack validate`, per-file error view, debug views.

Done when: editing a shader in a folder pack shows the result in game without a restart, and a failed edit keeps the
running picture with the error on screen.

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

- Licence of the `radiante/v1` headers: [decisions D-005](decisions.md#d-005) (owner's call, blocks phase 2's release).
- Whether packs may ship native-side resources (custom images in formats the renderer does not know): not planned.
