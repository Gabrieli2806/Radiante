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

Up and Down move through the options and the headings of the advanced groups, and on past the last
one to the buttons along the bottom; Up from the first option goes to the search box. Left and Right
move a slider by one step (ten with Shift), step a list, fold a group shut or open, or move between
the buttons. Enter or Space flips a switch, steps a list, folds a group or presses the button. Tab
moves to the next section (Shift+Tab: the previous one).

## Controller

The screen works with controller mods: those that move through menus the way Minecraft's arrow keys
do (Controlify and the like) and those that press keys for the controller's buttons. The D-pad or
left stick moves as the arrow keys above, A acts as Enter and B leaves the screen like **Done**. With
Controlify, also:

| Button | Does |
|---|---|
| LB / RB | Previous / next section |
| X | Apply |
| Y | Undo |
| Right stick | Up and down scroll the list (the focus stays on a row in view); left and right move a slider ten steps |

Controlify shows its button pictures beside Done, Apply and Undo and under the sections. Its virtual
mouse stays off on this screen unless you turn it on there.

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
| *The pack's name* | Only with a shader pack other than Radiante PT: that pack's own settings ([below](#the-packs-own-page)). |
| [Addons](addons.md) | Only with an addon installed: its own options, under its name. |

A setting only shows when the pipeline, GPU or shader pack supports it: DLSS Mode only with
RT-DLSS, Upscaling Mode only with RT-FSR or RT-XeSS, Reflex only on NVIDIA, frame generation not
with HDR Output. If something in these docs is missing from your screen, that is why.

## Presets

**Presets**, next to the search box, keeps your looks: save the current one under a name, switch
between them with a click, copy one as a code to share, or import a code someone sent you. A
preset holds lighting, sky, fog, water, tone and camera effects; the upscaler, frame generation
and HDR stay as they are.

## Shader packs

**Shader Packs**, beside Presets, chooses the path tracer: the built-in **Radiante PT** (the
default) or a pack you added. Packs go in `radiante/shaderpacks/` inside the game folder (**Open
Folder** opens it), as a `.zip` or as a folder, with the pack's `configs.json` at the top: a zip
of a folder that holds the pack (as GitHub's "Download ZIP" makes) is listed with that problem.
This is Radiante's own folder, not the game's `shaderpacks/`, which Iris and OptiFine use for a
different kind of pack.

The list shows each pack's icon, name, version, author and licence, where the pack gives them.
Click a pack and press **Use** (or double-click it, or press Enter) to switch: the shaders compile
as for any other change. If the pack does not load or a shader does not compile, Radiante goes
back to Radiante PT and shows what went wrong under the list (the compiler's messages, with file
and line); **Copy Error** copies it for the pack's author. If the game closes while a pack is
being tried (a crash, or the GPU stops responding), the next start goes back to Radiante PT and
says so here. Each pack keeps its own settings in a
file next to it (`<pack>.zip.txt`), so switching back and forth keeps them. Most of Radiante's
settings belong to Radiante PT; another pack shows the ones it shares with Radiante PT where they
always are, and its own on a page of its own.

### The pack's own page

With a pack other than Radiante PT in use, the sidebar gets one more section, named after the pack
and with its icon. It holds the settings the pack adds:

- **Pack Preset** at the top, when the pack has presets: sets of its settings made by its author.
  Picking one sets them all; change one afterwards and it shows **Custom**.
- The settings under headings the pack chooses (Sky, Clouds, Colour Grade...), each heading
  folding like an addon's. The pack's finer controls show only with **Advanced** on.
- Some settings only show while another one has a given value (a cloud's amount while the pack's
  clouds are on, for example).
- The panel shows the pack's description of each setting and, where the pack says, its
  **Performance impact**.

Changes are applied with **Apply** or **Done** like the rest: some change at once, others compile
the pack's shaders again. The renderer profile (Bedrock RTX) only sets the pack's settings that the
pack allows it to; the others stay as the pack and you set them.

A pack kept as a folder reloads by itself when one of its files changes, for pack authors: it is
checked in the background first, and an edit that does not compile leaves the picture as it was
and shows the error here. `/radiante pack validate [name]` checks a pack without switching to it,
and `/radiante debug view <view>` shows the albedo, normals, depth and other frame images instead of
the picture (`off` goes back). See [Making a shader pack](shader-packs.md#working-on-a-pack-in-game).

Packs made for Radiance (the mod Radiante grew from) use the same format but an older renderer
interface, and do not work as they are yet.

## Advanced

**Advanced: OFF / ON**, at the bottom, shows the fine-tuning controls of each section in folding
groups under its main ones (exposure and tone, camera effects, light, clouds, atmosphere, water,
fog, stars, sun and moon path, denoiser, renderer, upscaler, debug views, other pipeline settings).
Their values apply whether or not they are shown. They are the pipeline's own parameters: change
them one at a time, and use **Undo** or **Reset to Defaults** to come back.
