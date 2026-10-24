[← Back to index](README.md)

# Bedrock RTX resource packs

Radiante can read `.mcpack` resource packs made for **Minecraft Bedrock RTX** and convert them into a
LabPBR pack its own renderer understands, as well as read the sky and caustics files from a Bedrock
install on the same PC. Neither is required — Radiante ships its own maps for 159 vanilla textures —
but a Bedrock pack usually gives more detail and material variety than what is included.

## Using a `.mcpack` pack

1. Drop the `.mcpack` file straight into Minecraft's `resourcepacks` folder, just like any other
   resource pack.
2. Enable it from the game's resource pack menu: Radiante detects it directly in the list, without
   you needing to unzip it or rename it to `.zip` first.
3. The first time you enable it, Radiante converts it to a LabPBR pack and saves the result in
   `radiante/bedrock_packs/` inside the game directory (not next to the original `.mcpack`, so it
   does not show up twice in the pack list). Later uses read the converted copy directly, and only
   convert again if the `.mcpack` changed or a newer version of the converter requires it.

<!-- Screenshot pending: the resource pack list with a .mcpack visible and enable-able -->

### What exactly gets converted

Bedrock describes each texture with a "texture set": a colour texture, a MER(S) map (metalness,
emission, roughness, and optionally subsurface), and a height or normal map. For every Java block
texture that has a Bedrock counterpart, the converter builds:

- The colour texture (if it is size/format compatible with Java's).
- A LabPBR specular map (`_s.png`) from the MER — Bedrock's metalness, emission, roughness and
  subsurface are translated into the channels LabPBR expects.
- A LabPBR normal map (`_n.png`), from Bedrock's normal map if it has one, or derived from its
  height map otherwise.

Maps finer than the colour texture (96 px is common in Bedrock RTX packs, versus Java's 16 px) keep
their own detail when the colour texture can be scaled up to match without looking soft; otherwise
they are averaged down to the colour texture's size. The Bedrock pack's per-biome fog and water are
converted too, and feed Biome Ambiance: the pack's colours and which biomes are thick or clear
are kept, at Radiante's subtler strength (see [Sky & Water](settings/sky-fog-water.md)).

### The pack's materials as authored

The converted copy also keeps the pack's original MER maps. With **Bedrock Pack Materials** on (the
default, in the [Performance](settings/performance.md) section) metalness, roughness and which
texels glow are read from those maps at full detail, as Bedrock does, instead of from the LabPBR
copy: the small smooth specks of ores and the edges of metal keep their reflections, and a light
emitting block glows where the pack says it does. How much light a block casts still follows its
light level, so a pack's lights fit Radiante's light scale. It has no effect on Java packs.

## The renderer profiles

**Renderer Profile** (in the [Image](settings/image.md) section) has two profiles; changing a
setting one of them manages turns it into **Custom**.

- **Bedrock RTX (Enhanced)**, the default, brings the rest of the look to Bedrock RTX: Bedrock's
  sky and sunlight, bloom, a Bedrock-style tone curve built from the picture's histogram, exposure
  metered on the light reaching the surfaces in view, exact sRGB colours, Bedrock's glass and water
  reflections and water surface, sunlight filtered by glass, Bedrock's own fog volume and denser
  water. On top of Bedrock it keeps a few extras: light blocks with little glow in the pack look lit
  by their own light, and End portals glow. It works with or without a `.mcpack`; the two together
  are the closest to Bedrock RTX.
- **Realistic** starts from the same look and sets less fill light, more contrast, calmer colour,
  finer waves and clearer teal water, so that nothing looks like plastic. See
  [Image](settings/image.md) for every value.

## Files from a Bedrock install

Some parts of the Bedrock look need files that come with Minecraft Bedrock, not with a resource
pack. Radiante looks for each of them at, in order:

1. `radiante/bedrock/` inside the game directory.
2. `C:/XboxGames/Minecraft for Windows/Content/data/ray_tracing/` — the default path where
   Minecraft Bedrock (the Xbox/Microsoft Store version) installs them, if you have Bedrock
   installed on the same PC.

| File | Used for | Without it |
|---|---|---|
| `sky.png` (64×256) and `look_up_tables.png` (128 wide) | **Atmosphere: Bedrock** (in [Sky & Water](settings/sky-fog-water.md)): Bedrock RTX's sky and sunlight colours | The option is grey and shows Java (Bedrock not available); Radiante's own sky is used. |
| `water_n.tga` | Bedrock's water surface (three scrolling layers of its water normals), used by the Bedrock RTX (Enhanced) profile | The water keeps Radiante's own surface. |
| `caustics.png` and `wibbly.png` | **Bedrock Caustics**, under Advanced (off by default) | The option is grey; Radiante's procedural caustics are used. |

<!-- Screenshot pending: the sky with Atmosphere set to Java and to Bedrock, same time of day -->

### Copying the files by hand

If you do not have Bedrock installed on this PC but do have those files from another source, place
them under `radiante/bedrock/` inside the game directory, with these exact names:

```
<game directory>/radiante/bedrock/sky.png
<game directory>/radiante/bedrock/look_up_tables.png
<game directory>/radiante/bedrock/water_n.tga
<game directory>/radiante/bedrock/caustics.png
<game directory>/radiante/bedrock/wibbly.png
```

## Before / after

<!-- Screenshot pending: the same scene with a .mcpack enabled and disabled -->

## See also

- [styles-and-quality.md](styles-and-quality.md) — the "Bedrock" Picture Style tunes saturation and
  bounced light to look closer to Bedrock RTX; it is a colour adjustment independent from using a
  pack or Bedrock's sky files.
- The state of visual parity with Bedrock RTX (what is still missing, what already matches) is
  tracked in [issue #19](https://github.com/Gabrieli2806/Radiante/issues/19).
