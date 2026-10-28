# Bedrock RTX (Enhanced) defaults without a pack

With a Bedrock pack enabled, Radiante gets its fog, water and light block glow from the pack. Without any Bedrock pack
it used Radiante's own values, which look different. It now falls back to built-in values when no Bedrock pack is
enabled. (This was first done for the Bedrock RTX (Enhanced) renderer profile; profiles are gone and radiante-pt's
defaults are that look, so the fallback is always used.) This page lists what differs, what
was changed and what still needs a pack.

## Method

The values come from Radiante's own conversion code, so they are what the renderer reads with a converted pack enabled:

- `common/src/main/resources/assets/radiante/bedrock_defaults/fog.json` is the `assets/radiante/bedrock_fog.json`
  that `BedrockFog.convert` writes into a converted pack (converter version 23), with the comment replaced.
- `.../bedrock_defaults/emission.json` is built from the same converted pack's `assets/radiante/bedrock_mer/*.png` maps (the raw
  MER maps `PbrAtlases` reads), reduced to what `EmissionTiles.uploadFromMer` uses. For each sprite, `glow` is
  MER green² summed over the texels whose colour is opaque (alpha ≥ 128) and divided by all the sprite's texels, which
  is the mean the light it casts is made of. Maps finer than 16 px are averaged down by blocks first, as
  `stitchResource` does, and animated maps keep their first frame. Each sprite also gets the mean metalness and
  roughness of its glowing texels. `dark` lists the sprites the pack gives a MER map without any glow.

To regenerate them from another pack version, convert it in game, then run the two scripts again on the
converted zip (they are short: read the PNGs with Pillow and apply the formula above).

## How it is wired

- `BedrockDefaults` loads both files from the classpath, not through the resource manager, so they never count as an
  enabled pack. They are always in use; a pack that covers a biome or a sprite wins.
- Fog and water (`BiomeAmbiance.reloadPackFogIfNeeded`): when no resource pack provides
  `radiante:bedrock_fog.json`, the built-in file is read through the same code as a pack's
  (`readPackFog`, then `styled` for the air and the pack-water path for water, `asPacked` under Bedrock's own mode). The
  result matches having a converted pack with this fog enabled.
- Light blocks (`PbrAtlases.build`, `EmissionTiles.defaultMer`): when Bedrock glow is cast
  (`Pipeline.castsBedrockGlow`), there is no `radiante:bedrock_mer/` map in any enabled pack, and a light block's sprite
  is still vanilla's (colour from `vanilla`, specular map absent or Radiante's built-in one), a MER map is written for
  the sprite. Its green² adds up to the pack's `glow` over the sprite. Radiante's existing derived emission decides
  which texels glow and how much more than each other. That map then goes through `softenMer` and `uploadFromMer` like a
  pack's, so the block's cast light is exactly the pack's total, and its glowing texels show
  5 × green² × colour like an authored map. The glowing texels take the pack's mean metalness and roughness. Alpha 255
  means no height map, so these texels get no bevel.

## Comparison

"Old" is the radiante-pt default under Bedrock RTX (Enhanced) without a pack. "Pack" is what the renderer
gets with a converted pack that has these values. "New" is the default without a pack after this change.

### Light blocks

Units: **seen** is the glow a texel shows on screen, as a factor on its colour, at the default Emission Brightness:

- An authored texel shows 5 × G².
- An old non-authored texel shows about 1.25 × v, where v is its LabPBR emission (Radiante's built-in `_s` map, or
  the emission derived from the light level). The path is `labpbr.glsl` ×0.55 and squared, then `pt_material.glsl`
  takes the root ×14 / `VPT_BEDROCK_SEEN_EMISSION`, then the Emission Brightness factor of 0.12.

Values are given as peak / sprite mean. **Cast**, the light the block throws on its surroundings, works differently in
each case:

- With the pack and with the new default, cells are exact: the sprite mean of G² × `VPT_BEDROCK_SEEN_EMISSION`, which
  is the same as the seen mean.
- The old default cast on Radiante's light-level scale: about 4.8 × L / 15 on the glowing cells, before the shader's
  area boost of 1 to 5. For a full block of level 15 that is roughly 9 or more, against 0.1 to 0.2 with the pack's values.

| Sprite | L | Pack seen (peak / mean) | Old seen (peak / mean) | New seen mean and cast |
|---|---|---|---|---|
| torch | 14 | 0.315 / 0.0049 | 1.25 / 0.020 | 0.0049 (plus the point light, as before) |
| soul_torch | 10 | 1.53 / 0.024 | 1.25 / 0.020 | 0.024 |
| redstone_torch | 7 | 0.013 / 0.0006 | 0.71 / 0.044 | 0.0006 |
| copper_torch | 14 | 2.81 / 0.044 | 1.25 / 0.020 | 0.044 |
| lantern | 15 | 0.315 / 0.017 | 1.25 / 0.166 | 0.017 |
| soul_lantern | 10 | 0.106 / 0.0040 | 1.25 / 0.097 | 0.0040 |
| copper_lantern | 15 | 1.14 / 0.074 | derived | 0.074 |
| glowstone | 15 | 0.41 / 0.123 | 1.25 / 1.25 | 0.123 |
| sea_lantern | 15 | 0.335 / 0.161 | 1.25 / 1.25 | 0.161 |
| shroomlight | 15 | 0.315 / 0.126 | 1.25 / 1.25 | 0.126 |
| jack_o_lantern | 15 | 0.094 / 0.024 | 1.25 / 0.44 | 0.024 |
| end_rod | 14 | 0.315 / 0.036 | 1.25 / 0.185 | 0.036 |
| campfire_fire | 15 | 5.0 / 2.37 | built-in map, no emission byte | 2.37 |
| soul_campfire_fire | 10 | 0.93 / 0.45 | built-in map, no emission byte | 0.45 |
| fire_0 / fire_1 | 15 | 4.6 / 2.07 and 1.88 | 1.25 / 1.25 | 2.07 / 1.88 |
| soul_fire_0 / soul_fire_1 | 10 | 0.77 / 0.33 | 1.25 / 1.25 | 0.33 |
| ochre / verdant / pearlescent froglight (side) | 15 | 0.052 / 0.039 | 1.25 / 1.25 | 0.039 (top: 0.046) |
| redstone_lamp_on | 15 | 0.93 / 0.23 | derived | 0.23 |
| magma | 3 | 0.48 / 0.085 | 1.25 / 0.47 | 0.085 |
| beacon | 15 | 0.54 / 0.061 | derived | 0.061 |
| crying_obsidian | 10 | 0.71 / 0.074 | 1.25 / 0.32 | 0.074 |
| candle_lit (all colours) | 3 per candle | 4.56 / 0.019 | 1.25 / 0.10 | 0.019 |
| respawn_anchor_top | 15 | 0.71 / 0.088 | derived | 0.088 |
| cave_vines_lit (glow berries) | 14 | 0.069 / 0.0037 | 1.25 / 0.10 | 0.0037 × 5 (Enhanced glow berry boost, as with the pack) |
| copper_bulb_lit | 15 | 0.75 / 0.22 | 1.25 / 0.53 | 0.22 |
| nether_portal | 11 | 0.24 / 0.078 | 0.49 / 0.49 | 0.078 |
| glow_lichen | 7 | 0.019 / 0.0029 | 0.71 / 0.28 | 0.0029 |
| sea_pickle | 6 | 0.048 / 0.0019 | derived | 0.0019 |
| furnace_front_on | 13 | 0.15 / 0.020 | 1.25 / 0.175 | 0.020 |
| amethyst_cluster | 5 | 0.052 / 0.0059 | 1.25 / 0.68 | 0.0059 |
| redstone_ore | 9 (lit) | 0.001 / 0.0002 | 1.25 / 0.19 | 0.0002 |
| lava_still / lava_flow | 15 | no MER in the pack | derived, `LAVA_*` | unchanged: same as with the pack |

Notes:

- The full table, with 185 glowing sprites and 883 dark ones, is in `emission.json`.
- In the new default, how bright a single texel looks follows Radiante's derived pattern, not the pack's. Only the
  sprite total matches. The pack's own patterns can differ: for example, it lights a candle on one hot texel at
  G² 0.91.
- Under Enhanced, keeping each light block visibly lit (the `else` branch in `pt_material.glsl`, from Radiante's
  built-in `_s` map) still applies on top, as it does with the pack. So a redstone torch still visibly glows, while it
  casts what the pack gives it, next to nothing.
- Torches, lanterns, the end rod, the beacon, fire and campfires keep their `BedrockPointLights`, the same with or
  without the pack. The reference pack has no `local_lighting.json`.

### Air fog per biome

The pack values are the raw extinction per block = `max_density` × (scattering + absorption), followed by the
scattering albedo. With the pack, Radiante brings this into its own range through `BiomeAmbiance.styled`: the density
becomes 0.0026 × (1 − e^(−mean/0.011)), the tint is lifted, and the fade is at least 56 blocks. "New" is that styled
density, the same as with the pack. "Old" is Radiante's `BY_BIOME` density.

| Biome | Pack extinction r,g,b | Albedo | Full below to zero at | Old density | New density (= with pack) |
|---|---|---|---|---|---|
| plains | 0.00119, 0.00120, 0.00152 | 1, 1, 1 | 62 to 256 | 0.00055 | 0.00029 |
| meadow | 0.00077, 0.00079, 0.00110 | 0.93, 0.93, 0.95 | 92 to 320 | 0.00055 | 0.00020 |
| forest | 0.00529, 0.00530, 0.00549 | 0.13, 0.13, 0.16 | 62 to 292 | 0.0010 | 0.00100 |
| dark_forest | 0.0172, 0.0127, 0.0134 | 0.05, 0.08, 0.07 | 62 to 292 | 0.0016 | 0.00190 |
| pale_garden | 0.0483, 0.0529, 0.0578 | 0.02, 0.02, 0.03 | 62 to 288 | 0.0026 | 0.00258 |
| taiga | 0.0137, 0.0150, 0.0151 | 0.06, 0.05, 0.07 | 62 to 256 | 0.0014 | 0.00191 |
| snowy_plains | 0.00121, 0.00114, 0.00440 | 1, 1, 1 | 32 to 320 | 0.0013 | 0.00048 |
| jagged_peaks / windswept_hills | 0.00111 (grey) | 0.64 | 52 to 320 | 0.0004 / 0.0006 | 0.00025 |
| desert | 0.00199, 0.00606, 0.01062 | 1, 0.33, 0.21 | 62 to 192 | 0.0014 | 0.00112 |
| badlands | 0.00210, 0.00582, 0.00922 | 1, 0.35, 0.23 | 62 to 288 | 0.0014 | 0.00105 |
| savanna | 0.00157, 0.00372, 0.00662 | 0.75, 0.32, 0.19 | 62 to 256 | 0.0011 | 0.00079 |
| swamp | 0.0147, 0.0127, 0.0150 | 0.05, 0.09, 0.12 | 96 to 98 (152 styled) | 0.0021 | 0.00188 |
| mangrove_swamp | 0.00452, 0.00148, 0.00255 | 0.11, 0.53, 0.39 | 120 to 129 | 0.0019 | 0.00059 |
| jungle | 0.0126, 0.0106, 0.0093 | 0.10, 0.12, 0.13 | 62 to 256 | 0.0015 | 0.00163 |
| cherry_grove | 0.00726, 0.00533, 0.00728 | 0.81, 0.73, 0.82 | 62 to 256 | 0.0011 | 0.00118 |
| mushroom_fields | 0.0130, 0.0158, 0.0121 | 0.05, 0.04, 0.06 | 62 to 256 | 0.0015 | 0.00185 |
| beach | 0.00179, 0.00361, 0.00696 | 0.66, 0.33, 0.19 | 62 to 192 | 0.0008 | 0.00081 |
| ocean / deep_ocean | as plains | 1, 1, 1 | 62 to 256 | 0.0007 | 0.00029 |
| warm_ocean | 0.00227, 0.00335, 0.00465 | 0.52, 0.35, 0.29 | 62 to 256 | 0.0009 | 0.00070 |
| cold_ocean | 0.00113, 0.00127, 0.00127 | 0.71, 0.65, 0.65 | 52 to 320 | 0.0008 | 0.00027 |
| river | 0.00686, 0.00688, 0.00722 | 0.16, 0.16, 0.20 | 62 to 192 | 0.0009 | 0.00122 |

All 89 biomes the pack covers are in `fog.json`. Rain thickens the fog by the pack's `weather.max_density` /
`air.max_density` (about 1.0 to 1.4) instead of Radiante's 1.8. The Nether and End entries hold water only: the pack's
air fog applies in the overworld alone, the same as with the pack. The pack's cloud medium is 0.03 scattering and
0.03 absorption in every biome. Radiante's converter does not read it, so it has no effect with or without the pack.

### Water per biome

The pack values are extinction per block = scattering + absorption, followed by albedo. Under Bedrock's own mode,
`waterOf` uses them as written (`asPacked`), and the surface colour comes from the pack's `surface_color`. "Old"
worked the water out from the biome's vanilla water colour (`vanillaWater` ÷ the biome's clarity).

| Biome | Pack extinction | Albedo | Surface colour | Old extinction | Old albedo |
|---|---|---|---|---|---|
| plains | 0.301, 0.196, 0.103 | 0.015, 0.050, 0.178 | #0096F4 | 0.186, 0.138, 0.043 | 0.012, 0.043, 0.160 |
| ocean | 0.469, 0.341, 0.210 | 0.005, 0.037, 0.112 | #007ED3 | 0.186, 0.138, 0.043 | 0.012, 0.043, 0.160 |
| warm_ocean | 0.406, 0.241, 0.151 | 0.001, 0.080, 0.195 | #00ADE2 | 0.130, 0.040, 0.025 | 0.014, 0.140, 0.174 |
| cold_ocean | 0.103, 0.074, 0.051 | 0.029, 0.075, 0.223 | #0074C6 | 0.155, 0.136, 0.045 | 0.011, 0.023, 0.141 |
| river | 0.322, 0.246, 0.143 | 0.000, 0.006, 0.021 | #0080FF | 0.169, 0.126, 0.039 | 0.012, 0.043, 0.160 |
| swamp | 0.685, 0.481, 0.526 | 0.014, 0.020, 0.022 | #2C7A33 | 0.240, 0.206, 0.237 | 0.029, 0.047, 0.031 |
| taiga | 0.496, 0.277, 0.206 | 0.000, 0.030, 0.054 | #0084A5 | as plains | as plains |
| snowy_plains | 0.120, 0.074, 0.052 | 0.004, 0.081, 0.231 | #004999 | as plains | as plains |

The new default equals the pack in every biome the pack covers. Water is murkier and bluer than the old default,
and much murkier in swamps and oceans.

### Sun, sky and exposure

The reference pack has no files that set these, so nothing changes:

- It has no `atmospherics`, `lighting`, `color_grading` or `water` (Vibrant Visuals) JSON.
- Its `textures/environment` holds only `end_flash.tga` and `weather.tga`.

Sun intensity, sky, exposure and tone mapping were already the same with and without the pack (radiante-pt's
defaults, listed in `BedrockRtxValues`). The difference in brightness and colour the player
sees comes from fog, water and light block glow, and above all from how much light the blocks cast. By the estimate
above, the old level-based scale was one to two orders of magnitude higher, and auto-exposure adapted to it. This is
an estimate from the code, not a measurement.

## What cannot match without the pack

- **Textures:** colours, normal and height maps (the bevelled pixel edges), and the per-pixel metalness, roughness
  and subsurface of every block that is not a light block. Under Bedrock Pack Materials, these come only from the
  pack's maps.
- **Glow pattern:** where a light block glows within its texture follows vanilla's texture. Only the total is the
  pack's.
- **Colour of cast light:** it is the mean colour of vanilla's glowing texels, not of the pack's re-coloured textures.
- **Non-light blocks the pack makes glow faintly** (sculk, trial spawners, vaults, crafters, rails, command blocks,
  stems): Radiante finds no glowing texels in them, so they stay dark. The amounts involved are tiny (G² below 0.002).
- **Dark sprites** (a light block the pack gives no glow): they cast nothing, as with the pack, but still show
  Radiante's surface glow instead of the authored zero.
- **Bedrock glow off:** with Bedrock Pack Materials or Bedrock RTX's mode turned off, the light block glow table is not
  used; the fog and water table still is.

## Not checked in game

This change was built and the unit tests pass, but it has not been compared in game. Compare the same views in a
`Radiante*` test world, with SHaRC on, in three states: a converted pack enabled, no pack before this change, and no pack
after it.
