package com.g2806.radiante.client.render;

import com.g2806.radiante.mixin.world.GameRendererSpinAccessor;
import com.mojang.blaze3d.vertex.PoseStack;
import net.minecraft.client.Minecraft;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.client.renderer.SubmitNodeCollector;
import net.minecraft.client.renderer.entity.state.AvatarRenderState;
import net.minecraft.world.effect.MobEffects;
import org.jspecify.annotations.Nullable;

/**
 * Minecraft 26.2 port: what 26.3 extracts into LevelRenderState.playerRenderState and a few fields of the camera
 * state (world and camera entity partial ticks, first person), read from the player and the game renderer directly.
 * Everything on main that reads those fields reads them through here on this branch.
 */
public final class PlayerFrame {

    private PlayerFrame() {
    }

    public static float worldPartialTicks() {
        return Minecraft.getInstance().getDeltaTracker().getGameTimeDeltaPartialTick(false);
    }

    public static float cameraEntityPartialTicks() {
        Minecraft minecraft = Minecraft.getInstance();
        return minecraft.gameRenderer.mainCamera().getCameraEntityPartialTicks(minecraft.getDeltaTracker());
    }

    public static boolean isFirstPerson() {
        return Minecraft.getInstance().options.getCameraType().isFirstPerson();
    }

    public static boolean hasPlayer() {
        Minecraft minecraft = Minecraft.getInstance();
        return minecraft.player != null && minecraft.level != null;
    }

    /** The local player's model state, extracted the way 26.3's LevelExtractor does it; null without one. */
    public static @Nullable AvatarRenderState avatar() {
        Minecraft minecraft = Minecraft.getInstance();
        LocalPlayer player = minecraft.player;
        if (player == null || minecraft.level == null) {
            return null;
        }
        float partialTick = minecraft.getDeltaTracker()
            .getGameTimeDeltaPartialTick(!minecraft.level.tickRateManager().isEntityFrozen(player));
        return minecraft.getEntityRenderDispatcher().extractEntity(player, partialTick)
            instanceof AvatarRenderState state ? state : null;
    }

    public static float portalEffectIntensity() {
        LocalPlayer player = Minecraft.getInstance().player;
        if (player == null) {
            return 0.0f;
        }
        float t = worldPartialTicks();
        return player.oPortalEffectIntensity + (player.portalEffectIntensity - player.oPortalEffectIntensity) * t;
    }

    public static float nauseaEffectIntensity() {
        LocalPlayer player = Minecraft.getInstance().player;
        return player == null ? 0.0f : player.getEffectBlendFactor(MobEffects.NAUSEA, worldPartialTicks());
    }

    /** In degrees, as 26.3's LocalPlayer.getSpinningEffectAngle. */
    public static float spinningEffectAngle() {
        GameRendererSpinAccessor spin = (GameRendererSpinAccessor) Minecraft.getInstance().gameRenderer;
        return spin.radiante$spinningEffectTime() + worldPartialTicks() * spin.radiante$spinningEffectSpeed();
    }

    /** 26.3's FirstPersonHandsAndItemsRenderer.submitHandsWithItems, on 26.2's ItemInHandRenderer. */
    public static void submitHands(PoseStack poseStack, SubmitNodeCollector collector) {
        Minecraft minecraft = Minecraft.getInstance();
        LocalPlayer player = minecraft.player;
        if (player == null) {
            return;
        }
        float partialTicks = cameraEntityPartialTicks();
        minecraft.gameRenderer.itemInHandRenderer.submitHandsWithItems(partialTicks, poseStack, collector, player,
            minecraft.getEntityRenderDispatcher().getPackedLightCoords(player, partialTicks));
    }
}
