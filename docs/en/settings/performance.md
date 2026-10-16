[← Settings](../settings.md) · [Index](../README.md)

# Performance (settings category)

> This page is the reference for the controls that live in the "Performance" category of the
> settings screen. If you are after general tips for gaining FPS (which preset to pick, how your
> GPU compares, Distant Horizons' cost), that is a different page:
> **[performance.md](../performance.md)**, at the root of the documentation.

![Radiante settings: Performance](../../images/ui/settings-performance.png)

| Control | Range | Default | What it does |
|---|---|---|---|
| **Light Bounces** | 1 – 128 | 4 | How many times light bounces. 4 to 2 is about +50% FPS. Above 4 costs a lot and is for strong GPUs; mirrors and clear glass get extra bounces on their own. |
| **Mirror Reflections** | 0 – 64 | 8 | How many reflections inside reflections are traced on top of Light Bounces: mirrors facing each other, glass behind glass, water seen in a mirror. Only surfaces that are almost perfectly smooth count, so it needs a PBR resource pack; water and glass always count. Costs frame rate only where mirrors face each other. |
| **Mirror Smoothness Limit** | 0% – 200% | 30% | Metals at least this smooth are drawn as clean, sharp mirrors. 0% turns this off. |
| **Glass Reflections** | 0% – 200% | 100% | How much of the sky windows show on top of what is behind them. 0% is perfectly clear glass. |
| **Cache Deep Bounces** | On / Off | On | After the first bounce, light comes from the radiance cache instead of more tracing, and bounced surfaces skip block light lookups, as in Bedrock RTX. Much faster with many lights; bounced light may look slightly softer. |
| **One Bounce + Cache** | On / Off | Off | Like Bedrock RTX: only one bounce of light is traced everywhere, and the rest comes from the radiance cache where it lands. The biggest gain; indirect light in uncached spots is a little darker for a moment. |
| **Glass/Water Checkerboard** | On / Off | Off | Water and glass trace either their reflection or their refraction per pixel, alternating in a checkerboard, instead of both: one ray per pixel instead of two, as Bedrock RTX does. |
| **Carved Surfaces** (parallax) | On/Off | On | Depth on resource pack textures that have height maps — bricks and stones stand out. Off: flat textures and somewhat faster. Without a pack that has height maps, this control changes nothing visually. |
| **Bedrock Pack Materials** | On / Off | On | Bedrock RTX packs (`.mcpack`): metal, roughness and what glows are read straight from the pack's MER maps at full detail, as Bedrock does, instead of from the converted copy (ore specks and metal edges keep their reflections). No effect on Java packs. |
| **Volumetric Fog Quality** | 4 – 32 samples | 16 | Samples per ray for the volumetric fog (see [Sky, Fog & Water](sky-fog-water.md)). Lower is faster and a little noisier; 32 costs about 30% more than 16. Only shows up if your pipeline supports volumetric fog. |
| **Chunk Building** | Automatic · Manual | Automatic | Automatic sizes chunk building to your CPU so it does not starve the game while chunks load. Manual shows the three sliders below. |
| **Chunk Threads** | 1 – half your logical cores (max) | Half your logical cores | CPU threads that turn loaded chunks into ray tracing geometry. More loads new areas faster when you fly or teleport, but competes with the game (and, in singleplayer, with the integrated server generating those same chunks) for CPU. Lower it if the game stutters while loading terrain; raise it if chunks visibly take a while to pop in. |
| **Chunk Batch Size** | 1 – 64 | 12 | How many chunk sections are sent to the GPU in one acceleration structure build. Higher fills the world in faster but can cause frame time spikes while loading; lower is smoother but slower to catch up. |
| **Chunk Batches** | 1 – 64 | 12 | How many of those builds may be in flight on the GPU at once. Higher loads faster on strong GPUs and uses more video memory; lower it if you see hitches or run out of VRAM. |
| **Block Emission** | On/Off | On | Blocks that give off light (torches, lava, glowstone...) light up the world around them. Off saves some performance, but light sources then only glow themselves — needed for "Block Light Sampling" (in [Lighting](lighting.md)) to have anything to sample. |

## Why the Chunk Threads maximum is half your cores

It is not an arbitrary number: on a 24-thread CPU, measuring with 20 building threads dropped the
frame rate from 120 to about 65 for several seconds after every teleport, while 4 threads held the
game above 110 FPS and loaded the area just as fast. More threads than your CPU can spare without
taking them from rendering just makes the game run worse, not load sooner.
