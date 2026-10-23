package com.g2806.radiante.mixin.world;

import com.g2806.radiante.client.render.ModBackdrop;
import com.g2806.radiante.client.render.RadianteRenderer;
import com.mojang.blaze3d.buffers.GpuBufferSlice;
import net.minecraft.client.renderer.SkyRenderer;
import net.minecraft.client.renderer.state.level.SkyRenderState;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/**
 * Minecraft's sky pass, run over the traced picture with nothing of Minecraft's own in it: the sky, sun, moon and
 * stars are traced. What is left is what other mods add to the sky there (ModBackdrop), which lands on the traced
 * sky.
 */
@Mixin(value = SkyRenderer.class, priority = 2000)
public class SkyRendererMixin {

    @Unique
    private boolean radiante$starsDue;

    @Inject(method = {"renderSkyDisc", "renderDarkDisc", "renderSun", "renderMoon", "renderSunriseAndSunset",
        "renderEndSky", "renderEndFlash"}, at = @At("HEAD"), cancellable = true)
    private void radiante$noSkyOverTrace(CallbackInfo ci) {
        if (RadianteRenderer.isOverlayFrame()) {
            ci.cancel();
        }
    }

    // 26.2 port: no SkyRenderer.render; the sky pass calls renderSunMoonAndStars with the star brightness.
    @Inject(method = "renderSunMoonAndStars", at = @At("HEAD"))
    private void radiante$beforeSky(com.mojang.blaze3d.vertex.PoseStack poseStack, float sunAngle, float moonAngle,
        float starAngle, net.minecraft.world.level.MoonPhase moonPhase, float rainBrightness, float starBrightness,
        CallbackInfo ci) {
        // Stars are due when Minecraft would draw them; a mod that draws its own keeps renderStars from running.
        this.radiante$starsDue = starBrightness > 0.0f && RadianteRenderer.isOverlayFrame();
    }

    @Inject(method = "renderStars", at = @At("HEAD"), cancellable = true)
    private void radiante$noStarsOverTrace(CallbackInfo ci) {
        if (RadianteRenderer.isOverlayFrame()) {
            this.radiante$starsDue = false;
            ModBackdrop.setModStars(false);
            ci.cancel();
        }
    }

    @Inject(method = "renderSunMoonAndStars", at = @At("RETURN"))
    private void radiante$afterStars(CallbackInfo ci) {
        if (this.radiante$starsDue) {
            ModBackdrop.setModStars(true);
        }
    }
}
