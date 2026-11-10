[← Back to index](README.md)

# Troubleshooting

Find your problem below. If none fits, see [Still stuck](#still-stuck).

**Contents:** [The log](#first-the-log) · [Start-up](#start-up) · [Upscalers](#upscalers) ·
[Frame generation, Reflex and HDR](#frame-generation-reflex-and-hdr) · [Picture and speed](#picture-and-speed) ·
[Linux](#linux) · [Still stuck](#still-stuck)

## First: the log

Everything Radiante and its renderer say goes to `logs/latest.log`, as lines with `Radiante` and `[native]`. Search
for those first. That one file is enough to report a problem.

## Start-up

### The game is on OpenGL

Radiante needs Vulkan. The "Radiante: Vulkan needed" screen offers **Switch to Vulkan**; restart afterwards. You can
also set the Graphics API to "Default" or "Prefer Vulkan" in Video Settings.

If Minecraft switched to OpenGL on its own after a failed start (or because two instances opened at once),
"Radiante: restart the game" says so. It only lasts that session: restart, one instance at a time.

### "Radiante: could not start"

The renderer did not start this time, usually because several Minecraft instances opened at once or video memory
ran short. Close the other instances and restart. If it repeats, the log says why.

### My GPU does not support ray tracing

"Radiante: ray tracing unavailable" means the GPU or driver does not report `VK_KHR_ray_tracing_pipeline`. Check the
[requirements](getting-started.md#gpu) and update the driver. On Linux it also happens when the game runs on software
Vulkan (`llvmpipe`): see [Linux](#linux). You can keep playing on Minecraft's renderer.

### Black world

Search the log for `[native]`: the renderer says what failed. An error repeated every frame shows once, with its
count. On Linux, see [Linux](#linux).

## Upscalers

| Problem | Cause and fix |
|---|---|
| DLSS is missing from the Pipeline list | DLSS is NVIDIA only. Not installed, it shows as `RT-DLSS (Install)`. |
| XeSS is missing | XeSS is Windows only, and on non-Intel GPUs the driver must pass XeSS's own check. |
| It shows `(Restart required)` | Installed, but it loads at start: restart. |
| I installed DLSS and still use FSR | After the restart you chose **Keep the current pipeline**. Pick **RT-DLSS** in the Pipeline list. |
| The install button asks me to leave the world | Upscalers are installed from the menus and end in a restart. Go to the title screen, then Options → Video Settings → Radiante. |
| The download failed | Usually a firewall, proxy or no connection. FSR keeps working; retry from the settings. |

More in [Upscalers](upscalers.md).

## Frame generation, Reflex and HDR

- **Frame Generation does not show:** it is hidden while **HDR Output** is on.
- **Reflex does not show:** it needs an NVIDIA GPU and a recent driver (`VK_NV_low_latency2`).
- **HDR changes nothing:**
  1. Restart the game: HDR Output only applies at start.
  2. Turn HDR on in Windows for that monitor (Settings → System → Display → HDR). Supporting HDR is not enough.
  3. If the setting says "On, but not running yet" after a restart, step 2 is the problem.

## Picture and speed

- **It runs slowly:** see [Quality and performance](quality-and-performance.md). Start with the Quality level, then
  the upscaler mode and Light Bounces.
- **The picture looks wrong after changing settings:** hold **Ctrl** and press **Rebuild Shaders** (bottom left of
  the settings) to apply and build the shaders again.
- **I want a fresh install's settings back:** **Reset to Defaults**, bottom left of the settings.
- **Distant Horizons terrain looks odd:** lower Distant Horizons' own distance and check again. See
  [Distant Horizons](distant-horizons.md).

## Linux

Radiante runs on Linux x86-64 with the same jar as Windows. It needs:

- **glibc 2.35 or newer** (Ubuntu 22.04, Debian 12, Fedora 36, Arch...).
- **Your GPU's real Vulkan driver:** NVIDIA's, or Mesa (RADV, ANV) with ray tracing. `vulkaninfo --summary` must
  show your GPU, not `llvmpipe`.
- **Java 25.**

DLSS works on Linux; XeSS does not (there is no Linux runtime).

### Flatpak launchers

Launchers installed as Flatpak (Prism, Modrinth...) run in a sandbox that must see your driver. On NVIDIA it needs
the GL extension that **exactly** matches the system driver:

```sh
cat /sys/module/nvidia/version                                       # e.g. 595.91.07
flatpak install flathub org.freedesktop.Platform.GL.nvidia-595-91-07 # dots become dashes
flatpak run --command=vulkaninfo <launcher-id> --summary             # check inside the sandbox
```

Repeat after every driver update. Non-Flatpak launchers need nothing extra.

### What the log says

| Log line | Meaning and fix |
|---|---|
| `Using graphics device: llvmpipe` | Not on the GPU driver. See Flatpak above. |
| `GLIBC_2.xx not found` | The distribution is too old. |
| `UnsatisfiedLinkError` | The renderer could not be loaded. Delete `libprismrt.so` from the game's `radiante/` folder and start again. |
| `FSR3 CreateContext failed` | A renderer built without the Linux fixes. Use a release build. |

Wayland and X11 both work. If you see a black screen or flicker when switching windows, try the launcher on
X11/XWayland. Also check that the launcher runs the instance you think: Flatpak and non-Flatpak Modrinth keep
separate folders, and the log's `gameDir` shows which one is running.

## Still stuck

Ask on [Discord](https://discord.gg/DhBbAzugZ9) or open an
[issue](https://github.com/Gabrieli2806/Radiante/issues/new/choose) with your `latest.log`, GPU, system and loader.
Check the [open issues](https://github.com/Gabrieli2806/Radiante/issues) first.
