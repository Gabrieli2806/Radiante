[← Back to index](README.md)

# Building and releasing

For anyone building Radiante or publishing a version. Playing needs none of this.

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

## Windows

Visual Studio 2026 with the C++ workload.

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
uses its own folder: `run/` (Fabric), `run-neoforge/` and `run-forge/`, so they can run at once.

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
| `native-linux` | Builds `libprismrt.so` on Ubuntu 22.04 with g++ 13 and checks the glibc version it needs. |
| `jars` | Brings both renderers together, builds the jars and fails if any lacks either. |
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
