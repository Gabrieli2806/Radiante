package com.terraformersmc.modmenu.api;

/** Compile-time copy of Mod Menu's interface (only what Radiante uses); Mod Menu supplies the real one at runtime. */
public interface ModMenuApi {
    default ConfigScreenFactory<?> getModConfigScreenFactory() {
        return screen -> null;
    }
}
