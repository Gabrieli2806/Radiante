# Radiante documentation

![Radiante with DLSS Quality: forest and river in sunlight](../images/scenes/hero.webp)

Languages: [ES](../es/README.md) · **EN**

Radiante is a hardware ray tracing mod for Minecraft 26.3 (Fabric, NeoForge and Forge),
on Windows and Linux, built on
Minecraft's own Vulkan backend instead of creating a second Vulkan device next to it. Its native
renderer is PrismRT. Builds for Minecraft 26.2 (Fabric, NeoForge and Forge) and 26.4 snapshots
(Fabric) come from the same code; these pages describe the 26.3 version.

Documentation version: **1.1.0**.

## Getting started

1. **[Requirements](requirements.md)** — system, GPU by vendor, loader and Java.
2. **[Installation](installation.md)** — per-loader steps (Fabric, NeoForge, Forge) and what to
   expect on first launch.
3. **[Upscalers](upscalers.md)** — FSR built in; DLSS and XeSS downloaded in game.
4. **[Settings](settings.md)** — how the settings screen is organised, the Reset to Defaults button,
   and a link to every section.

## Features in depth

- **[Picture styles and quality levels](styles-and-quality.md)** — the Low/Medium/High/Ultra
  levels, and the Natural/Bedrock/Vivid styles.
- **[Bedrock RTX resource packs and look](bedrock-rtx.md)** — using a Bedrock `.mcpack`, the
  Bedrock RTX look radiante-pt has by default, and reading files from a Bedrock install on the
  same PC.
- **[Distant Horizons](distant-horizons.md)** — experimental far-terrain support.
- **[HDR](hdr.md)** — output on HDR displays, peak brightness and paper white.
- **[Frame Generation and NVIDIA Reflex](frame-generation-reflex.md)** — DLSS or FSR, and the
  low-latency mode.
- **[Performance](performance.md)** — what costs the most FPS and how to tune it for your GPU.
- **[Linux](linux.md)** — glibc, drivers, Flatpak launchers and what to check if it does not start.
- **[Common problems](problems.md)** — "unavailable" screens, graphics API switches, and what to do
  when something does not work.

## For developers

- **[Building and releasing](building.md)** — Windows, Linux, GitHub Actions and how a release goes out.
- **[Making mods for Radiante](api.md)** — the public API: ray tracing state, the sun, your own geometry, settings.
- **[Addon settings](addons.md)** — how a mod puts its options in Radiante's settings.
- **[How PrismRT works](../prismrt.md)** — the native renderer's architecture, for contributors (English).

## Settings reference by section

Radiante's settings screen (`F6` in game) has six sections. Each one has its own page with every
control, its default value, and what it does:

| In-game section | Document |
|---|---|
| Quality | [settings/quality-and-upscaling.md](settings/quality-and-upscaling.md) |
| Image | [settings/image.md](settings/image.md) |
| Lighting | [settings/lighting.md](settings/lighting.md) |
| Sky & Water | [settings/sky-fog-water.md](settings/sky-fog-water.md) |
| Performance | [settings/performance.md](settings/performance.md) |
| Other | [settings/other.md](settings/other.md) |

## What Radiante is, in short

- Hardware ray tracing for terrain: direct light, soft shadows and global illumination, with 4
  light bounces by default, a radiance cache (SHaRC) for the deeper ones, and ReSTIR for block
  lights.
- Physical sky, volumetric fog (froxel grid or per pixel), volumetric clouds with
  shadows, rain with refraction and wet ground, seamless glass.
- Biome Ambiance: every biome's own air and water, kept subtle, for vanilla, modded and Bedrock
  pack biomes alike.
- LabPBR (`_n`/`_s`) for blocks, entities and items; Bedrock RTX `.mcpack` resource packs, read
  with their own materials; radiante-pt's defaults are the Bedrock RTX look.
- Presets: save a look, switch between looks, share one as a code.
- FSR built in; DLSS and XeSS downloadable. DLSS or FSR frame generation and NVIDIA Reflex.
- HDR output, motion blur and depth of field.
- Experimental Distant Horizons support.
- Runs on the Vulkan device Minecraft already creates. The jar is about 15 MB.

## Known limitations

- NRD pipelines (FSR, XeSS, native) are somewhat noisier than DLSS Ray Reconstruction.
- AMD and Intel work but are less tested than NVIDIA.
- Frame generation and Reflex are tested on Fabric; on NeoForge and Forge they run the same code,
  untested so far. Frame generation is not available with HDR.
- XeSS is Windows only. After installing DLSS or XeSS, the next start asks whether to switch to
  its pipeline; you can also pick it later in the settings.
- Shader Execution Reordering is experimental (water may look white).
- Distant Horizons is still experimental: see [distant-horizons.md](distant-horizons.md).

Known bugs and ideas are tracked in [GitHub Issues](https://github.com/Gabrieli2806/Radiante/issues),
and where the project is heading in the
[roadmap issues](https://github.com/Gabrieli2806/Radiante/issues?q=is%3Aopen+label%3Aroadmap).

## Links

- [GitHub](https://github.com/Gabrieli2806/Radiante) — source code;
  [issues](https://github.com/Gabrieli2806/Radiante/issues) for bug reports and ideas.
- [Modrinth](https://modrinth.com/project/radiante) · [CurseForge](https://www.curseforge.com/minecraft/mc-mods/radiante)
- [Discord](https://discord.gg/DhBbAzugZ9) — for questions, reports, and discussing large changes
  before a PR.
- [Ko-fi](https://ko-fi.com/gabrieli2806)

## Licence

GPL-3.0. Radiante began as a fork of Radiance and MCVR by LJIONG and Interstellarss.
