# Making mods for Radiante

Languages: [ES](../es/api.md) · **EN**

Radiante has a small public API for other mods: everything in the package `com.g2806.radiante.api`.
The rest of Radiante is internal and can change in any version; build only on this package.

| Class | What it is for |
|---|---|
| `RadianteApi` | Whether the world is being ray traced right now, and where the sun and moon Radiante draws are |
| `RadianteGeometry` | Putting your own geometry (debris, effects, markers) into the ray traced world |
| `RadianteAddons` | Putting your mod's options in Radiante's settings screen, under **Addons** ([details](addons.md)) |

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

Your mod's options in Radiante's settings screen, stored by Radiante, read from any thread:

```java
RadianteAddons.Addon addon = RadianteAddons.register("mymod", Component.translatable("mymod.name"));
RadianteAddons.Setting<Boolean> showRays = addon.toggle("show_rays",
    Component.translatable("mymod.show_rays"), Component.translatable("mymod.show_rays.tooltip"), false);

if (showRays.get()) { ... }
```

The heading shows your mod's icon from its metadata (`addon.icon(...)` names another one). The full
guide, with lists of values and sliders: [Addon settings](addons.md).

## Stability

- Only `com.g2806.radiante.api` is public. Mixins into Radiante's other classes, or calls to them, can
  break in any version.
- The API keeps working across versions; new calls are added, and changes are listed in the
  changelog.
- Ideas for the API, or something your mod needs that is missing:
  [GitHub Issues](https://github.com/Gabrieli2806/Radiante/issues).
