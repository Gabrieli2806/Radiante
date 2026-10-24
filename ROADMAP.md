# Radiante — Roadmap

Backlog after 1.0.0. Only open work is listed; finished work is in the changelog. Nothing here is scheduled; it's a
backlog, not a promise.

## Visual bugs

- **Water.** No world looks fully right yet: water still shows a lot of noise, and so do glass and panes. Against
  Bedrock RTX the sea seen from above is still bluer and lighter than its green-teal, and water density from above has
  no visible effect.
- **Beacon beam.** It often causes light noise in far parts of the world.
- **First person hand colour.** The hand sometimes changes to a colour that is nowhere in view. Not reproducible yet.
- **Settings changed without a restart.** Sometimes they leave visual bugs that only a restart or rebuilding the preset
  fixes. Find what is not reset; a "Recompile shaders" button (clear the caches, rebuild the pipeline) would at least
  give players a fix that does not need a settings reset.
- **Torches in daylight.** A torch the sun shines on sometimes seems to give no light.
- **Grass block side overlay.** One face of a grass block sometimes does not show its overlay, not always. Suspects: the
  coplanar overlay quad, or the carved side walls.
- **Moon light.** The light comes from a point at the moon's position, not from the disc; with other moon phases it is
  clearer that the light is not coming from the moon itself.
- **Sun and moon behind clouds.** They stay almost fully visible when a cloud covers them.
- **F3.** The PrismRT text is so long it can overlap the right-hand column of the debug screen.
- **Inside lava / powder snow.** With the camera in lava or powder snow the world stays visible instead of vanilla's
  opaque dark red / white: the fog only has a dense branch for water. A tried fix (reverted, commit e6c92e6) went
  opaque but bright orange, because auto exposure brightens an all-fog screen; the colour probably has to be applied
  after tone mapping, or exposure held while submerged.
- **Black corners with PBR / 3D resource packs.** Parts of some blocks, usually corners, render black. They may be
  meant to be transparent: check those materials and the transparency handling before choosing a fix.

## Lighting, noise and image quality

- **Noise in every pipeline.** Better than before, but still noticeable next to Minecraft Bedrock in some
  circumstances; FSR and XeSS still noisier than DLSS. ReSTIR block lights: MIS weights for less bias, reuse on bounces.
- **Plastic look.** Some players say the picture looks somewhat plastic (the Realistic profile is a first answer).
  Investigate and experiment, including with other resource packs and settings.
- **Block light from the glowing pixels.** A block's light comes from the whole block, not from the lit parts of its
  texture (e.g. a redstone lamp); Bedrock lights only from the lit texture parts. Investigate.
- **Bedrock RTX parity.** Glowstone lights a room orange where Bedrock's reads cream; interiors seen from outside a
  little too bright; a room lit through a skylight still weaker than Bedrock's; daylight shadows darker and caustics
  sharper.
- **Froxel fog.** A far-visibility pass so distant valleys in shadow do not glow; tune the block light glow
  (`FROXEL_BLOCK_LIGHT_GLOW`).

## Performance

- **Low-end RTX PCs.** Bedrock RTX reaches about 70 fps with DLSS where Radiante gets 40-60. Find what to improve
  (e.g. 2 bounces by default with the bounce boost, cheaper volumetric light), and document a preset that holds 60 fps
  at 12 chunks with DLSS/FSR on a mid-range card (RTX 3060 / RX 6700). Under water still costs much more than on land.
- **Automatic Output Scale** from performance, only if switching does not freeze the game for seconds: it must be
  unnoticeable.
- **Cache size.** Prune the shader and pipeline caches (oldest first) past a limit, e.g. 100 MB.
- **Settings that do nothing under ray tracing.** Identify them; switch them off only if it measurably helps.

## Stability

- **Rare crashes when changing the resolution or some settings.** Investigate and prevent. Related hazard still open
  from the resource pack reload crash: texture descriptors are rewritten while frames that may sample them are in
  flight; reusing the existing image when size, format and mip count are unchanged would close it.

## Compatibility and testing

- **Visual smoke test suite**, run before every release and Minecraft version bump. Scenes as data (setup commands,
  camera pose, settings, what to check), one per regression already fixed; a dev-only runner with its own game
  directory and a fresh superflat world; automatic checks (shaders compiled, no errors or validation messages, no
  all-black frame, feature on/off differs), then golden images per GPU vendor; one HTML report per run.
- **Frame generation and Reflex.** Not supported with HDR output yet (the generated frames would each need the HDR
  composition). Create the DLSS frame generation feature ahead of time (its first frame holds the game ~2 s), measure
  latency, and test both on NeoForge, Forge and Linux.
- **Distant Horizons.** Large LOD areas still blink now and then as a coarse section is swapped for its children or
  back (ideas: swap only once the replacements are built, hysteresis or a cross-fade on the split distance, check
  whether DH's section timestamps change during generation). Also: memory and frame cost at DH's default 512 chunks,
  the seam with loaded chunks, textured detail for the nearest sections, NeoForge.
- **GPUs and mods.** Test AMD and Intel, not only NVIDIA; test the most used mods (Not Enough Animations, EMI/JEI,
  Xaero's maps, Create-style block entities) and list the results on the mod page.
- **Backport to 26.1.** Exploratory: check whether the render pearl Vulkan backend and the mixin targets are close
  enough to 26.3 to share a codebase, or need their own module.

## UX and accessibility

- **Controllers and keyboard.** Support Controlify and similar mods (universal if possible), and full keyboard
  navigation of the Radiante settings menu (arrow keys, Tab).

## Features

- **Wind.** Realistic wind movement for leaves, grass and the like, as some shaders have; optional, off by default.
- **Rain splash refraction**, as the old advanced pack did with a raster post pass.
