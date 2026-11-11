# Developer docs

For people changing Radiante itself. Start with [CONTRIBUTING.md](../../CONTRIBUTING.md); these pages go deeper.
They are in English only.

| Page | What it covers |
|---|---|
| [Building and releasing](building.md) | Requirements, Windows and Linux builds, the Linux fixes to keep, CI, publishing a version |
| [Testing](testing.md) | Development clients, test worlds, scripted runs, the smoke test, the settings sweep, the shader check under every setting, visual comparisons, unit tests |
| [Performance audit](performance-audit.md) | Where a frame's time goes, measured, and what could save the most |
| [Architecture](architecture.md) | How PrismRT works: JNI, frames, acceleration structures, pipelines, the path tracer |
| [Shader packs: design](shader-packs.md) | The pack contract: manifest, `extends`, hooks, settings, versioning, safety |
| [Bedrock RTX defaults](bedrock-defaults.md) | How radiante-pt's defaults were matched to Bedrock RTX, and what still differs |

Also: the [example shader packs](../../examples/shaderpacks/README.md), and [`docs/images/`](../images/README.md) for
the screenshots the docs use.
