# Example shader packs

Packs built on Radiante PT, to show what a pack can change and to start your own from. Each one says
`"extends": "radiante-pt@1"` in its manifest and holds only its own files: settings, names, and the hook files it
replaces. The renderer puts radiante-pt under it when it loads it, so the pack follows Radiante PT's updates
([design §5](../../docs/dev/shader-packs/design.md#5-inheritance-and-hook-points)).

Their settings are on a page of their own in Radiante's settings, named after the pack: each pack groups them
(`group`), describes them (`description`), marks their cost (`performance_impact`), hides the ones that do nothing as
things are set (`visible_when`) and offers a few presets (`presets`; not radiante-example)
([guide](../../docs/en/shader-packs.md#your-settings-page)). Radiante PT's own settings keep its defaults (the Bedrock
RTX look) unless a pack gives one a new default in its `configs.json`, as realistic does.

## radiante-example

A warm, contrasty look with light through leaves, a glow and optional lens effects, a ripple under water and a
thicker Nether haze. Each change is a pack setting:

| Setting | File | What it does |
| --- | --- | --- |
| Colour grade, Grade Strength | `hooks/post_grade.glsl` (turned on with `RADIANTE_HOOK_POST_GRADE` in `configs.json`) | Warm highlights, cool shadows, more contrast in the mids. The strength changes live |
| Bloom (0-1) | `hooks/post_grade.glsl` | A glow round bright things from Radiante's bloom pyramid, which the pack asks for with `RADIANTE_POST_BLOOM` in `configs.json` (`radianteAddBloom`, `common/pack_post.glsl`). Changes live |
| Lens Flare and Fringes (0-1) | `hooks/post_grade.glsl` | Ghosts of bright lights mirrored through the centre (the bloom read at mirrored places) and colour fringes towards the edges. Changes live |
| Light through leaves, strength | `hooks/surface.glsl`, `hooks/direct_light.glsl` | Sunlight behind leaves and small plants lights them from the back (`isLeavesSurface`, `isPlantSurface`); works with opacity micromaps. The strength changes live |
| Water tint | `hooks/water.glsl` | Water absorption: deeper, bluer water |
| Sky tint | `hooks/sky.glsl` | A cooler clear sky |
| Underwater Ripple (0-1) | `hooks/underwater.glsl` | With the camera under water the picture ripples (read a few pixels aside from `hookPostGradeInput`) and its edges darken; 0 turns it off |
| Nether Haze (0.5-3) | `hooks/dimension_sky.glsl` | How fast things fade into the Nether's fog (1 is Radiante PT's), warmer as it thickens |

It also has a debug view of its own, `/radiante debug view example_bloom`: the bloom alone (`debug_views` in
`configs.json`, `EXAMPLE_DEBUG_BLOOM` in the hook).

## inkwell

A path-traced comic book: sunlight in flat bands, colours snapped to a few inks, dark outlines round shapes and block
edges, a flat cream-and-blue sky and a warm paper tone. Shadows, bounces and block lights stay path traced, so the
picture keeps its light, only drawn differently.

| Setting | File | What it does |
| --- | --- | --- |
| Light Bands (2-8) | `hooks/direct_light.glsl` | How many flat steps the sunlight on a surface has; fewer is bolder |
| Cel Shading (0-1) | `hooks/direct_light.glsl` | Blend from Radiante PT's own sun shading (0) to the bands (1), with a hard highlight on smooth surfaces. Changes live |
| Poster Colours (0-16) | `hooks/surface.glsl` | Brightness levels of the textures, each texel keeping its hue; 0 or 1 keeps them as they are. Glowing texels are left alone |
| Ink Outlines (0-3) | `post_render/inkwell_ink.frag`, own passes | Outline width in pixels, from silhouettes (a depth jump of a fifth or more) and near right-angle creases, so leaf holes and texture bumps stay clean; fades out past about 100 blocks. 0 turns them off |
| Flat Sky | `hooks/sky.glsl` | A two-tone sky at the sky's own brightness, so day, dusk and night still light the world as before |
| Paper (0-1) | `post_render/inkwell_ink.frag` | Cream whites, lifted blacks and a fine grain |

It also shows a pack adding passes: `inkwell_copy` (radiante-pt's `post_copy.frag`, the picture into `post_motion`)
runs at the end of the post script, and `inkwell_ink` says `"after": "inkwell_copy"`.

## pixelated-lighting

**Built in.** Pixelated Lighting comes with Radiante: the native build zips this folder next to radiante-pt.zip
(`PRISM_BUILT_IN_EXTENDING_PACKS` in `native/CMakeLists.txt`) and the Shader Packs screen lists it as built in
(`ShaderPackLibrary.BUILT_IN_EXTRAS`). A change here needs the native build to reach the game.

Pixelated shadows and reflections, after BetterRTX's "Pixelated Lighting" preset. Surfaces are lit where they are,
with their own normals, so the light itself (its angle, a torch's falloff, bounce light, glow) stays smooth; only the
shadow rays and the reflection rays of a hit start from the centre of its texture texel. Each texel is then in
shadow or not, and every pixel of it reflects the same thing: shadow edges and reflections (water's too, near
objects included) come out in the texture's own pixels, in natural colours. What is seen through water and glass
keeps each pixel's own view. A retro palette with an ordered dither is there, off.

| Setting | File | What it does |
| --- | --- | --- |
| Texels per Block (0-16) | `hooks/surface.glsl` | 16: the texture's own texels (`hookTexelCentre`); lower merges texels, 0 turns it off |
| Pixelated Shadows | `hooks/ray_origins.glsl` | Shadow rays of the sun or moon, block lights and held lights start from the texel centre (`hookShadowOffset`), aimed at the same light. Changes live |
| Pixelated Reflections | `hooks/ray_origins.glsl` | Reflection rays start from the texel centre, turned to its view (`hookReflectionOffset`): glossy and mirror reflections and the reflection of water and glass. Changes live |
| Retro Palette (0-32) | `hooks/post_grade.glsl` | Levels per colour channel; 0 (the default) or 1 keeps the colours. Changes live |
| Dither (0-1) | `hooks/post_grade.glsl` | How much 4x4 ordered dither goes between palette levels. Changes live |

`hookSurface` only works out the offset from the hit to the texel centre; the pack's `hooks/ray_origins.glsl`
(without radiante-pt's `RADIANTE_HOOK_RAY_ORIGINS_DEFAULT`) hands it to radiante-pt, which moves the shadow and
reflection rays. The reflection of water and glass the camera sees is traced by the ray generation, which gets the
offset back in a fourth layer of `material_state`; `configs.json` asks for it
(`"textures": [{"name": "material_state", "depth": 4}]`). The random numbers stay each pixel's own, so the
denoiser cleans the noise as for Radiante PT.

It shows a pack changing where rays start without changing where light is computed.

## mellow-port

The look of [Mellow](https://modrinth.com/shader/mellow), an Iris pack by TheCMK under the MIT licence
(`LICENSE-MELLOW`), on Radiante's light: Radiante still traces the shadows, bounces and reflections, Mellow gives the
colours. Mellow's own noise images and LUTs are in `textures/` (MIT, from Mellow's `img/`), and its time of day and
colour scheme in `mellow/mellow_common.glsl`, its clouds in `mellow/mellow_clouds.glsl`. Mellow's rain and thunder grey
and darken its sky, clouds and sunlight, and thicken its clouds; the port does the same to its colours, with the
thunder from `radianteThunder()` (radiante-pt's `common/pack_world.glsl`). Its biome-dependent parts read the biome
from there too: the aurora only where snow falls, and the thick fog of a pale garden at night.

| Setting | File | What it does |
| --- | --- | --- |
| Sky Colours (0-1) | `hooks/sky.glsl` | Mellow's sky gradient for sunrise, day, sunset and night, with its sunset glare, at Radiante's sky brightness |
| Cloud Style (0-2), Amount, Opacity, Speed | `hooks/clouds.glsl` | Mellow's flat clouds (its default) or its volumetric ones, from its noise images, lit with its sun and sky colours. They cover the sun and moon, thicken and grey in rain as in Mellow, show in reflections and light the world |
| Cloud Shadows (0-1) | `hooks/cloud_shadow.glsl` | The clouds' shadows on the ground, from the same clouds the sky shows (Mellow has none; this is the port's own) |
| Stars (0-2) | `hooks/stars.glsl` | Mellow's stars (they replace Radiante's), under the clouds |
| Aurora (0-0.5), Aurora Everywhere, Aurora Light (0-4) | `hooks/sky_emission.glsl` | Mellow's aurora, under the clouds. As in Mellow it shows only where snow falls (`radianteBiomePrecipitation()`), fading in as the camera enters a snowy biome; Aurora Everywhere shows it in every biome. Every ray reaching the sky sees it, so it lights snow, water and the ground green and shows in rough reflections; rays bounced off the world get a smooth average of it (one noise sample, no curtains), Aurora Light times as bright (1 is as bright as it looks, the default 1.5) |
| Aurora Style (0 Mellow, 1 Realistic) | `hooks/sky_emission.glsl` | Realistic (the default, the port's own): curtains raymarched through a shell of the sky (20 steps), folding and rippling on domain-warped noise, with a sharp lower edge, vertical rays and a slow shimmer; green below, red and magenta above, a faint violet fringe under the edge; fades at the horizon and under the moon, and clouds cover it. Mellow: Mellow's soft green band |
| Rainbow (0-1) | `hooks/sky_emission.glsl` | The port's own (Mellow's is a hue wheel at a fixed place in the sky): a primary bow 42 degrees round the antisolar point and a fainter secondary at 51 with its colours reversed, from one dispersion peak per wavelength (400 to 700 nm), darker between them and lighter inside, so only a low sun shows it above the horizon. By day after rain, while the air is wet: it comes up as a shower that wetted the air eases off, is strongest just after and fades out one to two and a half minutes later (`radianteRainStoppedFor()`, `radianteWetness()`); not in heavy rain or thunder; the camera only, under the clouds |
| Sun Colour (0-1) | `hooks/direct_light.glsl` | Sunlight and moonlight take Mellow's hue; their strength stays Radiante's |
| Round Sun | `hooks/celestial.glsl` | Mellow's round sun and moon (its ROUND_SUN) in place of vanilla's square sprites, at Radiante's brightness. Off, as in Mellow |
| Water Normals, Water Normal Strength, Speed, Size | `hooks/water_surface.glsl`, `hooks/water_waves.glsl` | Mellow's water normals (two scrolling layers of its noise image) on the top of water, in place of Radiante's waves; the three settings are Mellow's WATER_NORMAL_* and scale Radiante's waves and caustics too. Strength changes live |
| Torch Colour (0-1) | `hooks/block_light.glsl` | The light blocks cast and the held light pulled towards Mellow's warm torch colour at their own brightness; 1 is Mellow's single colour for all block light |
| End Sky (0-1) | `hooks/dimension_sky.glsl` | Mellow's End sky (violet top, slowly turning green and orange lights round the horizon, brighter towards the End flash) at Radiante's brightness, and the End fog in its colour |
| Atmospheric Fog (0-1) | `hooks/fog.glsl` | Mellow's fog density (thicker at sunrise, at night and in rain, and far thicker in a pale garden at night), with Volumetric Light on |
| Water Colour (0-1) | `hooks/water.glsl`, `hooks/underwater_fog.glsl` | Mellow's green-teal water, as the shape of the water's absorption, and the colour the view fades into under water (Mellow's tint_underwater) |
| Vibrance, Saturation, Contrast, LUT | `hooks/post_grade.glsl` | Mellow's final grade on the tone-mapped picture; LUT picks one of Mellow's (off by default, as in Mellow). All change live |

Mellow's waving plants (WAVY_PLANTS, on in Mellow) become Radiante's own Wind, turned on by default in the port's
`configs.json`: Radiante moves foliage in a pass of its own, which a pack cannot replace. The time of day (sunrise,
day, sunset, night) is `radianteDayWeights()` from `common/pack_world.glsl`.

It shows what a port by hand looks like: the effect's function is kept, what it read from Iris (images, uniforms,
`worldTime`) is mapped to Radiante's, and its colours are scaled to Radiante's exposure. Mellow's exposure and ACES
curve, its screen-space effects (SSR, SSAO, bloom, TAA) and its shadows are left to Radiante.

The packs are GPL-3.0, as Radiante PT they build on.

## realistic

The Realistic renderer profile of earlier versions, as a pack. It has no shaders of its own but one hook: it gives
Radiante PT's settings other defaults, by name, and grades the finished picture. The player can change any of them as
usual; the values are those in [docs/dev/realistic-profile-values.md](../../docs/dev/realistic-profile-values.md).

| Setting | Radiante PT | Realistic | What it does |
| --- | --- | --- | --- |
| Bounce Light Boost | 1.5 | 0.8 | Less lift in the shadows from bounced light |
| Sky Light Boost | 1.8 | 1.0 | The sky's light at its own strength, more shade |
| Daytime Exposure | 0.48 | 0.42 | A touch darker by day, so skies and snow keep detail |
| Light Shafts | 0.015 | 0.008 | Half the haze glowing in the sun |
| Sun Glow | 0.08 | 0.04 | Half the halo round the sun |
| Mirror Smoothness Limit | 0.03 | 0.02 | Only truly polished metal is a clean mirror |
| Water Surface Mode | Bedrock | Realistic | Finer, irregular waves instead of Bedrock's water texture |
| Water Wave Strength | 0.35 | 0.5 | Waves that break the mirror into ripples |
| Clear Teal Water | 0 | 0.7 | Water that absorbs red first and stays clear in green and blue |
| Contrast (0.5-1.5) | - | 1.08 | `hooks/post_grade.glsl`: contrast about middle grey over the finished picture. Changes live |
| Saturation (0-2) | - | 0.92 | `hooks/post_grade.glsl`: a little less colour. Changes live |

The last two are the profile's tone mapping contrast and saturation. A pack cannot change tone mapping's settings, so
the hook applies them over the picture the way the Bedrock tone mapper, the default, applies its own: with tone
mapping at its defaults the result is what the profile showed, except in clipped highlights. A Neutral preset turns
the grade off. Not carried, because only tone mapping has them: the profile's bloom intensity (0.004 instead of 0.01)
and its slower eye adaptation (exposure speeds 4 up and 6 down instead of 8). The profile also left out the built-in
Bedrock fog, water and light block glow used without a Bedrock pack; Radiante PT has no setting for that, so they stay.

## Build and try

```
./gradlew shaderPackExamples
```

writes `build/shaderpacks/<pack>.zip`. Copy it to `radiante/shaderpacks/` in the game folder (in a dev run:
`run/radiante/shaderpacks/`), then Radiante settings → **Shader Packs** → the pack → **Use**.

A pack that extends radiante-pt is zipped as it is. A folder without `extends` is still staged on Radiante PT at
build time, the way the native build stages it (`native/src/shader/CMakeLists.txt`: the Radiante PT folder, then
`shader/util/` into `util/`, `src/common/` into `common/`, the SHaRC headers into `extern/sharc/include/`, and last
the pack's own files over them), for packs that replace whole files.

## Check a pack without the game

After the native build (`native/build-windows.bat`, which also stages radiante-pt):

```
cmake --build <build dir> --config Release --target shader_pack_stage
<build dir>/src/tools/Release/shader_pack_stage <radiante-pt folder> examples/shaderpacks/inkwell <out>
python native/src/tools/compile_pack.py <out>
```

`<radiante-pt folder>` is `<build dir>/src/shader/ray_tracing_internal_zip/radiante-pt` or an unzipped
`radiante-pt.zip`. `shader_pack_stage` puts the pack together exactly as the loader does (and
`shader_pack_stage --self-test` checks the merge rules); `compile_pack.py` compiles every shader with
glslangValidator (Vulkan SDK) and the defines the loader gives at the pack's default settings.

## Your own pack

1. Copy `radiante-example/` (or `inkwell/`) to a new folder here and change `display_name`, `version`, `author`,
   `license` and `description` in `configs.json`.
2. Keep only the hook files you change, and add settings, passes or shaders of your own.
3. `./gradlew shaderPackExamples`, copy the zip, try it. Errors show under the list in the Shader Packs screen.

More in [Making a shader pack](../../docs/en/shader-packs.md).
