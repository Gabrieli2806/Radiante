[← Back to index](README.md)

# Common problems

## First: the log

**Debug Logging** is on by default, and the native renderer's messages reach `logs/latest.log` as
`[native] ...` lines (errors always, even with it off). That one file is enough to report a
problem. Search it for `[native]` and `Radiante` first.

## The game is on OpenGL

Radiante needs Vulkan. If the game starts on OpenGL, the "Radiante: Vulkan needed" screen offers
**Switch to Vulkan**; restart afterwards. You can also set the Graphics API to "Prefer Vulkan" or
"Default" in the video settings yourself.

If Minecraft switched to OpenGL on its own after a failed start (or because another Minecraft was
opening at the same time), "Radiante: restart the game" says so. That is Minecraft's own safeguard,
and Radiante does not keep it as your choice: it only lasts that session. Restart, one instance at
a time.

## "Radiante: could not start"

The renderer did not start this time. This often happens when several Minecraft instances open at
once, or when video memory runs short. Close the other instances and restart; if it repeats, the
log says why.

## My GPU does not support ray tracing

"Radiante: ray tracing unavailable" screen: the GPU or driver does not report
`VK_KHR_ray_tracing_pipeline`. Check the [requirements](requirements.md#gpu) and update the driver.
It also happens with software Vulkan (`llvmpipe`), common on Linux when the game cannot see the real
driver: see [Linux](linux.md). You can keep playing without ray tracing on Minecraft's renderer.

## Black world

- **Linux, `FSR3 CreateContext` in the log**: a renderer built without the Linux fixes; see
  [Linux](linux.md).
- Search the log for `[native]`: the renderer says what failed, and an error repeated every frame
  shows once with its repeat count.

## DLSS or XeSS missing from the Pipeline selector

- **DLSS** is only offered on NVIDIA GPUs. When not installed it shows as `RT-DLSS (Install)`.
- **XeSS** only exists on Windows. On non-Intel GPUs it can be installed and still not become
  available, if the driver fails XeSS's own check.
- If you installed it, it still needs a restart: it shows as `(Restart required)`.

See [Upscalers](upscalers.md).

## I installed DLSS and I am still on FSR

The first menu after the restart asks whether to switch; if you chose **Keep the current
pipeline**, pick **RT-DLSS** in the Pipeline selector.

## The install button asks me to leave the world

Installing or deleting an upscaler is done from the menus and ends in a restart. Go back to the
title screen and open Radiante's settings from Options → Video Settings.

## The download failed

Usually the network: firewall, proxy, no connection. FSR keeps working. Retry from the settings. A
downloaded file that does not match its SHA-256 is discarded.

## Frame Generation or Reflex does not show

- **Frame Generation** is hidden while **HDR Output** is on.
- **Reflex** only shows on NVIDIA GPUs with `VK_NV_low_latency2` and `VK_KHR_present_id`. Update
  the driver.

See [Frame Generation and Reflex](frame-generation-reflex.md).

## I turned HDR on and see no change

1. Did you restart the game? HDR Output needs it.
2. Is HDR turned on in the system for that monitor, not just supported by it?
3. If the "HDR Output" description says "On, but not running yet", the problem is step 2.

See [HDR](hdr.md).

## Linux: the renderer does not load

`GLIBC_2.xx not found`, `UnsatisfiedLinkError`, `llvmpipe`: full list in [Linux](linux.md).

## Distant Horizons: odd far terrain

See [Distant Horizons](distant-horizons.md). If something breaks, lower DH's distance (not
Minecraft's) and check whether it still happens.

## It runs slowly

See the [performance guide](performance.md). First the Quality level, then the upscaler mode and
Light Bounces.

## The picture looks wrong after changing settings

Hold **Ctrl** and press **Rebuild Shaders** (Reset to Defaults turns into it) at the bottom left of
the settings: it applies the changes and builds the shaders again.

## I want the settings of a fresh install back

**Reset to Defaults**, at the bottom left of the settings. It resets everything, including pipeline
settings with no visible control, which brings back the Bedrock RTX look. The pipeline
goes back to RT-DLSS when DLSS is loaded, FSR otherwise. See [Settings](settings.md).

## None of this worked

Ask on [Discord](https://discord.gg/DhBbAzugZ9) or open an
[issue](https://github.com/Gabrieli2806/Radiante/issues/new/choose) with your `latest.log`, GPU,
system and loader. Check the [open issues](https://github.com/Gabrieli2806/Radiante/issues) first
in case it is already known.
