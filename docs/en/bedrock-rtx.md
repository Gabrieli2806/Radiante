[← Back to index](README.md)

# Bedrock RTX packs and look

Radiante reads resource packs made for **Minecraft Bedrock RTX** (`.mcpack`, `.mcaddon`) and looks like Bedrock RTX
by default. Neither needs a pack: Radiante ships its own maps for 159 vanilla textures. A Bedrock pack adds more
detail and materials.

## Using a `.mcpack`

1. Drop the `.mcpack` (or `.mcaddon`) into Minecraft's `resourcepacks` folder, or drag it onto the pack screen.
2. Enable it in the resource pack list, like any other pack. No unzipping or renaming.
3. The first time, Radiante converts it to a LabPBR pack in `radiante/bedrock_packs/`. Later starts reuse that copy.

<!-- Screenshot pending: the resource pack list with a .mcpack visible and enable-able -->

| What | How it is used |
|---|---|
| Colour, MER(S) and normal or height maps | Converted to LabPBR (`_s`, `_n`). With **Bedrock Pack Materials** on (default), metal, roughness and glow are read from the pack's own MER maps at full detail, as Bedrock does. |
| Per-biome fog and water | Feed [Biome Ambiance](settings/sky-fog-water.md#biome-ambiance), at Radiante's subtler strength. |
| Vibrant Visuals light colours (`local_lighting.json`) | Torches, lanterns, campfires and other lights take the pack's colour. Candles, amethyst and other listed blocks get a point light in that colour. |
| Shield attachables (e.g. dynamic light torches) | Drawn on the shield in hand; it glows and lights the world like a held torch. |

<details>
<summary>Details: add-ons, subpacks and limits</summary>

- Inside an `.mcaddon`, every resource pack is found (folders with a `manifest.json`, and nested `.mcpack` files).
  Several are layered in the order found, the first on top.
- **Behaviour packs are skipped:** custom blocks, functions and scripts only run in Bedrock. The pack list says
  "Bedrock add-on: resource pack only" and the log names them.
- A pack with **subpacks** is read with its first subpack. There is no subpack choice yet.
- Maps finer than the colour texture (96 px is common) keep their detail when the colour texture can be scaled up
  cleanly; otherwise they are averaged down.
- Only the hue of Vibrant Visuals colours is used; brightness follows Radiante's calibration. Static lights (lava,
  froglights, redstone lamps) keep their texture's glow.
- Archives inside the file are unpacked up to a size and count limit; anything left out is named in the log.

</details>

## The Bedrock RTX look

radiante-pt's defaults are the Bedrock RTX look: Bedrock's sky and sunlight, bloom, tone curve and exposure, glass,
water and fog. On top it adds a few extras, such as light blocks lit by their own light and glowing End portals. It
works with or without a `.mcpack`; the two together come closest to Bedrock RTX. **Reset to Defaults** brings it
back after changing settings.

Without a Bedrock pack, Radiante uses built-in values for each biome's fog and water and each light block's
brightness. Any Bedrock pack you enable replaces them.

Other looks are [shader packs](settings.md#shader-packs).

## Files from a Bedrock install

A few parts of the look use files that come with Minecraft Bedrock itself. Radiante looks for them in
`radiante/bedrock/` in the game folder, then in Bedrock's install folder
(`C:/XboxGames/Minecraft for Windows/Content/data/ray_tracing/`).

| File | Used for | Without it |
|---|---|---|
| `sky.png`, `look_up_tables.png` | **Atmosphere: Bedrock**: Bedrock RTX's sky and sunlight colours | The option is grey and Radiante's own sky is used. |
| `water_n.tga` | Bedrock's water surface | Radiante's own water surface. |
| `caustics.png`, `wibbly.png` | **Bedrock Water Caustics** (Advanced, off by default) | Radiante's own caustics. |

Without Bedrock on this PC, copy those files from another source into `<game folder>/radiante/bedrock/` with these
exact names.

<!-- Screenshot pending: the sky with Atmosphere set to Java and to Bedrock, same time of day -->
<!-- Screenshot pending: the same scene with a .mcpack enabled and disabled -->

Visual parity with Bedrock RTX is tracked in [issue #19](https://github.com/Gabrieli2806/Radiante/issues/19).
