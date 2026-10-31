# Changelog

All notable changes to this project are documented in this file.

## [Unreleased]

- Changing either held light clears obsolete denoiser and upscaler history instead of leaving a discoloured flash.

- Dropped torches, lanterns and other small point-light items use the placed block's calibrated light, once per stack.

- Held light brightness now controls the local held item's diffuse lighting without extra emitters from its hand and shadow models.

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
  [docs/en/addons.md](docs/en/addons.md).
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
