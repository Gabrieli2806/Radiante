# Radiante

Path-traced rendering for Minecraft 26.3 (Fabric, NeoForge and Forge), built on Minecraft's own Vulkan backend.
Builds for 26.2 (Fabric, NeoForge and Forge) and for 26.4 snapshots (Fabric) come from the same code.

[GitHub](https://github.com/Gabrieli2806/Radiante) · [Modrinth](https://modrinth.com/project/radiante) · [CurseForge](https://www.curseforge.com/minecraft/mc-mods/radiante) · [Discord](https://discord.gg/DhBbAzugZ9) · [Issues](https://github.com/Gabrieli2806/Radiante/issues) · [Full documentation](docs/en/README.md)

Languages: **EN** · [ES](README.es.md)

![Radiante with DLSS Quality: forest and river in sunlight](docs/images/scenes/hero.webp)

Radiante is a standalone path-traced renderer for Minecraft. Its native renderer, PrismRT, runs on Minecraft's render
pearl (`com.mojang.renderpearl`) Vulkan backend: instead of creating its own Vulkan device next to the game, it shares
the device Minecraft already created, so the ray tracer and the vanilla GUI draw into the same frame.

**Ray tracing off and on.** The same view with vanilla lighting and with Radiante.

| Off (vanilla) | On (DLSS Quality) |
|---|---|
| ![Off (vanilla)](docs/images/compare/raytracing-off.webp) | ![On (DLSS Quality)](docs/images/compare/raytracing-on.webp) |

## Features

- Hardware ray tracing (`VK_KHR_ray_tracing_pipeline`) for terrain, with path-traced direct lighting,
  shadows and global illumination, on Windows and Linux.
- Physically based sky, sun and atmospheric scattering.
- One built-in shader pack, `radiante-pt`, with direct sampling of block lights (torches, lava, lamps) and ReSTIR
  reuse of them, froxel or ray-marched volumetric fog, volumetric clouds with optional shadows, wet
  ground and seamless glass.
- Biome Ambiance: every biome of the overworld, the Nether and the End has an air of its own and every kind of
  water a look of its own, kept subtle and blended across borders. Biomes from mods and data packs get theirs
  worked out from their tags, climate and colours, with nothing to set up.
- LabPBR support (`_s`/`_n` maps) for blocks, atlases and entities/held items, with per-entity emission
  (glow item frames, end crystals, self-lit mobs, glowing dropped items). A custom resource pack with PBR maps (or a
  Bedrock `.mcpack`) is recommended to get the most out of these features.
- FSR upscaling and frame generation built in; NVIDIA DLSS (upscaling, Ray Reconstruction, frame generation up to
  4x on RTX 50) and Intel XeSS downloaded in game on request. NRD denoising for the non-DLSS pipelines.
- NVIDIA Reflex through `VK_NV_low_latency2`, no restart needed.
- Motion blur and depth of field, both toggleable. HDR output on HDR displays.
- Bedrock `.mcpack` resource pack support (materials read from the pack's own MER maps, per-biome fog and water),
  detected directly in the pack list.
- The Bedrock RTX look by default: Bedrock RTX's sky, exposure, tone curve, glass, water and fog are radiante-pt's
  defaults, with a few extras such as glowing End portals. Every setting can still be changed.
- Presets: save a look, switch between looks with a click, share one as a code.
- A settings screen with search, keyboard navigation and an Advanced view; changes compile in the background with a
  progress popup, and after an update the shaders are compiled on the title screen before the first world.
- [Distant Horizons](https://modrinth.com/mod/distanthorizons) support: its far terrain is path traced along with
  the rest of the world, one colour per block face, hidden column by column where near terrain is already built
  (optional; nothing changes without it). Frame cost grows with DH's render distance.
- Runs on the device Minecraft creates: no second Vulkan instance, no duplicated swapchain.
- One ~15 MB jar per loader carries both renderers; they are unpacked into the game folder on first start.

## Requirements

- Windows x64, or Linux x86-64 with glibc 2.35+ (Ubuntu 22.04, Debian 12, Fedora 36 or newer).
- A GPU with Vulkan ray tracing support (`VK_KHR_ray_tracing_pipeline` and
  `VK_KHR_acceleration_structure`).
- Minecraft 26.3 with one of:
  - Fabric Loader 0.19.5+ and Fabric API (built against 0.162.0+26.3),
  - NeoForge 26.3.0.10-beta+,
  - Forge 26.3-66.0.3+.
- Minecraft 26.2 (branch `mc/26.2`): Fabric Loader 0.19.5+ with Fabric API, NeoForge 26.2.0.93+ or Forge
  26.2-65.1.5+.
- Minecraft 26.4 snapshots (branch `mc/26.4`): Fabric only, Fabric Loader 0.20.1+ with Fabric API.
- FSR (upscaling and frame generation) is built in. NVIDIA DLSS (~115 MB) and Intel XeSS (~73 MB, Windows only)
  are downloaded in game on request, from NVIDIA's and Intel's own repositories, and used from the next start;
  the first menu offers DLSS on NVIDIA GPUs and XeSS on Intel ones, and Radiante settings can download or delete
  either.
- NVIDIA Reflex needs an NVIDIA GPU and driver with `VK_NV_low_latency2` (Windows and Linux); the option is hidden
  elsewhere.
- Java 25.

Debug logging is on by default: Radiante's diagnostics and the native renderer's own messages (prefixed
`[native]`, errors always) go to `latest.log`, so that one file is enough when reporting a problem. It can be turned
off in Radiante settings, under Other.

Full player documentation (every setting, upscalers, Linux, troubleshooting): [docs/en](docs/en/README.md).

### Linux notes

- The game must run on the real GPU driver. If the log shows `Using graphics device: llvmpipe`, Vulkan fell back
  to Mesa's software renderer: the world renders black and DLSS is not offered.
- **Flatpak launchers** (Modrinth, Prism, etc.) need the Flatpak GL runtime that matches the host NVIDIA driver
  exactly. Check the driver with `cat /sys/module/nvidia/version` (e.g. `595.91.07`) and install
  `flatpak install flathub org.freedesktop.Platform.GL.nvidia-595-91-07` (dots become dashes). Repeat after every
  driver update. Non-Flatpak launchers use the host driver directly and need nothing extra.
- Under Wayland with NVIDIA, Minecraft's OpenGL backend usually fails (`EGL_BAD_DISPLAY`); Radiante always keeps
  Vulkan available as a fallback.

#### Black or glitched world on Linux

Check `logs/launcher_log.txt` (or `latest.log`) of the instance you actually launch, in this order:

1. **`Using graphics device: llvmpipe`** - not on the GPU driver, see the Flatpak note above.
2. **`version 'GLIBC_2.xx' not found`** - the system glibc is older than the one `libprismrt.so` was built against.
   Release builds need glibc 2.35+; a `libprismrt.so` built on a newer distribution must keep the symbol pinning in
   `native/glibc_compat.h` and `native/src/prism/glibc_shims.cpp` (check with
   `objdump -T libprismrt.so | grep -o 'GLIBC_2\.[0-9]*' | sort -Vu | tail -1`).
3. **`UnsatisfiedLinkError: ... RendererProxy.<method>`** - the Java side is newer than the native library. Any change
   under `native/` (made on Windows too) needs `native/build-linux.sh` again before packaging.
4. **`FSR3: CreateContext failed`** or **`Failed to load shared shader pack`** - a FidelityFX or minizip build problem;
   the native library has to be rebuilt from this repository, which carries the Linux fixes below.
5. The launcher may run a different instance than the one you copied the jar into (Flatpak and non-Flatpak Modrinth
   keep separate folders: `~/.var/app/com.modrinth.ModrinthApp/data/ModrinthApp/profiles` and
   `~/.local/share/ModrinthApp/profiles`). The log's `gameDir` shows which one is running.

Linux-specific fixes in the bundled third-party code, which must be kept when updating it:

- **minizip-ng** is built with zlib-ng fetched and linked statically (`native/CMakeLists.txt`). Without zlib it
  silently loses deflate support, the built-in shader pack zip cannot be extracted, and the world is black.
- **FidelityFX SDK**: `wchar_t` is 4 bytes on Linux (2 on Windows). `FFX_RESOURCE_NAME_SIZE` stays 64 on every
  platform; the opaque context sizes (`FFX_SDK_DEFAULT_CONTEXT_SIZE`, `FFX_DENOISER_CONTEXT_SIZE`) are doubled off
  Windows, and the `static_assert`s checking that the private contexts fit are enabled everywhere. Shortening names
  instead (an earlier attempt) truncated shader binding names: FSR frame generation output black/garbage frames and
  the FSR upscaler failed to create, which made the `RT-FSR` pipeline render black. Names are formatted with
  `%ls`, never `%s`, in wide `swprintf` calls.
- `libprismrt.so` exports only the JNI entry points (`native/src/prism/exports.map`) and keeps its static libstdc++
  private; otherwise it clashes with the system libstdc++ already loaded in the game and crashes at startup.

## Building

```sh
native/build-windows.bat   # the native renderer (Linux: native/build-linux.sh)
./gradlew build            # one jar per loader in fabric/, neoforge/ and forge/ build/libs
```

The native build needs the Vulkan SDK and NVIDIA's DLSS SDK libraries, and Gradle does not rebuild it: run it again
after any change under `native/` and after switching branches. Requirements, Linux, CI and releases are in
[docs/en/building.md](docs/en/building.md); the development workflow is in [CONTRIBUTING.md](CONTRIBUTING.md).

## How it works

A detailed technical explanation of the native renderer is in [docs/prismrt.md](docs/prismrt.md).

- `VulkanInstance` / `VulkanDevice` creation is redirected into the native `createMerged` entry points, which
  add the ray tracing, descriptor indexing and upscaler extensions to whatever Minecraft asked for and merge
  the feature chains.
- Access to the graphics queue is serialised between Minecraft and the renderer through hooks installed over
  volk's function pointers.
- Each frame the renderer records its upload / world / composite command buffers and hands them to
  Minecraft's `VulkanCommandEncoder.execute`, then waits on Minecraft's timeline semaphore.
- Terrain is compiled into the renderer's own PBR vertex format from `ModelBlockRenderer` / `FluidRenderer`,
  mirroring Minecraft's section storage.
- Atlases are stitched on the GPU in 26.3, so the sampled copy is rebuilt from the sprite images, honouring
  the stitcher's per-sprite padding.

## Contributing

Bug reports, translations, shader packs and pull requests are welcome. [CONTRIBUTING.md](CONTRIBUTING.md) explains how
the project is organised, how to build, run and test it, the code conventions, branches per Minecraft version, and
what a pull request needs. Bugs and ideas are tracked in [GitHub Issues](https://github.com/Gabrieli2806/Radiante/issues).

## Licence

GPL-3.0. Radiante began as a fork of Radiance and its MCVR renderer. Much of it has been rebuilt since, and the project keeps moving further away from them.

Radiante is not affiliated with, endorsed by or sponsored by Mojang, Microsoft or NVIDIA. Minecraft, Minecraft Bedrock,
Bedrock RTX and NVIDIA are trademarks of their owners; their names are used only to describe compatibility with
their resource packs and the look radiante-pt's defaults aim for.
