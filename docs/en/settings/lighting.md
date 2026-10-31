[← Settings](../settings.md) · [Index](../README.md)

# Lighting

![Radiante settings: Lighting](../../images/ui/settings-lighting.png)

| Control | Range | Default | What it does |
|---|---|---|---|
| **Day Brightness** | 0% – 400% | 25% | Sun and sky light by day. |
| **Night Brightness** | 0% – 400% | 35% | Moon and sky light by night. |
| **Light Source Brightness** | 0% – 400% | 12% | Blocks that give off light: torches, lava, glowstone… |
| **Held Light Brightness** | 0% – 400% | 12% | What you hold: its own glow and the light it casts. Needs Held Item Light. |
| **Bounced Light** | 50% – 200% | 150% | How much light carries on after a bounce. 100% is physically correct; higher fills rooms like Bedrock RTX. |
| **Sky Light** | 50% – 400% | 180% | How much the open sky lights the world. |
| **Translucency** | 0% – 200% | 100% | Sun and moonlight let through leaves and plants from behind (they glow against a low sun), and wrapped a little past the shadowed edge of ice and snow, with a blue tint. 0% lights only their sunny side. Costs no extra rays. Shader packs can set it per surface (`surface.mat.subSurface` in `hookSurface`). |
| **Block Light Sampling** | On / Off | On | Each surface casts a shadow ray straight at a nearby light. Much less noise at night and in caves. Needs Block Emission (in [Performance](performance.md)). |
| **ReSTIR Block Lights** | On / Off | On | Reuses the light each pixel picked across frames and neighbours. Much less noise in rooms with many lights. Needs Block Light Sampling. |
| **ReSTIR Bounced Light** | On / Off | Off | ReSTIR GI: reuses the first bounce of light each pixel found across frames and neighbouring pixels. Less noise in rooms and caves lit only by bounced light; light that changes (a torch broken, a door opened) may take a moment to follow. |
| **Shader Execution Reordering** | On / Off | Off | SER: the GPU groups similar rays. Meant for RTX 40 or newer. Experimental: water may look white. |
| **Held Item Light** | On / Off | On | A torch or lantern in either hand lights its surroundings. |
| **First Person Shadow** | On / Off | On | Draws the player for shadows and reflections in first person. |

## Bounced Light and Sky Light are part of the Picture Style too

They change with the Picture Style (see [Image](image.md) and
[styles-and-quality.md](../styles-and-quality.md)). Changing them by hand turns the style into
Custom.


## Per-block HDR emission (Advanced)

Open **Lighting → Advanced → Block Emission**, then click a block's colour swatch. The picker has RGB controls,
exposure, and numeric **linear HDR RGB** fields. **(1,1,1)** preserves the material's colour and brightness;
**(0,0,0)** disables emission; **(10,10,10)** is ten times brighter. The maximum per channel is 65408.
Values multiply the sampled emission rather than replacing its texture. Done stages the colour; Apply on the
settings screen updates geometry and lighting. Cancel leaves the colour alone; Reset restores neutral white.

The searchable list includes built-in sources, emissive LabPBR/MER textures from active packs, vines and saved
custom entries. Textures discovered only through a resource pack also cast sampled light, even if the block has
no vanilla light level. Overrides persist when a pack is disabled and are included in shared settings codes.
Old 0–1000% settings migrate automatically to equal RGB multipliers.

A pack may supply material-only RGB emission tints in `assets/radiante/emission_tints.json`, for example
`{"minecraft:block/sea_lantern":[0.9,0.95,1.0]}`. These tint the material's diffuse-derived emission, preserving the
original diffuse texture. The player's picker multiplies this authored tint. Dedicated held-light colours retain
their existing calibration and take the player's additional tint.
