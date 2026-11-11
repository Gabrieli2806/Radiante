[← Developer docs](README.md)

# Building and releasing

For anyone building Radiante or publishing a version. The development workflow (testing, conventions, pull
requests) is in [CONTRIBUTING.md](../../CONTRIBUTING.md).

The mod has two parts: the native C++ renderer, PrismRT (`native/`, built with CMake), and the Java
mod (Gradle, one jar per loader). The renderer is built per system: `prismrt.dll` on Windows,
`libprismrt.so` on Linux.

## Common requirements

- Java 25.
- Vulkan SDK `1.4.341` or newer, with `VULKAN_SDK` set and `glslangValidator` on `PATH`.
- The NGX libraries from NVIDIA's DLSS SDK, which are not in the repository (DLSS runtimes are never
  shipped). Once, at the version the build expects:

```sh
git clone --depth 1 --branch v310.9.1 https://github.com/NVIDIA/DLSS.git /tmp/dlss
mkdir -p native/extern/DLSS/lib
cp -r /tmp/dlss/lib/Windows_x86_64 native/extern/DLSS/lib/   # or Linux_x86_64 on Linux
```

To see what a machine has and what it lacks, run the setup check: `native/setup-windows.ps1` (with `-Install` it
fetches the missing parts through winget and git) or `native/setup-linux.sh` (`--fetch-dlss` clones the DLSS
libraries; the rest comes from your distribution's packages).

```sh
powershell -ExecutionPolicy Bypass -File native\setup-windows.ps1 -Install
```

## Windows

Visual Studio 2026, or its Build Tools, with the C++ workload (the script finds either with `vswhere`).

```sh
native/build-windows.bat
./gradlew.bat build
```

`native/build-windows.bat` configures, builds and installs with CMake in `C:\rtb\radiante` (or in
`RADIANTE_BUILD_DIR` when set). The install step copies `prismrt.dll`, the shaders (the built-in
path tracer as `shaders/world/ray_tracing/radiante-pt.zip`) and the modules into
`common/src/main/resources/radiante-native/`, which is not in git. The jars land in
`fabric/build/libs`, `neoforge/build/libs` and `forge/build/libs`.

Gradle does not build the native part: any change under `native/src/shader` or `native/src/prism`,
and every branch switch, needs the native build again. Check the time of `prismrt.dll` and
`radiante-pt.zip` afterwards; if the system clock jumped back, MSBuild skips the edits and a clean
build is needed. From a git worktree the path can get too long for the FidelityFX shader compiler:
map the worktree to a short path (`subst W: <worktree>`) and set `RADIANTE_BUILD_DIR` to a build
folder of its own.

To test: `./gradlew.bat :fabric:runClient` (or `:neoforge:runClient`, `:forge:runClient`). Each
uses its own folder: `run-fabric/`, `run-neoforge/` and `run-forge/`, so they can run at once. They share
`run-shared/`: worlds, resource packs, shader packs, Radiante's settings, `options.txt` and the server list (mods and
mod configs stay per loader).

## Linux

cmake, ninja (or make), g++ 13 or newer, git and the Vulkan SDK.

```sh
native/build-linux.sh
./gradlew build
```

The script builds in `native/build/linux-release` (or `RADIANTE_BUILD_DIR`) and installs
`libprismrt.so` into `radiante-native/linux-x64/`. If CMake cannot find Vulkan, pass
`-DVulkan_LIBRARY=/usr/lib/x86_64-linux-gnu/libvulkan.so.1`. For the result to load on
Ubuntu 22.04, build on a system with glibc 2.35 (or check with
`objdump -T libprismrt.so | grep -oE 'GLIBC_2\.[0-9]+' | sort -Vu | tail -1`).

### Linux fixes to keep

Fixes in the bundled third-party code that must survive any update of it:

- **minizip-ng** is built with zlib-ng fetched and linked statically (`native/CMakeLists.txt`). Without zlib it
  silently loses deflate, the built-in shader pack zip cannot be extracted, and the world is black.
- **FidelityFX SDK:** `wchar_t` is 4 bytes on Linux. `FFX_RESOURCE_NAME_SIZE` stays 64 everywhere; the opaque context
  sizes (`FFX_SDK_DEFAULT_CONTEXT_SIZE`, `FFX_DENOISER_CONTEXT_SIZE`) are doubled off Windows, and the `static_assert`s
  that check the private contexts fit are on everywhere. Shortening the names instead truncated shader binding names
  and made RT-FSR render black. Wide `swprintf` calls format names with `%ls`, never `%s`.
- `libprismrt.so` exports only the JNI entry points (`native/src/prism/exports.map`) and keeps its static libstdc++
  private; otherwise it clashes with the system libstdc++ already in the game and crashes at start.
- glibc symbols are pinned (`native/glibc_compat.h`, `native/src/prism/glibc_shims.cpp`) so a library built on a newer
  distribution still loads on glibc 2.35.

When a Linux build fails at start, the log says which:

| Log line | Cause |
|---|---|
| `version 'GLIBC_2.xx' not found` | Built against a newer glibc without the pinning above. |
| `UnsatisfiedLinkError: ... RendererProxy.<method>` | The Java side is newer than `libprismrt.so`: run `native/build-linux.sh` again. |
| `FSR3: CreateContext failed`, `Failed to load shared shader pack` | A FidelityFX or minizip build without the fixes above. |

## Hand-ray comparison

For offline comparisons, set `VPT_HAND_PRIMARY_BOUNDS` in `world/world.rgen`: `0` traces the original full-screen
hand rays, `1` enables geometry bounds (the default), `2` visualizes the envelope, and `3` traces every hand ray
and marks actual hand hits green inside the envelope or red outside. Rebuild native and then Gradle after changing
the shader mode. Diagnostic modes are for coverage checks, not image quality or performance comparisons.

## One jar for both systems

A jar built on one system carries only its renderer. For one that works on both, put `prismrt.dll`
and `linux-x64/libprismrt.so` in `radiante-native/` before `gradlew build`. The libraries are packed
with xz next to `natives.sha256`; the mod unpacks and checks them on first start.

Check a jar: `unzip -l <jar> | grep -E 'prismrt.dll|libprismrt.so'`.

## GitHub Actions

`.github/workflows/build.yml` does all of the above on every push to a Minecraft version branch (`mc/26.2`, `mc/26.3`, `mc/26.4`) and on every pull request:

| Job | What it does |
|---|---|
| `version` | Reads `mod_version` and `minecraft_version` and decides whether this is a new version. |
| `native-windows` | Builds `prismrt.dll` on Windows with the Vulkan SDK and the DLSS SDK. |
| `native-linux` | Builds `libprismrt.so` on Ubuntu 22.04 with g++ 13 and checks the glibc version it needs. Compiles Radiante PT under every setting and every example shader pack, and runs the native tests. |
| `jars` | Brings both renderers together, runs the Java tests, builds the jars and fails if any lacks either. |
| `release` | Only for a new version: a GitHub release. |
| `publish` | Only for a new version that is not a pre-release, when configured: Modrinth and CurseForge. |

Pushes that only touch Markdown, `docs/` or `screenshots/` do not build.

## Publishing a version

1. Add `## [x.y.z] - date` to `changelog.md`.
2. Bump `mod_version` in `gradle.properties` and push to the branch of the Minecraft version.
3. The workflow sees there is no `v<mod_version>+<minecraft_version>` tag yet (for example
   `v1.1.0+26.3`), builds, and publishes a GitHub release with the jars. The notes are that
   version's changelog section. A version with a `-` (`1.2.0-beta.1`) is marked as a pre-release
   and stays on GitHub.
4. When configured, it uploads each loader's jar to Modrinth and CurseForge (a `0.x` version as a
   beta, any other as a release). A loader the branch builds no jar for is skipped (the 26.4 port is
   Fabric only). Once, in the repository settings:
   - Secrets: `MODRINTH_TOKEN`, `CURSEFORGE_TOKEN`.
   - Variables: `MODRINTH_ID`, `CURSEFORGE_ID`.

   A site without a token or ID is skipped; the GitHub release does not depend on either.

Pull requests build but never publish.
