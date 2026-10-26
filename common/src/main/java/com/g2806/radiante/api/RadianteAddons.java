package com.g2806.radiante.api;

import com.g2806.radiante.client.addon.AddonRegistry;
import java.util.List;
import java.util.function.Consumer;
import java.util.function.Function;
import net.minecraft.network.chat.Component;

/**
 * The public addon API: lets another mod put its own options in Radiante's settings screen, under an Addons section
 * with a heading per addon. Radiante draws them like its own options (Apply, Undo, Reset to Defaults, search, the
 * description beside the list, keyboard and controller), and stores their values in
 * {@code radiante/addons/<id>.properties}, so an addon needs no config code of its own.
 *
 * <p>Register once, from the addon's (client) initialiser, on any loader:
 *
 * <pre>{@code
 * RadianteAddons.Addon addon = RadianteAddons.register("mymod", Component.translatable("mymod.name"));
 * RadianteAddons.Setting<Boolean> debugRays = addon.toggle("debug_rays",
 *     Component.translatable("mymod.debug_rays"), Component.translatable("mymod.debug_rays.tooltip"), false);
 * ...
 * if (debugRays.get()) { ... }
 * }</pre>
 *
 * <p>Values can be read from any thread. A changed value is stored when the player applies the settings; change
 * listeners run then, on the client thread. Mistakes (a bad or repeated id or key, a listener that throws) are logged
 * and never stop Radiante's screen: a setting that cannot be shown still answers {@link Setting#get()} with its
 * default. Radiante is client-side only; on a dedicated server, do not call this.
 *
 * <p>This class and its nested interfaces are the whole addon API; it keeps working across Radiante versions.
 */
public final class RadianteAddons {

    private RadianteAddons() {
    }

    /**
     * Adds an addon to the settings screen, under {@code title}. {@code id} is the addon's mod id (lowercase letters,
     * digits, {@code _ - .}): it names the file the values are stored in, and the addon's version is looked up by it
     * to show beside the title. Registering the same id twice logs a warning and hands back an addon that is not shown.
     */
    public static Addon register(String id, Component title) {
        return AddonRegistry.register(id, title);
    }

    /** An addon's place in the settings: its options are listed under its title in the order they are added. */
    public interface Addon {

        /** The id it was registered with. */
        String id();

        /**
         * An on/off option, drawn as a checkbox. {@code tooltip} (may be null) is shown beside the list: say what it
         * does, and what turning it on or off changes.
         */
        Setting<Boolean> toggle(String key, Component title, Component tooltip, boolean defaultValue);

        /** A choice among {@code values}, stepped through with a click; each value is shown as it is written. */
        Setting<String> cycle(String key, Component title, Component tooltip, List<String> values, String defaultValue);

        /** As {@link #cycle(String, Component, Component, List, String)}, with each value shown as {@code label} says. */
        Setting<String> cycle(String key, Component title, Component tooltip, List<String> values, String defaultValue,
                              Function<String, Component> label);

        /** A whole number from {@code min} to {@code max} in steps of {@code step}, drawn as a slider. */
        Setting<Integer> slider(String key, Component title, Component tooltip, int min, int max, int step,
                                int defaultValue);
    }

    /** One option of an addon. */
    public interface Setting<T> {

        /** The value in use: the stored one, or the default until the player changes it. Safe from any thread. */
        T get();

        /** The default given when the option was added. */
        T defaultValue();

        /** The key the option was added with; also its name in the stored file. */
        String key();

        /**
         * Calls {@code listener} with the new value whenever the player applies a change to this option (on the
         * client thread). Returns this setting, for chaining.
         */
        Setting<T> onChange(Consumer<T> listener);
    }
}
