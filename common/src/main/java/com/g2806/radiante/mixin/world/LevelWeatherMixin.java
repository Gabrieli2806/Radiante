package com.g2806.radiante.mixin.world;

import com.g2806.radiante.client.photo.PhotoCinema;
import net.minecraft.world.level.Level;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/**
 * Photo Mode sets a weather for the photo alone (PhotoCinema): the client's level answers with it, so the sky, the
 * rain and the wet ground follow. The server's levels (a singleplayer world's own among them) keep theirs.
 */
@Mixin(Level.class)
public abstract class LevelWeatherMixin {

    @Shadow
    public abstract boolean isClientSide();

    @Inject(method = "getRainLevel", at = @At("RETURN"), cancellable = true)
    private void radiante$photoRain(float partialTicks, CallbackInfoReturnable<Float> cir) {
        if (isClientSide()) {
            float photo = PhotoCinema.rainOverride();
            if (photo >= 0.0f) {
                cir.setReturnValue(photo);
            }
        }
    }

    @Inject(method = "getThunderLevel", at = @At("RETURN"), cancellable = true)
    private void radiante$photoThunder(float partialTicks, CallbackInfoReturnable<Float> cir) {
        if (isClientSide()) {
            float photo = PhotoCinema.thunderOverride();
            if (photo >= 0.0f) {
                cir.setReturnValue(photo);
            }
        }
    }
}
