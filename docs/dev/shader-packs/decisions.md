# Shader packs: decisions

Each decision with its date, status and reason. Statuses: **accepted**, **proposed** (planned, not yet built),
**open** (needs the owner), **rejected**. Add new ones at the end; change a status instead of deleting an entry.

## D-001

**Packs live in `radiante/shaderpacks/`, not the game's `shaderpacks/`.** Accepted, 2026-10-27.
Iris and OptiFine read `shaderpacks/` and their packs are a different format; one folder for both would list packs
the other mod cannot load.

## D-002

**Radiance packs are ported, not emulated.** Accepted, 2026-10-27.
A compatibility layer would have to recreate Radiance's vertex layout over PrismRT's packed one, fake the bindings the
Advanced pack expects and map configuration keys PrismRT never had, forever, for two packs. Porting Vanilla PT onto
the public headers is a few days and proves the interface ([findings](radiance-packs.md)).

## D-003

**Packs build on Radiante PT by overlay; examples hold only their own files.** Accepted, 2026-10-27.
The first example pack copied 103 unchanged files out of 113. Examples now hold only what they change and
`./gradlew shaderPackExamples` stages them on Radiante PT; the loader's `extends` (phase 1) makes the same thing work
for packs players install.

## D-004

**Public interface versioned by major version, append-only within one.** Proposed, 2026-10-27.
See [design §4](design.md#4-versioning-and-compatibility). Packs need to know what they may rely on; Radiante needs
freedom to change its internals.

## D-005

**Licence of the `radiante/v1` headers.** Open, owner's decision.
Radiante PT and Radiance's packs are GPL-3.0. While packs must copy renderer headers, every pack is in practice a
derivative of GPL code. Publishing the v1 headers under a permissive licence (MIT, Apache-2.0, or a GPL exception for
the headers alone) would let packs of any licence exist without copying GPL code, while Radiante PT and the renderer
stay GPL. Before deciding: check which header contents are Radiante's own and which come from Radiance (those need
their authors' agreement to relicense).

## D-006

**Renderer profiles only touch a pack's declared shared settings.** Proposed, 2026-10-27.
`RendererProfile`/`BedrockRtxProfile` write Radiante PT's attribute names. With Radiance Vanilla PT that wrote a value
its enum did not have and broke its compile; the type check in `Pipeline.getModuleAttributes` is the stop-gap.
Packs will declare `profile_keys` (phase 4).

## D-007

**Foliage translucency first without dropping micromaps.** Accepted, 2026-10-27.
Lighting a leaf from behind at the shading point keeps opacity micromaps and their frame rate; only coloured shadows
on the ground are lost. A per-material micromap opt-out (phase 5) is for packs that want those too.
