# Addon settings

Languages: [ES](../es/complementos.md) · **EN**

A mod built on Radiante (an *addon*) can put its options in Radiante's own settings screen instead of
writing a config screen and config file of its own. When at least one addon registers, the settings
get an **Addons** section at the end of the sidebar, with a heading per addon (its icon, name and
version) and its options under it. Without addons nothing changes.

Addon options behave like Radiante's own: they are held until **Apply** or **Done**, **Undo** drops
them, **Reset to Defaults** puts them back to the addon's defaults, the search finds them (or all of
an addon's options by its name), the description panel shows their tooltip, and the keys and a
controller move through them.

## The API

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
