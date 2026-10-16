[← Back to index](README.md)

# Radiante settings

Opened with `F6` in a world, or from Radiante's button in Options → Video Settings (title screen
included). Changes add up and are applied together when you press "Done", so the renderer rebuilds
once. While it does, the **Applying settings...** screen shows for a few seconds.

![Radiante settings: Quality](../images/ui/settings-quality.png)

## Search

The **Search...** box filters settings across every category by name. The quickest way to find
something without knowing its category.

## Impact labels

The settings that cost the most show **Performance impact: Low / Medium / High / Varies** in their
tooltip. Varies means it depends on the scene (for example, on how many lights there are).

## Quality and Reset to Defaults

**Quality** (Low / Medium / High / Ultra / Custom) sets the costliest controls at once: upscaler
mode, light bounces, volumetric fog, clouds, render distance and more. Changing one of those by
hand turns it into Custom. Details in
[styles-and-quality.md](styles-and-quality.md#quality-levels).

**Reset to Defaults** returns everything to a fresh install, including pipeline and shader pack
settings that have no control here. The pipeline goes back to RT-DLSS when DLSS is loaded, FSR
otherwise.

## Settings that need a restart

Some (HDR Output, installing or deleting an upscaler) only apply after a restart. They are marked
**(restart)** and, when you leave the screen, a notice lists the pending changes with **Close the
game now** or **Later**.

## Categories

| Section | Contents |
|---|---|
| [Quality & Upscaling](settings/quality-and-upscaling.md) | Ray tracing switch, pipeline, DLSS or upscaling mode, render resolution, frame generation, Reflex. |
| [Image](settings/image.md) | Renderer profile, picture style, tone mapping, exposure, HDR, motion blur, depth of field. |
| [Lighting](settings/lighting.md) | Brightness, bounce light, block light sampling, ReSTIR, SER, item light. |
| [Sky, Fog & Water](settings/sky-fog-water.md) | Atmosphere, clouds and their shadows, stars, biome ambiance, fog and its style, rain, water, glass. |
| [Performance](settings/performance.md) | Light bounces, mirrors, cache, checkerboard, parallax, Bedrock pack materials, chunk building. |
| [Other](settings/other.md) | Block outline, other mods' overlays, text backgrounds, parallax edges, debug logging. |

A setting only shows when the pipeline, GPU or shader pack supports it: DLSS Mode only with
RT-DLSS, Reflex only on NVIDIA, SER only on RTX 40 or newer. If something in these docs is missing
from your screen, that is why.

## Presets

**Presets**, next to the search box, keeps your looks: save the current one under a name, switch
between them with a click, copy one as a code to share, or import a code someone sent you. A
preset holds lighting, sky, fog, water, tone and camera effects; the upscaler, frame generation
and HDR stay as they are.

## Advanced

**Advanced: OFF / ON**, at the bottom, shows the fine-tuning controls of each category in groups
under its main ones (exposure and tone, camera effects, light, clouds, atmosphere, water, fog,
stars, denoiser, renderer, upscaler, debug views). Their values apply whether or not they are
shown; a category says how many more it has while Advanced is off. They are the pipeline's own
parameters: change them one at a time, and use **Undo** or **Reset to Defaults** to come back.
