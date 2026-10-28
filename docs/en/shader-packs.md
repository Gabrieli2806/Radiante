# Making a shader pack

Languages: [ES](../es/paquetes-de-shaders.md) · **EN**

A shader pack is the path tracer Radiante runs: a zip of GLSL shaders and a `configs.json`. Radiante ships one,
Radiante PT; players add others in `radiante/shaderpacks/` and pick them under **Shader Packs** in Radiante's settings
([settings](settings.md#shader-packs)).

This page is for pack authors, as things are today. Pack support is young: the interface packs build on is not
frozen yet, so a pack may need updating after a Radiante release. What is planned, and how packs will stay working
across releases, is in the [shader packs design](../dev/shader-packs/README.md).

## Start from an example

A pack builds on Radiante PT and holds only what it changes. Two examples are in the source repository,
[`examples/shaderpacks/`](../../examples/shaderpacks/): `radiante-example` (a colour grade, light through leaves,
water and sky tints) and `inkwell` (a comic-book look: banded light, poster colours, ink outlines).

1. Clone the repository and copy one of them to a new folder next to it.
2. In `configs.json`, under `"radiante"`, set `display_name`, `version`, `author`, `license`, `description` and
   `icon`. Keep `"extends": "radiante-pt@1"`.
3. Change the hook files, add settings, passes or shaders; keep only the files you change.
4. `./gradlew shaderPackExamples` zips it into `build/shaderpacks/<folder>.zip`.
5. Copy the zip to `radiante/shaderpacks/` and choose it in **Shader Packs**.

## Building on Radiante PT: `extends`

With `"extends": "radiante-pt"` (or `"radiante-pt@1"`, the version of Radiante PT's hooks it is written for) in the
manifest, Radiante puts Radiante PT under the pack when it loads it:

- **Files**: Radiante PT's, then yours over them. A file at the same path replaces Radiante PT's.
- **`configs.json`**: merged. Attributes, textures, buffers and passes are matched by `name`: yours are added, and an
  entry with the name of one of Radiante PT's changes only the fields you give (`{"name": "...", "default_value":
  "..."}` changes a default). A new pass runs at the end of its stage unless it says `"before"` or `"after"` another
  pass. Script variables (`global_variables`) are matched by name; `commands` you give replace Radiante PT's, and a
  `{"base": true}` command among them runs Radiante PT's there.
- **`lang/*.lang`**: Radiante PT's names, then yours.

The exact rules are in the [design, section 5](../dev/shader-packs/design.md#5-inheritance-and-hook-points).

## Hooks

Radiante PT calls a small function at the places a pack most often wants to change. Each is a file under `hooks/`
that does nothing in Radiante PT; put your own file at the same path:

| File | Function | What you can do there |
| --- | --- | --- |
| `hooks/surface.glsl` | `hookSurface(inout SampledSurface surface, uint surfaceFlags, uint bounce)` | Change a surface's colour, roughness, normal or glow after its textures are read. `isLeavesSurface(surfaceFlags)` and the other tests in `util/vertex.glsl` say what was hit `hookTexelCentre(surface.uv, surface.worldPos)` gives the centre of the texel hit; moving `surface.worldPos` there and setting `g_hookViewDir` lights and reflects per texel (see the Pixel Light example) |
| `hooks/direct_light.glsl` | `hookDirectLight(SampledSurface surface, vec3 viewDir, vec3 lightDir, float NoL, inout vec3 lightBRDF)` | Change how the sun and moon light a surface (cel bands, light through leaves when `NoL < 0`) |
| `hooks/water.glsl` | `hookWaterAbsorption(inout vec3 extinction)` | Water colour: how fast each colour is absorbed |
| `hooks/sky.glsl` | `hookSky(inout vec3 radiance, vec3 direction)` | Sky colour; it also colours the light the sky casts |
| `hooks/fog.glsl` | `hookFog(float cosTheta, inout vec3 scattering, inout vec3 extinction)` | Haze and fog with Volumetric Light on |
| `hooks/post_grade.glsl` | `hookPostGrade(inout vec3 color, vec2 uv)` | Colour grade of the finished picture. Turn it on with `{"RADIANTE_HOOK_POST_GRADE": true}` in `execution_post.global_variables` |

Keep the `#ifndef`/`#define` guard of the file. Settings of your pack are GLSL defines, usable in any hook. A hook
file follows Radiante PT's updates by itself, which a copied shading file does not.

## Check a pack without the game

`examples/shaderpacks/README.md` shows how to put a pack together and compile all its shaders offline with the tools
in `native/src/tools/`: errors show with file and line, as they do under the list in game.

## What a pack has

| Part | Where | Notes |
| --- | --- | --- |
| Manifest | `configs.json`, `"radiante"` | Name, version, author, licence, description, icon: shown in the list |
| Settings | `configs.json`, `"attributes"` | Each attribute is a setting and a GLSL define; `"runtime": true` changes it live without rebuilding |
| Setting names | `lang/en_us.lang`, `es_es`, `zh_cn` | Keyed by the attribute name |
| Passes | `configs.json`, `"passes"` | Ray tracing, compute and full-screen passes, e.g. a colour grade after tone mapping |
| Shaders | the pack's folders, `hooks/` | GLSL, compiled when the pack is chosen |
| Base | `configs.json`, `"radiante"`, `"extends"` | `radiante-pt@1`: Radiante PT under the pack |

Useful helpers from Radiante PT's headers: `isLeavesSurface`, `isPlantSurface`, `isWaterSurface`, `isGlassSurface`
and the other surface tests in `util/vertex.glsl`; the sun, moon and weather in `worldUBO` and `skyUBO`.

## When something goes wrong

- A pack that does not load or compile is replaced by Radiante PT, and the compiler's messages (file and line) show
  under the list. **Copy Error** copies them.
- If the game closes while trying a pack, the next start goes back to Radiante PT and names the pack.
- Settings are kept per pack, so a pack starts from its own defaults, not from Radiante PT's.
- A pack made for another Radiante (`extends` of another base or hook version, a newer `api_version`, or
  `"requires": {"radiante": ">=..."}` this version does not meet) is listed with the reason and cannot be chosen.

## Licence

Radiante PT is GPL-3.0, so a pack built from its files is GPL-3.0 too. Whether the future public headers get a more
permissive licence is still open.
