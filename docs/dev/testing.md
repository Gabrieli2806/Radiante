[← Developer docs](README.md)

# Testing

How to check a change in game in a way others can repeat. The short rules are in
[CONTRIBUTING.md](../../CONTRIBUTING.md#5-test-your-change); this page has the details.

## Development clients

```sh
./gradlew :fabric:runClient       # or :neoforge:runClient, :forge:runClient
```

| Folder | Holds |
|---|---|
| `run-fabric/`, `run-neoforge/`, `run-forge/` | Each loader's mods and mod configs. They can run at the same time. |
| `run-shared/` | Shared by all three: worlds, resource packs, shader packs, Radiante's settings, `options.txt`, servers. |
| `screenshots/` (repository root) | Screenshots from every client. Ignored by git, except its subfolders (`art/`, `promo/`...). |

Fabric flags: `-PquickPlay="<world name>"` boots straight into a world; `-PvulkanValidation` turns on the Vulkan
validation layers and debug labels.

## Test worlds

> [!WARNING]
> Test only in worlds whose name starts with `Radiante` (for example `RadianteTest`), never in a world you play in.
> Scripts teleport, fill and summon.

- A fresh superflat is usually enough.
- A copy of a test world needs a different **level name**, not only a different folder.
- Do not delete a world copy while a client is running.

## Scripted runs

`DevAutomation` drives the client from two environment variables, for repeatable comparisons:

| Variable | Steps counted in | Typical use |
|---|---|---|
| `RADIANTE_DEV_PRESCRIPT` | Client ticks, from start | `100:testworld=RadianteSmoke` opens (or creates) a test world |
| `RADIANTE_DEV_SCRIPT` | World ticks, once a test world is open | `400:quit` |

```sh
RADIANTE_DEV_PRESCRIPT="100:testworld=RadianteSmoke" RADIANTE_DEV_SCRIPT="400:quit" ./gradlew :fabric:runClient
```

- Use `testworld=`, not `join=`: `join=` hangs on the experimental-features prompt, `testworld=` answers it.
- Scripts stop by themselves in a world whose name does not start with `Radiante`.
- The other actions (`hud=`, `fov=`, `slot=`, `debug=`, `rejoin=`...) are listed in `DevAutomation.run`.

## Shaders under every setting

The path tracer is compiled in game, with the player's settings as defines, so a shader that only breaks with one
setting off its default is not caught by building the mod. After any change under `native/src/shader`, run:

```sh
python native/src/tools/check_shaders.py
```

It compiles Radiante PT at the defaults, then once for each compiled-in setting on each of its other values and once
for each debug view (about fifty builds, under a minute). It needs Python 3 and `glslangValidator` from the Vulkan
SDK, nothing else: no native build and no game. The CI runs it on every push; `--only <text>` runs the builds whose
name contains the text.

## Settings files

Settings tests write to the development `radiante/` files (`options.properties`, `pipeline.yaml`,
`radiante-pt.zip.txt`) and to `options.txt`, all in `run-shared/`. Back them up before, restore them after.

## Visual changes

1. **Reproduce the bug first**, with a screenshot.
2. Compare **the same view** before and after: same world, position, time of day and settings. Toggle the change in
   place rather than walking between shots; `/fillbiome` changes a biome without moving.
3. Keep **SHaRC on** (`use_sharc`, the player's default): without it indirect light reads about 25% low.
4. **A/B Capture** in the Debug section saves the screenshot with a text file of every setting and GPU timing.
5. Check the window size in the log before trusting timings: a resized window changes them.

## Unit tests

- `./gradlew build` runs the Java tests in `common/src/test`: code that needs no running game, such as the Bedrock
  pack reader, shader pack settings, and the check that `BedrockRtxValues` agrees with radiante-pt's defaults.
- Small native tests and the offline shader pack checker are in `native/src/tools/`
  ([shader packs, §5](shader-packs.md#checking-a-pack-offline)).

## Mixins

A mixin with a wrong target compiles fine and only fails when its class loads in game. After touching a mixin, or
when porting to another Minecraft version, launch the game and open a world: the crash report names the mixin and
the descriptor it expected.
