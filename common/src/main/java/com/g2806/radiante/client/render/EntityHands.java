package com.g2806.radiante.client.render;

import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.entity.state.EntityRenderState;
import net.minecraft.client.renderer.state.level.CameraRenderState;
import net.minecraft.client.renderer.state.level.LevelRenderState;
import net.minecraft.world.level.GameType;
import org.joml.Matrix4f;

import static com.g2806.radiante.client.render.EntityManager.*;

/**
 * The first person hands and what they hold, Vivecraft's hands, and the lights of held items: what follows the
 * camera rather than standing in the world.
 *
 * <p>This was part of {@code EntityManager}, which collects everything else that is traced as entities.
 */
public final class EntityHands {

    private EntityHands() {
    }

    /**
     * The collector Vivecraft's hands go into instead of Minecraft's, while the frame is traced; Minecraft's own
     * then draws none of them over the traced picture. Null when Radiante is not tracing this view.
     */
    public static EntityCollector beginVrHands() {
        if (!RadianteRenderer.isOverlayFrame()) {
            return null;
        }
        VR_HANDS.reset();
        configureHeldLights(Minecraft.getInstance(), VR_HANDS);
        vrHandsCaptured = false;
        return VR_HANDS;
    }

    /** Vivecraft's hands were submitted camera-relative for the view being drawn, whose camera is at x, y, z. */
    public static void endVrHands(double x, double y, double z) {
        vrHandsX = x;
        vrHandsY = y;
        vrHandsZ = z;
        vrHandsCaptured = true;
    }

    static void collectVrHands() {
        if (!vrHandsCaptured || !com.g2806.radiante.client.compat.vivecraft.VivecraftCompat.isVrActive()) {
            vrHandsCaptured = false;
            return;
        }
        // Seen by camera rays and the first bounce (reflections), but not by shadow rays: the held light sits
        // inside the held item, which would otherwise shade it out.
        addPending(VR_HANDS, VR_HANDS_ID, vrHandsX, vrHandsY, vrHandsZ, RAY_TRACING_PARTICLE, false);
    }

    /**
     * Whether the first person hands are drawn this frame. The world uniform says so too: the ray generation traces a
     * second camera ray per pixel for the hands, and with none to find (spectator, F1, third person) it cost a
     * quarter of a millisecond for nothing.
     */
    public static boolean handsDrawn(Minecraft minecraft, LevelRenderState levelRenderState) {
        CameraRenderState cameraState = levelRenderState.cameraRenderState;
        return PlayerFrame.hasPlayer() && !minecraft.gameRenderer.gameRenderState().guiRenderState.isHudHidden
            && minecraft.options.getCameraType().isFirstPerson() && !cameraState.entityRenderState.isSleeping
            && minecraft.gameMode != null && minecraft.gameMode.getPlayerMode() != GameType.SPECTATOR
            // In VR, Vivecraft draws the hands at the controllers; vanilla's would sit in front of the face.
            && !com.g2806.radiante.client.compat.vivecraft.VivecraftCompat.isVrActive();
    }

    /** The held items and arms are submitted separately from the world and follow the camera. */
    static void collectHands(Minecraft minecraft, LevelRenderState levelRenderState,
        CameraRenderState cameraState) {
        if (!handsDrawn(minecraft, levelRenderState)) {
            return;
        }

        COLLECTOR.reset();
        POSE_STACK.setIdentity();
        // Minecraft poses the hands in its view space; rotate them into world orientation around the camera.
        POSE_STACK.mulPose(new Matrix4f(cameraState.viewRotationMatrix).invert());
        COLLECTOR.held(true);
        // The previous stack is still rendered while its equip animation lowers it after a slot change.
        var hands = (com.g2806.radiante.mixin.render.ItemInHandRendererAccessor)
            minecraft.gameRenderer.itemInHandRenderer;
        configureHeldLights(minecraft, COLLECTOR, hands.radiante$mainHandItem(), hands.radiante$offHandItem());
        if (minecraft.player != null) {
            COLLECTOR.heldGlass(isGlassItem(minecraft.player.getMainHandItem())
                || isGlassItem(minecraft.player.getOffhandItem()));
        }

        try {
            PlayerFrame.submitHands(POSE_STACK, COLLECTOR);
        } catch (RuntimeException e) {
            return;
        }

        addPending(HAND_ID, cameraState.pos.x(), cameraState.pos.y(), cameraState.pos.z(), RAY_TRACING_HAND);
    }

    static void configureHeldLights(Minecraft minecraft, EntityCollector collector) {
        if (minecraft.player == null) {
            return;
        }
        configureHeldLights(minecraft, collector, minecraft.player.getMainHandItem(), minecraft.player.getOffhandItem());
    }

    static void configureHeldLights(Minecraft minecraft, EntityCollector collector, EntityRenderState state) {
        if (state instanceof net.minecraft.client.renderer.entity.state.ArmedEntityRenderState armed) {
            collector.heldLights(HeldLight.isLightSource(armed.leftHandItemStack),
                HeldLight.isLightSource(armed.rightHandItemStack));
        } else {
            configureHeldLights(minecraft, collector);
        }
    }

    static void configureHeldLights(Minecraft minecraft, EntityCollector collector,
        net.minecraft.world.item.ItemStack mainStack, net.minecraft.world.item.ItemStack offStack) {
        if (minecraft.player == null) return;
        boolean main = HeldLight.isLightSource(mainStack);
        boolean off = HeldLight.isLightSource(offStack);
        boolean mainLeft = minecraft.player.getMainArm() == net.minecraft.world.entity.HumanoidArm.LEFT;
        collector.heldLights(mainLeft ? main : off, mainLeft ? off : main);
    }
}
