[← Back to index](README.md)

# Radiante settings

Opened with `F6` in a world, or from Radiante's button in Options → Video Settings (title screen
included). The sections are listed on the left (Quality, Image, Lighting, Sky & Water, Performance,
Other) and one is shown at a time; the panel on the right describes the option under the mouse.
Changes add up and are applied together with **Apply** (the screen stays open) or **Done**, so the
renderer rebuilds once; **Undo** drops what was changed since the last Apply. While a change
compiles, a popup in the top right corner shows the progress (compiling shaders, building pipelines,
then "Settings applied" or the error); you can keep using the menus and the game meanwhile. This
works the same in a world and on the title screen.

![Radiante settings: Quality](../images/ui/settings-quality.png)

## Search

The **Search...** box filters settings across every section by name. The quickest way to find
something without knowing its section. With Advanced off, a section says how many more matches it
has among the advanced settings.

## Keyboard

Up and Down move through the options, Left and Right move a slider by one step (ten with Shift) or
step a list, Enter or Space flips a switch, and Tab moves to the next section (Shift+Tab: the
previous one).

## What the panel says

- **Performance impact: None / Low / Medium / High / Varies** for the settings that cost frame
  time. Varies means it depends on the scene (for example, on how many lights there are).
- A row that does nothing as things are set is grey, and the panel says why: another setting has to
  be turned on first, another one takes its place, ray tracing is off, or Bedrock's files are missing.
- **Also decides:** the settings this one switches off or takes the place of.
- A setting the renderer profile manages says so: changing it turns the profile into Custom.

## Quality and Reset to Defaults

**Quality** (Low / Medium / High / Ultra / Custom) sets the costliest controls at once: upscaler
mode, light bounces, volumetric fog, clouds, render distance and more. Changing one of those by
hand turns it into Custom. Details in
[styles-and-quality.md](styles-and-quality.md#quality-levels).

**Reset to Defaults**, at the bottom left, returns everything to a fresh install, including
pipeline and shader pack settings that have no control here, and picks the Bedrock RTX (Enhanced)
profile again. Holding **Ctrl** turns it into **Rebuild Shaders**: it applies the changes and
builds the shaders again, for when the picture looks wrong after changing settings.

## Settings that need a restart

HDR Output, and installing or deleting an upscaler, only apply after a restart. HDR Output is
marked **(restart)** and, when you leave the screen, a notice lists the pending changes with
**Close the game now** or **Later**.

## Sections

| Section | Contents |
|---|---|
| [Quality](settings/quality-and-upscaling.md) | Ray tracing switch, quality level, pipeline, DLSS or upscaling mode, Ray Reconstruction, render and output resolution, frame generation, Reflex. |
| [Image](settings/image.md) | Renderer profile, picture style, exposure, FSR sharpness, HDR, motion blur, depth of field. |
| [Lighting](settings/lighting.md) | Brightness, bounced and sky light, block light sampling, ReSTIR, SER, held item light, first person shadow. |
| [Sky & Water](settings/sky-fog-water.md) | Atmosphere, clouds, stars, sun, sun path, rain, glass, biome ambiance, volumetric fog, water. |
| [Performance](settings/performance.md) | Light bounces, mirrors, glass, water texture, portal, rain, cache, checkerboard, parallax, Bedrock pack materials, fog quality, chunk building. |
| [Other](settings/other.md) | Block outline, other mods' overlays, text backgrounds, parallax edges, debug logging. |

A setting only shows when the pipeline, GPU or shader pack supports it: DLSS Mode only with
RT-DLSS, Upscaling Mode only with RT-FSR or RT-XeSS, Reflex only on NVIDIA, frame generation not
with HDR Output. If something in these docs is missing from your screen, that is why.

## Presets

**Presets**, next to the search box, keeps your looks: save the current one under a name, switch
between them with a click, copy one as a code to share, or import a code someone sent you. A
preset holds lighting, sky, fog, water, tone and camera effects; the upscaler, frame generation
and HDR stay as they are.

## Advanced

**Advanced: OFF / ON**, at the bottom, shows the fine-tuning controls of each section in folding
groups under its main ones (exposure and tone, camera effects, light, clouds, atmosphere, water,
fog, stars, sun and moon path, denoiser, renderer, upscaler, debug views, other pipeline settings).
Their values apply whether or not they are shown. They are the pipeline's own parameters: change
them one at a time, and use **Undo** or **Reset to Defaults** to come back.
