package com.terraformersmc.modmenu.api;

import net.minecraft.client.gui.screens.Screen;

/** Compile-time copy of Mod Menu's interface (only what Radiante uses); Mod Menu supplies the real one at runtime. */
@FunctionalInterface
public interface ConfigScreenFactory<S extends Screen> {
    S create(Screen parent);
}
