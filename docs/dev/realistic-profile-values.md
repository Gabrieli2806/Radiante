# Realistic profile values

The Realistic renderer profile was removed together with the profile selector. This page records what it set, so
that it can be rebuilt as a shader pack. Realistic was the Bedrock RTX (Enhanced) profile, which is now radiante-pt's
plain default, with the rows below set the other way. It set no Java-side flag or mode of its own: the Bedrock
original/enhanced switches, the Bedrock materials, atmosphere, fog and the no-pack Bedrock fog/water/glow fallback
(`BedrockDefaults`) were the same as under Enhanced, except that `BedrockDefaults` was only active under Enhanced, so
Realistic used Radiante's own built-in fog, water and light block glow values when no Bedrock pack was enabled.

`RT` is `render_pipeline.module.ray_tracing.attribute.` (radiante-pt `configs.json`); `TONE` is
`render_pipeline.module.tone_mapping.attribute.` (`modules/tone_mapping.yaml`).

| Attribute | Realistic | Enhanced (now the default) | Why |
|---|---|---|---|
| `RT bounce_light_boost` | 0.8 | 1.5 | bounced light below the default's lift, which fills every shadow like a second light |
| `RT sky_light_boost` | 1.0 | 1.8 | the sky's light at its own strength; the default adds an even ambient that flattens shade |
| `RT exposure_day_scale` | 0.42 | 0.48 | a touch less daylight brightness, so skies and snow keep detail |
| `RT light_shafts` | 0.008 | 0.015 | half the light shafts: haze glowing everywhere reads as a soft-focus filter |
| `RT sun_glow` | 0.04 | 0.08 | half the glow around the sun, which blooms into a milky halo |
| `RT mirror_max_roughness` | 0.02 | 0.03 | only truly polished metal is a clean mirror |
| `RT water_surface_mode` | `RT water_surface_mode.realistic` | `RT water_surface_mode.bedrock` | finer, irregular waves instead of Bedrock's magnified water texture |
| `RT water_wave_strength` | 0.5 | 0.35 | waves strong enough to break the mirror into ripples |
| `RT water_clear_teal` | 0.7 | 0.0 | water absorbs red first and stays clear in green and blue (teal, bottom visible) |
| `TONE contrast` | 1.08 | 1.0 | more separation between light and dark about middle grey |
| `TONE saturation` | 0.92 | 1.0 | colour a little below the textures' own |
| `TONE bloom_intensity` | 0.004 | 0.01 | bloom almost off |
| `TONE exposure_up_speed` | 4.0 | 8.0 | opening up in the dark takes a moment, as an eye does |
| `TONE exposure_down_speed` | 6.0 | 8.0 | closing in the light is quicker than opening in the dark |

These values are now the `realistic` example shader pack
([examples/shaderpacks/realistic](../../examples/shaderpacks/realistic)): the `RT` rows as new defaults for
radiante-pt's attributes, `contrast` and `saturation` as two live settings applied in `hooks/post_grade.glsl` the way
the Bedrock tone mapper applies its own. A pack cannot set tone mapping's settings, so `bloom_intensity` and the two
exposure speeds are not carried (that would need packs to give module settings defaults too), and radiante-pt has no
setting to leave out `BedrockDefaults`, so the pack keeps the built-in Bedrock fog, water and glow.
