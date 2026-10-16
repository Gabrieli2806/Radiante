# Changelog

All notable changes to this project are documented in this file.

## [1.0.0-beta.1] - 2026-10-16

First build of the standalone renderer, PrismRT. Not yet tested in game; report anything that looks or runs wrong.

### Highlights

- New native renderer: GPU resources, acceleration structures and the ray tracing pipeline rewritten. The pipeline
  compiles on all cores (about 4x faster first load) and shaders are prewarmed in the background.
- About 2.5x faster path tracing than the first PrismRT builds (5 ms instead of 13 ms per frame on an RTX 5070 at
  1280x720 with DLSS): shading stays in the hit shaders, with shader execution reordering for scattered bounces.
- F3 shows the GPU time per frame and its main stages.
- The DLSS / XeSS Install and Delete button is always shown under the settings.
- Shader pack settings, tone mapping, denoiser and stars apply live, without rebuilding the pipeline.
