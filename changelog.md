# Changelog

All notable changes to this project are documented in this file.

## [Unreleased]

- Rain: the dry ground round a campfire, furnace or other hot block has a ragged edge that frays into the wet, in pixels with Pixel Rain, instead of a rounded square, and a campfire lit in the rain dries it a step at a time over about 45 seconds instead of at once. Grass and earth take the rain into the soil: a little darker, never the slick grey of wet stone.

## [1.2.0] - 2026-11-13

- The denoiser settings shown with FSR and XeSS (Settings > Performance > Denoiser) work. They were twenty-four of REBLUR's, a denoiser that has not run since RELAX replaced it, so none of them changed anything. They are now three of RELAX's, applied without a rebuild: History Frames (how long the light is averaged over, 30), Spatial Passes (how far the spatial filter reaches, 5; fewer is sharper and noisier) and Anti Firefly (on). Their defaults are RELAX's own, which a comparison at 2 to 6 passes and 8 to 60 frames did not beat.

- DLSS Ray Reconstruction is told where to trust the current frame over its history (its bias mask): translucent particles such as campfire smoke, and lava's flowing surface. Their motion vectors do not follow how they move or change. Seen in a pan past a campfire, the smoke stays more solid and in place; its edges show a little more grain.

- Sharper with every upscaler, and steadier. The camera's sub-pixel jitter, which DLSS, FSR and XeSS build the picture's detail out of, took only 16 horizontal positions in a pixel where it should take 72 (a slip in the sequence cut its horizontal half short). It now covers the pixel evenly over a cycle of 128 frames. Measured on a textured floor, RTX 5070 at 1080p: DLSS Quality's edges 58 % stronger, and at Ultra Performance the picture moves a quarter less from frame to frame with the camera still.

- DLSS starts over after a teleport, a respawn or a change of dimension (a jump of more than 8 blocks in a frame), instead of smearing the last place over the new one for a moment.

- Light From Glowing Pixels does what it says with the default settings. It changed the cells a light block casts from only on the path used without Bedrock's glow; with it (the default) every block kept four cells a side whatever the option said, which is why nobody could see a difference. Finer cells, each shrunk to the texels that glow in it, on both paths now; the light a block casts in all is the same. The difference is small by design: where the light starts on the block, seen within a block or two of it.

- Shader packs, more for those who write them:
  - Textures by name. A texture that leaves its binding out of `configs.json` gets one of the pack's own, and the shader names it by a define (`RADIANTE_TEXTURE_<NAME>`, with `RADIANTE_PACK_SET_RAY_TRACING` or `RADIANTE_PACK_SET_POST` for the set), so a pack holds no number a later Radiante could take. Numbers written by hand work as before.
  - The player's state: `radianteHealth()`, `radianteHurt()` (the flash of a hit), `radianteFood()`, `radianteAir()`, `radianteSpeed()`, `radianteSprinting()` and `radianteOnGround()` in `common/pack_world.glsl`.
  - A `vec3` setting shows in the pack's settings as three sliders, one a channel; it had no row at all.
  - `radianteIsSplash()` in `radiante/v1/surface.glsl`: a drop of a splash of water. `radianteIsRain()` stays true for rain and snow sheets only.
  - `"requires": {"features": [...]}` is checked: a pack names what it uses that came after the first release with shader packs (`noise`, `clock`, `player_state`, `named_resources`, `shadow_softness`, `colour_settings`), and a Radiante that lacks one shows the pack in the list as needing it, by name, instead of failing to compile it when chosen. The same names hold on every Minecraft version's build.

- Aether: Vitals, on those values (a red pulse from the edges when hit; with under a third of the health left the colour drains and the corners close in; running out of air the edges close in dark blue, pulsing), Rush (at a sprint the edges of the picture streak outwards a little), the film grade's Shadow Tone and Highlight Tone as colours to set, and its noise image declared by name.

- Shader packs, for those who write them:
  - `radiante/v1/noise.glsl`, a public header of noise and colour helpers (hashes, value and fractal noise, Worley cells in two and three dimensions, a per-pixel, per-frame hash for dither and grain, luminance, saturation, sRGB to linear and back), so a pack need not carry its own.
  - `radianteTime()` and `radianteFrame()` in `common/pack_world.glsl`: a clock in seconds that runs on through the days (the day's fraction starts over every twenty minutes, and every pack that animated by it had to hide that) and the frame count.
  - `api_version: 1` is accepted: the `radiante/v1` headers ship, and a pack that said it used them was refused as made for a newer Radiante.
  - A script that runs a pass its stage does not have is refused when the pack is loaded, with the names it does have, instead of failing while the first frame is drawn. Two resources on one binding number say where a pack's own go (64 to 119).
  - `hookShadowSoftness()` is optional: `hooks/ray_origins.glsl` gained it after its two offsets, which made it a function every pack's file had to have. A file that has it says so with `#define RADIANTE_HOOK_SHADOW_SOFTNESS 1`; one without keeps Radiante PT's soft shadows. A pack that already had the function needs that line added for it to count. From now on a function added to an existing hook file is announced this way.
  - `check_example_packs.py` finds bindings that clash with Radiante PT's or lie outside a pack's range, which compiling alone never showed.

- The Aether example pack is 1.1: an aurora over snowy biomes at night (upright sheets of green fading to violet, with rays, that light the snow a little), film grain, and a wide-screen frame up to 2.39:1. It uses the new noise header and clock and declares `api_version: 1`.

- The Nether no longer burns the rock around a sea of lava. The lift that gives lamps a little light by day (above) also took hold wherever the eye was closed for another reason: beside lava, in the Nether or in a cave, it multiplied lava's own light up to five times. It now works only by day and as far as the camera stands open to the sky. And the Nether's eye may close three stops further than before, where beside a wide sea it stopped at its limit and the rock all around went one red.

- Pixel Rain (Sky, Fog & Water, on by default): the rain's marks in Minecraft's own look. Puddles have hard edges in pixels a sixteenth of a block across, the rings rain makes on them and on water step from pixel to pixel, wet surfaces get wet pixel by pixel (on walls and mobs in runs down from the top of each column of texels, longer the longer it rains), and the splashes of rain bend the world behind them pixel by pixel. Off gives the smooth, round shapes of before.

- Mobs and the player get wet in the rain as walls do: their skins took the share of leaves before and looked dry in a storm. Wet walls show: a wall faced the bright sky of the storm and mirrored it, which undid its darkening, so Vanilla RTX's bricks came out 8 % darker, too little to see. Walls now soak like stone (darker, less shiny), the water running down them in streaks. Wet Walls starts at 80% (was 50%). Values already saved are kept.

- Rain on the ground:
  - No dry band along walls and steps. The top of a block dried off towards a wall or a higher block beside it, which left a dry strip along every wall and step and a dry round patch in every inner corner while water stood all around; the rain falls straight down there too.
  - A block broken in the rain leaves the block under it dry, and it soaks and gathers water over the Wet-Up Time as the rest did; broken after the rain, it stays dry. The sides of the hole soak the same way. It used to be wet at once, or dry for a few seconds and then wet all at once.
  - Chunks that load a moment after the ground round them is read no longer show dry for seconds and then turn wet all at once, which in a walk looked like the wet ground changing at every chunk crossed. Blocks broken or put down near the camera show within a fraction of a second.

- The redstone torch's halo is a smooth, see-through red glow round its head, the same in every frame. It was met by rays at random (three in ten), which showed as a speckle of red dots the denoiser smeared about. The redstone wall torch has its halo too: vanilla's wall torch model paints it with a corner texel of the torch texture that Bedrock textures (Vanilla RTX) leave clear, so it had none; it is drawn with an edge texel of the ring now. The halo casts no shadow.

- Heat keeps the rain off: the ground round fire, lava, magma, a lit campfire, furnace, blast furnace or smoker stays drier in the rain and dries first after it, over about a block and a half; round a torch, a lantern or a lit candle standing on it, over about a block.

- Aether 1.2: rainbows. A rainbow opposite the sun as the rain starts and for about a minute after it stops, in front of the clouds and seen in puddles and water; at night a pale moonbow opposite the moon; about one rain in five gives a double bow, its second arc's colours the other way round. Rainbows setting, 0 to 2.

- The splashes rain throws up where it lands (and every other splash of water: a fish, a boat's wake) are drops of water: seen through, the ground behind each bent a little as through a small lens, with a faint glint of the sky, as the old raster packs refracted the screen behind them. They were opaque pale specks, which on dark ground showed as grey dots, and cast shadows of their own; they cast none now.

- Quality and Performance in the manual lists Minecraft's own video settings that do nothing with ray tracing on (Smooth Lighting, Entity Shadows, Mipmap Levels, Texture Filtering, Improved Transparency, Chunk Fade-In, Chunk Update priority, Cutout Leaves): measured on and off, neither the picture nor the GPU time per pass moved.

- More of the Advanced exposure and tone curve settings grey out and say why when they do nothing as the others stand. Bedrock RTX's light meter (the default) keeps limits and a speed of its own, so Max Exposure, the exposure speeds and Exposure Adaptation do nothing under it, as Min Exposure already showed; with automatic exposure off none of them does. The Bedrock curve's Dynamic Range and Shadow Min Slope only change the picture with Max Brightening above 0 (its default, as in Bedrock, is 0: the curve then never lifts the shadows, whatever their shape), and the curve's settings do nothing under another tone mapping method. Found by the settings sweep.

- Bloom is off by default, and gentler when turned on: Bloom Intensity starts at 0.08 (was 0.15). Values already saved are kept.

- Depth of Field for play. Auto focus no longer runs out to the sky (stored as a distance of 65504, not the zero the shader looked for: with the crosshair on the sky the whole world blurred) and no longer takes the hand, food being eaten, a raised shield or a map for what to focus on; with nothing to focus on it stays where it was. The hand and what it holds are never blurred. The focus glides (a quarter of the way a frame at most, was half). Blur Strength is the same share of the picture at any resolution (at 4K it was half as strong as at 1080p) and goes on working to the end of its range (it stopped at about 170%). Reset to Defaults turns Auto Focus back on.

- Motion Blur for play. 100% is now half a frame's motion, a film camera's shutter (it was one and a half frames: any turn of the mouse smeared the whole picture), so a saved strength gives about a third of the smear it gave. The longest streak is a share of the picture's height, the same at any resolution (was 20 pixels). The hand and what it holds are not blurred, so a swing of the sword stays sharp, and a pixel whose motion is no number (the frame after a teleport) is left alone.

- Shader packs: the Mellow port example loads again. It had its textures on binding numbers 48 to 50, which Radiante PT has since taken for its own, and two resources on one number stop a pack from loading. A pack's own textures and buffers go on 64 to 119, now written down in the developer guide; the Aether and Aquarela examples moved there too. The guides also name the third function `hooks/ray_origins.glsl` must have, `hookShadowSoftness()`: a pack written from them did not compile.

- HDR calibration: the first page says to lower the value until the marks show and then raise it until they disappear (from a peak set too high they were never seen), and to turn on HGIG if they never do. HDR Peak Brightness goes down to 200 nits (was 400), for displays that reach less.

- A light block lights the same in the hand as set down. Only torches did: measured in a sealed room, the same patch of wall read with the block set down and then held at the same spot, a lantern in the hand lit 2.3 times what it lit set down, a campfire 1.9, glowstone and a sea lantern over 4, shroomlight nearly 6. Glowstone, lamps, froglights, shroomlight, magma and a bucket of lava now cast in the hand by the very glow their faces cast set down, falling off the same way; a campfire held casts its flame's light; and a lantern set down no longer shadows its own light with its frame (it lit less than a torch). All of them are now within a tenth or so of each other, held and set down.

- Lights by day. With the eye closed for the sun a torch, a campfire or a lantern lit nothing around it, not even the shaded ground at its foot, and looked switched off. Their light now counts for more the further the eye is closed (by the square root of it, five times at most): in the shade there is a warm pool of light around them, under the sun a faint one, and by night nothing changes. What glows also shows at least a flame's brightness by day, on the ground and in the hand.

- The Nether and lava. Lava shows its texture: its bright swirls were over white and the whole sheet came out one pale, flat peach in a haze of bloom; it is now a deep orange with its pattern, by day in the Overworld too. The eye is opened a little further in the Nether (0.35 of the metered exposure, was 0.25), so the rock beside a sea of lava is no longer a few dark reds against one bright sheet, and lava's light is a little yellower than its texture, so quartz, gold or basalt lit by it keep a colour of their own.

- After the time is set (`/time set night` by day) sand and other pale blocks no longer stay lit for a moment and fade: the light kept from frame to frame, in the radiance cache and in the denoiser, belonged to the hour before. It is dropped on the frame the sun jumps, and the cache catches up within half a second.

- In first person, standing in water and looking down no longer shows the player's own body under the surface (nor through a cloud from inside it). The body, there for its shadow and its reflection, was hidden only while the view ran dead straight; water bends it. It now shows only in a view a mirror-like surface turned back: the reflection on the water is as it was.

- Campfire smoke no longer glows as it leaves the fire. Inside a block of light level 15 every particle is at full light, which is how glowing particles are told; a particle now glows only where the light around it is less than that.

- Particles that Minecraft draws at full light (a torch's flame, sparks, drops of lava, an end rod's glitter) glow: they are bright in the dark and the light bouncing around picks a little of it up. The other particles (campfire smoke, dust, falling leaves) cast a shadow; before, no particle did.

- Grass, tall grass, ferns and flowers seen from below are no longer black. They are lit as if they leaned half way up, and looking up at them from under that lean they were shaded as a surface seen from behind.

- The redstone torch shows its own head. It was a flat, pale red cube half again the head's size with none of its texture: Minecraft's model has a halo around the head, six one sided faces that the tracer saw from both sides and made glow. They are now a see-through, faintly glowing shell: the head shows through it with its texture, on the floor and on a wall.

- Torches, lanterns and the other small lights look like the light comes from them. Their light is strongest beside the flame and fades with distance, a pool of light on the wall around a torch, where it used to light a whole wall almost evenly. And what glows has a halo: Bloom Intensity starts at 0.15 (was 0.01, too little to see), Bloom Threshold at 1.0 (was 0) so only what is brighter than white blooms and the daylight picture is as it was, Bloom Scatter at 0.7 (was 0.5). Values already saved are kept; Reset to Defaults gives the new ones. A shader pack that draws Radiante's bloom itself gets the new threshold and scatter too.

- Water, seen from under it: no more bright circle of sky overhead with a dark mirror around it (the circle appeared the moment the camera went under and followed the player). The surface is looked straight through from below, as Bedrock RTX shows it, and a view up at it traces one path where it traced two: under water the frame costs far less (the path tracer went from 20.7 ms to 5.8 ms a frame looking up from a pool, RTX 5070 at DLSS Quality).

- Waterfalls, tanks and every other side of water are looked straight through. They bent the view going in and again coming out, so what was behind a waterfall slid and shrank as the camera moved, and a ray that met the far side at a slant never came out of the water.

- Water glows less from outside (the light it scatters counts half as much seen through the surface as from inside it), and not at all in a room with no sky over it: a pool in a sealed room used to keep a third of the open sky's blue.

- Less fog by default: Volumetric Fog Strength starts at 60% (was 100%), where the morning air no longer stands as a pale veil over everything. A value already saved is kept; Reset to Defaults gives the new one.

- Aerial Perspective is off by default (0%, was 100%). It is a second haze over the distance, on top of the fog's; it is still there for those who want far terrain (Distant Horizons' too) to fade into the sky. Aerial Perspective Start is grey while it is off. A value already saved is kept.

- More Advanced settings say when they have no effect instead of doing nothing: Indirect Light Strength while Bedrock Pack Materials is on, and the exposure and curve settings that belong to another light meter or tone mapping curve than the one chosen (the histogram's range and percentiles, the centre's size, the least and the manual exposure, middle grey, the white point, clamping).

- Shader Execution Reordering is removed. It broke water (a flat pale sheet, nothing seen through or mirrored) and, kept away from water, stopped the game on the card it was tried on; it was experimental and off by default.

- Water Waves is grey unless the Water Surface (Advanced) is Realistic, the only one with waves of its own: with Bedrock's surface, the default, the slider did nothing.

- The eye adapts over the same time at any frame rate. With Bedrock RTX's light meter (the default) the exposure moved a share of the way each frame, so at 30 frames a second a room took four times longer to come up than at 120.

- The pictures of the documentation and the README are retaken with the look as it is now: the settings screens, the comparisons and the scenes.

- Radiante PT has one look: the Bedrock RTX (Enhanced) look is simply its default settings. The hidden switches behind it (`bedrock_original`, shown as "Bedrock RTX (Experimental)", and `bedrock_enhanced`) are gone, and so is the other branch of the shaders, the remains of the old Realistic profile. Settings of the old Realistic and Custom profiles are no longer reset on the first start.

- Sky Light, Bounced Light, Daytime Exposure, Night-time Exposure and Glass Reflections are removed, with the advanced attributes `basic_radiance` and `water_density_from_above`: nothing read them in that one look.

- Picture Style (Image) sets only the tone mapping curve and Saturation: Natural is PBR Neutral at 100%, Bedrock the Bedrock RTX curve at 100%, Vivid ACES at 130%. It no longer changes bounced or sky light.

- The Realistic example shader pack (`examples/shaderpacks/realistic`) and its page of values are removed.

- New Entity Light Reach row (Lighting, 8 to 256 blocks, 64 by default): how far from the player glowing dropped items, item frames and end crystals still light their surroundings. The option was saved before but had no row.

- Rows that did nothing without saying so are grey and say why: Eye Adaptation, Eye Adaptation Speed and Low-Light Boost while the Exposure Metering Mode (Advanced) is Bedrock RTX's, the default, which has its own limits and speed; Daytime Low-Light Limit unless the metering mode is the incident light meter; Texture Contrast while Exact sRGB Textures (Advanced) is on, the default; Cache Deep Bounces and One Bounce + Cache when the radiance cache (Use SHaRC, Advanced) is off.

- Reset to Defaults also resets Light From Glowing Pixels, Light From Unlit Blocks, Other Mods' Overlays, Text Display Backgrounds, Entity Light Reach, the frame generation backend, HDR Peak Brightness and HDR Paper White, and the Seamless Glass Scope.

- `options.properties` no longer writes the old `vanillaSunPath` key; it is still read from old files and old share codes.

- Eight more settings apply at once instead of rebuilding the shaders: Carved Surfaces, Cloud Shadows, Water Surface, PBR Sampling Mode, Transparent Split Mode, Clouds in Reflections, Clouds in Clear Weather and the Bedrock fresnel of glass and water. 78 of radiante-pt's 108 settings now change live.

- For contributors: `python dev/smoke/settings.py`, the settings sweep, changes each setting in a view where it should show and reports the ones that leave the picture as it was (`docs/dev/testing.md`). A scripted run reads its script from a file with `RADIANTE_DEV_SCRIPT="@<file>"`, and `opt=field=value` goes through the option's own setter when it has one. `BedrockRtxValuesTest` and `SettingsCodeTest` replace `OptionsMigrationTest`.

- A settings file is no longer lost to a crash or a full disk in the middle of a save: it is written whole beside the old one and then takes its place. A `pipeline.yaml` or `options.properties` that cannot be read is kept as `pipeline.yaml.bak` or `options.properties.bak` before the defaults replace it.

- `/radiante showcase` and `/radiante biome` need a world with commands (cheats) allowed: they build with `fill` and move the player, which a survival world without commands should not allow.

- `/radiante showcase` and `/radiante biome` work again: since `/radiante pack` and `/radiante debug` were added they answered "Incorrect argument".

- A stored pipeline that cannot be applied no longer stops the game from starting: the default pipeline is used.

- Options changed while the game runs are read safely by the render and chunk building threads, in the mod and in the renderer.

- A graphics driver failure while the renderer queues work is reported in the log and stops that step, where the game could wait for ever before.

- For contributors: native method signatures, the vertex layout and the modules' image counts are checked when building; the example shader packs are compiled and the native tests run in the CI; a test looks up every mixin's targets in the Minecraft the build compiles against. `native/extern` holds only what the build reads (about 4200 files fewer).

- ReSTIR Block Lights and ReSTIR Bounced Light now reuse light across frames and neighbouring pixels: neither ever found last frame's samples, so each ran on its new samples alone. With the reuse, block lights are far less noisy before denoising but their shadows are softer where several lights meet.

- ReSTIR Block Lights is off by default: the plain sampler is quicker (about an eighth off the ray tracing pass in a lit room), gives sharp shadows, and was less noisy than ReSTIR had been without its reuse. A setting saved as on stays on.

- Turning ReSTIR Block Lights off no longer leaves the world undrawn: its shaders did not compile, as the wind's motion vectors read last frame's uniforms, which were only declared with ReSTIR on.

- Cache Deep Bounces, One Bounce + Cache and Glass/Water Checkerboard apply at once, without rebuilding the shaders.

- Shader packs: the first public header, `radiante/v1/surface.glsl`, says what a ray hit, one function per surface flag (`radianteIsWater(surfaceFlags)`, `radianteIsLeaves`, `radianteIsThin`...). A pack includes it with `#include <radiante/v1/surface.glsl>` and gets the one of the Radiante it runs on, not a copy it carries; Radiante PT and the example packs use it. The old tests in `util/vertex.glsl` (`isLeavesSurface`...) stay, so packs need no change.

- Shader packs: `radiante/v1/uniforms.glsl` gives a pack with passes of its own the per-frame uniforms under names of their own: `RadianteWorld` (the camera, the fog, the hands' lights, the wind, the pack's live settings) and `RadianteSky` (the sun and the moon, the weather, the biome, the water). The native build checks they lay out as the renderer's `WorldUBO` and `SkyUBO` do.

- Shader packs: a pack that uses Radiante only through `radiante/v1` and the `radiante-pt@1` hooks can keep its own licence, through an additional permission in LICENSE-EXCEPTION.md like the one for DLSS and XeSS.

- With Wind on, leaves and plants no longer shake while the weather changes between clear, rain and thunder.

- With Wind on, a plant sways in one piece: the front and back faces of its planes no longer cut through each other in patches.

- Shader packs: opacity micromaps keep the solid parts of cut-out terrain (leaves, plants) as "unknown opaque", so a ray traced without the forced two-state reading asks the any-hit shader there and nowhere else, for light let through foliage. Radiante PT reads them in two states as before.

- Shader packs: vines, hanging roots and moss, lily pads, sugar cane, kelp, bamboo, cobwebs and particles carry a thin surface flag (bit 27, isThinSurface), so a pack can let light through them as through leaves and plants.

- Items lowering during a hand swap keep their dedicated-light ownership, preventing a bright mesh-light flash.

- Changing either held light clears obsolete denoiser and upscaler history instead of leaving a discoloured flash.

- Dropped torches, lanterns and other small point-light items use the placed block's calibrated light, once per stack.

- Held light brightness now controls the local held item's diffuse lighting without extra emitters from its hand and shadow models.

- New Calibrate HDR screen sets HDR Peak Brightness and Paper White with a test pattern and pictures; the sliders stay.

- Setting the time no longer leaves the sky white with fog lit by the previous hour for seconds.

- Unlit redstone ore casts a quarter of its light and the full light once touched; its texture's glow stays.

- New Light From Unlit Blocks option (Lighting, on by default): blocks with no light level, such as crimson and warped stems, cast light from their emissive textures.

- Per-texture HDR pickers now control cast light only; visible emission keeps the resource pack's colour and strength, including in mirrors and glass.

- Advanced block emission controls now use HDR RGB pickers with exposure and exact numeric input; existing percentage settings migrate automatically.
- Emissive LabPBR/MER textures are discovered from resource packs and supply sampled light even on blocks without vanilla light levels. Packs can author material emission tints without changing diffuse textures.

- Advanced Lighting now offers searchable 0–1000% emission sliders for each built-in light texture, including material glow, light cast, and held lights.

- A/B Capture in the Debug section: a screenshot with the settings and GPU timings saved beside it.
- F3 now also shows TLAS instances, entity BLAS per frame, sections pending and the renderer's VRAM use.
- Debug views `fog_inscatter` and `fog_transmittance`: the fog on its own.
- Debug views `raw_diffuse` and `raw_specular`: the noisy signals before the denoiser.
- Debug views `direct`, `indirect` and `sharc`: the light on each pixel split by where it came from.
- Debug view `rain`: per pixel wetness (red), open sky (green) and standing water (blue).
- New Debug section in the Radiante settings, shown while Advanced is on: a debug view picker (with a pack's own
  views), SHaRC Debug Mode, HDR Debug View and Debug Logging moved there, GPU Timings in F3, a Bug Report Summary that
  copies versions, GPU and settings, and buttons for the logs and shader packs folders. New heat map debug views:
  `anyhit` (any-hit shader runs per pixel on the camera ray, blue 1 to red 16+, for finding hidden faces and stacked
  surfaces) and `bounces` (rays per pixel past the camera ray).
- Fog Veil Dimming (Sky & Water, 25%): the light shafts' hazy air now also dims what is behind it, by a share of
  the light it scatters, so distant lava, lights and land fade into the fog instead of keeping their full glow under a
  pale veil. In full rain the air still dims the distance though the sun is gone.
- Bedrock's volumetric fog (default profile) carries the biome's fog on past its 50-block volume: distant lava,
  lights and hills now fade into the fog with distance instead of keeping their full glow behind a fogged foreground.
- Pixelated Lighting: puddles and rain rings come out in the texture's pixels, with hard edges. Shader packs can
  move where rain's marks are worked out with the new `hooks/rain_surface.glsl`.
- In first person, the player now shows in mirror-like reflections (wet ground, puddles, water, windows) while First
  Person Shadow is on, as in third person. Before, only their shadow showed.
- Rain is cleaner and cheaper. Rain streaks are no longer shaded as surfaces: the camera sees through them with a
  faint glint of the overcast sky (Rain Streaks slider), so the
  denoiser and upscaler stop taking the flickering streaks for the ground behind them, which smeared and sparkled
  through every storm. Bounced rays and the radiance cache no longer meet rain at all.
- Clouds in rain are heavy grey masses lit only by the overcast, darker underneath and in a storm; they took part of
  the full sun before and glowed against the dim sky.
- Rain soaks the ground over a couple of minutes: it darkens within seconds, then water gathers in patches that
  grow and merge into one film; sheltered ground near the open (beside walls, at the rim of a tree) gets damp with
  rain blown in, fading over two blocks instead of stopping at a hard line.
- Rain wets walls, tree trunks and leaves open to it too (less than the ground), fades out over a few blocks at the
  edge of a biome where it does not rain, and the ground under leaves stays damp. New settings in Sky, Fog & Water:
  Wetness, Wet Walls, Wet-Up Time and Drying Time.
- Puddles (Sky, Fog & Water): in steady rain, standing water with rain rings covers the flat tops of open full
  blocks and mirrors the sky and the lamps (less of it, and less of a mirror, on grass and earth); once the rain stops it dries up in patches over about three
  minutes. Slabs, stairs, roofs and leaves only darken a little.
- Lightning Flash (Sky, Fog & Water): a thunderstorm's lightning now lights the world from the bolt's direction, with
  shadows, and lights up the clouds while the sky flashes; the bolt's own glow no longer lights a storm by night white. The overcast sky is heavier overhead and darker in a storm.
- With Distant Horizons, its terrain under the chunks that are built is left out of the rays instead of being skipped
  by each one: rays near the ground crossed it under the real terrain and paid for every face they passed. In a forest
  at an 8 chunk render distance radiante-pt's trace took 2.4 ms less (17.6 to 15.2 ms).
- First-person hand primary rays use conservative bounds from the current animated geometry, with padded projection
  and full-screen fallback when bounds cannot be trusted.
- Hand Lighting Samples (Performance → Advanced → Renderer, 1–8, default 3) controls the indirect-lighting
  samples for first-person hands and held items. Lower values are faster with more noise; higher values are smoother.

- Volumetric fog (Froxel style, and Bedrock's fog volume) holds still while you look around: its light now lives in
  two grids fixed to the world around the camera (half-block cells to 32 blocks, two-block cells to 128) instead of a
  grid cut along the view, and each pixel marches its ray through them. Turning the camera used to re-cut the air into
  other cells, so light shafts and the fog's shape shifted with the view; now the same view gives the same fog however
  you got there.
- ReSTIR Bounced Light (Lighting, off): experimental ReSTIR GI for the first bounce of light off what the camera sees.
  Each pixel keeps its bounce's sample in a reservoir and merges it with last frame's and its neighbours' (a
  visibility ray checks a neighbour's sample), for less noise in rooms and caves lit by bounced light. Off by default:
  behind DLSS Ray Reconstruction the gain is small and the light reads a few percent darker.
- Bounced light no longer reads the full-size texture far away: the ray cone that picks a texture's level of detail
  now widens with the lobe a bounce is drawn from (diffuse or rough), so light bounced off distant foliage and
  textured ground stops sparkling.
- On a device Radiante does not support yet (macOS, Windows on ARM such as Snapdragon laptops, 32-bit Java, Android
  launchers), or when its renderer's library fails to load, the game no longer crashes: it starts with Minecraft's own
  renderer, a one-time screen names the device and says why, and Radiante's settings show that message instead.
- Aerial Perspective Start (Sky & Water, 128 blocks): no distance haze nearer than this; it fades in over the next
  half of that distance and is counted from there, not from the camera, so nearby land and buildings stay clear.
- Translucency (Lighting, 100%): leaves and small plants let the sun and moon through from behind, tinted by their
  colour and brightest looking towards the light, so a tree glows against a low sun instead of standing dark; ice and
  snow take the light a little past their shadowed edge, with a blue tint. No extra rays. Shader packs can set how much
  per surface with `surface.mat.subSurface` in `hookSurface`, and test ice and snow with `isFrostSurface`.
- Volumetric fog's light shafts (Bedrock's fog volume) no longer break up into drifting cloudy blotches while the
  camera moves: the froxel light is filtered over its neighbours before it is integrated, so a window's shaft stays a
  smooth beam.
- Glow lichen glows and lights much less (level 2 instead of its light level 7): spread over a cave wall it lit the
  cave like a room of torches.
- Powered redstone dust glows by its power level, as in Bedrock RTX: none at power 0, then up to power 15 in vanilla's
  colour for that power, and it casts a faint red light next to it (a straight line far under a redstone
  torch). Before, dust looked the same powered or not. A Bedrock pack's emission for the dust textures no longer makes
  unpowered dust glow.
- Lit redstone torches glow again, and powered repeaters and comparators glow on their torches (and the lit line of
  the repeater's top) like a redstone torch: repeaters and comparators draw the torch's lit texture without giving any
  light, and that had taken the texture off the glowing ones.
- Water under the camera now depends on the body of water, not only on the biome: with the head in a pool or a small
  pond the view reaches further and the water is lighter, while lakes and the sea keep the biome's look. Radiante
  measures the water column and how far the water runs around the camera (eased as you swim). Looking up out of the
  water, only the water itself fogs the view now: the sky and the shore past the surface no longer vanish into sea
  blue. Shader packs can read the size with `radianteWaterDepth()` and `radianteWaterWidth()`.
- Under water by day the view is no longer as bright as the land above: the daylight that lights what you see has
  come down through the water over you, so it dims and turns blue-green the deeper you swim (red goes first). The
  night look under water is unchanged.
- Fog you can see. With the defaults the air had no haze at all: Bedrock's fog volume only carries the biome's fog,
  which is zero with Biome Ambiance off (the default), and turning Volumetric Fog on or off changed only the last few
  blocks before the render distance. Now:
  - **Aerial Perspective** (Sky & Water, 100%): far terrain, Distant Horizons' included, fades towards the sky behind
    it, about an eighth of the way at 16 chunks and two fifths at 64 at sea level, thicker in rain, thunder and humid
    biomes, thinner over deserts and from mountain tops, none in caves. Worked out per pixel from the distance, so it
    is free and has no noise, and near chunks and LODs fade alike. It makes up the floor under Biome Ambiance's haze
    instead of stacking on it.
  - Bedrock's fog volume (the default fog) now shows light shafts through leaves, windows and skylights, so
    Volumetric Fog on looks different from off; Light Shafts and Volumetric Fog Strength work with it again.
  - Less noise in the volumetric fog: each fog cell is lit at points spread evenly through it over time instead of at
    random, keeps a longer history, reads it back smoothly as the camera moves, and lets a rare bright sample in
    gently; torch glow in the fog looks at four lights instead of two. The Radiante fog style's march starts its
    steps on a fine, even pattern instead of white noise.
- A second built-in shader pack, **Pixelated Lighting**, listed next to Radiante PT under Shader Packs: pixelated
  shadows and reflections, after BetterRTX's preset of that name. Shadow edges from the sun, torches and held lights
  follow the texture's pixels, and reflections, water's included, are solid squares, one per texel, near objects too;
  the light itself (its angle, a torch's falloff, bounce light, glow) stays smooth, and what is seen through water and
  glass stays sharp. Natural colours by default; the old example's retro palette is an option (Retro Palette, off).
  Settings: Texels per Block, Pixelated Shadows, Shadow Softness (hard sun and moon shadows, so their edges step in
  texel squares), Pixelated Reflections (water read as one flat facet per texel) and the palette. It replaces the Pixel Light
  example: a pipeline that used `shaderpacks/pixel-light.zip` switches to the built-in pack, at its new defaults.
- Shader packs: a new hook, `hooks/ray_origins.glsl` (`hookShadowOffset`, `hookReflectionOffset`), to start a hit's
  shadow rays and reflection rays somewhere else than where it is lit; the camera's reflections off water, glass and
  mirrors follow. Radiante PT compiles to the same shaders as before.
- Renderer profiles are gone: radiante-pt's defaults are now the Bedrock RTX (Enhanced) look, all in one, so the
  Renderer Profile selector, the Realistic and Custom profiles and the "set by the profile" notes are removed. Every
  setting the profile used to hold (exposure, Bedrock's tone curve, glass, water, fog, low-light boost...) is a plain
  setting with that value as its default, and the built-in Bedrock fog, water and light block glow are used whenever no
  Bedrock pack provides them. Tone Mapping offers Bedrock RTX's curve, and the Bedrock picture style matches the
  defaults. Low-Light Boost, Glass Reflections and Water Murkiness read 100% at the Bedrock value. A player who had
  Realistic or Custom gets the settings those profiles set put back to the defaults once, on the first start; other
  settings are kept. Realistic's look comes back as an example shader pack (below).
- Without a Bedrock pack, Radiante now has built-in defaults for the fog and water of every biome and for the glow and
  cast light of each light block, laid over the pixels Radiante finds glowing in vanilla's texture. Textures, normal
  maps and other materials still need a pack, and an enabled Bedrock pack replaces all of it. See
  [docs/dev/bedrock-defaults.md](docs/dev/bedrock-defaults.md).
- Shader packs can tell how wet the air is after rain: `radianteWetness()` (Iris's `wetness`, with its default
  half-lives) and `radianteRainStoppedFor()`, seconds since the rain stopped, in `common/pack_world.glsl`. Radiante PT
  reads neither. Mellow port 0.8.0: the rainbow now comes after rain and stays for a minute or two while the air is
  still wet, strongest just after, instead of only the few seconds the rain takes to stop.
- Shader packs: a new hook, `hooks/sky_emission.glsl` (`hookSkyEmission(direction, seen)`), for light glowing in the
  sky that every ray reaching it sees, so it lights the world too; `seen` lets a pack give bounced rays a cheap,
  smooth version. Radiante PT compiles to the same shaders as before. Mellow port 0.7.0 uses it: the aurora now
  faintly lights snow, water and the ground in its colour and shows in rough reflections (Aurora Light), and a
  rainbow, primary and secondary, opposite a low sun in light rain, mostly as it stops (Rainbow).
- Mellow port 0.6.0: a realistic aurora, now the default (Aurora Style; Mellow's own is still there): folded,
  rippling curtains with a sharp lower edge and rays, green below and red above, drifting and shimmering slowly,
  fading at the horizon and under the moon and hidden by clouds. Still only in snowy biomes unless Aurora Everywhere.
- Shader packs get more hook points, each in a file of its own so older packs keep compiling: the sun's and moon's
  discs (`hooks/celestial.glsl`), radiante-pt's water waves and caustics (`water_waves`, strength, speed and size),
  any water's surface for a pack's own normals or foam (`water_surface`), the view from under water (`underwater_fog`
  in the path tracer, `underwater` on the finished picture), the Nether's and the End's sky and fog
  (`dimension_sky`) and the colour of block light, cast, held and seen (`block_light`). `common/pack_world.glsl` adds
  the time of day read off the sun (`radianteSunAngle()`, `radianteDayWeights()`, the moon's phase). With its own
  hooks Radiante PT compiles to the same shaders as before; packs need no change. Wind stays the renderer's: a pack
  can only turn it on or set its strength by default. The Mellow port (0.5.0) uses them: Mellow's round sun (off, as
  in Mellow), its water normals and their settings, its warm torch light, its underwater colour and End sky, and
  Radiante's wind on for its waving plants; the example pack adds a ripple under water and a thicker Nether haze.
- Shader packs get a settings page of their own: with a pack other than Radiante PT in use, Radiante's settings have
  a section named after the pack, with its icon, holding the settings the pack adds (Radiante PT's stay where they
  are, and the pack's no longer land among Radiante's advanced settings). Packs can group and order them, put some
  behind Advanced, describe them, mark their performance impact, set a slider's step and hide a setting while another
  has a given value (`group`, `order`, `advanced`, `description`, `performance_impact`, `step`, `visible_when` on an
  attribute), and offer presets of their settings as a row at the top of the page (`presets`). Packs' setting names
  from their `lang` files now show everywhere. A pack built on Radiante PT starts Radiante PT's settings at Radiante
  PT's defaults, or at its own where it gives one a new default. The example packs (Mellow port, Pixel Light, Inkwell,
  the example pack) use all of it, in English, Spanish and Chinese.
- Shader pack development: a pack kept as a folder in `radiante/shaderpacks/` reloads by itself when one of its files
  is saved. It is checked first (loaded and compiled in the background while the game runs); an edit that does not
  compile is not used, the picture stays as it was, and the error shows in a popup and on the Shader Packs screen.
  New client commands: `/radiante pack validate [name]` checks a pack without switching to it and prints the report in
  the chat, `/radiante pack reload` reloads the active folder pack, and `/radiante debug view <view>` shows the
  albedo, normals, roughness, metallic, emission, depth, motion vectors or kind of surface instead of the picture
  (`off` goes back; not saved). Packs can declare their own debug views (`debug_views` in `configs.json`).
- Shader packs get far more live settings: 256 `"runtime": true` slots instead of 64 (Radiante PT used 62 of them), and
  they now work in post passes and the post grade hook too (`common/pack_post.glsl`). A pack can ask for Radiante's
  bloom pyramid (`RADIANTE_POST_BLOOM`) and read it there, for bloom, glow and lens flares; it is drawn only for a pack
  that asks. Radiante PT renders as before. Example packs: Pixel Light's palette and dither and the Mellow port's grade
  change live; the example pack gains bloom, a lens flare with colour fringes, a live grade strength and a debug view
  of its own.
- Shader packs can react to the weather and the biome: `common/pack_world.glsl` in Radiante PT gives hooks the thunder
  strength, and the temperature, downfall, snowy, dry and rainy weights and id of the biome at the camera, faded over
  a couple of seconds across a border (`radianteThunder()`, `radianteBiomeSnowy()`, `radianteBiomeIs(...)`), also in
  the post grade hook. Radiante PT itself is unchanged. The Mellow port uses them: its aurora shows in snowy biomes
  and is on by default again, as in Mellow, thunder darkens its sky, sun and clouds, and a pale garden fogs up at night.
- Shader Packs screen (button beside Presets in Radiante's settings): lists the built-in Radiante PT and the packs in
  `radiante/shaderpacks/` (zip or folder, with icon, name, version, author and licence from the pack's manifest),
  opens the folder, and switches packs with the usual background rebuild. A pack that does not load or compile is
  replaced by Radiante PT again, and the renderer's messages (the compiler's, or why `configs.json` was rejected) are
  shown under the list with a button to copy them; the renderer now logs why a pack's `configs.json` was rejected
  instead of quietly loading the built-in pack. Zips that wrap the pack in a folder are flagged in the list. Iris and OptiFine packs put in that folder
  are listed as such, with a note that they belong to Iris. A pack
  that takes the game down while it is being tried is replaced by Radiante PT
  at the next start. A setting value a pack does not offer (radiante-pt's Bedrock water carried into another pack
  with the same setting) goes back to that pack's default instead of breaking its shaders.
- A new example shader pack, Realistic (`examples/shaderpacks/realistic`): the old Realistic profile as a pack built
  on Radiante PT. It gives Radiante PT's settings the profile's values as defaults (less bounce and sky light lift,
  softer shafts and sun glow, finer teal water) and adds the profile's contrast (1.08) and saturation (0.92) as two
  live settings over the finished picture, with Realistic and Neutral presets. The profile's bloom intensity and
  exposure speeds belong to tone mapping, which a pack cannot set, and are not carried; see
  [docs/dev/realistic-profile-values.md](docs/dev/realistic-profile-values.md).
- Shader pack authors: a guide (`docs/en/shader-packs.md`, `docs/es/paquetes-de-shaders.md`), an example pack in
  `examples/shaderpacks/` that holds only the files it changes and is built on top of Radiante PT with
  `./gradlew shaderPackExamples`, and the design and roadmap for pack support in `docs/dev/shader-packs/`.
- Shader packs can build on Radiante PT instead of copying it: `"extends": "radiante-pt@1"` in the manifest, and the
  pack holds only its own files; Radiante PT's files go under it and `configs.json` and the lang files are merged
  (attributes, textures, buffers and passes by name, new passes placed with `before`/`after`). Radiante PT gains six
  hook points a pack replaces (`hooks/surface.glsl`, `direct_light`, `water`, `sky`, `fog`, `post_grade`); with its
  own empty hooks it compiles to the same shaders as before, so nothing changes in the built-in pack. Three more
  hooks let a pack bring its own clouds and night sky: a cloud layer drawn after the rain and over the sun and moon
  (`hooks/clouds.glsl`), the shadow of those clouds on the world (`cloud_shadow`) and stars (`stars`), also empty in
  Radiante PT. The example
  pack is rewritten on them, and a second example, Inkwell, gives a comic-book look (banded light, poster colours,
  ink outlines, flat sky). Packs made for another base, a newer `api_version` or another Radiante version
  (`requires.radiante`) are listed with the reason. Every pack zip is now extracted entry by entry, refusing paths
  that leave the pack, symbolic links and more than 20000 files or 512 MB actually written. Packs that copied
  Radiante PT's files keep working as before.
- Bedrock `.mcaddon` files (and `.mcpack` files holding several packs) in the resource pack folder are listed and
  converted like a `.mcpack`: the resource packs inside are found (pack folders at any depth, nested `.mcpack`
  files), a pack's first subpack is laid over it, and behavior/script packs are skipped with a line in the log, since
  they only run in Bedrock. A pack's Vibrant Visuals light colours (`local_lighting.json`) colour the point lights of
  torches, lanterns, fire and the like while it is enabled, and add point lights to the other blocks it lists as one
  (candles, amethyst), in the Bedrock RTX profiles. A pack's shield attachable (the torch shield of Vazah's Dynamic
  Lights) is drawn on Java's shield with its glowing texels, and the shield then gives a light in the hand. Nested
  archives are unpacked only within size, count and compression-ratio limits.
- Leaves are marked for the shader pack (`LEAVES_SURFACE_BIT`, `isLeavesSurface` in `util/vertex.glsl`), as small
  plants already are, so a pack can treat the leaves of trees apart: let light through them, say. Nothing changes in
  the built-in pack.
- Addon settings API (`com.g2806.radiante.api.RadianteAddons`): a mod built on Radiante can add toggles, value lists
  and sliders to Radiante's settings screen, under an Addons section with a heading per addon (the mod's icon from its
  metadata, its name and version); Radiante stores their values in `radiante/addons/<id>.properties`. The section
  only shows when an addon is installed. Each addon's options fold away under its heading (open when it is the only
  addon, folded when there are more, remembered per addon; a search opens the ones it finds something in). See
  [docs/en/api.md](docs/en/api.md#radianteaddons).
- When a newer DLSS or XeSS is available, the settings button for it reads "Update DLSS" instead of "Delete DLSS" and
  opens the update offer, so skipping the start-up prompt for a version no longer leaves no way to update.
- Faster in forests, fields and villages where the GPU driver offers opacity micromaps (`VK_EXT_opacity_micromap`):
  the leaves, grass, flowers and other cut-out blocks of each section are baked into micromaps, so sun and block
  light shadow rays and camera rays no longer run the any-hit shader on them. The path tracing pass took 5-23% less
  GPU time than before in most test views, for about 3 MB of micromaps and 10 MB of their inputs at 12 chunks.
  Animated sprites (fire, kelp...), glass and odd faces are still decided by the shader, and GPUs without the
  extension keep the shader path.
- Far leaves, grass and flowers are cut out texel by texel at any distance, as shadows already were, instead of by
  the texture's mip levels: distant trees keep their gaps and look less flat.
- Less noise for the same rays: the path's BSDF and sun-disc samples are Owen-scrambled Sobol points across frames
  (Burley 2020) instead of independent random numbers, so the frames the denoiser averages cover each pixel more
  evenly. The noise left in the denoised picture at a still view dropped 7-24% in the test views, for 1-5% more
  path tracing time.
- The settings screen works with a controller (#31). Controller mods that move through menus with Minecraft's own
  focus navigation (Controlify and the like) or press keys for the buttons now step through the options: D-pad or
  stick to move and change values, A to flip or press, B to leave. With Controlify the bumpers change section, X
  applies, Y undoes, the right stick scrolls (or moves a slider ten steps), and its button pictures show beside the
  buttons. The keys reach the buttons along the bottom and the advanced groups' headings too, the description
  follows the focused row (under it when there is no side panel), and the focus stays put when a change lays the
  screen out again.
- The settings screen's arrow keys, Enter, Space and Tab work again on 26.3, where Minecraft's key codes changed;
  a left click on a list steps it forward again, a right click back.
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

First stable release of Radiante with its own renderer, PrismRT. Radiante began as a fork of Radiance and its MCVR renderer (GPL-3.0);
much of it has been rebuilt since, and some of their code remains.

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
