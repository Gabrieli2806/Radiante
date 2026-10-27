# Making a shader pack

Languages: [ES](../es/paquetes-de-shaders.md) · **EN**

A shader pack is the path tracer Radiante runs: a zip of GLSL shaders and a `configs.json`. Radiante ships one,
Radiante PT; players add others in `radiante/shaderpacks/` and pick them under **Shader Packs** in Radiante's settings
([settings](settings.md#shader-packs)).

This page is for pack authors, as things are today. Pack support is young: the interface packs build on is not
frozen yet, so a pack may need updating after a Radiante release. What is planned, and how packs will stay working
across releases, is in the [shader packs design](../dev/shader-packs/README.md).

## Start from the example

Packs today are built on Radiante PT. The quickest start is the example pack in the source repository,
[`examples/shaderpacks/radiante-example`](../../examples/shaderpacks/): it holds only the files it changes (a colour
grade, light through leaves, water and sky tints, each a setting).

1. Clone the repository and copy `examples/shaderpacks/radiante-example/` to a new folder next to it.
2. In `configs.json`, under `"radiante"`, set `display_name`, `version`, `author`, `license`, `description` and
   `icon`.
3. Change shaders or add your own; keep only the files you change.
4. `./gradlew shaderPackExamples` builds `build/shaderpacks/<folder>.zip` on top of Radiante PT.
5. Copy the zip to `radiante/shaderpacks/` and choose it in **Shader Packs**.

## What a pack has

| Part | Where | Notes |
| --- | --- | --- |
| Manifest | `configs.json`, `"radiante"` | Name, version, author, licence, description, icon: shown in the list |
| Settings | `configs.json`, `"attributes"` | Each attribute is a setting and a GLSL define; `"runtime": true` changes it live without rebuilding |
| Setting names | `lang/en_us.lang`, `es_es`, `zh_cn` | Keyed by the attribute name |
| Passes | `configs.json`, `"passes"` | Ray tracing, compute and full-screen passes, e.g. a colour grade after tone mapping |
| Shaders | the pack's folders | GLSL, compiled when the pack is chosen |

Useful helpers from Radiante PT's headers: `isLeavesSurface`, `isPlantSurface`, `isWaterSurface`, `isGlassSurface`
and the other surface tests in `util/vertex.glsl`; the sun, moon and weather in `worldUBO` and `skyUBO`.

## When something goes wrong

- A pack that does not load or compile is replaced by Radiante PT, and the compiler's messages (file and line) show
  under the list. **Copy Error** copies them.
- If the game closes while trying a pack, the next start goes back to Radiante PT and names the pack.
- Settings are kept per pack, so a pack starts from its own defaults, not from Radiante PT's.

## Licence

Radiante PT is GPL-3.0, so a pack built from its files is GPL-3.0 too. Whether the future public headers get a more
permissive licence is still open.

## Radiance packs

Packs made for Radiance, the project Radiante's renderer comes from, do not run as they are: the renderer's data
changed since. See [what happened when they were tried](../dev/shader-packs/radiance-packs.md).
