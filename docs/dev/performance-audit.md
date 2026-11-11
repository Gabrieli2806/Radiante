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
| Light bounces 4 to 2 (one bounce traced) | 23.0 (-56 %) | 11.2 (-52 %) | 9.7 (-44 %) |
| Light bounces 4 to 1 (no bounce, direct light only) | 10.2 (-81 %) | | |
| Block Light Sampling off | 26.5 (-50 %) | 12.2 (-47 %) | 9.4 (-45 %) |
| One Bounce + Cache | 32.8 (-38 %) | 18.0 (-22 %) | 11.2 (-35 %) |
| Checkerboard for water and glass | 45.1 (-14 %) | 20.4 (-12 %) | 16.6 (-3 %) |
| First Person Shadow off | 47.0 (-10 %) | 23.2 (0 %) | 16.9 (-1 %) |
| Clouds off (vanilla clouds) | 49.8 (-5 %) | 22.0 (-5 %) | 15.9 (-8 %) |
| Cache Deep Bounces off | 49.5 (-6 %) | 23.8 (+3 %) | 17.3 (+1 %) |
| Radiance cache (SHaRC) off | | 25.4 (+9 %) | 17.8 (+3 %) |
| Volumetric fog off | 52.5 (0 %) | | |
| Mirror bounces 8 to 0, hand samples 3 to 1, parallax off, ReSTIR on | within 1 % | | |

## Findings, by what they could save

1. **The radiance cache rarely gets to answer outdoors, and that is how it is built.** With Cache Deep Bounces a
   path may take the cached light from its third surface on (the second bounce's hit), never at the second, so the
   default always traces two bounces and can only save the third and fourth. Its heat map (`sharc_debug_mode`
   `heatmap`) shows it answering for about 4 % of the pixels in the flowers view and 20 % in the dock view: outdoors
   most first bounces leave for the sky and end there, and the paths that go on are the ones among leaves and
   flowers, the dearest to trace. Turning it off changes the time by a few percent either way. Asking it one surface
   earlier is exactly what One Bounce + Cache does (finding 2): the saving is there, behind that setting.

2. **One Bounce + Cache is as it should be.** Light Bounces counts the surfaces of a path, so "2" traces one
   bounce: that is what One Bounce + Cache has to be held against, not "more than two bounces". It costs 16 % more
   than that one plain bounce in the flowers view (the cache lookups) and gives the light of every later bounce for
   it, at 35 % under the default. In the dock view it costs 60 % more than one plain bounce, on purpose: what is seen
   through water (the bed of the pond) gets a bounce of its own as a first hit does, without which pools came out
   lit by the sun and nothing else. It is the cheapest setting that still fills rooms and shade with bounced light.

3. **Block Light Sampling is half the frame, by day, outdoors.** One lantern and a few candles in view, and sampling
   them costs 45 to 50 % of the world pass. The sampler takes one shadow ray a surface (it picks one light among its
   candidates), but on every bounce of every path. What to try, cheapest first: no block light ray where no light is
   in range of the surface's section (the lists are per section already); the ray only on the first two bounces by
   day, when the sun and sky outweigh a lamp by orders of magnitude; fewer of its shadow rays through foliage, whose
   cut-out leaves run the any-hit shader (`shadow.rahit`) on the way. Each changes the picture a little and needs a
   look at a lamp-lit room at night, where an earlier attempt to skip deeper bounces left walls a fifth darker.

4. **Shader Execution Reordering is gone.** It took a quarter off where there was no water (flowers view) and
   broke every pixel whose first surface was water: a flat pale sheet, nothing seen through or mirrored. Without the
   one call that regroups the threads the water was right and nothing was saved, so the fault is what the driver does
   with this ray generation shader's state across that call. Keeping pixels that start on water or glass out of it
   hung the GPU when the setting was turned on (no answer for five seconds, and the game stopped), twice in two
   tries. A setting that saves a quarter on some cards and stops the game is not worth having: the code, the setting
   and the device extension are removed. Worth another look when the EXT version of the extension is what drivers
   ship.

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

With One Bounce + Cache on and block light sampling cut to what a daylit scene needs (findings 2 and 3), a frame
traced at 720p would go from 23 to 30 ms to somewhere near 10 to 13 ms on this card: 4K through DLSS Performance
(traced at 1080p) at about 30 to 40 frames a second, through Ultra Performance (720p) at 70 to 90. That is the reachable version of the 90 frames a second target, and it is an estimate: the savings
overlap (fewer bounces leave fewer block light rays to save), and each has to be measured again once the one before
it is in.

## Besides

- A setting that rebuilds the pipeline reloads the whole shader pack first (0.4 of the 0.9 s of a rebuild whose
  shaders are cached). The pack could be kept when only values changed.
- The first build of a combination of settings takes about 13 s (37 shaders to SPIR-V, then the ray tracing
  pipeline); the title screen's warm-up could build the combinations the quality levels use.
