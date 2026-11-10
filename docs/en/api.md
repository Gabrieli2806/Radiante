# Making mods for Radiante

Languages: [ES](../es/api.md) · **EN**

Radiante has a small public API for other mods: everything in the package `com.g2806.radiante.api`.
The rest of Radiante is internal and can change in any version; build only on this package.

| Class | What it is for |
|---|---|
| `RadianteApi` | Whether the world is being ray traced right now, and where the sun and moon Radiante draws are |
| `RadianteGeometry` | Putting your own geometry (debris, effects, markers) into the ray traced world |
| `RadianteAddons` | Putting your mod's options in Radiante's settings screen, under **Addons** ([below](#radianteaddons)) |

The API is plain Java, without loader-specific entrypoints, so the same calls work on Fabric,
NeoForge and Forge.

## Radiante is client-only

Radiante only loads on the client. A mod that depends on it is a client mod too (on Fabric:
`"environment": "client"`). In single player the integrated server runs inside the client, so
server-side code in your mod (mob AI, game rules) can still read Radiante there; on a dedicated
server Radiante is not loaded. If your mod should also run on servers, keep every call to the API in
a class you only load when Radiante is present (for example after
`FabricLoader.getInstance().isModLoaded("radiante")`), and make Radiante optional.

## Setting up the project

Download the Radiante jar for your Minecraft version and loader from the
[GitHub releases](https://github.com/Gabrieli2806/Radiante/releases) (for example
`radiante-fabric-1.1.0+26.3.jar`), put it in a `libs/` folder of your project and add it in
`build.gradle`:

```gradle
dependencies {
    // Compile against Radiante and run it in the dev client; players install Radiante themselves.
    implementation files("libs/radiante-fabric-1.1.0+26.3.jar")
}
```

Use `compileOnly` instead if Radiante is optional for your mod and you do not want it in your dev
client. Do not bundle Radiante inside your jar.

Then declare the dependency, so a player without Radiante gets a clear message instead of a crash:

```json
// fabric.mod.json
"depends": {
  "radiante": ">=1.1.0"
}
```

On NeoForge and Forge, add a `[[dependencies.yourmod]]` entry with `modId="radiante"` to your
`neoforge.mods.toml` or `mods.toml`. Ask for the first Radiante version that has every API call you
use: `RadianteAddons` is newer than 1.1.0 (see the changelog).

## RadianteApi

```java
import com.g2806.radiante.api.RadianteApi;

if (RadianteApi.isRayTracingEnabled()) {
    // Radiante is drawing the world; Minecraft's own level passes are not running.
}

// The sun as Radiante draws it, following the player's sun path settings (lean and turn).
float sunAngle = level.environmentAttributes().getDimensionValue(EnvironmentAttributes.SUN_ANGLE);
Vec3 toSun = RadianteApi.sunDirection(sunAngle * (float) (Math.PI / 180.0));
Vec3 toMoon = RadianteApi.moonDirection(sunAngle * (float) (Math.PI / 180.0));
```

- `isRayTracingEnabled()`: true while Radiante traces the world. Code that draws with Minecraft's
  level render will not show up then; use `RadianteGeometry` for that.
- `sunDirection(angle)` and `moonDirection(angle)`: unit vectors from the world towards the sun and
  the moon, in world space. The angle is Minecraft's `SUN_ANGLE` in radians. The sun can lean and
  turn in Radiante's settings, so do not compute it yourself: this is the sun that lights the world
  and casts the shadows the player sees.
- `celestialTransform(angle)` and `moonTransform(angle)`: the same as a rotation matrix, for drawing
  things that move with the sky.

*Radiante Death Light* uses `sunDirection` to burn undead only where the traced sun reaches them.

## RadianteGeometry

While Radiante traces the world, Minecraft's level passes do not run, so anything a mod draws there
by itself never reaches the screen. Entities, block entities and particles are already traced. For
the rest there are two ways in, both called every frame on the render thread while the world is
traced.

**A provider** writes quads with a texture:

```java
RadianteGeometry.register("mymod:debris", sink -> {
    Vec3 camera = sink.camera();
    VertexConsumer quads = sink.quads(Identifier.parse("minecraft:textures/atlas/blocks.png"),
        RadianteGeometry.Alpha.CUTOUT);
    for (Debris piece : MyMod.debris()) {
        // Four vertices make a quad; positions are relative to the camera.
        piece.emit(quads, camera, sink.partialTick());
    }
});
```

**A submitter** is for a mod that already submits models, items or text to Minecraft's
`SubmitNodeCollector`: the same calls, made on the collector it is given, are traced.

```java
RadianteGeometry.registerSubmitter("mymod:holograms", collector -> MyMod.submitHolograms(collector),
    false); // false: it does not cast shadows (light, markers, labels)
```

- `Alpha`: `OPAQUE` (cheapest), `CUTOUT` (texels there or not), `TRANSLUCENT` (glass-like), or
  `DITHERED` (a faded ghost, for previews and guides).
- `register(name, provider, castsShadows)`: pass `false` for previews, guides and markers.
- A second registration with the same name replaces the first; `unregister(name)` removes it.

## RadianteAddons

A mod built on Radiante (an *addon*) can put its options in Radiante's own settings screen instead of
writing a config screen and config file of its own. When at least one addon registers, the settings
get an **Addons** section at the end of the sidebar, with a heading per addon (its icon, name and
version) and its options under it. Without addons nothing changes. Clicking a heading (or Enter, Space
or a controller's A on it) folds that addon's options away or opens them again: with one addon they
start open, with two or more folded, and Radiante remembers each one's choice.

Addon options behave like Radiante's own: they are held until **Apply** or **Done**, **Undo** drops
them, **Reset to Defaults** puts them back to the addon's defaults, the search finds them (or all of
an addon's options by its name), the description panel shows their tooltip, and the keys and a
controller move through them.

Everything is in `com.g2806.radiante.api.RadianteAddons`; the rest of Radiante is internal. It is
plain Java with no loader-specific entrypoint, so the same call works on Fabric, NeoForge and Forge.
Call it once from your mod's **client** initialiser (Radiante is client-only):

```java
import com.g2806.radiante.api.RadianteAddons;
import java.util.List;
import net.minecraft.network.chat.Component;

public final class MyAddonClient {
    public static RadianteAddons.Setting<Boolean> debugRays;
    public static RadianteAddons.Setting<String> aiMode;
    public static RadianteAddons.Setting<Integer> range;

    public static void init() {
        RadianteAddons.Addon addon = RadianteAddons.register("mymod", Component.translatable("mymod.name"));

        debugRays = addon.toggle("debug_rays", Component.translatable("mymod.debug_rays"),
            Component.translatable("mymod.debug_rays.tooltip"), false);

        aiMode = addon.cycle("ai_mode", Component.translatable("mymod.ai_mode"),
            Component.translatable("mymod.ai_mode.tooltip"), List.of("off", "avoid_sun", "seek_shade"), "avoid_sun",
            value -> Component.translatable("mymod.ai_mode." + value));

        range = addon.slider("range", Component.translatable("mymod.range"),
            Component.translatable("mymod.range.tooltip"), 8, 64, 4, 16)
            .onChange(value -> MyAddon.LOGGER.info("Range is now {}", value));
    }
}
```

Then read a value wherever it is needed, from any thread (the server thread in single player
included): `if (MyAddonClient.debugRays.get()) { ... }`.

| Call | Shown as |
|---|---|
| `toggle(key, title, tooltip, default)` | a checkbox |
| `cycle(key, title, tooltip, values, default)` (optionally a `label` function) | a value that steps through `values` on click |
| `slider(key, title, tooltip, min, max, step, default)` | a slider of whole numbers |

- `id` is your mod id: Radiante shows the mod's version beside the title and keeps the values in
  `radiante/addons/<id>.properties`. Ids and keys use lowercase letters, digits, `_`, `-` and `.`.
- The icon left of the title is your mod's own, found by that id: the `icon` in `fabric.mod.json`,
  or the `logoFile` in `neoforge.mods.toml` / `mods.toml`. A square PNG of 32 x 32 or less is shown
  as it is (pixel art stays sharp); a larger one is scaled down. To show another texture, call
  `addon.icon(Identifier.fromNamespaceAndPath("mymod", "textures/gui/addon.png"))` after `register`.
  A missing or unreadable icon is logged once and the heading goes without one.
- Titles and tooltips are your own `Component`s; put their texts in your mod's lang files. The
  tooltip may be `null`. Keep it short: what the option does, and what changing it does.
- `onChange` listeners run on the client thread when the player applies a change.
- Mistakes do not break the settings screen: an invalid or repeated id or key, a default that is
  not one of the values, or a listener that throws is logged (`Radiante/addons`) and skipped. A
  setting that could not be listed still returns its default from `get()`.
- If your addon also runs on a dedicated server, do not call the API there: Radiante is not loaded
  on servers.

To make Radiante optional for your mod, keep the calls in a class you only load when Radiante is
present (for example after `FabricLoader.getInstance().isModLoaded("radiante")`).

## Stability

- Only `com.g2806.radiante.api` is public. Mixins into Radiante's other classes, or calls to them, can
  break in any version.
- The API keeps working across versions; new calls are added, and changes are listed in the
  changelog.
- Ideas for the API, or something your mod needs that is missing:
  [GitHub Issues](https://github.com/Gabrieli2806/Radiante/issues).
