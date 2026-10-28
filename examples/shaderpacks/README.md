# Example shader packs

Packs built on Radiante PT, to show what a pack can change and to start your own from. Each one says
`"extends": "radiante-pt@1"` in its manifest and holds only its own files: settings, names, and the hook files it
replaces. The renderer puts radiante-pt under it when it loads it, so the pack follows Radiante PT's updates
([design §5](../../docs/dev/shader-packs/design.md#5-inheritance-and-hook-points)).

## radiante-example

A warm, contrasty look with light through leaves. Each change is a pack setting:

| Setting | File | What it does |
| --- | --- | --- |
| Colour grade | `hooks/post_grade.glsl` (turned on with `RADIANTE_HOOK_POST_GRADE` in `configs.json`) | Warm highlights, cool shadows, more contrast in the mids |
| Light through leaves, strength | `hooks/surface.glsl`, `hooks/direct_light.glsl` | Sunlight behind leaves and small plants lights them from the back (`isLeavesSurface`, `isPlantSurface`); works with opacity micromaps. The strength changes live |
| Water tint | `hooks/water.glsl` | Water absorption: deeper, bluer water |
| Sky tint | `hooks/sky.glsl` | A cooler clear sky |

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

Both packs are GPL-3.0, as Radiante PT they build on.

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
