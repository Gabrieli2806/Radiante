# Contributing to Radiante

Thanks for wanting to help. Bug reports, shader tweaks, translations, documentation and code are all welcome. This
guide covers how the project is organised, how to build and test it, the conventions the code follows, and what a
pull request needs to land.

Quick links: [Issues](https://github.com/Gabrieli2806/Radiante/issues) ·
[Discord](https://discord.gg/DhBbAzugZ9) · [Building in detail](docs/en/building.md) ·
[How the renderer works](docs/prismrt.md) · [Shader pack author guide](docs/en/shader-packs.md)

## Contents

1. [Ways to help](#1-ways-to-help)
2. [Before you start](#2-before-you-start)
3. [How the project is organised](#3-how-the-project-is-organised)
4. [Setting up a development build](#4-setting-up-a-development-build)
5. [Running and testing](#5-running-and-testing)
6. [Writing the change](#6-writing-the-change)
7. [Branches and Minecraft versions](#7-branches-and-minecraft-versions)
8. [Using AI tools](#8-using-ai-tools)
9. [Commits](#9-commits)
10. [Pull requests](#10-pull-requests)
11. [Releases (maintainers)](#11-releases-maintainers)
12. [Licence of contributions](#12-licence-of-contributions)

## 1. Ways to help

- **Report a bug.** Use the bug report form in [Issues](https://github.com/Gabrieli2806/Radiante/issues/new/choose).
  Include the Radiante and Minecraft versions, the loader, your GPU and driver, the log (`logs/latest.log`) and, for
  anything visual, a screenshot. A bug that can be reproduced gets fixed far sooner.
- **Suggest an idea.** Use the feature request form. Bigger directions are the issues labelled `roadmap`; there is no
  separate roadmap file.
- **Translate.** Player-facing text lives in `common/src/main/resources/assets/radiante/lang/` (`en_us`, `es_es`,
  `zh_cn`); the player documentation is in `docs/en` and `docs/es`.
- **Make a shader pack.** Looks other than the default are shader packs built on radiante-pt, not settings. See
  [docs/en/shader-packs.md](docs/en/shader-packs.md) and the examples in [examples/shaderpacks/](examples/shaderpacks/).
- **Write code.** Pick an open issue, or open one first for anything large (see below).

## 2. Before you start

- **Check the open issues** (`gh issue list`, or the Issues tab) before working on a bug or a feature: it may already
  be known, in progress, or have notes on what was tried. Issues use the labels `bug`, `enhancement`, `visual`,
  `lighting`, `noise`, `performance`, `stability`, `compatibility`, `needs-repro` and `roadmap`.
- **Talk first about large changes.** New render passes, a new upscaler backend, changes to the shader pack
  interface or loader-parity work are much easier to land if they are discussed in an issue or on
  [Discord](https://discord.gg/DhBbAzugZ9) before the code is written.
- **Put what you find in the issue.** Even a dead end ("tried X, it did not help because Y") saves the next person
  time.

## 3. How the project is organised

Radiante has two halves:

- **The Java mod** (`common/` plus `fabric/`, `neoforge/` and `forge/`) collects the world, the entities and the
  settings each frame and hands them to the renderer.
- **PrismRT** (`native/`), a C++/Vulkan renderer, traces the scene on Minecraft's own Vulkan device. The path tracer
  itself is PrismRT's built-in shader pack, **radiante-pt**.

| Where | What |
|---|---|
| `common/` | Everything that is plain Minecraft: renderer glue, mixins, settings, screens, assets. Compiled against vanilla only, so loader code cannot creep in. |
| `fabric/`, `neoforge/`, `forge/` | Each loader's small glue: entrypoint (key bindings, client tick, settings screen) and a `RadiantePlatform` implementation registered under `META-INF/services`. |
| `common/.../client/pipeline/Pipeline.java` | Settings stored in `pipeline.yaml`, presets, building the pipeline. |
| `common/.../client/option/Options.java` | Plain options saved in `radiante/options.properties`. |
| `common/.../client/gui/` | The settings screen (`RadianteOptionsScreen`, `SettingsLayout`, `AdvancedOptions`, `Tunable`), the shader packs screens, toasts, background rebuild, start-up warm-up. |
| `common/.../client/render/` | Chunk and entity collection, point lights, emission, held light, biome ambiance. |
| `common/.../client/bedrock/` | Bedrock resource packs (converter, fog, lighting) and `BedrockRtxValues`, the Bedrock RTX values that are radiante-pt's defaults. |
| `common/src/main/resources/assets/radiante/lang/` | Texts, in `en_us`, `es_es` and `zh_cn`. |
| `native/src/prism/` | The renderer: `gpu/`, `render/` (modules, framework, pipeline), `middleware/` (JNI, one file per Java class). |
| `native/src/shader/world/ray_tracing/internal/radiante-pt/` | The path tracer: `configs.json` (attributes, textures, passes), `world/`, `pt/`, `common/`, and `hooks/`, the points shader packs replace. |
| `examples/shaderpacks/` | Example packs built on radiante-pt. |
| `docs/en`, `docs/es` | Player documentation, one page per settings section. |
| `docs/prismrt.md` | How the renderer works, in detail. When the code and this page disagree, the code wins. |
| `changelog.md` | What changed in each version. |
| `.github/workflows/build.yml` | CI and releases. |

## 4. Setting up a development build

The full reference, including Linux and the CI, is [docs/en/building.md](docs/en/building.md). The short version:

### Requirements

- **Java 25** (JDK).
- **Vulkan SDK 1.4.341 or newer**, with `VULKAN_SDK` set and `glslangValidator` on `PATH`.
- **Windows:** Visual Studio 2026 with the C++ workload. **Linux:** cmake, ninja, g++ 13+, git.
- **An RTX-capable GPU** to actually see the result (any GPU with Vulkan ray tracing support).
- **The NGX libraries from NVIDIA's DLSS SDK.** They are not in the repository (and the DLSS runtimes are never
  shipped; players download them in game). Get them once:

```sh
git clone --depth 1 --branch v310.9.1 https://github.com/NVIDIA/DLSS.git /tmp/dlss
mkdir -p native/extern/DLSS/lib
cp -r /tmp/dlss/lib/Windows_x86_64 native/extern/DLSS/lib/   # or Linux_x86_64 on Linux
```

### Steps

1. Fork the repository and clone your fork.
2. Check out the branch of the Minecraft version you will work on (normally `mc/26.3`, see
   [section 7](#7-branches-and-minecraft-versions)).
3. Build the native renderer, then the mod:

```sh
native/build-windows.bat      # Linux: native/build-linux.sh
./gradlew build               # one jar per loader in fabric/, neoforge/ and forge/ build/libs
```

The native build installs `prismrt.dll` (or `libprismrt.so`), the shaders and the modules into
`common/src/main/resources/radiante-native/`. That folder is **not in git**.

### Which build a change needs

Gradle does **not** rebuild the native part. A change under `native/` followed by Gradle alone produces a jar with the
**old** renderer and shaders, and the change never shows in game.

| What changed | Run |
| --- | --- |
| Java only (`common/`, `fabric/`, `neoforge/`, `forge/`) | Gradle |
| Shaders (`native/src/shader/**`: `.glsl`, `.rgen`, `.rchit`, `.rahit`, `.comp`, `configs.json`, `lang/`) | Native build, then Gradle |
| Native C++ (`native/src/prism/**`, `native/src/common/**`) | Native build, then Gradle |
| Switching branches | Native build, then Gradle (the native output is not in git) |

To make sure a native change made it in, check that `prismrt.dll` and
`radiante-native/shaders/world/ray_tracing/radiante-pt.zip` are newer than your edit. If the system clock ever jumped
back, MSBuild skips the edits and a clean build is needed.

### Common pitfalls

- **`./gradlew build` regenerates the JNI headers in `native/include/`.** Revert them before committing unless your
  change is meant to alter them.
- **Building from a git worktree** can make paths too long for the FidelityFX shader compiler. Map the worktree to a
  short path (`subst W: <worktree>`, or a junction under `C:\rtb\`) and set `RADIANTE_BUILD_DIR` to a build folder of
  its own.
- **Shaders are loaded once at start.** Restart the game after a native build.

## 5. Running and testing

### Development clients

```sh
./gradlew :fabric:runClient       # or :neoforge:runClient, :forge:runClient
```

Each loader runs in its own folder (`run-fabric/`, `run-neoforge/`, `run-forge/`), so they can run at the same time.
Worlds, resource packs, shader packs, Radiante's settings, `options.txt` and the server list are shared through
`run-shared/`; mods and mod configs stay per loader. Screenshots land in the repository's `screenshots/` folder, which
git ignores (only its subfolders such as `art/` and `promo/` are tracked).

Useful Fabric flags:

- `-PquickPlay="<world name>"` boots straight into a world.
- `-PvulkanValidation` turns on the Vulkan validation layers and debug labels.

### Test worlds

- **Test only in worlds whose name starts with `Radiante`** (for example `RadianteTest` or `RadianteParityA`) under
  `run-shared/saves/`. A fresh superflat is usually enough. Never point a development run at a world you play in.
- A copy of a test world needs a **different level name** too, not only a different folder name.

### Scripted runs

`DevAutomation` can drive the client from environment variables, which is how repeatable comparisons are made:

- `RADIANTE_DEV_PRESCRIPT` runs steps counted in client ticks, before a world is open. Use
  `testworld=<name>` to open (or create) a `Radiante*` test world; avoid `join=`, which hangs on the
  experimental-features prompt.
- `RADIANTE_DEV_SCRIPT` runs steps counted in world ticks, once a test world is open, for example `400:quit`.

```sh
RADIANTE_DEV_PRESCRIPT="100:testworld=RadianteSmoke" RADIANTE_DEV_SCRIPT="400:quit" ./gradlew :fabric:runClient
```

Scripts refuse to run in a world whose name does not start with `Radiante`.

Settings tests write to the development `radiante/` files (`options.properties`, `pipeline.yaml`,
`radiante-pt.zip.txt`) and to `options.txt`. Back them up first and restore them afterwards.

### Unit tests

`./gradlew build` runs the Java unit tests in `common/src/test` (code that needs no running game, such as the Bedrock
pack reader and the check that `BedrockRtxValues` agrees with radiante-pt's defaults). Small native tests live in
`native/src/tools/`.

### Checking a visual change

- **Reproduce the bug first**, then fix it.
- Compare **before and after screenshots of the same view**: same world, position, time of day and settings. Toggle
  the change at one spot rather than walking between shots.
- Parity and visual runs need **SHaRC on** (`use_sharc`), which is the player's default; without it indirect light
  reads noticeably low.
- Say plainly in the pull request what you did **not** check in game.

## 6. Writing the change

### General

- **Keep `common/` loader-agnostic.** Behaviour that differs per loader goes behind `RadiantePlatform` and is
  implemented in `fabric/`, `neoforge/` and `forge/`, never with `instanceof` checks against a loader's classes in
  `common/`.
- **Comments explain why**, in full sentences, in the style of the surrounding code: why a value or check exists, not
  what a line does.
- **Match the surrounding code** for naming, structure and comment density.
- **One logical change at a time.** Leave unrelated reformatting out.

### Mixins

- A mixin whose target method or descriptor is wrong **does not fail the compile**; it fails when the class loads in
  game. Every mixin change must be checked by launching the game, and when porting, against that version's jar.
- Prefer injections that leave vanilla behaviour in place for other mods (`@WrapOperation`, `@Inject`) over
  overwriting.

### Compatibility with other mods

Fixes should be general, not tied to one mod. If mod X breaks because Radiante skips or replaces something vanilla
does (for example `LevelRenderer.render`, whose head hooks still run through `LevelRendererSkipMixin`), restore that
vanilla behaviour so every mod relying on it benefits, instead of special-casing X by name. Only fall back to a
mod-specific patch when no general fix exists, and keep it optional (reflection, no hard dependency).

### Shaders and radiante-pt

- The path tracer is in `native/src/shader/world/ray_tracing/internal/radiante-pt/`. Any edit there needs the native
  build (see [section 4](#which-build-a-change-needs)).
- **radiante-pt's defaults are the Bedrock RTX look.** A change to one of them goes in `configs.json` (or
  `tone_mapping.yaml`) **and** in `BedrockRtxValues`; a unit test checks they agree. Other looks are shader packs, not
  new settings.
- **The hook files and `common/pack_*.glsl` are the shader pack interface** (`radiante-pt@1`). Keep their signatures,
  and with radiante-pt's own empty hooks the result must compile to the same shaders. Check a pack-facing change
  offline with `shader_pack_stage` and `native/src/tools/compile_pack.py` (see
  [examples/shaderpacks/README.md](examples/shaderpacks/README.md)).

### Adding a setting

A new setting usually touches all of these:

1. The attribute in radiante-pt's `configs.json` (`"runtime": true` when it can change without a rebuild).
2. A `Tunable`, or a row in `RadianteOptionsScreen`.
3. Its entry in `OptionImpact`.
4. The text and tooltip in **all three** lang files.
5. The settings page in `docs/en` and `docs/es`.
6. A line in `changelog.md`.

### Text and translations

- Every player-facing text goes in **all three** lang files (`en_us`, `es_es`, `zh_cn`) with the same keys, the same
  `%s` placeholders and the same `\n` line breaks. If you cannot translate one, copy the English text and say so in
  the pull request.
- Tooltips stay short: what the setting does, and what raising or lowering it does.

### Documentation and changelog

- A change players will notice gets a line in `changelog.md`.
- A change to a setting or a feature updates its page in `docs/en` and `docs/es`.

## 7. Branches and Minecraft versions

- Each Minecraft version has its own branch, named after it: `mc/26.2`, `mc/26.3` and `mc/26.4`. There is no `main`.
- **`mc/26.3` is the default branch** for now; when a newer version becomes stable, its branch becomes the default.
- Work on **`mc/26.3`** unless the change only applies to another version. Name your branch after what it does, for
  example `fix/rain-motion-vectors` or `feat/end-crystal-tint`.
- **Maintainers port** merged changes to the other branches: merge `mc/26.3` into them, adapt the API differences,
  check the mixins against that version's jar, rebuild native and build. You do not need to port your own change,
  but if you know it behaves differently on another version, say so.
- Loaders per branch: 26.2 and 26.3 build for Fabric, NeoForge and Forge; the 26.4 snapshots are Fabric only until
  NeoForge and Forge publish builds for them.

## 8. Using AI tools

AI coding assistants are welcome here; parts of Radiante itself are written with them. What matters is the result,
and the same standard applies whoever or whatever typed the code:

- **You are the author.** You must understand every line you submit and be able to explain why it is there and why it
  is correct. "The assistant wrote it" is not an answer to a review question.
- **Test it for real.** Build it (native build included when `native/` changed), run it in game in a `Radiante*` test
  world, and compare before and after for visual changes. A change that compiles but was never run is not ready.
- **Check what it claims.** Assistants confidently invent API methods, mixin targets, Vulkan behaviour and
  benchmark results. Verify against the actual Minecraft jar, the code and the game, not against the assistant's
  explanation.
- **Keep it in scope.** Review the diff before opening the pull request and remove unrelated rewrites, reformatting,
  renamed variables and comments that only restate the code.
- **Respect licences.** Do not submit generated code that reproduces another project's code under an incompatible
  licence.
- **Be honest in the description.** State what you tested and what you did not. Pull requests that were plainly not
  run, or whose author cannot answer questions about them, will be closed.

## 9. Commits

- Subject: short and lowercase, as `area: what changed`. For example
  `render: mark thin surfaces and particles, which let light through` or `docs: credit Radiance`.
- Body, when the reason is not obvious: why the change was made, in full sentences.
- **No AI attribution** in commits or pull requests: no `Co-Authored-By` lines for AI tools, no "Generated with ..."
  trailers. Write them as your own.
- Write `fixes #<n>` only when the fix is verified in game. Otherwise reference the issue (`#<n>`) and leave it open.

## 10. Pull requests

### Checklist

- [ ] Based on the right branch (normally `mc/26.3`).
- [ ] Native build run, if anything under `native/` changed.
- [ ] `./gradlew build` passes (all loaders, unit tests included).
- [ ] Checked in game, in a `Radiante*` test world; screenshots before and after for visual changes.
- [ ] Texts in all three lang files; docs and `changelog.md` updated where players will notice.
- [ ] `native/include/` headers reverted unless they are part of the change.
- [ ] No unrelated changes in the diff.

### Description

Say:

- **What** changed and **why**.
- **How you tested it**: loader, GPU, world, settings, and what you compared.
- **What you did not check.**
- The issue it fixes or advances.

Pull requests are built by CI (both renderers and every loader's jar) but never publish anything. A maintainer
reviews, may ask for changes, and merges; changes are usually squashed into one commit that keeps you as the author.

## 11. Releases (maintainers)

Releases are cut by the GitHub Actions workflow, never by hand. **Changing `mod_version` in `gradle.properties` and
pushing publishes a release**, so only do it on purpose:

1. Add a `## [x.y.z] - date` section to `changelog.md`.
2. Bump `mod_version` in `gradle.properties` and push to the branch of the Minecraft version.
3. The workflow sees there is no `v<mod_version>+<minecraft_version>` tag yet, builds both renderers and the jars,
   and publishes a GitHub release with that tag, with the changelog section as its notes. A version with a `-` (such
   as `1.2.0-beta.1`) is a pre-release and stays on GitHub.
4. When configured, the same run uploads each loader's jar to Modrinth and CurseForge. Details in
   [docs/en/building.md](docs/en/building.md#publishing-a-version).

## 12. Licence of contributions

Radiante is licensed under the GPL-3.0, with an additional permission under section 7
([LICENSE-EXCEPTION.md](LICENSE-EXCEPTION.md)) that allows it to be distributed together with the NVIDIA DLSS,
Streamline, Reflex and Intel XeSS runtime libraries. By submitting a contribution you agree that it is licensed under
the GPL-3.0 with that same additional permission.

Do not copy code from projects whose licence is not compatible with this, and name the source of anything you adapt
from another project in the pull request.
