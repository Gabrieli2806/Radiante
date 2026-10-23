package com.g2806.radiante.mixin.debug;

import com.g2806.radiante.client.gui.RadianteDebugEntries;
import net.minecraft.client.gui.components.debug.DebugScreenEntries;
import net.minecraft.client.gui.components.debug.DebugScreenEntry;
import net.minecraft.core.Registry;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/**
 * Debug entries are a client registry, filled once at bootstrap and frozen before mods start; Radiante's are added
 * while Minecraft fills it, the same on every loader.
 */
@Mixin(DebugScreenEntries.class)
public class DebugScreenEntriesMixin {

    @Inject(method = "bootstrap", at = @At("RETURN"))
    private static void radiante$registerEntries(Registry<DebugScreenEntry> registry,
        CallbackInfoReturnable<Object> cir) {
        RadianteDebugEntries.register(registry);
    }
}
