# Performance audit, November 2026

Where a frame's time goes, measured with `dev/smoke/bench.py` on an RTX 5070 at 1920x1080 in the `RadianteBenchmark`
test world (a pond, a dock with a lantern, a flower forest), Radiante PT at its defaults. Times are the renderer's own
GPU times per pass; the lowest of two turns counts. The bench files and the pictures of every variant are in
`build/bench/audit-a`, `audit-b` and `audit-c` of the machine that ran them.

The question behind it: how far is Radiante from tracing every pixel of a 4K picture at 90 frames a second, the rate
quoted for Bedrock RTX?

## The cost grows with the pixels traced

| Traced at | Pixels | Frame (dock) | World pass (dock) | Frame (flowers) | World pass (flowers) |
|---|---|---|---|---|---|
| 640x360 (DLSS Ultra Performance) | 0.23 M | 10.2 ms | 7.5 ms | 7.9 ms | 5.3 ms |
| 960x540 (Performance) | 0.52 M | 18.3 ms | 14.9 ms | 13.8 ms | 10.5 ms |
| 1280x720 (Quality) | 0.92 M | 30.5 ms | 25.6 ms | 22.9 ms | 18.3 ms |
| 1920x1080 (DLAA) | 2.07 M | 60.5 ms | 53.6 ms | 45.4 ms | 38.7 ms |

The world pass (the path tracer) is 85 to 90 % of the frame at every size and costs 19 to 26 ns a pixel; everything
else (the cache, the fog grid, DLSS, tone mapping) is under 3 ms together. Nothing but the path tracer is worth
optimising.

Every pixel of a 4K picture is 8.3 M pixels: about 160 to 215 ms a frame on this card, 5 to 6 frames a second. 90
frames a second there is 11 ms, some twenty times less. No tuning closes that: it needs fewer pixels traced (an
upscaler), fewer rays a pixel, or both. Bedrock RTX does both: it traces below the output size and upscales, traces
one bounce and takes the rest from an irradiance cache. The honest target is the same picture for the same rays: 4K
through an upscaler from 720p to 1080p, with the path cut to what Bedrock traces.

## What each part costs

World pass, in milliseconds, against the default.

| Change | 1080p traced, dock | 720p traced, dock | 720p traced, flowers |
|---|---|---|---|
| Default | 52.5 | 23.2 | 17.2 |
| Light bounces 4 to 2 | 23.0 (-56 %) | 11.2 (-52 %) | 9.7 (-44 %) |
| Light bounces 4 to 1 | 10.2 (-81 %) | | |
| Block Light Sampling off | 26.5 (-50 %) | 12.2 (-47 %) | 9.4 (-45 %) |
| Shader Execution Reordering on | 30.4 (-42 %) | 13.3 (-42 %) | 13.0 (-24 %) |
| One Bounce + Cache | 32.8 (-38 %) | 18.0 (-22 %) | 11.2 (-35 %) |
| Checkerboard for water and glass | 45.1 (-14 %) | 20.4 (-12 %) | 16.6 (-3 %) |
| First Person Shadow off | 47.0 (-10 %) | 23.2 (0 %) | 16.9 (-1 %) |
| Clouds off (vanilla clouds) | 49.8 (-5 %) | 22.0 (-5 %) | 15.9 (-8 %) |
| Cache Deep Bounces off | 49.5 (-6 %) | 23.8 (+3 %) | 17.3 (+1 %) |
| Radiance cache (SHaRC) off | | 25.4 (+9 %) | 17.8 (+3 %) |
| Volumetric fog off | 52.5 (0 %) | | |
| Mirror bounces 8 to 0, hand samples 3 to 1, parallax off, ReSTIR on | within 1 % | | |

## Findings, by what they could save

1. **The radiance cache does not shorten paths outdoors.** Cache Deep Bounces is on, and its point is that a path
   ends at the second hit when the cache has an answer. Here turning it off, or the whole cache off, changes the time
   by a few percent, while tracing two bounces instead of four halves it. So in open terrain nearly every path runs
   all four bounces: the cache is paid for (its update paths, its resolve) and gives almost nothing back. Indoors
   (the showcase rooms) it did answer, which is why four bounces cost no more than two there. To look at:
   `sharcAllowQuery` in `pt_bounce.glsl` (the least distance and roughness it asks for), how far from the camera the
   cache holds entries, and how many queries miss (the `sharc` debug view shows them). Worth up to 45 % of the frame.

2. **One Bounce + Cache costs more than two bounces without it.** It is the mode that traces what Bedrock RTX traces,
   and should be the cheapest of all; it saves 22 to 38 % where plain two bounces save 44 to 56 %. Either its budget
   does not hold for every kind of path (the dock view is half water, whose reflection and refraction both go on), or
   the cache misses of finding 1 fall back to full paths. Same place to look.

3. **Block Light Sampling is half the frame, by day, outdoors.** One lantern and a few candles in view, and sampling
   them costs 45 to 50 % of the world pass. The sampler takes one shadow ray a surface (it picks one light among its
   candidates), but on every bounce of every path. What to try, cheapest first: no block light ray where no light is
   in range of the surface's section (the lists are per section already); the ray only on the first two bounces by
   day, when the sun and sky outweigh a lamp by orders of magnitude; fewer of its shadow rays through foliage, whose
   cut-out leaves run the any-hit shader (`shadow.rahit`) on the way. Each changes the picture a little and needs a
   look at a lamp-lit room at night, where an earlier attempt to skip deeper bounces left walls a fifth darker.

4. **Shader Execution Reordering saves a quarter, and is off because it breaks water.** In the flowers view (no
   water) it takes 24 % off with the same picture. In the dock view the water comes out a flat pale blue with nothing
   seen through it or mirrored in it (`build/bench/audit-b/dock__ser=true.png`), and part of the 42 % saved there is
   that missing work. The setting's own description says so ("water can look white"). The primary rays are not the
   reordered ones, and the water they hit is wrong all the same, so the fault is in what the reordering variant of the
   ray generation shader compiles to, not in the reordered trace itself. Fixing it gives every RTX 40 and 50 card a
   quarter of the frame, and it could then be on by default there.

5. **The player's own body costs 10 % at 1080p traced.** First Person Shadow draws the player so they cast a shadow;
   every ray leaving the camera starts inside that model's bounds. At 720p the cost is gone, so it is worth a look
   only for those who trace at the window's size: whether the model's geometry is marked opaque, and whether camera
   rays can skip it by mask as shadow rays choose to.

6. **Water and glass trace two paths a pixel.** Checkerboard (one of the two a pixel, alternating) saves 12 to 14 %
   in a view that is half water. It is off by default because DLSS's guides do not describe a lobe that is missing
   on alternate pixels. Left as it is.

7. **Nothing to gain** from the fog (the grid costs 0.2 ms), mirror bounces, hand samples, parallax or ReSTIR in
   these views.

## What it adds up to

If findings 1 to 4 were all fixed, a frame traced at 720p would go from 23 to 30 ms to somewhere near 8 to 10 ms on
this card: 4K through DLSS Performance (traced at 1080p) at about 40 to 50 frames a second, through Ultra Performance
(720p) near 90. That is the reachable version of the 90 frames a second target, and it is an estimate: the savings
overlap (fewer bounces leave fewer block light rays to save), and each has to be measured again once the one before
it is in.

## Besides

- A setting that rebuilds the pipeline reloads the whole shader pack first (0.4 of the 0.9 s of a rebuild whose
  shaders are cached). The pack could be kept when only values changed.
- The first build of a combination of settings takes about 13 s (37 shaders to SPIR-V, then the ray tracing
  pipeline); the title screen's warm-up could build the combinations the quality levels use.
