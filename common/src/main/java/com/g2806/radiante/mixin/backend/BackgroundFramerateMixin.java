package com.g2806.radiante.mixin.backend;

import com.g2806.radiante.client.option.Options;
import com.g2806.radiante.client.render.RadianteRenderer;
import com.llamalad7.mixinextras.injector.ModifyReturnValue;
import com.mojang.blaze3d.platform.FramerateLimitTracker;
import net.minecraft.client.Minecraft;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;

/**
 * The frame rate while the game's window is in the background: still on screen, but another window has the focus.
 * Minecraft slows down only a minimized window or an idle player, so a path traced world on a second screen, or
 * behind a browser, kept the graphics card at full load, its heat and fans too. Capped at Background Frame Rate
 * (Options.backgroundFrameRate) while ray tracing is on; nothing while the development scripts drive the game.
 */
@Mixin(FramerateLimitTracker.class)
public abstract class BackgroundFramerateMixin {

    @Shadow
    @Final
    private Minecraft minecraft;

    @ModifyReturnValue(method = "getFramerateLimit", at = @At("RETURN"))
    private int radiante$capInBackground(int limit) {
        int cap = Options.backgroundFrameRate;
        if (cap <= 0 || this.minecraft.isWindowActive() || !RadianteRenderer.isRayTracingEnabled()
            || com.g2806.radiante.client.DevAutomation.isScripted()) {
            return limit;
        }
        return Math.min(limit, cap);
    }
}
