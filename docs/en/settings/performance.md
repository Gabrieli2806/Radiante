[← Settings](../settings.md) · [Index](../README.md)

# Performance (settings section)

> This page is the reference for the controls that live in the "Performance" section of the
> settings screen. If you are after general tips for gaining FPS (which quality level to pick, how
> your GPU compares, Distant Horizons' cost), that is a different page:
> **[performance.md](../performance.md)**, at the root of the documentation.

![Radiante settings: Performance](../../images/ui/settings-performance.png)

Defaults are radiante-pt's, the Bedrock RTX look ([bedrock-rtx.md](../bedrock-rtx.md)).

| Control | Range | Default | What it does |
|---|---|---|---|
| **Light Bounces** | 1 – 128 | 4 | How many times light bounces. 4 to 2 is about +50% FPS. Above 4 costs a lot and is for strong GPUs; mirrors and clear glass get extra bounces on their own. Grey while One Bounce + Cache is on. |
| **Mirror Reflections** | 0 – 64 | 8 | How many reflections inside reflections are traced on top of Light Bounces: mirrors facing each other, glass behind glass, water seen in a mirror. Only surfaces that are almost perfectly smooth count, so it needs a PBR resource pack; water and glass always count. Costs frame rate only where mirrors face each other. |
| **Mirror Smoothness Limit** | 0% – 200% | 30% | Metals at least this smooth are drawn as clean, sharp mirrors. 0% turns this off. |
| **Glass Reflections** | 0% – 100% | 100% | How much of the sky windows show on top of what is behind them. 0% is perfectly clear glass; 100% is Bedrock's full reflection. |
| **Water Texture** | 0% – 100% | 10% | How much of the resource pack's water texture shows over the surface, as Bedrock RTX draws it. 0% is clear, see-through water without the pattern. |
| **Clear Teal Water** | 0% – 100% | 0% | How far the water of every biome is moved towards clear teal water: red is absorbed within a few blocks while green and blue carry on for dozens, so you see the bottom and the light shafts far off. 0% is the pack's own water. |
| **Portal Transparency** | 0% – 100% | 50% | How much of the pack's see-through share a Nether portal keeps. 100% is as the resource pack draws it, 0% is opaque. |
| **Rain Opacity** | 0% – 100% | 0% | How much of each rain streak shows. Low is thin, nearly clear falling water that catches the light; 100% is the pack's rain texture as an opaque sheet. |
| **Rain Streaks** | 0% – 200% | 100% | How much rain streaks catch the light of the overcast sky over them. 0% leaves the rain invisible. |
| **Cache Deep Bounces** | On / Off | On | After the first bounce, light comes from the radiance cache instead of more tracing, and bounced surfaces skip block light lookups, as in Bedrock RTX. Much faster with many lights; bounced light may look slightly softer. |
| **One Bounce + Cache** | On / Off | Off | Like Bedrock RTX: only one bounce of light is traced everywhere, and the rest comes from the radiance cache where it lands. The biggest gain; indirect light in uncached spots is a little darker for a moment. |
| **Glass/Water Checkerboard** | On / Off | Off | Water and glass trace either their reflection or their refraction per pixel, alternating in a checkerboard, instead of both: one ray per pixel instead of two, as Bedrock RTX does. Off by default, as Bedrock's full-resolution picture needs both. |
| **Carved Surfaces** (parallax) | On / Off | On | Depth on resource pack textures that have height maps — bricks and stones stand out. Off: flat textures and somewhat faster. Without a pack that has height maps, this control changes nothing visually. |
| **Bedrock Pack Materials** | On / Off | On | Bedrock RTX packs (`.mcpack`): metal, roughness and what glows are read straight from the pack's MER maps at full detail, as Bedrock does, instead of from the converted copy (ore specks and metal edges keep their reflections). No effect on Java packs. |
| **Volumetric Fog Quality** | 4 – 32 samples | 16 | Samples per ray for the Radiante fog style (see [Sky & Water](sky-fog-water.md)). Lower is faster and a little noisier; 32 costs about 30% more than 16. Grey with the Froxel style or Bedrock's fog volume, which do not use it. |
| **Chunk Building** | Automatic · Manual | Automatic | Automatic sizes chunk building to your CPU (1 to 6 threads, 4 to 12 sections per batch and batches) so it does not starve the game while chunks load. Manual shows the three sliders below. |
| **Chunk Threads** | 1 – half your logical cores | What Automatic picks for your CPU | CPU threads that turn loaded chunks into ray tracing geometry. More loads new areas faster when you fly or teleport, but competes with the game (and, in singleplayer, with the integrated server generating those same chunks) for CPU. Lower it if the game stutters while loading terrain; raise it if chunks visibly take a while to pop in. |
| **Chunk Batch Size** | 1 – 64 | What Automatic picks for your CPU | How many chunk sections are sent to the GPU in one acceleration structure build. Higher fills the world in faster but can cause frame time spikes while loading; lower is smoother but slower to catch up. |
| **Chunk Batches** | 1 – 64 | What Automatic picks for your CPU | How many of those builds may be in flight on the GPU at once. Higher loads faster on strong GPUs and uses more video memory; lower it if you see hitches or run out of VRAM. |
| **Block Emission** | On / Off | On | Blocks that give off light (torches, lava, glowstone...) light up the world around them. Off saves some performance, but light sources then only glow themselves — Block Light Sampling and ReSTIR (in [Lighting](lighting.md)) need it. |

## Why the Chunk Threads maximum is half your cores

It is not an arbitrary number: on a 24-thread CPU, measuring with 20 building threads dropped the
frame rate from 120 to about 65 for several seconds after every teleport, while 4 threads held the
game above 110 FPS and loaded the area just as fast. More threads than your CPU can spare without
taking them from rendering just makes the game run worse, not load sooner.
