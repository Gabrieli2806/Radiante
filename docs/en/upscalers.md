[← Back to index](README.md)

# Upscalers: FSR, DLSS and XeSS

Radiante traces the world at a lower resolution than the screen and an upscaler brings it to the
final resolution. Only FSR ships inside the jar; DLSS and XeSS are downloaded from the game when
you want them, so the jar stays small and nobody downloads what their GPU cannot use.

| | FSR | DLSS | XeSS |
|---|---|---|---|
| In the jar | Yes | No, ~115 MB | No, ~73 MB |
| GPU | Any | NVIDIA only | Windows; Intel Arc recommended, other GPUs depend on their driver |
| System | Windows and Linux | Windows and Linux | Windows only |
| Source | Built in | NVIDIA's repository (DLSS `v310.9.1`) | Intel's repository (XeSS `3.0.2`) |
| Pipeline | RT-FSR | RT-DLSS (with or without Ray Reconstruction) | RT-XeSS |
| Frame generation | FSR, 2x | DLSS, up to 4x on RTX 50 | — |

## First start

FSR is the default on every GPU. On first reaching the title screen:

- **NVIDIA:** a window offers DLSS.
- **Intel:** a window offers XeSS (Windows only).
- **AMD:** nothing is offered; FSR is what there is.

The window has three buttons: **Download (N MB)**, **Not now** (asks again next start) and
**Don't ask again**. Progress shows while downloading; **Continue in the background** closes the
window and the download goes on.

![First-start offer to download DLSS](../images/ui/download-offer.png)

![DLSS download with a progress bar](../images/ui/download-progress.png)

## From the settings

In [Quality](settings/quality-and-upscaling.md), the **Pipeline** selector lists the
upscalers your GPU can use even when they are not installed:

- `RT-DLSS (Install)`: can be downloaded.
- `RT-DLSS (Restart required)`: downloaded, loads after a restart.

Picking one of those turns the bottom bar into **Install DLSS (115 MB)**, **Restart to use DLSS**
or **Delete DLSS**, as fits. Installing and deleting are done **outside a world**: in a world the
button asks you to leave first. Upscalers are loaded when the game starts, so installing or
deleting always ends in a forced restart.

![Pipeline selector showing (Install) and the Install DLSS button](../images/ui/pipeline-install.png)

![Forced restart screen after installing or deleting an upscaler](../images/ui/download-restart.png)

**Upscalers.** The same view with each upscaler. The FSR and no-upscaler pipelines also draw a clearer sky, so judge the textures.

*DLSS Quality*

![DLSS Quality](../images/compare/upscalers-dlss-quality.webp)

*DLSS Ultra Performance*

![DLSS Ultra Performance](../images/compare/upscalers-dlss-ultra-performance.webp)

*FSR 3*

![FSR 3](../images/compare/upscalers-fsr.webp)

*No upscaler*

![No upscaler](../images/compare/upscalers-none.webp)

## After installing DLSS or XeSS

After the restart, the first menu asks whether to switch to the new pipeline: **Switch
(recommended)** or **Keep the current pipeline**. If you keep it, pick **RT-DLSS** (or **RT-XeSS**)
in the Pipeline selector whenever you want. A fresh install where DLSS was already downloaded starts
on RT-DLSS on its own.

When a Radiante update uses a newer DLSS or XeSS than the one installed, the same window offers it
as an update (**Update** or **Skip this version**); the installed one keeps working meanwhile.

**Reset to Defaults** picks RT-DLSS when DLSS is loaded and FSR otherwise.

## Where they are stored

In the game's `radiante/` folder (next to `mods/`). Each file is checked against its SHA-256 before
use; one that does not match is discarded. What you declined or deleted is remembered in
`radiante/downloads.properties`, so you are not asked again.

To remove an upscaler: **Delete** in the settings. Files in use are deleted on the next start.

## If the download fails

The window shows `Download failed: <reason>`. Usually it is a firewall, a proxy or no connection.
FSR keeps working meanwhile; retry from the settings. If the reason is not obvious, `latest.log`
has the detail: see [Common problems](problems.md).
