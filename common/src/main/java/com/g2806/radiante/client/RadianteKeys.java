package com.g2806.radiante.client;

import com.mojang.blaze3d.platform.InputConstants;
import net.minecraft.client.KeyMapping;
import net.minecraft.resources.Identifier;

/** Radiante's key bindings, in their own section of the Controls screen. Each loader registers them. */
public final class RadianteKeys {

    public static final KeyMapping.Category CATEGORY =
        KeyMapping.Category.register(Identifier.fromNamespaceAndPath("radiante", "radiante"));

    public static final KeyMapping OPEN_SETTINGS =
        new KeyMapping("key.radiante.open_settings", InputConstants.KEY_F6, CATEGORY);
    // Unbound by default too: Photo Mode is for whoever looks for it in Controls, and F9 is a key other mods take.
    public static final KeyMapping PHOTO_MODE =
        new KeyMapping("key.radiante.photo_mode", InputConstants.UNKNOWN.getValue(), CATEGORY);
    // Unbound by default: switching rebuilds the whole world, too costly to sit on a key pressed by accident, and
    // the settings screen has the same switch.
    public static final KeyMapping TOGGLE_RAY_TRACING =
        new KeyMapping("key.radiante.toggle_ray_tracing", InputConstants.UNKNOWN.getValue(), CATEGORY);

    // Unbound by default: opens Before / After straight from the game, to compare Radiante off and on or two packs.
    public static final KeyMapping COMPARE =
        new KeyMapping("key.radiante.compare", InputConstants.UNKNOWN.getValue(), CATEGORY);

    /** Switches Held Item Light on and off; unbound by default. */
    public static final KeyMapping TOGGLE_HELD_LIGHT =
        new KeyMapping("key.radiante.toggle_held_light", InputConstants.UNKNOWN.getValue(), CATEGORY);

    public static final KeyMapping[] ALL = {OPEN_SETTINGS, PHOTO_MODE, TOGGLE_RAY_TRACING, COMPARE, TOGGLE_HELD_LIGHT};

    private RadianteKeys() {
    }
}
