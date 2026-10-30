[← Settings](../settings.md) · [Index](../README.md)

# Debug

Tools for finding out what the renderer is doing and for reporting problems. The section is only
there while **Advanced** is on.

| Control | Values | Default | What it does |
|---|---|---|---|
| **Debug View** | Off, and the views of the pack in use | Off (not saved) | Shows one of the renderer's inner images instead of the picture, the same as `/radiante debug view <view>`. Changing it rebuilds the shaders. |
| **SHaRC Debug Mode** | Off, and the cache's views | Off | Shows the light cache's internal views instead of the picture. |
| **HDR Debug View** | On/Off, only while HDR Output is on | Off (not saved) | Tints the SDR parts of the picture blue. See [HDR](../hdr.md). |
| **GPU Timings in F3** | On / Off | On | Lists the time of each render pass on the F3 screen. The same choice as in the debug options screen (`F3` + `F6`). |
| **Debug Logging** | On / Off | On | Writes Radiante's and the native renderer's detail to `latest.log` (`[native] ...` lines). On by default so one log is enough to report a problem. Renderer errors reach the log even with it off. |
| **Bug Report Summary** | Copy | | Copies the versions, the GPU, the running pipeline and every pipeline setting to the clipboard, and writes them to the game log. Paste it into your report. |
| **Game Logs** | Open Folder | | Opens the folder with `latest.log`. |
| **Shader Packs** | Open Folder | | Opens the folder shader packs are loaded from. |

## Debug views

| View | Shows |
|---|---|
| Albedo, Normals, Roughness, Metallic, Emission, Depth, Motion Vectors | The frame's image of that name. |
| Surface Kinds | One colour per kind of surface the camera sees: water, lava, glass, leaves, plants... |
| Any-hit Heat Map | How many times an any-hit shader ran per pixel on the camera ray: blue is 1, through cyan, green and yellow to red at 16 or more; black is none. Hot areas cost frame time: stacked translucent surfaces, or hidden faces the ray crosses, such as Distant Horizons terrain under the real chunks. On GPUs with opacity micromaps most leaves and terrain are settled without an any-hit shader, so they read black. |
| Bounces Heat Map | How many rays each pixel traced after the camera ray, on the same scale. Shows where paths run long, such as between mirrors. |
| Rain | Red: how wet the surface is. Green: how open it is to the sky (works in any weather). Blue: standing water. Red and blue need rain, and rise as the wetness builds up. Water and glass stay black. |

A shader pack's own views are listed after these, under their own names. The view is not saved, so
it is off again the next time you open the game.
