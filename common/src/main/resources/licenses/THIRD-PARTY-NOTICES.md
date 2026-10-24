# Third party notices

Radiante itself is licensed under the GNU General Public License v3.0, as is Radiance,
the mod it derives from. This file covers the third party components redistributed
inside the mod jar, or downloaded by it, which are **not** under the GPL and carry their
own terms.

This software contains source code provided by NVIDIA Corporation.

## Radiante's renderer

`radiante-native/prismrt.dll` (Windows) and `radiante-native/linux-x64/libprismrt.so`
(Linux) are PrismRT, Radiante's own renderer, and are GPL-3.0. They link statically
against the components below.

| Component | Owner | License |
| --- | --- | --- |
| DLSS / NGX SDK (Super Resolution, Ray Reconstruction, Frame Generation) | NVIDIA | [NVIDIA RTX SDK](NVIDIA-RTX-SDK-LICENSE.txt) |
| NRD (NVIDIA Real-time Denoisers) | NVIDIA | [NVIDIA RTX SDK](NVIDIA-NRD-LICENSE.txt) |
| FidelityFX SDK (FSR upscaling and frame generation, and FSR 1 EASU/RCAS for the output resolution upscale) | AMD | [MIT](AMD-FidelityFX-LICENSE.txt) |

## Source shipped in the shader pack

The built-in shader pack (`radiante-native/shaders/world/ray_tracing/radiante-pt.zip`)
carries the shader headers of SHaRC (Spatially Hashed Radiance Cache), by NVIDIA, under
the [NVIDIA RTX SDK](NVIDIA-RTX-SDK-LICENSE.txt) license.

## Downloaded on request, not in the jar

The DLSS and XeSS runtimes are not shipped. When a player asks for them in game, Radiante
downloads them from the vendors' own repositories into the game's `radiante/` folder:

| File | Component | Owner | License |
| --- | --- | --- | --- |
| `dlss/nvngx_dlss.dll`, `nvngx_dlssd.dll`, `nvngx_dlssg.dll` (Windows); `dlss/libnvidia-ngx-dlss*.so.*` (Linux) | DLSS Super Resolution, Ray Reconstruction and Frame Generation | NVIDIA | [NVIDIA RTX SDK](NVIDIA-RTX-SDK-LICENSE.txt) |
| `libxess.dll` (Windows) | XeSS Super Resolution | Intel | [Intel XeSS](Intel-XeSS-LICENSE.txt) |

They stay proprietary files, are not licensed under the GPL, and are combined with
Radiante under the additional permission in [LICENSE-EXCEPTION.md](LICENSE-EXCEPTION.md).
NVIDIA Reflex is used through the driver's `VK_NV_low_latency2` extension; no Reflex or
Streamline library is shipped or downloaded.

## Note on the NVIDIA RTX SDK terms

Section 2 of the NVIDIA RTX SDK license sets conditions on redistributing its binaries:
the application must add material functionality beyond the SDK, the notice "This
software contains source code provided by NVIDIA Corporation" must accompany derived
source, and the SDK may not be distributed as a standalone product. Radiante is a
Minecraft renderer that uses these components as one part of a larger work, and the
notice appears above.
