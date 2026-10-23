# Changelog

All notable changes to this project are documented in this file.

## [1.0.1] - Unreleased

### Settings

- Applying a settings change (preset, profile, shader switches) no longer shows a blocking "Applying settings..."
  screen on the title screen. A popup in the top right corner (a vanilla toast with the Radiante icon) shows the
  progress, then "settings applied" or the error, in a world and out of it, and the menus stay usable. The wait screen
  is now only used in a world when the change cannot be compiled in the background.

### Startup

- After an update changes the mod, its shaders or the graphics driver, the shaders and ray tracing pipelines are
  compiled in the background a few seconds after the title screen shows, with the top right popup showing the
  progress, so the first world no longer stands still for many seconds while they compile. Radiante remembers the
  build it compiled for (`radiante/warmup.properties`) and does this only when it changes, or when the pipeline cache
  is missing. A world opened before it finishes waits for the rest instead of compiling twice.

### Renderer profiles

- New Realistic profile (Settings > Image > Renderer Profile), next to Bedrock RTX (Enhanced) and Custom. It is
  Bedrock RTX (Enhanced) with the settings that make a picture look like plastic set the other way: bounce light 0.8
  and sky light 1.0 (less ambient fill), daylight exposure 0.42, contrast 1.08, saturation 92 %, bloom 0.004, light
  shafts and sun glow halved, water waves 0.5 with the realistic surface and a 40 % water pattern, mirror roughness
  limit 0.02, slower eye adaptation. Enhanced is unchanged.

## [1.0.0] - 2026-10-20

First stable release of Radiante with its own renderer, PrismRT. Radiante began as a fork of Radiance and MCVR (GPL-3.0)
and keeps their credits and licence; what the renderer shares with them is in the licence notices.

### Renderer

- PrismRT: GPU resources, acceleration structures and the ray tracing pipeline rewritten. The pipeline compiles on
  all cores (about 4x faster first load) and shaders are prewarmed in the background, so changing a setting that needs
  new shaders no longer stalls the game.
- About 2.5x faster path tracing than the first PrismRT builds, and shadow rays traced at the end of the hit shader:
  20-46 % less GPU time at 4K with DLSS Quality on an RTX 5070, and 1.5-8 ms less under water.
- F3 shows the GPU time per frame and its main stages ("PrismRT GPU").
- Shader pack settings, tone mapping, the denoiser and stars apply live, without rebuilding the pipeline.

### Bedrock RTX (Enhanced), the only profile

- The Radiante, Bedrock RTX and Experimental profiles are replaced by one: Bedrock RTX (Enhanced), plus Custom. It is the
  default for new installs and for Reset to Defaults.
- Close to Bedrock RTX with the Vanilla RTX pack: water (medium, colour, the pack's water texture, one bounce for what
  is seen through it), torches and lanterns, lava, grass, the sun and moon setting properly at the horizon, and the
  moon as Bedrock shows it. Enhanced keeps a few extras on purpose: the End portal's surface glows, and light blocks the
  pack gives little glow look lit by their own light.
- Water seen through glass or from inside it stays transparent; the pack's water texture shows only on water seen from
  open air. A Water Texture slider turns it down to clear water.

### Upscaling and denoising

- DLSS Ray Reconstruction can be turned off while using DLSS (NRD then denoises, DLSS only upscales). It is on by
  default and recommended on NVIDIA RTX GPUs.
- Pipelines are named by their upscaler: RT-DLSS, RT-FSR, RT-XeSS and RT-Native. NRD is the denoiser of all of them
  except DLSS with Ray Reconstruction, and the best pipeline is chosen for the GPU (Ray Reconstruction where there is
  one, then NRD + DLSS, NRD + XeSS, NRD + FSR).
- NRD is cleaner: no sparks, glows stay sharp, real hit distances.
- Output Resolution: render and upscale to a size below the window's (Native, 1440p, 1080p, 900p, 720p or a custom
  percentage) and let FSR 1 bring it to the window size. The HUD stays at full resolution.
- The DLSS / XeSS Install and Delete button is always shown.

### Sky

- Sun Path: Vanilla, Tilted or Custom. With Advanced on, the sun's path tilt and rotation and its sprite's rotation can
  be set, and the moon can follow its own path (tilt, rotation, sprite rotation) instead of the sun's.

### Fixes

- Linux on AMD (Mesa RADV): mobs, armor stands and dropped items no longer show black parts; texture lookups now carry
  the non-uniform mark that driver needs.
- Black lines down the grooves of a quartz pillar seen along its face.
- Enchanted dark armor (netherite) is no longer coated in violet: the glint is a faint sheen again.
- The player's own name tag no longer floats round the camera in first person when a mod such as Essential forces it on.

### Known limits

- Under water still costs much more than on land (about 19 ms against 3.6 ms at 1080p on an RTX 5070).
- With a lower output resolution, HDR output and FSR frame generation are untested, the setting is ignored in VR, and
  FSR 1's sharpness is fixed.
- Daylight shadows read darker than Bedrock RTX's, and caustics are sharper.
- On AMD the first compile of a new ray tracing pipeline takes about 48 s (the driver allows one thread); it is cached.
## [1.0.0-beta.1] - 2026-10-16

First build of the standalone renderer, PrismRT. Not yet tested in game; report anything that looks or runs wrong.

### Highlights

- New native renderer: GPU resources, acceleration structures and the ray tracing pipeline rewritten. The pipeline
  compiles on all cores (about 4x faster first load) and shaders are prewarmed in the background.
- About 2.5x faster path tracing than the first PrismRT builds (5 ms instead of 13 ms per frame on an RTX 5070 at
  1280x720 with DLSS): shading stays in the hit shaders, with shader execution reordering for scattered bounces.
- F3 shows the GPU time per frame and its main stages.
- The DLSS / XeSS Install and Delete button is always shown under the settings.
- Shader pack settings, tone mapping, denoiser and stars apply live, without rebuilding the pipeline.
