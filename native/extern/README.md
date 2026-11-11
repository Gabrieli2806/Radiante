# Third-party code

The libraries the native renderer is built with, copied into the repository rather than fetched, so a checkout
builds as it is. Each keeps its own licence file.

| Folder | What | Version here | Used for |
|---|---|---|---|
| `DLSS` | NVIDIA NGX headers and a CMake wrapper | SDK v310.9.1 (the tag the CI fetches `lib/` from) | DLSS upscaling, ray reconstruction and frame generation |
| `FidelityFX-SDK` | AMD FidelityFX SDK | 1.1.4 | FSR upscaling and frame generation; `sdk/include` for the FSR 1 shaders |
| `glm` | GLM | 1.0.2 | Vectors and matrices |
| `json` | nlohmann/json | 3.12.0 | Shader pack manifests |
| `minizip-ng` | minizip-ng | 4.1.0 | Shader pack zips |
| `nrd` | NVIDIA Real-Time Denoisers | 4.17.3 | The NRD pipelines' denoiser |
| `sharc` | NVIDIA SHaRC | 1.6.5 | Radiance cache (shader headers only) |
| `stb` | stb | `stb_image` 2.30, `stb_image_write` 1.16 | Reading and writing images |
| `tinyexpr` | TinyExpr | not recorded | Expressions in shader pack manifests |
| `vma` | Vulkan Memory Allocator | 3.3.0 | GPU memory |
| `volk` | volk | header version 343 | Loading Vulkan functions |
| `xess` | Intel XeSS 2 SDK | not recorded | XeSS upscaling (headers and the import library; the runtime is downloaded in game) |

The versions are read from each library's own files. Which upstream commit each folder was copied from was not
recorded when they came in: when you update one, write its tag or commit in this table.

## What is left out

Only what the build reads is kept. Documentation, tests, samples and prebuilt programs that came with the libraries
were removed (the FidelityFX SDK's `docs/`, `samples/`, `framework/` and `PrebuiltSignedDLL/`; `glm`'s `doc/` and
`test/`; `json`'s `docs/` and `tests/`; `vma`'s `docs/`; `stb`'s `tests/`; `xess`'s `doc/` and `samples/`; `sharc`'s
`docs/`). Leave them out when updating a library. `DLSS/lib/` is not in the repository either: the setup scripts
and the CI fetch it from NVIDIA's DLSS repository.

## Changes made here

Some of this code differs from upstream, and an update has to carry the changes over:

- **FidelityFX SDK** and **minizip-ng**: the Linux fixes listed in
  [docs/dev/building.md](../../docs/dev/building.md#linux-fixes-to-keep).
- **DLSS**: `CMakeLists.txt` and `README.md` are this project's wrapper around NVIDIA's headers and libraries.

Other folders are not known to be changed; compare with upstream before assuming so.
