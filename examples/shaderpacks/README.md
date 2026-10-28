# Example shader packs

Packs built on Radiante PT, to show what a pack can change and to start your own from. Each one says
`"extends": "radiante-pt@1"` in its manifest and holds only its own files: settings, names, and the hook files it
replaces. The renderer puts radiante-pt under it when it loads it, so the pack follows Radiante PT's updates
([design §5](../../docs/dev/shader-packs/design.md#5-inheritance-and-hook-points)).

## radiante-example

A warm, contrasty look with light through leaves, a ripple under water and a thicker Nether haze. Each change is a
pack setting:

| Setting | File | What it does |
| --- | --- | --- |
| Colour grade | `hooks/post_grade.glsl` (turned on with `RADIANTE_HOOK_POST_GRADE` in `configs.json`) | Warm highlights, cool shadows, more contrast in the mids |
| Light through leaves, strength | `hooks/surface.glsl`, `hooks/direct_light.glsl` | Sunlight behind leaves and small plants lights them from the back (`isLeavesSurface`, `isPlantSurface`); works with opacity micromaps. The strength changes live |
| Water tint | `hooks/water.glsl` | Water absorption: deeper, bluer water |
| Sky tint | `hooks/sky.glsl` | A cooler clear sky |
| Underwater Ripple (0-1) | `hooks/underwater.glsl` | With the camera under water the picture ripples (read a few pixels aside from `hookPostGradeInput`) and its edges darken; 0 turns it off |
| Nether Haze (0.5-3) | `hooks/dimension_sky.glsl` | How fast things fade into the Nether's fog (1 is Radiante PT's), warmer as it thickens |

## inkwell

A path-traced comic book: sunlight in flat bands, colours snapped to a few inks, dark outlines round shapes and block
edges, a flat cream-and-blue sky and a warm paper tone. Shadows, bounces and block lights stay path traced, so the
picture keeps its light, only drawn differently.

| Setting | File | What it does |
| --- | --- | --- |
| Light Bands (2-8) | `hooks/direct_light.glsl` | How many flat steps the sunlight on a surface has; fewer is bolder |
| Cel Shading (0-1) | `hooks/direct_light.glsl` | Blend from Radiante PT's own sun shading (0) to the bands (1), with a hard highlight on smooth surfaces. Changes live |
| Poster Colours (0-16) | `hooks/surface.glsl` | Levels per colour channel of the textures; 0 or 1 keeps them as they are. Glowing texels are left alone |
| Ink Outlines (0-3) | `post_render/inkwell_ink.frag`, own passes | Outline width in pixels, from depth jumps and normal turns; fades out past about 100 blocks. 0 turns them off |
| Flat Sky | `hooks/sky.glsl` | A two-tone sky at the sky's own brightness, so day, dusk and night still light the world as before |
| Paper (0-1) | `post_render/inkwell_ink.frag` | Cream whites, lifted blacks and a fine grain |

It also shows a pack adding passes: `inkwell_copy` (radiante-pt's `post_copy.frag`, the picture into `post_motion`)
runs at the end of the post script, and `inkwell_ink` says `"after": "inkwell_copy"`.

## pixel-light

Path tracing on the texture's grid. Every hit point is moved to the centre of its texture texel and seen from
there, so shadows, block light, bounces, reflections and highlights land per texel: shadow edges and torch falloff come out in the
texture's own pixels. A retro palette with an ordered dither goes over the picture.

| Setting | File | What it does |
| --- | --- | --- |
| Light Texels per Block (0-16) | `hooks/surface.glsl` | 16: light per texture texel (`hookTexelCentre`); lower merges texels, 0 turns it off |
| Flat Texels | `hooks/surface.glsl` | Drops normal maps, which would shade inside a texel |
| Palette Levels (0-32) | `hooks/post_grade.glsl` | Levels per colour channel; 0 or 1 keeps the colours |
| Dither (0-1) | `hooks/post_grade.glsl` | How much 4x4 ordered dither goes between palette levels |

It shows a pack changing where light is computed (`SampledSurface.worldPos` in `hookSurface`), not only its colour.

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
| Stars (0-2), Aurora (0-0.5), Aurora Everywhere | `hooks/stars.glsl` | Mellow's stars (they replace Radiante's) and its aurora, under the clouds. As in Mellow the aurora shows only where snow falls (`radianteBiomePrecipitation()`), fading in as the camera enters a snowy biome; Aurora Everywhere shows it in every biome |
| Sun Colour (0-1) | `hooks/direct_light.glsl` | Sunlight and moonlight take Mellow's hue; their strength stays Radiante's |
| Round Sun | `hooks/celestial.glsl` | Mellow's round sun and moon (its ROUND_SUN) in place of vanilla's square sprites, at Radiante's brightness. Off, as in Mellow |
| Water Normals, Water Normal Strength, Speed, Size | `hooks/water_surface.glsl`, `hooks/water_waves.glsl` | Mellow's water normals (two scrolling layers of its noise image) on the top of water, in place of Radiante's waves; the three settings are Mellow's WATER_NORMAL_* and scale Radiante's waves and caustics too. Strength changes live |
| Torch Colour (0-1) | `hooks/block_light.glsl` | The light blocks cast and the held light pulled towards Mellow's warm torch colour at their own brightness; 1 is Mellow's single colour for all block light |
| End Sky (0-1) | `hooks/dimension_sky.glsl` | Mellow's End sky (violet top, slowly turning green and orange lights round the horizon, brighter towards the End flash) at Radiante's brightness, and the End fog in its colour |
| Atmospheric Fog (0-1) | `hooks/fog.glsl` | Mellow's fog density (thicker at sunrise, at night and in rain, and far thicker in a pale garden at night), with Volumetric Light on |
| Water Colour (0-1) | `hooks/water.glsl`, `hooks/underwater_fog.glsl` | Mellow's green-teal water, as the shape of the water's absorption, and the colour the view fades into under water (Mellow's tint_underwater) |
| Vibrance, Saturation, Contrast, LUT | `hooks/post_grade.glsl` | Mellow's final grade on the tone-mapped picture; LUT picks one of Mellow's (off by default, as in Mellow) |

Mellow's waving plants (WAVY_PLANTS, on in Mellow) become Radiante's own Wind, turned on by default in the port's
`configs.json`: Radiante moves foliage in a pass of its own, which a pack cannot replace. The time of day (sunrise,
day, sunset, night) is `radianteDayWeights()` from `common/pack_world.glsl`.

It shows what a port by hand looks like: the effect's function is kept, what it read from Iris (images, uniforms,
`worldTime`) is mapped to Radiante's, and its colours are scaled to Radiante's exposure. Mellow's exposure and ACES
curve, its screen-space effects (SSR, SSAO, bloom, TAA) and its shadows are left to Radiante.

The packs are GPL-3.0, as Radiante PT they build on.

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
