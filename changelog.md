# Changelog

All notable changes to this project are documented in this file.

## [Unreleased]

- The built-in shader pack is renamed from `vanilla-pt` to `radiante-pt` (shown as Radiante PT). Its stored
  settings and the pipeline's pointer to it move over by themselves on the first start; the old zip is removed.
- ROADMAP.md is gone: where the project is heading is tracked in GitHub issues labelled roadmap, next to
  the bugs and ideas.
- Rain Refraction is removed: its pass never drew anything (rain is traced in the world). Saved settings that still
  hold it are ignored.
- Clean-up of code that was never reached: about 2,600 lines of Java, native and shader code, among them the
  post-rendered entity path, unused native setters (max fps, vsync) and unused lang keys. No change in the picture.
- Entities that give off light (glow item frames, end crystals, glow squids, blazes...) keep lighting the scene when
  the camera turns away from them, as far as the Entity Light Reach; they used to be dropped with the view frustum
  and the room's light changed with the camera (#40).
- The PrismRT GPU line on the F3 screen wraps onto more lines instead of running into the right-hand column (#13).
- The shader cache (`radiante/cache/shaders`) is trimmed at startup to 100 MB, least recently used first, and the
  pipeline cache is deleted past 256 MB and rebuilt by the warm-up on the next start (#23).
- Wind (Sky & Water, off by default): leaves, grass, flowers, crops, vines, sugar cane and lily pads sway near the
  player, harder in rain and thunder, and seagrass and kelp sway with the current. The geometry itself moves (the
  sections nearby are refit every frame), so shadows and reflections follow, and the motion vectors carry the sway so
  DLSS and the denoisers do not smear it. Wind Strength (0-200%) applies live. Off, it costs nothing (#32).
- A texture Minecraft makes again at the same size, format and mip count keeps its image, and its descriptor is not
  rewritten under the frames in flight: the hazard left from the resource reload crash (#25).
- New Light From Glowing Pixels option (Lighting, off by default): light blocks cast their light from the glowing
  parts of their texture, a lit redstone lamp from its filaments, as Bedrock RTX does, instead of from the whole
  block. The same amount of light, from more and smaller sources (#18).
- The grass block's side overlay (and any layer lifted over a face of its block) is no longer carved by Carved
  Surfaces, which along its edges could let the view through to the side under it, so that face showed no overlay
  (#10).
- The sun and the moon are hidden behind vanilla clouds: through a cloud's haze they stayed almost fully visible.
  Their halos and the stars go with them (#12).
- Moonlight comes from the lit part of the moon's disc, not from a point at its centre: with a crescent moon the
  light visibly came from beside it (#11).

## [1.1.0] - 2026-10-24

### Settings

- Applying a settings change (preset, profile, shader switches) no longer shows a blocking "Applying settings..."
  screen on the title screen. A popup in the top right corner (a vanilla toast with the Radiante icon) shows the
  progress, then "settings applied" or the error, in a world and out of it, and the menus stay usable. The wait screen
  is now only used in a world when the change cannot be compiled in the background.
- The settings can be used from the keyboard: Up and Down move through the options, Left and Right move a slider by
  one step (ten with Shift) or step a list, Enter or Space flips a switch, Tab moves to the next section.
- Holding Ctrl turns Reset to Defaults into Rebuild Shaders: it applies the changes and builds the shaders again, for
  when the picture looks wrong after changing settings.
- The Install / Delete button of DLSS and XeSS shows only on the Quality section.
- New Cloud Height slider (Sky & Water): raises or lowers the Vanilla clouds.
- Glowing Item Light Distance is removed: Minecraft stops drawing dropped items long before it mattered.
- Atmosphere shows Java (Bedrock not available), greyed with the reason, when no Bedrock install or files are found,
  instead of a Bedrock choice that did nothing. Bedrock Caustics is greyed the same way, and off by default.
- Biome Ambiance is off by default; Water Texture defaults to 10 %.
- Seamless Glass on glass blocks shows the glass's colour and clarity evenly, without the streaks drawn inside the
  texture, so a wall of glass reads as one clear sheet.

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
  shafts and sun glow halved, water waves 0.5 with the realistic surface, mirror roughness
  limit 0.02, slower eye adaptation. Enhanced is unchanged.

### Water

- New Clear Teal Water slider (Settings > Performance, default 0 %): moves the water of every biome towards clear teal
  water, where red is absorbed within a few blocks and green and blue carry on for dozens. The bottom, the light shafts
  and the circle of sky overhead show far off instead of closing into a deep blue murk, and a lake seen from above
  turns teal. The Realistic profile sets 70 %; Bedrock RTX (Enhanced) is unchanged.

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

- The Nether portal is see-through under Bedrock packs (Vanilla RTX gave it a metal material that shaded it as an
  opaque slab). A Portal Transparency slider sets how much of the pack's transparency it keeps (default 50%).

- Rain is falling water: thin, nearly clear streaks that tint what is behind them a little and catch the sun, sky
  and lamps, instead of the grey, opaque streaks of vanilla's rain texture. A Rain Opacity slider sets how much of
  each streak shows (default 0%, clear water that only catches the light).

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
