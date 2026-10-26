[← Back to index](en/README.md)

# How PrismRT works

For contributors who want to change the renderer. Playing needs none of this.

PrismRT is Radiante's native renderer: a C++ library (`prismrt.dll` on Windows, `libprismrt.so` on Linux) that
traces the Minecraft world with Vulkan ray tracing and hands the finished picture back to the game. It does not
create a Vulkan device of its own: it works on Minecraft's instance, device and queues, and its command buffers run
inside Minecraft's own submits.

Everything here is taken from the code. When this page and the code disagree, the code wins; the comments at the
top of each file are the most precise description of what it does.

## The big picture

```
 Java (common/src/main/java/com/g2806/radiante)          native (native/src/prism)
 ─────────────────────────────────────────────          ─────────────────────────
 mixins: Vulkan instance/device creation   ──createMerged──▶  gpu/instance, gpu/device
 ChunkProxy / EntityProxy / PlayerProxy    ──geometry───▶  render/chunks, entities, world
 TextureProxy / BufferProxy                ──uploads────▶  render/textures, buffers
 Pipeline (pipeline.yaml, presets)         ──buildNative▶  render/pipeline (WorldPipeline)
 Pipeline.setRuntimeAttribute              ──live value─▶  module / shader pack / packParams
 RadianteRenderer → RendererProxy.renderFrame ─────────▶  render/render_framework (Framework)
            ◀── VkCommandBuffer[] (upload, world, fuse) ──
 Minecraft submits them, then RendererProxy.markSubmitted(timeline, value)
```

The Java side decides *what* to draw: it meshes sections into the renderer's PBR vertex format, collects entities,
stores the settings and describes the world pipeline (which modules, connected by which images). The native side
decides *how*: it owns the acceleration structures, the shaders, the modules and the frame slots.

### The JNI boundary

The entry points live in `native/src/prism/middleware/`, one file per Java class, named after it
(`com_g2806_radiante_client_pipeline_Pipeline.cpp` implements `Pipeline`'s `native` methods, and so on). Shared
rules are in `middleware/jni_support.hpp`:

- **Pointers and handles travel as `long`.** `jni::as<T*>(jlong)` and `jni::toLong(ptr)` convert both ways. A
  `VkImage`, a native struct Java laid out in off-heap memory (`Pipeline.buildNative(long params)`), a section
  slot index: all are longs.
- **No C++ exception may reach the JVM.** Every entry point runs its body through `jni::guard(subject, fallback,
  work)`, which logs the failure under `subject` and returns `fallback` instead.
- **Errors come back as text.** `RendererProxy.drainNativeErrors` / `drainNativeInfo` let Java pull what the native
  log collected and show it in game.

The library is loaded by `RadianteClient` with `System.load`, after the optional XeSS runtime.

### Living inside Minecraft's device

Instance and device creation are redirected into the native `createMerged` entry points, which add the ray
tracing, descriptor indexing and upscaler extensions to what Minecraft asked for and merge the feature chains.
Access to the graphics queue is serialised between Minecraft and the renderer (`gpu/queue_lock.*`, exposed to Java
as `RendererProxy.lockQueue` / `unlockQueue`).

## Frame lifecycle

The renderer's frame loop runs *inside* Minecraft's. `Framework` (`render/render_framework.*`) owns a ring of
**frame slots** (`FrameworkContext`). Each slot holds three command buffers, executed in this order:

| Command buffer | What it records |
| --- | --- |
| `uploadCommandBuffer` | texture and buffer uploads, BLAS builds and compactions |
| `worldCommandBuffer` | the world pipeline: TLAS build, ray tracing, denoise, upscale, tone map, post |
| `fuseCommandBuffer` | the blit of the traced picture into Minecraft's target, frame generation capture, the traced depth written into Minecraft's depth texture |

One frame, step by step:

```
 acquireContext()
   ├─ pick next slot, waitForPreviousSubmit()        (Minecraft's timeline semaphore)
   ├─ gpu::retire::openSlot(slot)                     destroy what this slot retired last time round
   ├─ begin command buffers
   └─ resetFrame() on buffers, textures, world, chunks, entities
 renderFrame(target, w, h, traceW, traceH, format)
   ├─ size or settings changed?  → swapchain reconstruct + recreate()
   ├─ performQueuedUpload() for textures and buffers
   ├─ WorldPipelineContext::render()                  every module records its work
   ├─ recordAfterWorld(): frame generation capture, fuseInto(target), McDepth write, HDR note
   └─ end command buffers → return [upload, world, fuse]
 Minecraft executes them in its own submit
 markSubmitted(timeline, value)                       the slot remembers which submit holds it
```

`renderFrame` takes two sizes: the target's (`width x height`) and the trace size (`traceWidth x traceHeight`).
They differ for the extra views some mods draw between the main ones (Vivecraft's handheld camera, telescopes);
keeping them apart avoids rebuilding the pipeline every time the views alternate.

The same split carries the **output resolution** setting (`OutputResolution`, `Options.outputResolution`): Java hands
over a trace size below the window's (1080p in a 4K window, say), so the whole world pipeline (tracing, denoising,
upscaling, post) works at that size, and a size change rebuilds it as a window resize does. In `recordAfterWorld`, a
picture smaller than the target is brought to the target's size by `OutputUpscaler` (`render/output_upscaler.*`): FSR 1
EASU then RCAS, compiled from the FidelityFX SDK's own headers (`shader/display/fsr1_easu.comp`, `fsr1_rcas.comp`),
then copied into Minecraft's target, which draws its HUD over it at full resolution. Frame generation and HDR output
take that upscaled picture as the world fused into Minecraft's image; the traced depth for Minecraft's overlays is
point sampled to the target's size by `mc_depth.comp` as before. At the native size none of this runs. F3 shows the
pass as `output_fsr1` and the upscaler line names both sizes.

Every frame and every rebuild holds `Framework::recreateMtx()`. Anything that changes what a frame reads (a live
setting, a rebuild) takes the same lock, so changes always fall between two frames.

### Rebuilds

`Framework::recreate()` rebuilds the frame slots' work and the world pipeline for the current size and blueprint.
Before it starts it:

1. cancels and joins any background shader prewarm (it holds the old pipeline's objects);
2. submits what the current frame already recorded (uploads, structure builds) on its own, so no chunk is left
   with an acceleration structure that was never built;
3. waits for the queue, then rebuilds; the driver's pipeline cache is saved when the rebuild ends.

A failed rebuild sets `lastRebuildFailed()`; frames then draw no world until a later rebuild succeeds.
`recreatePending()` lets Java run a pending rebuild from a worker thread while the render thread keeps the window
alive (`ApplyingSettingsScreen`, only used inside a world when the change cannot be prewarmed; outside a
world the build just hands the blueprint over and the compile happens with the first frame, see `BackgroundRebuild`).

## The GPU layer (`native/src/prism/gpu`)

A thin layer over Vulkan: buffers and images allocated through VMA, command buffers, descriptor tables, pipelines,
shader binding tables and acceleration structures. Most classes derive from `SharedObject<T>` and are held by
`std::shared_ptr`.

### Resource lifetime: `gpu::retire`

Frames are recorded while earlier ones still run, so an object dropped while recording frame N may still be in use
by the frames before it. Instead of each owner keeping old objects alive by hand, destruction goes through
`gpu/retire.*`:

- `retire::later(fn)` queues `fn` in the list of the slot that is open now;
- `retire::openSlot(slot)` runs that slot's list when the slot comes round again, after `acquireContext` has waited
  for the GPU work it held;
- `retire::drain()` runs everything, only once the device is idle (rebuilds that wait for it, shutdown);
- `retire::configure(n)` is called when the number of slots changes; what was queued becomes orphans run at the
  next drain.

It is thread safe, and the closures run outside its lock, because a destructor may retire more objects.
`FrameResourceRetainer::retain(ptr)` is the shorthand for "keep this `shared_ptr` alive until no frame can use it".

### Tracked image state and `CommandBuffer::sync`

Each `gpu::Image` carries an `ImageState`: the layout, pipeline stages and accesses of its last use. A pass does not
write barriers by hand; it declares how it is about to use its images:

```cpp
commandBuffer->sync({
    {radiance, gpu::ImageUsage::StorageWrite, 0, /*discard=*/true},
    {normalRoughness, gpu::ImageUsage::Sampled},
});
```

`sync` emits one barrier for all of them, each waiting only on what that image was last used for. Reads after reads
in the same layout get no barrier at all. `discard` lets the transition drop contents about to be overwritten.
`ImageUsage::General` is for images handed to code that is not ours (NRD, DLSS, FSR, XeSS). Code that transitions
an image itself writes the new layout through `Image::imageLayout()`, which also marks the state as "all earlier
work", so the next tracked use waits on everything. Buffers and acceleration structures use
`CommandBuffer::memoryBarrier`.

### Descriptors

`DescriptorTableBuilder` (`gpu/descriptor.*`) declares sets and bindings and builds a `DescriptorTable` (sets,
layouts, pipeline layout and push constant ranges). Every set without a dynamic buffer is created with
`UPDATE_AFTER_BIND`, `UPDATE_UNUSED_WHILE_PENDING` and `PARTIALLY_BOUND`: it can be rewritten while a frame that
uses it is in flight, and bindings a pipeline never reads may stay unwritten. That is what lets textures and
per-frame resources be rebound without stalling.

## Acceleration structures

All structure code is in `gpu/as.*`, used by `render/chunks.*`, `render/entities.*` and the ray tracing module's
`submodules/world_prepare.*`.

### BLAS per section

Every loaded section of the world is a slot in the section grid (`Chunks`). Java hands over a section's geometry
with `ChunkProxy.rebuildSingle`; the calling thread copies it, builds its lights and packs indices, positions and
materials into **one buffer per section**, so rebuilding a section frees exactly its own memory.

- **Near-player rebuilds** (`important`) are built straight away with `PREFER_FAST_BUILD` and swapped in by the next
  frame.
- **The rest** wait in a queue and are built in batches alongside the frames. A batch's uploads, structure builds
  and compacted-size queries go to the GPU in one submission with its own fence (on the secondary queue when it
  shares the main queue's family); its sections are swapped in once that fence signals. Batch size follows a small
  per-frame time budget measured on the render thread.
- Batched sections are built with `PREFER_FAST_BUILD | ALLOW_COMPACTION`. Once the compacted size is known,
  `Chunks::compactFinishedBlases` copies each one into its own, smaller structure, ahead of the TLAS build. A compaction is skipped
  if the slot was rebuilt or dropped meanwhile.
- Builds are versioned: a slot only swaps in a build newer than the one it shows, so a slow build never replaces a
  later one.

### BLAS per entity

Entities (`render/entities.*`) are packed per frame into shared index/position/material buffers and get their
structures through a `gpu::BlasBatch` with `PREFER_FAST_TRACE`. Entities with a prebuilt structure skip the build.

### TLAS

`gpu::TlasBuilder` is one per frame slot. Each frame it is cleared, receives one instance per section and entity
(`add(transform, customIndex, mask, sbtOffset, flags, blas)`), writes the instances straight into mapped memory and
records a build with `PREFER_FAST_TRACE`. Its instance, storage and scratch buffers only grow, so a normal frame
allocates nothing; the returned structure is the same object until it has to grow.

**Update vs rebuild:** the TLAS is rebuilt every frame (no refit). A changed section or entity gets a new BLAS; the
one exception is wind (below). What is cached is the work around the TLAS: `WorldPrepare` keeps the section
instance list (`chunkCache_`) and only rebuilds it when the grid's content version changes, and the hit SBT is
refreshed only when that list does.

### Wind: refit foliage

With the pack's Wind setting on, Java gives every vertex of leaves, grass, flowers, vines and water plants a wind
code (`ChunkManager.setWind`, `PBRVertexWriter.wind`, carried in `PBRVertex::postBase.x`): its kind and how far it is
from where the plant is held. A geometry with such vertices is packed with float positions (the code in their fourth
component, read by the shaders as `PositionVertex::pad0`) and a copy of them at rest, and its section's BLAS is built
with `ALLOW_UPDATE` and left uncompacted. Every frame `Chunks::recordWind`, ahead of the TLAS build, takes the
sections within 48 blocks of the camera, moves their foliage from rest with `world/wind/wind_displace.comp` and refits
their structures in place (`gpu::recordBlasUpdates`); sections that leave that reach go back to rest once. The field
itself is `shader/util/wind.glsl`, which the path tracer also evaluates, at this frame's and the last frame's wind
time (`WorldUBO.windTime`, `windStrength`, `windWeather`), for the motion vectors of what it moved. With Wind off no
section carries a code, and nothing is moved or refit. F3 shows the pass as `wind`.

## Ray tracing pipelines

### Creation

`gpu/pipeline.cpp` builds ray tracing pipelines as **deferred host operations**
(`VK_KHR_deferred_host_operations`): the calling thread and as many helper threads as
`vkGetDeferredOperationMaxConcurrencyKHR` says are useful join the operation until it completes. A pipeline compiled
on one thread alone took most of a minute for a new set of settings. Background compiles (prewarm) use a smaller
thread limit. The log line reports the time and the number of threads. Set `RADIANTE_NO_DEFERRED_COMPILE` to
compile on the calling thread only.

All pipelines go through the device's `VkPipelineCache` (`Device::pipelineCache`), saved to a file at the end of a
rebuild and when the device goes.

### Recursion depth

`maxPipelineRayRecursionDepth` is **2** (`ray_tracing_module.cpp`): the ray generation shader traces, and the hit
shaders trace only shadow and edge queries that the shadow group answers without tracing further. Two levels mean
no invocation reserves stack for a third.

### Shader groups and the SBT

Group 0 is the ray generation shader, then the miss groups, then one hit group per hit group name in the pack's
`configs.json` (closest hit, any hit and intersection stages as configured). Each section instance gets its own
range of hit records (`sbtOffset`), one per geometry.

### Shader compilation and prewarm

Pack shaders are GLSL compiled at runtime (`shader_pack/shader_compile.cpp`). Every compile is keyed by everything
it gets (sources and defines) and stored in a SPIR-V cache under `cache/shaders` in the mod's folder.

`ShaderPrewarm` (`render/shader_prewarm.*`) compiles, on a background thread while the game keeps drawing with its
current pipeline, the shaders and ray tracing pipelines that a pipeline with new settings will need. Nothing it
makes is used: it only fills the SPIR-V and driver caches, so the rebuild that follows takes a fraction of a second.
One job runs at a time; starting another cancels the running one. Java drives it with `Pipeline.prewarmNative` and
polls `Pipeline.prewarmStatus` (state, shaders done/total, pipelines done/total).

## The path tracer

The built-in pack is `radiante-pt`, in
`native/src/shader/world/ray_tracing/internal/radiante-pt/`. It is installed as a zip next to the library and
loaded as a shader pack, like any external one.

### Hit shaders shade

Each surface kind has its own closest hit shader in `world/` (`default.rchit`, `no_height.rchit`, `clouds.rchit`,
`end_portal.rchit`, `end_gateway.rchit`, `water_mask.rchit`, `world_no_reflect.rchit`, `transparent_only.rchit`) and
shades the surface where it is hit: material, direct light, the next bounce's direction. The ray generation shader
(`world/world.rgen`) only drives the path: it sets up the camera ray, traces, reads back what the hit shader left in
the payload, and decides whether to go on. Misses (`world.rmiss`, `world_no_volumetric.rmiss`, `hand.rmiss`) draw the
sky. Any-hit shaders (`*.rahit`) do alpha testing; shadow rays use `shadow.rahit` / `shadow.rmiss`.

```
 world.rgen ── traceRayEXT ──▶ TLAS ──▶ default.rchit / clouds.rchit / ...  (shade, write MainRay)
     ▲                                  world.rmiss / hand.rmiss          (sky)
     └──────── reads radiance, throughput, next direction ◀───────────────┘
```

The default shading does not trace its shadow rays (sun or moon, block light, held lights) where it samples the
lights: it leaves them in `pt_deferred_shadow.glsl` and traces them as its closest hit ends, when almost nothing else is
live. A ray traced from the middle of the shading made the hit shader keep everything it still needed across the call,
which cost more than the rays themselves: 15 ms against 8 for the world pass of a torch-lit room at 1280x720 on an
RTX 5070, a fifth to a quarter of it in daylight.

This keeps every shader small. A design where the hit shaders only record the hit and the raygen shades it was tried
and measured: with the whole shading inlined into one raygen and all of a pixel's traces funnelled through one loop,
the world pass took about 2.5x as long on an RTX 5070 (13 ms against 5 ms per frame at 1280x720 with DLSS), so it was
dropped.

### Payload

The main-path payload (location 0) is `MainRay` in `util/ray_payloads.glsl`: origin and direction, the radiance and
throughput carried so far, the ray cone (texture LOD), the previous scene position (motion vectors), the normal, the
direct light, the random seed and a word of state bits (bounce, inside a boat, camera path, in glass, ignore water).
Shadow and visibility rays use `ShadowRay` at location 1.

### Where the shading lives (`pt/*.glsl`)

| File | Role |
| --- | --- |
| `pt_bindings.glsl` | every descriptor and payload the shaders declare |
| `pt_primary.glsl` / `pt_bounce.glsl` | camera ray set-up and trace; bounces, reflection/refraction split |
| `pt_hit_context.glsl` | the hit (position, normal, UVs) rebuilt from the triangle |
| `pt_material.glsl` | textures, LabPBR, Bedrock MER and height maps, glint, rain |
| `pt_shade_default.glsl` | the default shading the closest hit shaders include |
| `pt_direct.glsl` | sun/moon shadow ray, block lights, sky, motion vectors |
| `pt_deferred_shadow.glsl` | the shadow rays of the default shading, traced as its closest hit ends |
| `pt_glass.glsl`, `pt_media.glsl`, `pt_parallax.glsl` | glass, water medium, height-map parallax |
| `pt_fog.glsl`, `pt_volumetric.glsl`, `pt_froxel.glsl` | fog settings, per-pixel volumetric march, froxel fog |
| `pt_sharc.glsl`, `pt_sharc_update.glsl` | SHaRC query and update paths |
| `pt_outputs.glsl`, `pt_side_channels.glsl` | glow outlines; what shading hands back besides the path state |

Shared code (atmosphere, water, block light, clouds, LOD coverage) is in `radiante-pt/common/`, and generic helpers
(BRDF, LabPBR, sampling, ray cones) in `native/src/shader/util/`.

### Shader Execution Reordering

On devices with `GL_NV_shader_invocation_reorder` (`VPT_SER_SUPPORTED`) and with the player's toggle on, scattered
bounce rays are traced into a hit object and regrouped with `reorderThreadNV` before their closest hit shader runs
(`VPT_TRACE_PATH_REORDERED` in `world.rgen`), so threads that run the same hit shader run together. Reordering moves
the work, not the pixel: `gl_LaunchIDEXT` and every image coordinate stay the same.

### SHaRC radiance cache

SHaRC is a world-space hash grid of cached radiance (`use_sharc`, on by default). It has three parts:

- **update**: a sparse `SHARC_UPDATE` variant of the raygen traces paths that fill the cache
  (`pt_sharc_update.glsl`; `configs.json` names the pass as `"update_pass": "world"`): one per
  `sharc_update_downsample_factor` squared pixels, the factor scaled up above a 1280x720 render size so the cache gets
  the same number of paths a frame at any larger size (its cells are in the world and do not shrink with the render);
- **resolve**: `sharc_resolve.comp` folds the frame's accumulated radiance into the resolved cache and evicts stale
  entries;
- **query**: deep bounces may take the cached light instead of tracing on (`pt_sharc.glsl`).

The native side (`ray_tracing_module.cpp`, descriptor set 4) owns the hash, accumulation and resolved buffers and
reads `sharc_scene_scale`, `sharc_accumulation_frame_num`, `sharc_stale_frame_num_max`,
`sharc_update_downsample_factor` and `sharc_debug_mode`.

### Measuring

F3 shows the GPU time of the world pipeline, averaged over 60 frames, with its larger stages
(`PrismRT GPU: 5.10 ms (world 3.10, sharc_paths 0.32, sharc_resolve 0.30, DLSS 0.56, ...)`; the SHaRC update is shown as its paths and its resolve), from timestamp queries (`render/gpu_profiler.*`).
With the environment variable `RADIANTE_GPU_PROFILE` set, every stage is also logged every 300 frames
(`[gpu profile]`).
## Denoising, upscaling and post

The world pipeline is a chain of **modules** (`render/modules/world/`), each a `WorldModule` with a per-slot
context. Java assembles one of a few presets (`Pipeline.assemble*Internal`):

```
 DLSS RR:   ray_tracing ─▶ dlss (ray reconstruction)             ─▶ tone_mapping ─▶ post_render
 NRD+FSR:   ray_tracing ─▶ nrd ─▶ fsr_upscaler                   ─▶ tone_mapping ─▶ post_render
 NRD+XeSS:  ray_tracing ─▶ nrd ─▶ xess_sr                        ─▶ tone_mapping ─▶ post_render
 NRD only:  ray_tracing ─▶ nrd ─▶ temporal_accumulation          ─▶ tone_mapping ─▶ post_render
```

The ray tracing module writes the guide buffers the later stages need (`modules/ray_tracing.yaml`): radiance,
diffuse and specular albedo, normal/roughness, motion vectors, linear and first-hit depth, specular hit depth,
first-hit direct and indirect diffuse, specular, emission, refraction, and fog image and transmission.

- **NRD** (`nrd/`): RELAX diffuse+specular. `nrd/prepare.comp` and `nrd/compose.comp` pack the inputs and put the
  denoised signals back together. Built only with `PRISM_ENABLE_NRD`. Java sets the pack attribute `nrd_signals`
  (`VPT_NRD_SIGNALS`) on every build whose pipeline has NRD: the path tracer then writes its signals for NRD (the
  glow of the surface seen kept out of them, the direct light always diffuse, the bounce's length as the diffuse hit
  distance).
- **DLSS** (`dlss/`): DLSS Super Resolution and Ray Reconstruction through NGX; RR replaces NRD when available.
  `Pipeline.isRayReconstructionAvailable` says whether NGX offers RR; without it the default falls back to NRD +
  DLSS super resolution, then (XeSS first on Intel) NRD + FSR, NRD + XeSS, NRD alone.
- **FSR** (`fsr_upscaler/`): FidelityFX upscaler (`PRISM_ENABLE_FFX_UPSCALER`).
- **XeSS** (`xess_upscaler/`): Intel XeSS SR (`PRISM_ENABLE_XESS`, needs `extern/xess`).
- The `shader/world/upscaler/*.comp` passes bring motion vectors, depth and normals to the output size for the
  upscalers.
- **Tone mapping** (`tone_mapping/`): histogram exposure (`hist.comp`, `exposure.comp`), bloom down/up, the tone
  curve, and a Bedrock-style curve (`bedrock_hist.comp`, `bedrock_curve.comp`). The exposure buffer is also read by
  HDR output (`render/hdr_output.*`).
- **Post render** (`post_render/`): turns first-hit distances into depth, copies the tone-mapped image out and runs
  the pack's `post_render` stage: stars, depth of field, motion blur. Text, name tags, particles and rain are traced
  in the world, not drawn afterwards.

Fog and clouds are mostly in the path tracer: volumetric fog per pixel (`pt_volumetric.glsl`) or as froxels
(`froxel/integrate.comp`, `froxel/carry.comp`), and volumetric clouds resolved over time
(`volumetric_cloud/temporal.comp`) and composited by `volumetric_cloud/apply.comp`, which also hands the cloud
transmittance to the star pass.

Frame generation (DLSS or FSR) and Reflex live in `render/framegen/`, after the world pipeline.

## Settings: what applies live and what rebuilds

### `pipeline.yaml` and module configs

Each module's images and attributes are declared in `common/src/main/resources/modules/*.yaml`
(`outputImageConfigs`, `inputImageConfigs`, `attributeConfigs` with name, type and default). The player's pipeline
is stored in `pipeline.yaml` in the mod's folder (`Pipeline.PipelineConfigStorage`): the mode (`PRESET` or custom),
and per module the attribute values. `Pipeline.buildNative(long)` lays that out in native memory as
`WorldPipelineBuildParams`; the native side keeps it as the **blueprint**, so a resize or vsync change can rebuild
without asking Java again.

A shader pack declares its own attributes in `configs.json`, each with a type, a default and a `define` (a macro,
a template such as `"uint(X)"`, or per-value variants).

### Live values: `Pipeline.setRuntimeAttribute`

When the player changes a setting, Java (`Pipeline.pushRuntimeChanges`) first tries to apply it live:

```
 setRuntimeAttribute(name, value)            under recreateMtx
   ├─ module other than ray_tracing?  → WorldModule::setRuntimeAttribute
   │      (nrd, tone_mapping, post_render implement it)
   ├─ else shader pack attribute marked "runtime": true?
   │      → ShaderPack::setRuntimeAttribute → slot + packed vec4
   │      → Buffers::setPackParam(slot, value)   (outside the lock)
   └─ anything else → false → Java marks a structural change → full rebuild
```

A value applied live is also written into the blueprint, so the next rebuild keeps it.

**`WorldUBO.packParams`** is an array of `PRISM_PACK_PARAM_COUNT` (64) `vec4`s at the end of the world uniform
(`native/src/common/shared.hpp`). For a `"runtime": true` attribute, the shader pack loader does not compile the
value in: the define becomes a read such as `int(worldUBO.packParams[N].x)` or `vec3(worldUBO.packParams[N].xyz)`.
The shader text is then the same for every value, nothing recompiles, and the SPIR-V cache stays valid. Bools,
ints, floats and vec3 fit; anything else must be compiled in.

What needs a rebuild:

- any pack attribute without `"runtime": true` (it changes a define, so shaders recompile);
- ray tracing module attributes such as SHaRC on/off;
- `tone_mapping`'s `bloom_enable` (it decides which images and pipelines exist);
- the preset, upscaler, shader pack or any module change;
- a pipeline whose last rebuild failed.

Rebuilds that change shaders are usually preceded by a prewarm, so the wait is short.

## Building

The native build is CMake (`native/CMakeLists.txt`). The scripts are the usual entry point:

- **Windows:** `native/build-windows.bat` (Visual Studio 2026, x64). Builds into `%RADIANTE_BUILD_DIR%`
  (default `C:\rtb\radiante`) and installs `prismrt.dll` and the shaders into the mod's resources.
- **Linux:** `native/build-linux.sh` (cmake, ninja or make, g++ 13+, Vulkan SDK with `VULKAN_SDK` set). Builds into
  `native/build/linux-release` by default and installs `linux-x64/libprismrt.so`. Extra arguments go to CMake.

Both pass `-DPRISM_ENABLE_NRD=ON -DUSE_AMD=ON`. Options:

| Option | Default | Effect |
| --- | --- | --- |
| `PRISM_ENABLE_NRD` | ON | NRD denoiser, built as a static library from `extern/nrd` |
| `PRISM_ENABLE_FFX_UPSCALER` | ON | FidelityFX upscaler |
| `PRISM_ENABLE_XESS` | ON | XeSS; turned off with a warning when `extern/xess` is missing |
| `USE_AMD` | ON | AMD specific optimisations and features |

Fixed shaders (`native/src/shader/**` outside `world/ray_tracing/internal`) are compiled to SPIR-V at build time
with `glslangValidator`; built-in packs are zipped and installed under `shaders/world/ray_tracing`. Pack shaders are
compiled at runtime.

**Any change under `native/` needs a native build**, including pack shaders, since the pack is installed by the
build. The build output is not in git, so build again after switching branches too. Full instructions, the DLSS SDK
step and CI are in [Building and releasing](en/building.md).

## Source map

| Path | Responsibility |
| --- | --- |
| `native/src/prism/middleware/` | JNI entry points, one file per Java class; `jni_support` |
| `native/src/prism/gpu/` | Vulkan layer: device, VMA, buffers, images, command buffers and `sync`, descriptors, pipelines, SBT, acceleration structures, `retire`, queue lock, diagnostics |
| `native/src/prism/render/render_framework.*` | frame slots, `renderFrame`, `markSubmitted`, `recreate` |
| `native/src/prism/render/renderer.*` | the `Renderer` singleton that holds the subsystems |
| `native/src/prism/render/pipeline.*` | blueprint, `WorldPipeline`, module registry, prewarm entry |
| `native/src/prism/render/chunks.*`, `entities.*`, `world.*` | section grid and its BLAS, entities, the world as a whole |
| `native/src/prism/render/textures.*`, `buffers.*`, `emission.*` | atlases and materials, uploads and the world uniform, emissive light lists |
| `native/src/prism/render/shader_prewarm.*` | background compile into the caches |
| `native/src/prism/render/hdr_output.*`, `mc_depth.*`, `gpu_profiler.*` | HDR output, traced depth for Minecraft's overlays, GPU timings |
| `native/src/prism/render/output_upscaler.*` | FSR 1 upscale of a lower output resolution to the window |
| `native/src/prism/render/framegen/` | frame generation (DLSS, FSR), present thread, Reflex |
| `native/src/prism/render/modules/world/` | the world pipeline modules |
| `…/ray_tracing/` | the ray tracing module, SHaRC buffers, `submodules/world_prepare` (TLAS, SBT) |
| `…/shader_pack/` | pack loading, attributes and defines, runtime values, execution scripts, shader compile |
| `…/nrd/`, `dlss/`, `fsr_upscaler/`, `xess_upscaler/`, `temporal_accumulation/` | denoiser, upscalers, accumulation |
| `…/tone_mapping/`, `post_render/` | exposure, bloom, tone curve; post passes |
| `native/src/prism/util/` | logging, background threads, parallel helpers |
| `native/src/common/` | structs shared by C++ and GLSL (`shared.hpp`: `WorldUBO`, `packParams`), singletons |
| `native/src/shader/` | fixed shaders: display, tone mapping, NRD prepare/compose, upscaler helpers, `util/` |
| `native/src/shader/world/ray_tracing/internal/radiante-pt/` | the built-in path tracer pack: `configs.json`, `world/`, `pt/`, `common/`, post passes, clouds, froxels |
| `common/src/main/java/com/g2806/radiante/client/proxy/` | Java side of the JNI calls |
| `common/src/main/java/com/g2806/radiante/client/pipeline/` | presets, `pipeline.yaml`, live values, prewarm |
| `common/src/main/resources/modules/` | module image and attribute declarations |

## Debugging tips

- **Environment variables** (read once by the native side):
  - `RADIANTE_DEV_PROFILE`: CPU timings of `renderFrame` per phase, printed for frames slower than 25 ms and as
    averages every 300 frames; also profiles world preparation.
  - `RADIANTE_GPU_PROFILE`: GPU timings per pass (`render/gpu_profiler.*`).
  - `RADIANTE_GPU_CHECKPOINTS`: GPU checkpoints, to see how far the GPU got when a frame never completes.
  - `RADIANTE_NO_DEFERRED_COMPILE`: compile ray tracing pipelines on one thread.
  - `RADIANTE_FRAME_GENERATION`, `RADIANTE_FG_DUMP`, `RADIANTE_FG_SHOW_GENERATED`: frame generation debugging.
- **Hangs:** a watchdog in `render_framework.cpp` reports, when Minecraft stops handing frames over, how far the GPU
  got through the last submit and what the queue is doing, on stderr as well as the log.
- **Validation:** build the mod with `-PvulkanValidation` for the validation layers and debug labels.
- **Settings that should be live but rebuild:** turn on debug logging in the options; Java logs
  `[settings] <name> not applied live; rebuilding` for each one.
- **SHaRC:** `sharc_debug_mode` (`hash_grid`, `occupancy`, `heatmap`) visualises the cache. Compare images with SHaRC on, since it is on by default.
- **Stale native build:** if a shader or C++ change seems to do nothing, check the time of `prismrt.dll` and build
  again (clean first if the clock moved backwards).
- **Image barriers:** if a pass reads garbage, check that every image it touches goes through
  `CommandBuffer::sync` with the right `ImageUsage`, and that code passing images to third-party SDKs marks them with
  `imageLayout()` afterwards.
