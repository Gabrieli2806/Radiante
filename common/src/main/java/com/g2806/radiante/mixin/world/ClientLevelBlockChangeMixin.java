package com.g2806.radiante.mixin.world;

import com.g2806.radiante.client.render.RainExposure;
import net.minecraft.client.multiplayer.ClientLevel;
import net.minecraft.core.BlockPos;
import net.minecraft.world.level.block.state.BlockState;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/**
 * Tells the rain and snow map of every block that changes, as the world renderer is told: a block broken and put
 * back between two of its reads looked to it as if nothing had changed (RainExposure.onBlockChanged).
 */
@Mixin(ClientLevel.class)
public class ClientLevelBlockChangeMixin {

    @Inject(method = "sendBlockUpdated", at = @At("HEAD"))
    private void radiante$onBlockChanged(BlockPos pos, BlockState oldState, BlockState newState, int flags,
        CallbackInfo ci) {
        RainExposure.onBlockChanged(pos, oldState, newState);
    }
}
