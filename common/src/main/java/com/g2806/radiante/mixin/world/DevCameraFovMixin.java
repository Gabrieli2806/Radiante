package com.g2806.radiante.mixin.world;

import net.minecraft.client.Camera;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/**
 * Development: the field of view a camera path asks for (DevCamera), in fractions of a degree so a zoom is smooth;
 * the option itself only takes whole degrees. Nothing changes unless a path with a FOV column is playing.
 */
@Mixin(Camera.class)
public abstract class DevCameraFovMixin {

    @Inject(method = "calculateFov", at = @At("RETURN"), cancellable = true)
    private void radiante$devFov(float partialTicks, CallbackInfoReturnable<Float> cir) {
        double fov = com.g2806.radiante.client.DevCamera.fovOverride();
        if (!Double.isNaN(fov)) {
            cir.setReturnValue((float) fov);
        }
    }
}
