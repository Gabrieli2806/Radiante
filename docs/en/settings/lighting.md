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


## Per-block emission (Advanced)

Open **Lighting → Advanced → Block Emission**. Each texture profile has a **0–1000%** slider in 1% steps:
**100%** keeps the current material, **0%** removes its emission, and **1000%** multiplies it by ten.
These multiply Light Source Brightness and Held Light Brightness rather than replacing them. Search by block or
texture name, then press **Apply**. **Reset** restores 100% for every profile.

The list includes the built-in emissive textures, active light-source textures and saved resource-pack overrides.
Variants such as lit redstone lamps have separate rows. **Cave Vines** with berries have tip and stem profiles;
**Vines**, **Glow Lichen**, twisting and weeping vines also have controls for packs that make them glow.
A multiplier does not turn a non-emissive material into a light. Texture profiles affect both visible glow and the
light cast, including corresponding held and point lights. Overrides stay saved when their resource pack is disabled
and are included in shared settings codes.
