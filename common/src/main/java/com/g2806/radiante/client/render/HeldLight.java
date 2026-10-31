package com.g2806.radiante.client.render;

import net.minecraft.client.Minecraft;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.world.InteractionHand;
import net.minecraft.world.entity.HumanoidArm;
import net.minecraft.world.item.BlockItem;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.Items;
import net.minecraft.world.phys.Vec3;
import org.joml.Vector3f;
import org.joml.Vector4f;

/**
 * The light of whatever the player holds: a torch, lantern, glowstone, a lava bucket. Vanilla has no such light;
 * the item only glows itself. Here it becomes a small point light where the item is held, bright in proportion
 * to the block's own light level, which the shaders sample like a block light.
 */
public final class HeldLight {

    /**
     * Radiance of a light level 15 item. A torch in the hand lit a cave several times wider than the same torch set
     * down; both now meet in the middle (BedrockPointLights.STRENGTH).
     */
    private static final float STRENGTH = 0.35f;
    /** How far a level 15 light reaches, in blocks; vanilla light fades out over the same distance. */
    private static final float REACH = 15.0f;

    private static final Vector4f NONE = new Vector4f(0.0f);

    private HeldLight() {
    }

    /**
     * Camera-relative position (xyz) and reach (w) of the light held in one hand; zero when that hand holds nothing
     * lit. Each hand is its own light, so a torch in each hand lights both sides. The light sits where the item is:
     * in first person where vanilla draws the hand holding it, lower right or lower left of the view; seen from
     * outside, at that hand beside the player's body; in VR, at the controller.
     */
    public static Vector4f position(Minecraft minecraft, Vec3 camera, float partialTicks, InteractionHand hand) {
        LocalPlayer player = minecraft.player;
        if (player == null) {
            return NONE;
        }
        int level = levelOf(player.getItemInHand(hand));
        if (level <= 0) {
            return NONE;
        }
        Vec3 at = handPosition(minecraft, player, hand == InteractionHand.MAIN_HAND, partialTicks);
        float reach = pointLightOf(player.getItemInHand(hand)) != null ? POINT_LIGHT_REACH : REACH * level / 15.0f;
        return new Vector4f((float) (at.x - camera.x), (float) (at.y - camera.y), (float) (at.z - camera.z), reach);
    }

    /** Where the hand holding an item is, in the world. */
    private static Vec3 handPosition(Minecraft minecraft, LocalPlayer player, boolean mainHand, float partialTicks) {
        // In VR the item is in the controller's hand, wherever that is.
        Vec3 vrHand = com.g2806.radiante.client.compat.vivecraft.VivecraftCompat.handPosition(
            mainHand ? InteractionHand.MAIN_HAND : InteractionHand.OFF_HAND);
        if (vrHand != null) {
            return vrHand;
        }
        boolean rightArm = (player.getMainArm() == HumanoidArm.RIGHT) == mainHand;
        double side = rightArm ? 1.0 : -1.0;
        Vec3 eye = player.getEyePosition(partialTicks);
        Vec3 at;
        if (minecraft.options.getCameraType().isFirstPerson()) {
            // Vanilla places a held item about half a block to the side, half down and three quarters ahead of
            // the eye, turning with the view.
            Vec3 forward = player.getViewVector(partialTicks);
            // From the yaw alone, so looking straight up or down still has a side.
            Vec3 right = Vec3.directionFromRotation(0.0f, player.getViewYRot(partialTicks)).cross(UP).normalize();
            Vec3 up = right.cross(forward);
            at = eye.add(forward.scale(0.7)).add(right.scale(0.45 * side)).add(up.scale(-0.4));
        } else {
            // A hanging arm's hand, beside the body and a little ahead of it.
            float bodyYaw = net.minecraft.util.Mth.rotLerp(partialTicks, player.yBodyRotO, player.yBodyRot);
            Vec3 forward = Vec3.directionFromRotation(0.0f, bodyYaw);
            Vec3 right = forward.cross(UP).normalize();
            at = eye.add(0.0, -0.95 * player.getScale(), 0.0).add(right.scale(0.4 * side)).add(forward.scale(0.15));
        }
        return at;
    }

    private static final Vec3 UP = new Vec3(0.0, 1.0, 0.0);

    /** Radiance (rgb) and 1 in w while the hand holds a lit item, all zero otherwise. */
    public static Vector4f color(Minecraft minecraft, InteractionHand hand) {
        LocalPlayer player = minecraft.player;
        if (player == null) {
            return NONE;
        }
        ItemStack stack = player.getItemInHand(hand);
        int level = levelOf(stack);
        if (level <= 0) {
            return NONE;
        }
        int[] pointLight = pointLightOf(stack);
        if (pointLight != null) {
            // The light the same block casts set down (BedrockPointLights), on the held light's scale: the shaders
            // multiply a placed one by (100 / 12) and the Emission Brightness, a held one by 16 and the Held Light
            // Brightness, both 12 by default.
            float scale = BedrockPointLights.CAST_STRENGTH * (100.0f / 12.0f) / 16.0f / 255.0f;
            return new Vector4f(pointLight[0] * scale, pointLight[1] * scale, pointLight[2] * scale, 1.0f);
        }
        return new Vector4f(tintOf(stack).mul(STRENGTH * level / 15.0f), 1.0f);
    }

    /**
     * The colour of the light the held block casts when set down, when that is one of Bedrock's point lights and they
     * are cast; null otherwise. A torch, lantern or end rod then lights the same in the hand as on the
     * ground: the same colour, strength and fall-off, so walking into a cave and setting it down changes nothing.
     */
    private static int[] pointLightOf(ItemStack stack) {
        if (!com.g2806.radiante.client.pipeline.Pipeline.castsBedrockGlow()) {
            return null;
        }
        int[] pack = BedrockPointLights.packHeldColour(stack.getItem());
        if (pack != null) {
            // A shield with a torch on it (an enabled Bedrock pack's attachable) lights as that torch would.
            return pack;
        }
        if (!(stack.getItem() instanceof BlockItem blockItem)) {
            return null;
        }
        return BedrockPointLights.colourOf(blockItem.getBlock());
    }

    /** Light level of a torch, for an item an enabled pack makes a light in the hand. */
    private static final int TORCH_LEVEL = 14;

    /**
     * A set down light has no reach but the inverse square law; the held one's fade (sampleOneHeldLight) starts this
     * far out, so within the 12 blocks a torch visibly lights it takes off under a few percent.
     */
    private static final float POINT_LIGHT_REACH = 40.0f;

    /** Whether this item has a dedicated held light, including while that light is switched off. */
    public static boolean isLightSource(ItemStack stack) {
        return levelOf(stack) > 0;
    }

    private static int levelOf(ItemStack stack) {
        if (stack.isEmpty()) {
            return 0;
        }
        if (stack.is(Items.LAVA_BUCKET)) {
            return 15;
        }
        if (BedrockPointLights.packHeldColour(stack.getItem()) != null) {
            return TORCH_LEVEL;
        }
        if (stack.getItem() instanceof BlockItem blockItem) {
            return blockItem.getBlock().defaultBlockState().getLightEmission();
        }
        return 0;
    }

    private static Vector3f tintOf(ItemStack stack) {
        int[] pack = BedrockPointLights.packHeldColour(stack.getItem());
        if (pack != null) {
            return new Vector3f(pack[0] / 255.0f, pack[1] / 255.0f, pack[2] / 255.0f);
        }
        String id = BuiltInRegistries.ITEM.getKey(stack.getItem()).getPath();
        if (id.contains("soul")) {
            return new Vector3f(0.45f, 0.85f, 1.0f);
        }
        if (id.contains("redstone")) {
            return new Vector3f(1.0f, 0.25f, 0.15f);
        }
        if (id.contains("sea_lantern") || id.contains("end_rod") || id.contains("froglight")
            || id.contains("beacon") || id.contains("conduit")) {
            return new Vector3f(0.9f, 0.95f, 1.0f);
        }
        if (id.contains("lava") || id.contains("magma")) {
            return new Vector3f(1.0f, 0.5f, 0.2f);
        }
        return new Vector3f(1.0f, 0.7f, 0.35f);
    }
}
