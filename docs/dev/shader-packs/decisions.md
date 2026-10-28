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

**Packs build on Radiante PT by overlay; examples hold only their own files.** Accepted, 2026-10-27; the overlay is
replaced by `extends` for packs that say it (D-008).
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

## D-008

**`extends` merges configs.json by name, field by field; only radiante-pt can be a base.** Accepted, 2026-10-27.
Field-by-field merging lets a pack change one default or one shader of a pass without restating the rest, and the
same rule for attributes, textures, buffers and passes is one rule to learn. A pack's script replaces the base's
(`{"base": true}` splices the base's in) rather than being merged command by command: scripts are small programs,
and a merge that guesses where commands go would be wrong in ways nobody sees. A new pass without `before`/`after`
runs at the end of its stage, the place a post effect usually wants. Extending other packs (chains, cycles, versions
of third-party bases) waits until someone needs it.

## D-009

**Hooks are files a pack replaces, with exact no-op defaults.** Accepted, 2026-10-27.
One function per file under `hooks/`, replaced whole, instead of a list of functions in configs.json or a macro
layer: it uses the overlay rule packs already have, needs no new loader code, and a pack's hook reads like any GLSL.
Radiante PT's defaults compile to the same SPIR-V as before (checked offline); where the call site had to change
(`pt_direct.glsl`, light behind a surface), the default file defines `RADIANTE_HOOK_DIRECT_LIGHT_DEFAULT` and the
original code stays. A pack's own hook file must not define it.

## D-010

**The post grade hook is opt-in through an execution global.** Accepted, 2026-10-27.
A grade pass needs the picture copied first (a pass cannot sample the image it writes), so an always-on hook pass
would cost two full-screen passes in Radiante PT for nothing. `RADIANTE_HOOK_POST_GRADE` (false in Radiante PT) turns
both on; a pack sets it in its `execution_post` globals. Not a setting, so it never shows in the settings screen.

## D-011

**Every pack zip is extracted entry by entry with its own checks.** Accepted, 2026-10-27.
minizip's `save_all` trusts the sizes in the zip's headers and recreates symbolic links, so a zip could write past
any size limit or, through a link, outside the pack's folder. The loader now writes each entry itself: names that
leave the folder, symbolic links, unreadable headers, more than 20000 files, more than 512 MB actually written, an
entry longer than its header says, or a bad CRC refuse the zip and remove what was written. Checked with crafted zips
(`native/src/tools/zip_safety_test.py`).
