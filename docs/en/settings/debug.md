[← Settings](../settings.md) · [Index](../README.md)

# Debug

Tools for finding out what the renderer is doing and for reporting problems. The section is only
there while **Advanced** is on.

| Control | Values | Default | What it does |
|---|---|---|---|
| **Debug View** | Off, and the views of the pack in use | Off (not saved) | Shows one of the renderer's inner images instead of the picture, the same as `/radiante debug view <view>`. Changing it rebuilds the shaders. |
| **SHaRC Debug Mode** | Off, and the cache's views | Off | Shows the light cache's internal views instead of the picture. |
| **HDR Debug View** | On/Off, only while HDR Output is on | Off (not saved) | Tints the SDR parts of the picture blue. See [HDR](../hdr.md). |
| **GPU Timings in F3** | On / Off | On | Lists on the F3 screen the time of each render pass, and the scene: TLAS instances, entity BLAS built per frame, sections waiting to build, and the device memory of the renderer's own allocator (Minecraft's is not counted). Both can also be hidden on their own in the debug options screen (`F3` + `F6`). |
| **Debug Logging** | On / Off | On | Writes Radiante's and the native renderer's detail to `latest.log` (`[native] ...` lines). On by default so one log is enough to report a problem. Renderer errors reach the log even with it off. |
| **Bug Report Summary** | Copy | | Copies the versions, the GPU, the running pipeline and every pipeline setting to the clipboard, and writes them to the game log. Paste it into your report. |
| **A/B Capture** | Capture | | Closes the screen (applying what you changed), and a second later saves `screenshots/radiante_ab_<time>.png` with a `.txt` of the same name: the Bug Report Summary, GPU timings included. If the change rebuilds the shaders, wait for the rebuild with a second capture. |
| **Game Logs** | Open Folder | | Opens the folder with `latest.log`. |
| **Shader Packs** | Open Folder | | Opens the folder shader packs are loaded from. |

## Debug views

| View | Shows |
|---|---|
| Albedo, Normals, Roughness, Metallic, Emission, Depth | The frame's image of that name. |
| Motion Vectors | Where each pixel moved since the last frame: grey is still, more red is rightwards, less red leftwards, more green downwards, less green upwards; 32 pixels reach full colour. |
| Surface Kinds | One colour per kind of surface the camera sees: water, lava, glass, leaves, plants... |
| Any-hit Heat Map | How many times an any-hit shader ran per pixel on the camera ray: blue is 1, through cyan, green and yellow to red at 16 or more; black is none. Hot areas cost frame time: stacked translucent surfaces, or hidden faces the ray crosses, such as Distant Horizons terrain under the real chunks. On GPUs with opacity micromaps most leaves and terrain are settled without an any-hit shader, so they read black. |
| Bounces Heat Map | How many rays each pixel traced after the camera ray, on the same scale. Shows where paths run long, such as between mirrors. |
| Rain | Red: how wet the surface is. Green: how open it is to the sky (works in any weather). Blue: standing water. Red and blue need rain, and rise as the wetness builds up. Water and glass stay black. |
| Direct Light, Indirect Light | The noisy light on diffuse surfaces before the denoiser, compressed to the screen's range: straight from the lights, or after bouncing. A surface whose path took the mirror lobe this frame reads black, so expect noise. Rain hides the sun, so Direct Light goes dark in it. |
| SHaRC Contribution | The light each pixel's paths took from the SHaRC cache, compressed to the screen's range, before exposure, so it reads dim. Black with SHaRC off, and nearly black outdoors on open ground, where bounces leave for the sky and never ask the cache; indoors most pixels show it. |
| Raw Diffuse, Raw Specular | The noisy signals of the first surface before the denoiser, compressed to the screen's range. With NRD they are the signals NRD reads; with DLSS Ray Reconstruction it reads the whole picture instead, and these show the same light split apart. |
| Fog Light, Fog Transmittance | What the fog does on its own: the light it adds (compressed to the screen's range), and how much of what is behind it gets through, white for all, black for none. |

A shader pack's own views are listed after these, under their own names. The view is not saved, so
it is off again the next time you open the game.
