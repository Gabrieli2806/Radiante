# Example shader packs

Packs built on Radiante PT, to show what a pack can change and to start your own from. Each folder holds only the
files that pack changes; the rest comes from Radiante PT when the pack is built.

## radiante-example

A warm, contrasty look with light through leaves. Each change is a pack setting:

| Setting | File | What it does |
| --- | --- | --- |
| Colour grade | `post_render/example_grade.frag`, a pass in `configs.json` | Warm highlights, cool shadows, more contrast in the mids |
| Light through leaves, strength | `pt/pt_direct.glsl`, `pt/pt_shade_default.glsl` | Sunlight behind leaves and small plants lights them from the back (`isLeavesSurface`, `isPlantSurface`); works with opacity micromaps. The strength changes live |
| Water tint | `common/water_medium.glsl` | Water absorption: deeper, bluer water |
| Sky tint | `common/world_miss_eval_impl.glsl` | A cooler clear sky |

`configs.json` has the manifest (name, version, author, licence, icon) and the five settings; `lang/` names them.
GPL-3.0, as Radiante PT it is built from.

## Build and try

```
./gradlew shaderPackExamples
```

writes `build/shaderpacks/radiante-example.zip`. Copy it to `radiante/shaderpacks/` in the game folder (in a dev run:
`run/radiante/shaderpacks/`), then Radiante settings → **Shader Packs** → Radiante Example Pack → **Use**.

The task stages each example the way the native build stages Radiante PT (`native/src/shader/CMakeLists.txt`): the
Radiante PT folder, then `shader/util/` into `util/`, `src/common/` into `common/`, the SHaRC headers into
`extern/sharc/include/`, and last the example's own files over them. Once the loader supports `extends`
([roadmap](../../docs/dev/shader-packs/roadmap.md) phase 1), a folder like this one is the pack itself.

## Your own pack

1. Copy `radiante-example/` to a new folder here and change `display_name`, `version`, `author` and `license` in
   `configs.json`.
2. Change or add files; only keep the ones you change.
3. `./gradlew shaderPackExamples`, copy the zip, try it. Errors show under the list in the Shader Packs screen.

Overridden shading files (`pt/pt_direct.glsl`…) follow Radiante PT's version at the time you copied them: rebuild and
check your pack after a Radiante update. Hook files (phase 1) are meant to remove that step. More in
[Making a shader pack](../../docs/en/shader-packs.md).
