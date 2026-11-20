package com.g2806.radiante.mixin.world;

import com.g2806.radiante.client.photo.PhotoCinema;
import net.minecraft.client.ClientClockManager;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/**
 * Photo Mode sets a time of day for the photo alone (PhotoCinema): the clocks this client reads answer with it, on
 * the day they are in, so the moon keeps its phase. Nothing is sent to the server and nothing changes for anyone
 * else, which is what lets it work on a server too.
 */
@Mixin(ClientClockManager.ClientClockInstance.class)
public class ClientClockInstanceMixin {

    @Inject(method = "totalTicks", at = @At("RETURN"), cancellable = true)
    private void radiante$photoTime(CallbackInfoReturnable<Long> cir) {
        long photo = PhotoCinema.timeOverride();
        if (photo >= 0L) {
            cir.setReturnValue(Math.floorDiv(cir.getReturnValueJ(), 24000L) * 24000L + photo);
        }
    }
}
