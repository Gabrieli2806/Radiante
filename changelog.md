# Changelog

All notable changes to this project are documented in this file.

## [2.0.0-beta.1] - 2026-10-16

First build of the standalone renderer, PrismRT. Not yet tested in game; report anything that looks or runs wrong.

### Changed

- New native renderer: GPU resources, acceleration structures and the ray tracing pipeline rewritten. The pipeline
  compiles on all cores (about 4x faster first load) and shaders are prewarmed in the background.
- Path tracer: hit shaders only record the hit and all shading runs in one place, with shader execution reordering;
  smaller ray payload; several lighting fixes.
- Shader pack settings, tone mapping, denoiser and stars apply live, without rebuilding the pipeline.
## [1.0.0] - 2026-10-11

First stable release. Covers everything since 0.5.0 (0.6.0 to 0.9.1 were never documented here).

### Added

- **Biome Ambiance** (was Biome Fog; on by default). Every biome of the overworld, the Nether and the End has an air
  of its own, and every kind of water a look of its own, all of it subtle: warm dust over deserts and badlands, a
  cold bright mist over snow, a low green murk in swamps, nearly clear air on the peaks; red, teal and ash in the
  Nether, violet in the End. Under water it is clear and bright by a beach and in warm oceans, dark and close in the
  deep oceans, murky in a swamp, and darker the deeper the camera is.
  - The biomes around the camera are averaged and eased in, so crossing a border fades from one to the next.
  - A converted Bedrock RTX pack's own fog and water per biome are used: its colours and which biomes are thick or
    clear, at Radiante's subtler strength.
  - Biomes from mods and data packs need no setup: their look is worked out from their tags (the loaders' shared
    `c:` tags too), their climate, and their own fog and water colours.
  - "Biome Ambiance Strength" scales it from 0 to 400 %. It works with or without Volumetric Fog.
- **Stars at Altitude**: the stars are clearer the higher the camera is, twice as bright from a mountain top as from
  sea level at 100 % (0 to 400 %), and a little sharper or dimmer by the biome's air.
- **Bedrock RTX profile** (Renderer Profile: Radiante or Bedrock RTX; Bedrock RTX is the default for new installs
  and after Reset to Defaults): Bedrock-style volumetric fog, a tone curve
  built from the picture's histogram, bloom, incident light metering (the exposure follows the light reaching the
  surfaces in view, not how bright the picture is), exact sRGB colours and water with Bedrock's normal map.
- **Bedrock packs' materials as authored** ("Bedrock Pack Materials", on by default): a converted `.mcpack` keeps
  its MER maps, and metalness, roughness and which texels glow are read from them at full detail. The converter also
  keeps the authored normals; packs are converted again once (converter version 15).
- **Presets**: save the current look under a name, switch between looks with a click, and share one as a code (Copy
  Code / Import Code). A preset holds lighting, sky, fog, water, tone and camera effects; upscaler, frame
  generation and HDR stay as they are.
- **Advanced settings** switch: the fine-tuning controls (exposure and tone, camera effects, light, clouds,
  atmosphere, water, fog, stars, denoiser, renderer, upscaler, debug views) stay out of the way until asked for.
- New settings: Render Resolution and a Custom upscaling mode, FSR Sharpness, Texture Contrast, Eye Adaptation
  Speed, Daytime and Night-time Exposure, Low-Light Boost and Daytime Low-Light Limit, Depth of Field Blur and Focus
  Range, Auto Focus and Focus Distance, Motion Blur Strength, Mirror Reflections and Mirror Smoothness Limit, Glass
  Reflections, Cloud Coverage, Density and Quality, Fog Distance, Stars, Seamless Glass Scope (blocks, panes or
  both), One Bounce + Cache and Glass/Water Checkerboard (both as Bedrock RTX does it, for more frame rate), Other
  Mods' Overlays and Text Display Backgrounds.
- **A Ray Tracing switch**, in the settings and on a key (unbound by default), and Chunk Building set to automatic or
  manual.
- Mod support: [Vivecraft](https://modrinth.com/mod/vivecraft) (VR, hands included), Physics Mod (debris and
  ragdolls), Litematica (schematics), JourneyMap (waypoint beacons), Mod Menu (its button opens the settings), and
  terrain generation requests from Distant Horizons. What other mods draw over the world is shown over the traced
  picture.
- **Skies drawn by other mods stay behind the traced world.** Whatever a mod draws before the terrain, in the sky
  pass or at the start of the main pass, is kept on the traced sky and hidden by the traced world, as in vanilla:
  BetterEnd's nebulae and stars, Spyglass Astronomy's stars, planets and constellations. When a mod draws the
  stars itself, Radiante's own are left out. Not tied to any mod; needs Other Mods' Overlays on.
- Seamless glass for panes as well as blocks, stacked panes included.
- `/radiante showcase` builds a gallery of test rooms (or every block, by category) and `/radiante biome` walks
  through every biome of the dimension, both in singleplayer.
- The Radiante and Bedrock pack converter versions are shown in the log and at the top of the settings screen.
- While settings are applied, the window title shows the shader compilation progress.

### Changed

- **Water seen from above is refracted and coloured by its depth**: clear over the shallows, dark over the deep.
  Before it was one flat tint.
- Water seen through its side (a waterfall, an aquarium wall, a pool's edge) is the same water as from above: clear,
  with the world reflected in it, instead of a streaked blue sheet.
- Out at sea, with the incident light meter, noon is as bright as on land; it was a dull dusk there, which lifted on
  reaching the shore.
- In the Bedrock RTX profile the Nether and the End have a haze too; they had none.
- Static block entities are cached and reused between frames.
- Removed: Pixelated Lighting, Far Bounce Distance and Far Light Bounces.
- Modrinth and CurseForge uploads of a 1.x version are marked as a release; 0.x versions and pre-releases stay beta.
- The documentation's settings reference matches the settings screen again, in English and Spanish.

### Fixed

- Black dots in the brightest spots of the picture (a sun glint on ice, a beacon's core, bright texels of some
  resource packs): radiance past what a half float holds was stored as infinity and came out of the tone mapper
  black. It is capped where it is stored and again where it is tone mapped.
- Water: the dark navy top of a waterfall next to its clear face, the flickering white lines along falling water,
  bright patches of unfogged sky deep under water, the brightness under water changing with the view, stars showing
  through water, and the view not entering water behind a glass pane.
- GPU reads that were not synchronised: DLSS Ray Reconstruction's guide images, animated textures rewritten while
  the ray tracer sampled them, and an exposure buffer that started from uninitialised memory.
- Running out of memory while queueing a chunk section no longer takes the game down; the section is skipped and
  built the next time it changes.

## [0.5.0] - 2026-10-02

First beta. Covers everything since 0.2.0 (0.3.0 and 0.4.0 were never documented here).

### Added

- **Linux support** (x86-64, glibc 2.35+). One jar per loader carries both the Windows and the Linux renderer and
  picks the right one at start. Linux-specific fixes in the bundled FidelityFX and minizip builds, and a
  `libcore.so` that only exports its JNI entry points.
- **Smaller jars (about 15 MB, was 125 MB).** The native libraries ship xz-packed and are unpacked into the game
  folder on first start, checked against their SHA-256.
- **DLSS and XeSS on demand.** They are no longer in the jar. FSR (upscaling and frame generation) is built in and is
  the default everywhere; on NVIDIA the first menu offers DLSS, on Intel XeSS. They are downloaded from NVIDIA's and
  Intel's own repositories (fixed version, hash checked) with a progress bar. The Pipeline selector lists them as
  `(Install)` / `(Restart required)`, and the settings bar has Install, Delete and Restart buttons. Installing or
  deleting is done outside a world and always ends in a forced restart.
- **NVIDIA Reflex without Streamline**, straight on `VK_NV_low_latency2`: no restart to toggle it, works on Linux, and
  Streamline is gone.
- **Distant Horizons: per-column coverage hiding**. Far terrain is no longer remeshed when
  chunks load; it is hidden per ray where the near terrain is built. Far terrain only counts as replaced once the
  renderer actually draws what replaces it.
- **Frame generation** on its own present thread, with native DLSS and FSR providers.
- Froxel volumetric fog with a fog-style toggle, rain refraction, light from dropped and emissive
  entities (with a reach slider), seamless glass including panes, and a "Cache Deep Bounces" performance toggle.
- New settings: Upscaling Mode for FSR and XeSS (the render resolution, as DLSS Mode is for DLSS, set by the
  Quality level too), Frame Generation Type (Auto, DLSS or FSR, where the GPU runs both), ReSTIR Block Lights, Shader
  Execution Reordering (RTX 40 and newer, experimental), Far Bounce Distance and Far Light Bounces, Cloud Shadows,
  Wet Ground in Rain, Glowing Item Light Distance, Seamless Glass and Rain Refraction.
- Settings screen: a search box, a performance impact label on the options that cost the most, an "Applying
  settings" screen while the renderer rebuilds, and a notice listing what needs a restart when leaving.
- Debug logging is on by default, and the renderer's own messages (errors always) now reach `latest.log` as
  `[native] ...`, so one log is enough to report a problem.
- GitHub Actions builds both renderers and the three loader jars, and publishes a release when `mod_version`
  changes (and to Modrinth / CurseForge when configured).

### Changed

- Translucent particles (campfire smoke, dust) are kept by their alpha like solid surfaces instead of looking like
  glass.
- Tinted glass is dark and see-through but keeps its texture; glass reflections are capped.
- Reset to Defaults now falls back to FSR instead of no upscaler.
- Light Bounces now goes up to 8. The Low quality level uses 2 bounces instead of 1, which left rooms flat.
- Frame generation is no longer tied to the DLSS pipeline or to Fabric: FSR frame generation works with any
  upscaler. It leaves V-Sync to the player and is hidden while HDR output is on.
- Light grid and the Streamline frame generation were tried and removed (flicker, no gain).

### Fixed

- Black world on Linux with FSR: FidelityFX binding names were truncated on Linux, so FSR could not be created; and
  minizip lacked deflate there, so the shader pack could not be extracted.
- Black screen with no explanation on machines without hardware ray tracing (software Vulkan): the renderer now stays
  off and says why.
- Far-terrain blinking and holes when moving fast with Distant Horizons.
- Fog glow on block edges and a bedrock-atmosphere glow on glass.
- Crash on quit from static destructors running after the Vulkan driver was gone.

## [0.2.0] - 2026-09-26

### Added

- NeoForge and Forge builds alongside Fabric, from one shared codebase.
- Experimental Distant Horizons support: its far terrain is path traced with the rest of the world, and DH keeps
  generating with ray tracing on.
- Held light sources light the world from the hand holding them, with their own brightness slider.
- LabPBR maps for entities and held items; Bedrock `.mcpack` resource packs (fog and water included).
- Volumetric fog improvements, rain/snow motion vectors, motion blur and depth of field toggles, pixelated
  lighting option.
- Light from end crystals (purple), end portal frame eyes, glow item frames.

### Fixed

- Noise on moving mobs and players, and on the held item and map, under DLSS.
- Grass block side overlays flickering at edges and corners.
- Compatibility with mods that hook the vanilla world renderer (Not Enough Animations).
- Vanilla clouds follow the player's settings when Distant Horizons changes them.
- Chunk loading stutter, crashes on quit.

## [0.1.0] - 2026-09-16

### Added

- Initial release for Minecraft 26.3 on Fabric, running on Minecraft's own `com.mojang.renderpearl` Vulkan
  backend.
- Shared Vulkan instance and device: ray tracing, descriptor indexing and upscaler extensions are merged into
  the ones Minecraft requests, with the feature chains combined.
- Serialised access to the graphics queue between Minecraft and the renderer.
- Ray-traced terrain: sections compiled into the renderer's PBR vertex format and kept in sync with
  Minecraft's section update tracker.
- Path-traced lighting, shadows and physically based sky driven by the vanilla sky and fog state.
- Texture mirroring for the GPU-stitched 26.3 atlases, including per-sprite stitcher padding.
- DLSS, FSR 3 and XeSS upscaling plus NRD denoising, with the `vanilla-pt` and `advanced` shader packs.
