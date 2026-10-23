package com.g2806.radiante.mixin.world;

import com.g2806.radiante.client.render.RadianteRenderer;
import net.minecraft.client.renderer.chunk.ChunkSectionsToRender;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/**
 * Minecraft's terrain draws, stopped while its level render runs over the traced world (LevelRendererSkipMixin). It
 * has no terrain of its own then, but mods hang draws of world objects on the end of these - Physics Mod its debris
 * - and Radiante traces those itself; drawn here as well they would show twice.
 */
@Mixin(value = ChunkSectionsToRender.class, priority = 500)
public class ChunkSectionsToRenderMixin {

    // 26.2 port: renderGroup opens its own render pass (26.3 is handed one), and translucent terrain is a
    // renderGroup of its own rather than renderOit.
    @Inject(method = "renderGroup", at = @At("HEAD"), cancellable = true)
    private void radiante$noTerrainOverTrace(net.minecraft.client.renderer.chunk.ChunkSectionLayerGroup group,
        com.mojang.blaze3d.textures.GpuSampler sampler, CallbackInfo ci) {
        if (RadianteRenderer.isOverlayFrame()) {
            if (group == net.minecraft.client.renderer.chunk.ChunkSectionLayerGroup.OPAQUE) {
                // Where the terrain would cover what was drawn before it (a mod's sky), the traced world does.
                com.mojang.blaze3d.pipeline.RenderTarget target = group.outputTarget();
                try (com.mojang.blaze3d.systems.RenderPass pass = com.mojang.blaze3d.systems.RenderSystem.getDevice()
                    .createCommandEncoder().createRenderPass(() -> "Radiante traced world", target.getColorTextureView(),
                        java.util.Optional.empty(), target.getDepthTextureView(), java.util.OptionalDouble.empty())) {
                    com.g2806.radiante.client.render.ModBackdrop.restore(pass);
                }
            }
            ci.cancel();
        }
    }
}
