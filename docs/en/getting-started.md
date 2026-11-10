[← Back to index](README.md)

# Getting started

What you need, how to install Radiante, and what happens the first time it runs.

## Requirements

| | Windows | Linux |
|---|---|---|
| System | Windows 10 or 11, x64 | x86-64 with glibc 2.35+ (Ubuntu 22.04, Debian 12, Fedora 36 or newer) |
| GPU | Vulkan hardware ray tracing (see below) | The same, on the GPU's real driver (not `llvmpipe`) |
| Java | 25 | 25 |

The same jar works on both systems. macOS, Windows on ARM, 32-bit Java and Android launchers are not supported:
there the game starts with Minecraft's own renderer and says so once.

### GPU

Ray tracing in Vulkan is required (`VK_KHR_ray_tracing_pipeline` and `VK_KHR_acceleration_structure`). There is no
software fallback.

| | NVIDIA | AMD | Intel |
|---|---|---|---|
| Cards | RTX 20 or newer | RX 6000 or newer | Arc |
| FSR (upscaling, frame generation) | Built in | Built in | Built in |
| DLSS (upscaling, Ray Reconstruction, frame generation) | Downloaded in game | — | — |
| XeSS (Windows only) | If the driver passes XeSS's check | Same | Recommended |
| NVIDIA Reflex | With a recent driver | — | — |

NVIDIA is the main target: DLSS Ray Reconstruction gives the cleanest picture. AMD and Intel work, but are tested
less.

### Minecraft and loader

Pick the jar for your Minecraft version and loader. All of them come from the same code and look the same.

| Minecraft | Fabric | NeoForge | Forge |
|---|---|---|---|
| **26.3** | Loader 0.19.5+ and Fabric API (built with 0.165.2) | 26.3.0.101+ | 26.3-66.0.3+ |
| 26.2 | Loader 0.19.5+ and Fabric API | 26.2.0.93+ | 26.2-65.1.5+ |
| 26.4 snapshots | Loader 0.20.1+ and Fabric API | — | — |

Fabric is the most tested. NeoForge and Forge run the same code, frame generation and Reflex included.

### Recommended

- A resource pack with LabPBR maps (`_n`/`_s`), or a Bedrock RTX `.mcpack`: see [Bedrock RTX](bedrock-rtx.md).
  Without one, Radiante uses its own maps for 159 vanilla textures.
- [Distant Horizons](https://modrinth.com/mod/distanthorizons), to trace far terrain too:
  see [Distant Horizons](distant-horizons.md).
- An HDR display: see [HDR](hdr.md).

## Install

Radiante is client-only: servers do not need it. Download it from [Modrinth](https://modrinth.com/project/radiante),
[CurseForge](https://www.curseforge.com/minecraft/mc-mods/radiante) or
[GitHub](https://github.com/Gabrieli2806/Radiante/releases).

1. Install the loader: [Fabric](https://fabricmc.net/use/) (and put **Fabric API** in `mods`),
   [NeoForge](https://neoforged.net/) or [Forge](https://files.minecraftforge.net/).
2. Put the Radiante jar for that loader (`radiante-fabric-…`, `radiante-neoforge-…` or `radiante-forge-…`) in
   `mods`.
3. Start the game with that profile.

> [!NOTE]
> On Linux with a Flatpak launcher (Prism, Modrinth...), read [Linux](troubleshooting.md#linux) first: the sandbox has
> to see your GPU's driver.

## First start

1. **The renderer is unpacked.** Radiante unpacks its native library into the game's `radiante/` folder and checks
   it. This only repeats after an update.
2. **Shaders compile in the background.** A popup at the top right shows the progress, a few seconds after the title
   screen. It only happens again after an update or a new graphics driver. You can use the menus meanwhile.
3. **DLSS or XeSS is offered.** FSR is built in. On NVIDIA a window offers DLSS (~115 MB); on Intel (Windows), XeSS
   (~73 MB). Download it, leave it for later, or never be asked again. See [Upscalers](upscalers.md).
4. **Enter a world.** Radiante turns on with the Bedrock RTX look.

If something else shows up instead:

| Screen | What to do |
|---|---|
| "Radiante: Vulkan needed" | The game is on OpenGL. Press **Switch to Vulkan** and restart. |
| "Radiante: ray tracing unavailable" | The GPU or driver has no ray tracing, or the game is on software Vulkan. See [Troubleshooting](troubleshooting.md#my-gpu-does-not-support-ray-tracing). |

<!-- Screenshot pending: the full "Radiante: ray tracing unavailable" screen -->

## Keys

| Key | Action |
|---|---|
| `F6` | Opens Radiante's settings (also: Options → Video Settings → Radiante). |
| Toggle Ray Tracing (unbound) | Turns ray tracing on or off at once. Give it a key in Minecraft's controls. |

## Next

- [Settings](settings.md): how the settings screen works.
- [Quality and performance](quality-and-performance.md): get the most FPS out of your GPU.
- [Troubleshooting](troubleshooting.md): if something does not start or looks wrong.
