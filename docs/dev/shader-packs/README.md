# Shader packs

For Radiante contributors and pack authors. What shader packs are in Radiante, where the work stands, and where it is
going. Players: see [Shader packs in the settings](../../en/settings.md#shader-packs). Pack authors starting today:
[Making a shader pack](../../en/shader-packs.md).

## The idea

Radiante traces Minecraft on its own Vulkan renderer, PrismRT. The path tracer itself is a **shader pack**: a zip of
GLSL shaders and a `configs.json` that PrismRT compiles and runs. Radiante ships one, **Radiante PT** (`radiante-pt`),
and can load others from `radiante/shaderpacks/`.

The goal is for Radiante to be to path-traced packs what Iris is to OpenGL shaders: a stable platform people build
looks on, with the renderer's features (hardware ray tracing, opacity micromaps, SHaRC, ReSTIR, DLSS Ray
Reconstruction, Distant Horizons, Bedrock RTX materials) open to every pack, not only the built-in one: a stable
interface, versioning, error reporting and a settings layout around the pack format.

## Features

What a pack can do, built (B) or planned (P):

- **Choose packs in game** (B): the Shader Packs screen lists Radiante PT and the packs in `radiante/shaderpacks/`
  with icon, name, version, author, licence and description, and switches with the background rebuild.
- **Small packs on Radiante PT** (B): `"extends": "radiante-pt@1"`; the pack holds only its own files and follows
  Radiante PT's updates.
- **Hook points** (B): change surfaces, direct light, water, sky, fog and the final grade with one small GLSL file
  each; `hookTexelCentre` and `g_hookViewDir` let a pack move where light is computed (Pixel Light).
- **Own passes and images** (B): add full-screen, compute or ray tracing passes, placed with `before`/`after`.
- **Own settings** (B): every attribute is a setting with its own names in three languages; `"runtime": true` ones
  change live in the ray tracing shaders.
- **Safe failures** (B): a pack that does not load, compile or crashes the GPU is replaced by Radiante PT with the
  reason; zips are checked entry by entry.
- **Settings page per pack** (P): groups, descriptions, performance impact, presets; live settings in post passes too.
- **More hooks** (P): clouds (shape, density), waves (height, speed), foam, weather and time.
- **Stable headers `radiante/v1`** (P): packs that keep compiling when Radiante's internals change.
- **Materials** (P): per-block material ids, micromap opt-out for leaves, LabPBR and Bedrock RTX materials in one call.
- **Renderer features for every pack** (P): DLSS-RR / NRD, SHaRC, ReSTIR, Distant Horizons through public helpers.
- **Development loop** (P): hot reload of folder packs, validate command, debug views.
- **Sharing** (P): a pack category on Modrinth and CurseForge, linked from the screen.

## Principles

1. **A pack keeps working while Radiante changes.** Packs build on a versioned public interface, never on the
   renderer's internals. Breaking that interface needs a new major API version.
2. **Small packs.** A pack holds what it changes, not a copy of Radiante PT. Copies drift and break.
3. **Radiante PT is a pack like any other.** It uses the public interface too, so the interface is always enough for a
   complete path tracer.
4. **Failing packs never cost the player their game.** A pack that does not load, does not compile or crashes the
   GPU is replaced by Radiante PT, and the reason is shown.
5. **Every feature is opt-in and documented.** Nothing a pack can use is reachable only by reading the renderer's
   source.

## Where it stands

| Area | State |
| --- | --- |
| Shader Packs screen (list, metadata, switch, open folder), laid out like the settings | Done |
| Fallback to Radiante PT with the compiler's errors shown; crash recovery | Done |
| Per-pack settings (`<pack>.zip.txt`), values checked against the pack's types | Done |
| Example pack built as an overlay on Radiante PT (`examples/shaderpacks/`) | Done, replaced by `extends` |
| Pack inheritance (`extends`) and hook points in Radiante PT | Done, [roadmap](roadmap.md) phase 1 |
| Example packs on `extends` and hooks (radiante-example, Inkwell, Pixel Light) | Done; Inkwell's outlines shimmer, Pixel Light in progress |
| Versioned public headers (`radiante/v1`) | Planned, phase 2 |
| `api_version` and `requires.radiante` checked in the list | Done (groundwork for phase 2) |

## Documents

| Document | What it holds |
| --- | --- |
| [design.md](design.md) | How shader packs work and the contract a pack builds on: anatomy, manifest, versioning, inheritance and hooks, settings, materials, resources, safety |
| [roadmap.md](roadmap.md) | Phases with deliverables, acceptance criteria and effort |
| [iris-looks.md](iris-looks.md) | Converting the look of Iris and OptiFine packs onto Radiante's light (experimental) |
| [../../en/shader-packs.md](../../en/shader-packs.md) | Guide for pack authors, today |
| [../../../examples/shaderpacks/](../../../examples/shaderpacks/) | The example packs, how they are built and checked offline |

## Keeping this up to date

- A change to anything a pack reads (vertex layout, surface flags, uniforms, bindings, `configs.json` keys, include
  files) updates [design.md](design.md) in the same commit, and says in `changelog.md` whether packs need changes.
- Once `radiante/v1` exists, those changes go through it: append-only within a version, or a new version
  ([design.md, versioning](design.md#4-versioning-and-compatibility)).
- Roadmap items link their GitHub issue; a finished phase is marked done with the commit or release it shipped in.
- The example packs are rebuilt and tried after every change to Radiante PT they touch (`./gradlew shaderPackExamples`,
  then select them in game); a change to a hook's call site is checked offline first (`native/src/tools/`,
  [design §5](design.md#checking-a-pack-offline)).
