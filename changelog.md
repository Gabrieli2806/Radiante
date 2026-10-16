# Changelog

All notable changes to this project are documented in this file.

## [1.0.0-beta.1] - 2026-10-16

First build of the standalone renderer, PrismRT. Not yet tested in game; report anything that looks or runs wrong.

### Highlights

- New native renderer: GPU resources, acceleration structures and the ray tracing pipeline rewritten. The pipeline
  compiles on all cores (about 4x faster first load) and shaders are prewarmed in the background.
- Path tracer: hit shaders only record the hit and all shading runs in one place, with shader execution reordering;
  smaller ray payload; several lighting fixes.
- Shader pack settings, tone mapping, denoiser and stars apply live, without rebuilding the pipeline.
