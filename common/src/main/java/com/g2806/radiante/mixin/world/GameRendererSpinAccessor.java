package com.g2806.radiante.mixin.world;

import net.minecraft.client.renderer.GameRenderer;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

/** 26.2 keeps the nausea and portal wobble on the game renderer; 26.3 moved it to the player render state. */
@Mixin(GameRenderer.class)
public interface GameRendererSpinAccessor {

    @Accessor("spinningEffectTime")
    float radiante$spinningEffectTime();

    @Accessor("spinningEffectSpeed")
    float radiante$spinningEffectSpeed();
}
