package com.g2806.radiante.mixin.render;

import com.g2806.radiante.client.compat.emfetf.RadianteModelFeatureRenderer;
import com.mojang.blaze3d.vertex.VertexConsumer;
import net.minecraft.client.renderer.feature.RenderTypeFeatureRenderer;
import net.minecraft.client.renderer.rendertype.RenderType;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/**
 * Redirects {@link RenderTypeFeatureRenderer#getVertexBuilder} into Radiante's {@link RadianteModelFeatureRenderer}
 * when replaying entity models for ray tracing, while letting ETF's priority-800 {@code HEAD} and {@code RETURN}
 * injections swap variant {@link RenderType}s and attach {@code ETFVertexConsumer} metadata around it.
 */
@Mixin(RenderTypeFeatureRenderer.class)
public abstract class RenderTypeFeatureRendererMixin {

    @Inject(method = "getVertexBuilder", at = @At("HEAD"), cancellable = true)
    private void radiante$redirectVertexBuilder(RenderType renderType, CallbackInfoReturnable<VertexConsumer> cir) {
        if ((Object) this instanceof RadianteModelFeatureRenderer radiante) {
            cir.setReturnValue(radiante.vertexBuilderFor(renderType));
        }
    }
}
