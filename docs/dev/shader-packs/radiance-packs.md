# Radiance packs on Radiante

What happened when the original Radiance packs were tried on Radiante (2026-10-27, RTX 5070, world
`RadianteShaderPacksTerrain`), and why. Packs downloaded from GitHub as data and repacked with `configs.json` at the
zip's top.

## What Radiance's format had, and lacked

PrismRT inherited Radiance's pack format (manual: `Minecraft-Radiance/awsome-radiance-shaderpacks`,
`doc/manual_en.md`): a `configs.json` with a minimal manifest (`"radiance": {"shader_pack": true, "display_name"}`),
typed attributes turned into GLSL defines and translated through `lang` files, textures and buffers in a custom
descriptor set, passes (`ray_tracing`, `compute`, `full_screen`, `render`) chosen each frame by execution scripts, and
SHaRC hooks.

It lacked: a stable interface (every pack carried copies of the renderer's headers), versioning, declared bindings,
error reporting (a bad pack fell back silently), metadata, settings layout, hot reload, debug views and per-material
hooks.

## What PrismRT changed since

- Geometry is packed: 40-byte `PackedMaterialVertex` (was 80-byte `MaterialVertex`), and chunk geometry uses a
  compact layout flagged by the low bit of its buffer address (`isCompactAddress`, `unpackMaterialVertex` in
  `shader/util/vertex.glsl`, written by `packChunkGeometry` in `prism/render/chunks.cpp`).
- Surface flags in bits 17-30 of `packedData` (water, rain, held, glass, plant, pane and caps, blend, unlit, lava,
  layer, leaves); Radiance had bit 16 only.
- `WorldUBO`/`SkyUBO` grew append-only: Radiance's prefix is unchanged.
- Runtime attributes (`"runtime": true`, `packParams`), opacity micromaps, Shader Execution Reordering, ReSTIR, NRD
  signal mode, wind refit, block and entity light lists, Distant Horizons coverage, Bedrock materials.
- The ray tracing module's outputs are set 3 bindings 0-15.

## Results

| Pack | Result |
| --- | --- |
| Vanilla PT as GitHub's "Download ZIP" | Listed with "configs.json is inside a folder": not loadable as downloaded |
| Advanced (GPL-3.0) | Rejected: `root.passes[1].inputs.images must be an array` |
| Vanilla PT (GPL-3.0), first try | 3 shaders failed to compile: Radiante's Bedrock RTX profile wrote Radiante PT's `water_surface_mode.bedrock` into an attribute of the same name that Vanilla PT's enum lacks. Fixed on Radiante's side (`Pipeline.getModuleAttributes` resets such values) |
| Vanilla PT, after the fix | Loads, compiles 44 shaders, builds; the GPU is lost within two seconds (`VK_ERROR_DEVICE_LOST`), twice |

## Why

- **Vanilla PT**: reads geometry with Radiance's layout (80-byte vertices, 32-bit indices, float positions) over
  PrismRT's packed and compact buffers: wild addresses, device lost. Read from the code, not confirmed with a
  debugger. Its post passes for particles, weather and text are skipped (PrismRT traces those in the world).
- **Advanced**: written for a newer Radiance than PrismRT forked from: pass `inputs`/`outputs` as access-mode maps,
  keys PrismRT does not know (`indirect_args`, `async_group`, `clear_outputs`, `segmentation_mask`, `nrd_mode`…),
  attribute keys `group`, `description`, `performance_impact`, `preset`, `visible_when`, and outputs PrismRT does not
  provide (set 3 up to binding 23, set 1 binding 10).

## What follows

Port, not emulate ([D-002](decisions.md#d-002)): Vanilla PT onto `radiante/v1` in phase 3. Advanced's attribute
metadata names are adopted in phase 4 so its files stay readable; its extra outputs come with phase 6.
