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
water and sky tints) and `inkwell` (a comic-book look: banded light, poster colours, ink outlines). `realistic`
only gives Radiante PT's settings other defaults and grades the picture, the smallest way to make a look of your own.

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
| `hooks/clouds.glsl` | `hookCloudLayer(vec3 direction)` | A cloud layer over the sky: returns its light (rgb) and how much sky shows through (a). Drawn after rain and over the sun and moon |
| `hooks/cloud_shadow.glsl` | `hookCloudShadow(vec3 worldPos, vec3 lightDir)` | How much sun or moon light your clouds let through to a surface (0 to 1) |
| `hooks/stars.glsl` | `hookStars(inout vec3 radiance, vec3 direction)` | Stars or aurora in the sky the camera sees; return 0 to hide Radiante's own stars, 1 to keep them |
| `hooks/sky_emission.glsl` | `vec3 hookSkyEmission(vec3 direction, bool seen)` | Light glowing in the sky (aurora, rainbow), for every ray that reaches it, so it also lights the world; `seen` is false for rays bounced off the world: give those a cheap, smooth version |
| `hooks/celestial.glsl` | `hookSunDisc(inout vec3 radiance, vec3 direction, vec3 sunDirection, vec3 sunLight)`, `hookMoonDisc(...)` | The sun's and moon's discs: shape, size, colour. `sunLight * coverage` is as bright as Radiante's own disc; the sunlight on the world does not change |
| `hooks/water_waves.glsl` | `hookWaterWaves(inout float strength, inout float speed, inout float size)` | How steep, fast and long Radiante's waves are (Realistic and Bedrock water), caustics included |
| `hooks/water_surface.glsl` | `hookWaterSurface(inout SampledSurface surface, vec3 position, float waveHeight)` | Your own water normals, tint or foam on any water (`waveHeight` is high on the crests); `mat.transmission = 0` makes a point opaque |
| `hooks/underwater_fog.glsl` | `hookUnderwaterFog(inout vec3 colour, inout float visibility)` | The colour the view fades into under water, and how far you see |
| `hooks/underwater.glsl` | `hookUnderwater(inout vec3 color, vec2 uv)` | The picture under water: ripples, a tint. Runs with the post grade pass (`RADIANTE_HOOK_POST_GRADE`) |
| `hooks/dimension_sky.glsl` | `hookNetherSky`, `hookEndSky(inout vec3 radiance, vec3 direction, bool seen)`, `hookNetherFog`, `hookEndFog(inout vec3 colour, inout float density)` | The Nether's and the End's sky and fog (the overworld's sky is `hooks/sky.glsl`) |
| `hooks/block_light.glsl` | `hookBlockLight(inout vec3 radiance, uint kind)` | The colour and strength of block light: what torches, lava or soul fire cast, what you hold, and the glow you see. No block id: tell lights apart by colour |
| `hooks/fog.glsl` | `hookFog(float cosTheta, inout vec3 scattering, inout vec3 extinction)` | Haze and fog with Volumetric Light on |
| `hooks/post_grade.glsl` | `hookPostGrade(inout vec3 color, vec2 uv)` | Colour grade of the finished picture. Turn it on with `{"RADIANTE_HOOK_POST_GRADE": true}` in `execution_post.global_variables` |

Keep the `#ifndef`/`#define` guard of the file, and leave out the `RADIANTE_HOOK_..._DEFAULT` define the default
file has. Settings of your pack are GLSL defines, usable in any hook. Keep them compile-time unless they must change
live: Radiante PT leaves a pack only two `"runtime": true` settings. A hook file follows Radiante PT's updates by
itself, which a copied shading file does not.

Leaves and plants swaying in the wind are moved outside the pack, so there is no hook for it; a pack can turn
Radiante PT's Wind on by default (`{"name": "render_pipeline.module.ray_tracing.attribute.wind", "default_value":
"render_pipeline.true"}` in its attributes) and set `wind_strength` the same way.

### Weather, biome and time of day

`#include "common/pack_world.glsl"` in a hook gives you the weather, the biome at the camera and the time of day, in
every hook including `post_grade.glsl`:

| Function | Value |
| --- | --- |
| `radianteRain()`, `radianteThunder()` | Rain and thunder, 0 to 1 (Iris's `rainStrength`, `thunderStrength`) |
| `radianteWetness()`, `radianteRainStoppedFor()` | The air's wetness, the rain smoothed so it lingers after rain (Iris's `wetness`), and seconds since the rain stopped (0 while it rains), for what follows rain for minutes |
| `radianteBiomeTemperature()`, `radianteBiomeDownfall()` | The biome's temperature and downfall |
| `radianteBiomeSnowy()`, `radianteBiomeDry()`, `radianteBiomeRainy()` | 0 to 1: snow falls here, nothing falls here, rain falls here |
| `radianteBiomePrecipitation()` | 0 nothing, 1 rain, 2 snow (Iris's `biome_precipitation`) |
| `radianteBiomeIs(RADIANTE_BIOME_PALE_GARDEN)` | 0 to 1: how much the camera is in that biome |
| `radianteSunDirection()`, `radianteMoonDirection()`, `radianteSunHeight()` | Where the sun and moon are; the sun's height, -1 to 1 |
| `radianteSunAngle()` | 0 sunrise, 0.25 noon, 0.5 sunset, 0.75 midnight (Iris's `sunAngle`) |
| `radianteDayWeights()`, `radianteSunrise()`, `radianteDay()`, `radianteSunset()`, `radianteNight()` | How much it is sunrise, day, sunset and night, adding up to 1 |
| `radianteMoonPhase()`, `radianteMoonLit()` | The moon's phase, 0 full to 7, and how much of it is lit |

The biome values fade over about two seconds when the camera crosses into another biome, so an effect tied to them
fades too. `RADIANTE_BIOME_<NAME>` exists for every vanilla biome; for a modded one, the header explains how to work
out its value from its id. For example, an aurora only where it snows:
`strength *= radianteBiomeSnowy();` (the Mellow port does this).

### Live settings, bloom and lens effects in the grade

An attribute with `"runtime": true` changes without a rebuild, in the ray tracing shaders and in the post passes
alike: its define reads the world uniform (`worldUBO.packParams`, 256 slots, Radiante PT uses about 60). Because the
value is read while the shader runs, test it with `if`, not `#if`: `if (int(MY_PALETTE) > 1) { ... }`. Pixel Light's
palette and dither and the Mellow port's grade work this way.

`hooks/post_grade.glsl` sees `common/pack_post.glsl`, which a pack's own post passes can include too. It declares
`worldUBO` and `skyUBO` (define `RADIANTE_POST_UNIFORMS_DECLARED` first if your pass declares them itself) and the
bloom:

| Function | Value |
| --- | --- |
| `radianteBloom(uv)` | The picture before tone mapping, blurred: Radiante's bloom pyramid, half the screen's size, on the exposed scale (1 is about white) |
| `radianteBloomDisplay(uv, strength)` | The same tone mapped and encoded for the screen, to add to the grade's colour |
| `radianteAddBloom(color, uv, strength)` | `color` with the bloom added as light; 0.05 to 0.3 is a usual glow |

The pyramid is only drawn for a pack that asks: `{"RADIANTE_POST_BLOOM": true}` in
`execution_post.global_variables`, next to `RADIANTE_HOOK_POST_GRADE` (otherwise `radianteBloom` is black, and with
Radiante's own Bloom setting on, the picture already has it). Its threshold and spread are Radiante's bloom settings.
Sample it at other places for lens effects: mirrored through the centre for flares, stretched for streaks. For
chromatic aberration, read `hookPostGradeInput` (the picture) at a slightly different `uv` per channel. The example
pack does all three, each strength a live setting.

## Your settings page

The settings your pack adds (the attributes Radiante PT does not have) get a page of their own in Radiante's
settings, named after the pack and with its icon; Radiante PT's settings stay on Radiante's pages. Every key below is
optional, the renderer ignores them, and a key with the wrong kind of value is skipped with a line in the log:

```json
{
    "name": "render_pipeline.module.ray_tracing.attribute.mellow_cloud_opacity",
    "type": "float_range:0.0-0.38",
    "default_value": "0.24",
    "define": "MELLOW_CLOUD_OPACITY",
    "group": "mellow.group.clouds",
    "order": 22,
    "step": 0.02,
    "description": "mellow.desc.cloud_opacity",
    "performance_impact": "low",
    "advanced": false,
    "visible_when": {"attribute": "mellow_cloud_style", "not_equals": 0}
}
```

| Key | What it does |
| --- | --- |
| `group` | The heading the setting goes under: a key of your lang files (`mellow.group.clouds=Clouds`). Without it the setting sits at the top of the page |
| `order` | Settings are sorted by it (default 0), then by their place in the file; headings come in the order of their first setting |
| `advanced` | `true`: shown only with Advanced on, under the group's other settings |
| `description` | A lang key for the text in the description panel (or plain text, shown as written) |
| `performance_impact` | `low`, `medium` or `high`: shown under the description as for Radiante's own settings |
| `step` | The slider's step for `int_range` and `float_range` (`0.05`, `2`...), counted from the minimum |
| `visible_when` | `{"attribute": "...", "equals": value}`, or `"not_equals"`; the value can be a list. The setting shows only while that one matches. `true`, `"true"` and `"render_pipeline.true"` are the same value, `2` and `"2.0"` too, and an enum value can be its last part (`"off"`) |

Names in these keys (`visible_when`, `presets`) may leave out
`render_pipeline.module.ray_tracing.attribute.`. Settings of type `vec3` have no row yet.

**Presets** go at the top of `configs.json`, next to `attributes`: named sets of your settings, shown as a **Pack
Preset** row at the top of the page. Picking one sets its values (live or with a rebuild, as each setting does); the
row shows the preset the values match, or Custom. A preset sets only your own settings. Give every preset the same
settings, so the one in use is never ambiguous:

```json
"presets": [
    {"name": "mellow.preset.clear", "description": "mellow.preset.clear.desc",
     "values": {"mellow_cloud_amount": 14, "mellow_cloud_opacity": 0.16, "mellow_fog": 0.1}}
]
```

**Radiante PT's settings** start at Radiante PT's defaults, the Bedrock RTX look, in a pack built on it. To start one
somewhere else, list it in your `configs.json` with the same name and a new default; the rest of its entry comes from
Radiante PT (`{"name": "render_pipeline.module.ray_tracing.attribute.sun_glow", "default_value": "0.5"}`). The player
can still change it. The `realistic` example pack is made of such entries. Tone mapping (exposure, contrast,
saturation, bloom) is not a pack's: change the picture in `hooks/post_grade.glsl` instead.

The names of groups, descriptions and presets go in the same `lang/*.lang` files as the settings' names, in every
language you ship. The example packs use all of these keys.

## Working on a pack in game

Put the pack in `radiante/shaderpacks/` as a folder, not a zip, and choose it.

- **Hot reload**: save a file of the pack and the game picks it up within about two seconds. The pack is checked
  first (loaded and compiled in the background, while you keep playing); if it compiles, the pipeline is built again
  with it, the same way a settings change is applied. If it does not, the picture you had stays, a popup shows the
  first error, and the whole report is on the Shader Packs screen (**Copy Error**) and in the game log.
- `/radiante pack reload` does the same now; `/radiante pack validate [name]` checks any pack in the folder (the
  active one without a name) without switching to it, and prints the report in the chat.
- **Debug views**: `/radiante debug view <view>` shows one of the frame's images instead of the picture: `albedo`,
  `normal`, `roughness`, `metallic`, `emission`, `depth`, `motion` (motion vectors) or `flags` (the kind of surface:
  water, lava, glass, leaves, plants...). `/radiante debug view off` goes back; without a view it lists them. The view
  is not saved.
- Your own debug views: list them in `configs.json`, `"debug_views": [{"name": "wetness", "define": "MY_DEBUG_WET"}]`.
  The define is 0, and 1 (or `"value"`) while that view is chosen, so `#if MY_DEBUG_WET` in any shader shows what you
  want, for example in `hookPostGrade`. A pack that extends Radiante PT keeps its views and adds its own.

## Check a pack without the game

`examples/shaderpacks/README.md` shows how to put a pack together and compile all its shaders offline with the tools
in `native/src/tools/`: errors show with file and line, as they do under the list in game. `compile_pack.py
--debug-view <view>` compiles a debug view too.

## What a pack has

| Part | Where | Notes |
| --- | --- | --- |
| Manifest | `configs.json`, `"radiante"` | Name, version, author, licence, description, icon: shown in the list |
| Settings | `configs.json`, `"attributes"` | Each attribute is a setting and a GLSL define; `"runtime": true` changes it live without rebuilding |
| Setting names | `lang/en_us.lang`, `es_es`, `zh_cn` | Keyed by the attribute name; also the names of groups, descriptions and presets |
| Settings page | `configs.json`, attribute keys, `"presets"` | Groups, order, descriptions, cost, presets ([above](#your-settings-page)) |
| Passes | `configs.json`, `"passes"` | Ray tracing, compute and full-screen passes, e.g. a colour grade after tone mapping |
| Shaders | the pack's folders, `hooks/` | GLSL, compiled when the pack is chosen |
| Base | `configs.json`, `"radiante"`, `"extends"` | `radiante-pt@1`: Radiante PT under the pack |

Useful helpers from Radiante PT's headers: `isLeavesSurface`, `isPlantSurface`, `isWaterSurface`, `isGlassSurface`
and the other surface tests in `util/vertex.glsl`; the sun, moon and weather in `worldUBO` and `skyUBO`.

## When something goes wrong

- A pack that does not load or compile is replaced by Radiante PT, and the compiler's messages (file and line) show
  under the list. **Copy Error** copies them. An edit to a folder pack that does not compile is not built at all: the
  running pack stays.
- If the game closes while trying a pack, the next start goes back to Radiante PT and names the pack.
- Settings are kept per pack, so a pack starts from its own defaults, not from Radiante PT's.
- A pack made for another Radiante (`extends` of another base or hook version, a newer `api_version`, or
  `"requires": {"radiante": ">=..."}` this version does not meet) is listed with the reason and cannot be chosen.

## Licence

Radiante PT is GPL-3.0, so a pack built from its files is GPL-3.0 too. Whether the future public headers get a more
permissive licence is still open.
