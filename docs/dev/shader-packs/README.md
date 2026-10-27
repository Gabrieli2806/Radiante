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
Reconstruction, Distant Horizons, Bedrock RTX materials) open to every pack, not only the built-in one. Radiance, the
project PrismRT comes from, had the pack format but not the platform around it: no stable interface, no versioning,
no error reporting, no settings layout. That is the gap this work closes.

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
| Shader Packs screen (list, metadata, switch, open folder) | Done (`feature/shaderpacks`) |
| Fallback to Radiante PT with the compiler's errors shown; crash recovery | Done |
| Per-pack settings (`<pack>.zip.txt`), values checked against the pack's types | Done |
| Example pack built as an overlay on Radiante PT (`examples/shaderpacks/`) | Done |
| Pack inheritance (`extends`) and hook points in Radiante PT | Planned, [roadmap](roadmap.md) phase 1 |
| Versioned public headers (`radiante/v1`) and `api_version` check | Planned, phase 2 |
| Original Radiance packs | Do not run: [findings](radiance-packs.md) |

## Documents

| Document | What it holds |
| --- | --- |
| [design.md](design.md) | How shader packs work and the contract a pack builds on: anatomy, manifest, versioning, inheritance and hooks, settings, materials, resources, safety |
| [roadmap.md](roadmap.md) | Phases with deliverables, acceptance criteria and effort |
| [decisions.md](decisions.md) | Decisions taken and decisions still open (licence of the public headers) |
| [radiance-packs.md](radiance-packs.md) | What happened when the original Radiance packs were tried, and why |
| [../../en/shader-packs.md](../../en/shader-packs.md) | Guide for pack authors, today |
| [../../../examples/shaderpacks/](../../../examples/shaderpacks/) | The example pack and how it is built |

## Keeping this up to date

- A change to anything a pack reads (vertex layout, surface flags, uniforms, bindings, `configs.json` keys, include
  files) updates [design.md](design.md) in the same commit, and says in `changelog.md` whether packs need changes.
- Once `radiante/v1` exists, those changes go through it: append-only within a version, or a new version
  ([design.md, versioning](design.md#4-versioning-and-compatibility)).
- A decision goes in [decisions.md](decisions.md) with its date, status and reason, including the ones that end as
  "no".
- Roadmap items link their GitHub issue; a finished phase is marked done with the commit or release it shipped in.
- The example pack is rebuilt and tried after every change to Radiante PT it touches (`./gradlew shaderPackExamples`,
  then select it in game).
