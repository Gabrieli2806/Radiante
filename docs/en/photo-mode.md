[← Back to index](README.md)

# Photo Mode

Photo Mode pauses the scene, frees the camera and averages frame after frame of the traced picture through a camera-traced physical thin-lens model. With **Denoiser** on (the default) it averages the denoised picture and comes out clean in a few dozen samples; off, it averages the raw path tracing for a reference picture with no denoiser in it, which needs thousands of samples before the noise is gone.

## Opening Photo Mode

Photo Mode has no key until you give it one: in Minecraft's **Options → Controls → Key Binds**, under **Radiante**, bind **Photo Mode**, then press that key while in a world.

> [!TIP]
> Enter Photo Mode from first person for a clean camera without your character in the shot, or switch to third person (`F5`) first if you want your character posed in the world while you fly the camera around them.

## Controls

| Input | Action |
|---|---|
| `WASD` / `Space` / `Shift` | Fly the Photo Mode camera (hold `Ctrl` to move faster) |
| Mouse wheel (on viewport) | Adjust camera fly speed (`Shift` + wheel adjusts Field of View) |
| Drag mouse (on viewport) | Aim the camera |
| Left-click (on viewport) | Focus the lens on the block or entity under the cursor |
| `H` or `F1` | Hide or show the Photo Mode UI |
| `F2` | Queue saving a PNG once target samples are reached |
| `R` | Reset camera pose, lens and exposure |
| `Esc` | Leave Photo Mode and return to normal play |

## Camera and physical thin-lens settings

- **Denoiser (On / Off)**: On, the game's denoiser (DLSS Ray Reconstruction, or the one your upscaler uses) cleans each frame and Photo Mode averages them: clean in a few dozen samples. Off, the raw path tracing is averaged, a reference with no denoiser, which needs thousands of samples. Switching rebuilds the pipeline for a moment.
- **Target Samples (`64` / `256` / `1024` / `4096`)**: How many path-traced samples per pixel accumulate before ray tracing pauses (and before a queued capture writes to disk).
- **Aperture**: Physical radius of the camera lens in blocks (`0` = pinhole with infinite depth of field). Higher values produce shallower depth of field and true ray-traced bokeh through glass, water and reflections.
- **Focus Distance**: Distance in blocks to the sharp focal plane. Click any block or entity directly in the viewport to pull focus onto it.
- **Blades (`Round` / `5` / `6` / `7` / `8`)** and **Blade Angle**: Switches between a circular iris and a polygonal aperture iris with rotatable bokeh highlights.
- **Field of View (`15°`–`120°`)** and **Camera Roll (`-45°`–`+45°`)**: Telephoto or wide-angle framing and Dutch-angle camera tilt.
- **Exposure (`-4` to `+4 EV`)**: Adjusts tone-mapped brightness live **without resetting** your accumulated samples.
- **Hands**: Hides or shows first-person hands and held items.

## Saving PNG and HDR OpenEXR

Captures are written to your game folder's `screenshots/` directory (`radiante_photo_<date>_<time>.png` / `.exr`):

- **Save PNG**: Saves the tone-mapped, colour-graded 8-bit PNG image.
- **Save EXR**: Saves the raw linear 16-bit float (`HALF`) RGB scene radiance accumulated before tone mapping as an OpenEXR 2.0 (`.exr`) file, ready for external grading in Darktable, Blender, DaVinci Resolve or Photoshop.
- **Save PNG + EXR**: Saves both files from the same converged accumulation buffer.
