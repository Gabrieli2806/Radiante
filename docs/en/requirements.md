[← Back to index](README.md)

# Requirements

## System

| | Windows | Linux |
|---|---|---|
| Architecture | x64 | x86-64 |
| Minimum version | Windows 10/11 | glibc 2.35 or newer: Ubuntu 22.04, Debian 12, Fedora 36 or later |
| Graphics API | Vulkan | Vulkan, on the GPU's real driver (not `llvmpipe`) |
| Java | 25 | 25 |
| Minecraft | 26.3 (also 26.2 and 26.4 snapshots, see [Loader](#loader)) | same |

The same jar works on both systems: it carries the Windows renderer (`prismrt.dll`) and the Linux
one (`libprismrt.so`) and picks the right one at start. macOS, Windows on ARM (Snapdragon and other ARM64
laptops), 32-bit Java and Android launchers are not supported: there the game starts with Minecraft's own
renderer and a one-time screen says that this device is not supported yet. The same happens if the renderer's
library cannot be loaded on a supported system (`logs/latest.log` has the reason).

Linux has a few details of its own (Flatpak launchers, Wayland, glibc): see [Linux](linux.md).

## GPU

Hardware ray tracing in Vulkan is required: `VK_KHR_ray_tracing_pipeline` and
`VK_KHR_acceleration_structure`. There is no software mode; without it the game keeps Minecraft's
own renderer.

| | NVIDIA | AMD | Intel |
|---|---|---|---|
| Cards | RTX 20 or newer | RX 6000 or newer | Arc |
| FSR (upscaling and frame generation) | Yes, built in | Yes, built in | Yes, built in |
| DLSS (upscaling, Ray Reconstruction, frame generation) | Yes, downloaded in game (~115 MB), Windows and Linux | No | No |
| XeSS (~73 MB, Windows only) | Can be installed; works if the driver passes XeSS's own check | Same as NVIDIA | Recommended; offered on first start |
| NVIDIA Reflex | Yes, if the driver has `VK_NV_low_latency2` and `VK_KHR_present_id` | No | No |
| Shader Execution Reordering | RTX 40 or newer | No | No |

NVIDIA is the main target today: DLSS Ray Reconstruction is the least noisy path. AMD and Intel
work with FSR (and XeSS) but have not been tested as deeply yet.

How DLSS and XeSS are downloaded: [Upscalers](upscalers.md).

## Loader

Minecraft 26.3 with **one** of:

| Loader | Minimum version |
|---|---|
| Fabric | Fabric Loader `0.19.5+` and Fabric API (built against `0.162.0+26.3`) |
| NeoForge | `26.3.0.10-beta+` |
| Forge | `26.3-66.0.3+` |

The other Minecraft versions have builds of their own:

| Minecraft | Loaders |
|---|---|
| 26.2 | Fabric Loader `0.19.5+` with Fabric API, NeoForge `26.2.0.93+`, Forge `26.2-65.1.5+` |
| 26.4 snapshots | Fabric only: Fabric Loader `0.20.1+` with Fabric API |

All jars come from the same code and look the same. Frame generation and Reflex do not depend on
the loader: they are the same code everywhere. They are tested on Fabric (Windows); NeoForge and
Forge share the code but are not tested yet.

## Recommended, not required

- A resource pack with LabPBR maps (`_n`/`_s`) or a Bedrock RTX `.mcpack`: see
  [Bedrock RTX](bedrock-rtx.md). Without one, Radiante uses its own maps for 159 vanilla textures.
- [Distant Horizons](https://modrinth.com/mod/distanthorizons) to trace far terrain too: see
  [Distant Horizons](distant-horizons.md).
- An HDR display for HDR output: see [HDR](hdr.md).
