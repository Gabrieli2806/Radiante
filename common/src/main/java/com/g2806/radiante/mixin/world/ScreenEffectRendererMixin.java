package com.g2806.radiante.mixin.world;

import com.g2806.radiante.client.render.RadianteRenderer;
import com.llamalad7.mixinextras.injector.v2.WrapWithCondition;
import net.minecraft.client.renderer.ScreenEffectRenderer;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;

/**
 * Under water Minecraft darkens the view with a texture of its own. The traced water already is the view under water
 * (its colour, depth and light), and the texture on top made it murkier still: left out while tracing. The other
 * screen effects (fire, the block the head is in, the totem) are drawn (GameRendererMixin.radiante$screenEffects).
 */
@Mixin(ScreenEffectRenderer.class)
public class ScreenEffectRendererMixin {

    @WrapWithCondition(method = "submit", at = @At(value = "INVOKE",
        target = "Lnet/minecraft/client/renderer/ScreenEffectRenderer;submitWater(Lnet/minecraft/client/renderer/state/level/PlayerRenderState$WaterOverlay;Lcom/mojang/blaze3d/vertex/PoseStack;Lnet/minecraft/client/renderer/SubmitNodeCollector;)V"))
    private boolean radiante$noWaterOverlayWhenTracing(
        net.minecraft.client.renderer.state.level.PlayerRenderState.WaterOverlay overlay,
        com.mojang.blaze3d.vertex.PoseStack poseStack, net.minecraft.client.renderer.SubmitNodeCollector collector) {
        return !RadianteRenderer.isRayTracingEnabled();
    }
}
