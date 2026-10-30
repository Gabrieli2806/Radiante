package com.g2806.radiante.mixin.world;

import net.minecraft.client.multiplayer.ClientLevel;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

/** The sky's lightning flash timer, which the level keeps private (LightningFlash). */
@Mixin(ClientLevel.class)
public interface ClientLevelAccessor {

    @Accessor("skyFlashTime")
    int radiante$skyFlashTime();
}
