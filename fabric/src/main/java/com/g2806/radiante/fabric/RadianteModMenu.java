package com.g2806.radiante.fabric;

import com.g2806.radiante.client.gui.RadianteOptionsScreen;
import com.terraformersmc.modmenu.api.ConfigScreenFactory;
import com.terraformersmc.modmenu.api.ModMenuApi;
import net.minecraft.client.Minecraft;

/**
 * Mod Menu's "Configure" button opens Radiante's settings. Loaded only when Mod Menu is installed (its entrypoint);
 * the interfaces are compiled against a copy of theirs (src/modmenuStubs) and come from Mod Menu at runtime.
 */
public final class RadianteModMenu implements ModMenuApi {

    @Override
    public ConfigScreenFactory<?> getModConfigScreenFactory() {
        return parent -> new RadianteOptionsScreen(parent, Minecraft.getInstance().options);
    }
}
