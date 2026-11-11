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

- `pipe=preset:<key>` switches pipeline, by the preset's translation key (`render_pipeline.preset.rt_nrd_fsr`,
  `...rt_nrd_xess`, `...rt_nrd`, `...rt_dlss`). An unknown key falls back to the best pipeline silently: the
  pass names in the `fps=` line (`Nrd`, `XessSr`, `FSRUpscaler`, `DLSS`) say which one really ran.
- XeSS runs on any ray tracing GPU once its runtime is there: `download=xess` fetches it into the
  development folder, and the next start finds it.
- Use `testworld=`, not `join=`: `join=` hangs on the experimental-features prompt, `testworld=` answers it.
- Scripts stop by themselves in a world whose name does not start with `Radiante`.
- `RADIANTE_DEV_SCRIPT="@<file>"` reads the script from that file, for one too long for an environment variable.
  Its steps may be on lines of their own.
- `opt=<field>=<value>` sets a Radiante option for the run. It goes through the option's own setter when it has one
  (`set<Field>(value, write)`), so options the renderer is told about (`collectChunkEmission`,
  `entityLightReach`) take effect.
- The other actions (`hud=`, `fov=`, `slot=`, `debug=`, `rejoin=`...) are listed in `DevAutomation.run`.

## Smoke test

Before a release, and after a Minecraft version bump or a merge between version branches, run the visual smoke
test:

```sh
python dev/smoke/smoke.py
```

It starts a development client, opens the test world `RadianteSmokeSuite`, builds the showcase gallery
(`/radiante showcase`) and takes a picture of each of its rooms, by day and at night, then of each feature in
`toggles` off and on. It fails when the renderer did not start, a shader did not compile, the renderer logged an
error, a picture is black or one flat colour, or a feature's two pictures do not differ. The pictures, each
view's frame rate and GPU times, and what to look at are in `build/smoke/<time>/report.html`: read it, the
automatic checks only catch what is plainly broken. About five minutes; it needs the native renderer built, a GPU
that can ray trace, and Python with Pillow and numpy. It backs up the development settings and puts them back.

- `--loader neoforge` (or `forge`) runs another loader's client; `--only <text>` the scenes whose name has it.
- `--golden <folder>` compares each picture with the one of the same name from an earlier run (keep one set per
  GPU vendor: their denoisers and upscalers differ) and reports the difference without failing on it.
- Scenes are data, in `dev/smoke/scenes.json`: the commands that set a view up and a line saying what to check.
  Add a scene for a regression once it is fixed.

## What a setting costs

```sh
python dev/smoke/bench.py
```

walks a client through the views of `dev/smoke/bench.json` and, at each, through its variants (a setting moved
off its default, then put back), reading the renderer's own GPU time per pass (the F3 numbers, averaged over 60
frames). It prints a table per view and writes it with a picture of every variant to `build/bench/<time>/bench.md`,
so what a cheaper setting saves can be seen next to what it does to the picture.

- GPU times do not move with the frame limiter (Minecraft drops to 30 fps after a minute without input), which
  is why they are measured rather than the frame rate.
- **Nothing else may be using the GPU.** An animated wallpaper, a browser playing video or a second game moves
  the numbers by more than most settings do. The bench file's last variant is the default again: when it does
  not come out as the first one did, the run measured the other program, not Radiante. Check with
  `nvidia-smi` that the GPU is idle before starting.
- Compare variants within one run only.

## Settings sweep

```sh
python dev/smoke/settings.py
```

checks that every setting does something. It starts a development client and, for each setting listed per view in
`dev/smoke/settings.json`, takes a picture of the unchanged view, changes the setting, waits, takes a picture, puts
the setting back, waits and takes the unchanged picture again. A setting whose picture differs from both unchanged
ones by no more than they differ from each other does nothing in that view. It writes `report.md` and
`summary.json` to `build/settings-sweep/<time>/`. It reports rather than fails: what it finds is for a person to
read. A full run is about an hour. Like the smoke test, it backs up the development settings and puts them back.

- `--only <view>[,<view>...]` runs only those views; `--list` prints the tests and stops.
- `--report <folder>` writes the report of a run already made again, from its output folder.
- `--out <folder>` chooses the output folder, `--loader neoforge` (or `forge`) another loader's client, and
  `--sweep <file.json>` another sweep file.
- A view must show the thing its settings change. The test world `RadianteSmokeSuite` is in a biome where it never
  rains, so the rain view shows nothing there.

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

- `./gradlew build` (or `./gradlew :common:test` alone) runs the Java tests in `common/src/test`: code that needs
  no running game, such as the Bedrock pack reader, shader pack settings, shared settings codes (`SettingsCodeTest`)
  and how settings files are written.
- Some of them keep two hand-kept lists in step, and fail when a change was made to one side only:

  | Test | What it compares |
  |---|---|
  | `MixinTargetsTest` | Every method, field and class a mixin names, against the Minecraft the build compiles with |
  | `ModuleLayoutTest` | A module's YAML image lists, against `inputImageNum`/`outputImageNum` in its C++ class |
  | `PresetAssemblyTest` | Every preset, assembled from the modules' YAML: its connections name images that exist, of the same format, one source each |
  | `TunableDefaultsTest` | A slider's default and range, against its attribute in `configs.json` or the module's YAML |
  | `StagedOptionsTest` | The option names the settings screen stages as text, against the fields of `Options` |
  | `BedrockRtxValuesTest` | The `BedrockRtxValues` table, against radiante-pt's defaults in `configs.json` and `tone_mapping.yaml` |
  | `LangFilesTest` | The keys and `%s` placeholders of the three lang files |

- The native tests in `native/src/tools/` need no game and no GPU. Build them with the `native_tests` target and
  run them with ctest; the CI does both:

  ```sh
  cmake --build <build dir> --config Release --target native_tests
  ctest --test-dir <build dir> -C Release --output-on-failure
  ```

- The offline shader pack checker is there too ([shader packs, §5](shader-packs.md#checking-a-pack-offline)).

## Example shader packs

The packs in `examples/shaderpacks` hold only the hook files they change, so a hook whose signature changes in
radiante-pt breaks them without breaking radiante-pt, and one of them ships inside the mod. After a change to a
hook, to a file in radiante-pt's `common/` or to an example, run:

```sh
python native/src/tools/check_example_packs.py --stage <build dir>/src/tools/Release/shader_pack_stage.exe
```

(`<build dir>/src/tools/shader_pack_stage` on Linux.) It stages each pack on radiante-pt as the loader does and
compiles it at the pack's default settings. The CI runs it on every push.

## Mixins

A mixin with a wrong target compiles fine and only fails when its class loads in game. `MixinTargetsTest`
(`./gradlew :common:test`) catches the usual case without the game: it looks every method, field and class the
mixins name up in the Minecraft the build compiles against, and lists the ones that are not there. It checks names,
not descriptors, and not the mixins for other mods, so after touching a mixin, or when porting to another Minecraft
version, still launch the game and open a world: the crash report names the mixin and the descriptor it expected.
