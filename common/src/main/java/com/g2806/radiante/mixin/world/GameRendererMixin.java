package com.g2806.radiante.mixin.world;

import com.g2806.radiante.client.render.ChunkManager;
import com.g2806.radiante.client.render.RadianteRenderer;
import com.llamalad7.mixinextras.injector.ModifyExpressionValue;
import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.GameRenderer;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;

@Mixin(GameRenderer.class)
public abstract class GameRendererMixin {

    /** Frames of traced world before the world's picture is worth taking; the loading screen is gone by then. */
    private static final int WORLD_ICON_MIN_FRAMES = 120;

    @Shadow
    public abstract net.minecraft.client.renderer.state.GameRenderState gameRenderState();

    @Shadow
    @Final
    private net.minecraft.client.renderer.DebugCrosshairRenderer debugCrosshairRenderer;

    @Shadow
    @Final
    private net.minecraft.client.renderer.fog.FogRenderer fogRenderer;

    @Shadow
    @Final
    private com.mojang.blaze3d.pipeline.RenderTarget mainRenderTarget;

    @Shadow
    @Final
    private net.minecraft.client.renderer.Projection hudProjection;

    @Shadow
    @Final
    private net.minecraft.client.renderer.ProjectionMatrixBuffer hud3dProjectionMatrixBuffer;

    @Shadow
    @Final
    private net.minecraft.client.renderer.ScreenEffectRenderer screenEffectRenderer;

    @Shadow
    @Final
    private net.minecraft.client.renderer.feature.FeatureRenderDispatcher featureRenderDispatcher;

    @Shadow
    @Final
    private net.minecraft.client.renderer.SubmitNodeStorage handAndScreenSubmitNodeStorage;

    /**
     * What Minecraft lays over the view from inside the camera: the flames when the player burns, the block the head is
     * in, the totem's flash. They are drawn in the pass that draws the hand, which is skipped while tracing because the
     * hand is traced, so they were never seen; they are drawn here the same way, over the traced picture. (The water
     * overlay is left out, ScreenEffectRendererMixin: the traced water is the view under water.)
     */
    private void radiante$screenEffects(float partialTicks) {
        net.minecraft.client.renderer.state.GameRenderState state = this.gameRenderState();
        if (this.mainRenderTarget.getDepthTexture() == null) {
            return;
        }
        net.minecraft.client.renderer.state.level.CameraRenderState cameraState =
            state.levelRenderState.cameraRenderState;
        this.hudProjection.setupPerspective(0.05F, 100.0F, cameraState.hudFov,
            state.windowRenderState.width, state.windowRenderState.height);
        com.mojang.blaze3d.systems.RenderSystem.setProjectionMatrix(
            this.hud3dProjectionMatrixBuffer.getBuffer(this.hudProjection),
            com.mojang.blaze3d.ProjectionType.PERSPECTIVE);
        // In front of everything, as in vanilla, where this pass starts from a cleared depth.
        com.mojang.blaze3d.systems.RenderSystem.getDevice().createCommandEncoder()
            .clearDepthTexture(this.mainRenderTarget.getDepthTexture(), 0.0);
        this.screenEffectRenderer.submit(state.optionsRenderState.cameraType.isFirstPerson(),
            cameraState.entityRenderState.isSleeping, partialTicks, this.handAndScreenSubmitNodeStorage,
            state.guiRenderState.isHudHidden);
        this.featureRenderDispatcher.renderAllFeatures(this.handAndScreenSubmitNodeStorage);
    }

    /**
     * The three coloured axes F3 puts at the crosshair. Minecraft draws them at the end of the pass that draws the
     * hand, which is skipped while tracing because the hand is traced; they are drawn here the same way, over the
     * traced picture.
     */
    private void radiante$debugCrosshair() {
        net.minecraft.client.renderer.state.GameRenderState state = this.gameRenderState();
        if (!state.levelRenderState.render3dCrosshair || !state.optionsRenderState.cameraType.isFirstPerson()
            || state.guiRenderState.isHudHidden || this.mainRenderTarget.getDepthTexture() == null) {
            return;
        }
        net.minecraft.client.renderer.state.level.CameraRenderState cameraState =
            state.levelRenderState.cameraRenderState;
        this.hudProjection.setupPerspective(0.05F, cameraState.depthFar, cameraState.hudFov,
            state.windowRenderState.width, state.windowRenderState.height);
        com.mojang.blaze3d.systems.RenderSystem.setProjectionMatrix(
            this.hud3dProjectionMatrixBuffer.getBuffer(this.hudProjection),
            com.mojang.blaze3d.ProjectionType.PERSPECTIVE);
        // In front of everything, as in vanilla, where the hand pass starts from a cleared depth.
        com.mojang.blaze3d.systems.RenderSystem.getDevice().createCommandEncoder()
            .clearDepthTexture(this.mainRenderTarget.getDepthTexture(), 0.0);
        this.debugCrosshairRenderer.render(cameraState, state.windowRenderState.guiScale);
    }

    @WrapOperation(method = "render", at = @At(value = "INVOKE",
        target = "Lnet/minecraft/client/renderer/GameRenderer;renderLevel(Lnet/minecraft/client/DeltaTracker;)V"))
    private void radiante$renderLevel(GameRenderer gameRenderer, net.minecraft.client.DeltaTracker deltaTracker,
        Operation<Void> original) {
        com.g2806.radiante.client.DevAutomation.frame();
        if (!RadianteRenderer.isRayTracingEnabled()) {
            original.call(gameRenderer, deltaTracker);
            return;
        }

        // Vanilla's level render still runs, so the per-frame hooks other mods put on it (at its start and end,
        // and at the start of LevelRenderer.render) run on every loader as they would without Radiante. What it
        // would draw is stopped: the level (LevelRendererSkipMixin) and the hand and screen effects (below), all
        // of which the tracer draws instead.
        RadianteRenderer.setTracingLevel(true);
        try {
            original.call(gameRenderer, deltaTracker);
        } finally {
            RadianteRenderer.setTracingLevel(false);
        }
        // With mod overlays on the frame was traced from inside that level render (LevelRendererSkipMixin), which
        // then went on over the traced picture; otherwise it is traced now.
        if (!RadianteRenderer.wasFrameTraced()) {
            RadianteRenderer.renderLevel(gameRenderer, this.gameRenderState().levelRenderState);
            radiante$screenEffects(deltaTracker.getGameTimeDeltaPartialTick(false));
            radiante$debugCrosshair();
        }
        com.g2806.radiante.client.DevAutomation.frameDrawn();
    }

    // 26.2 has no render3dHud: renderLevel draws the hand, the screen effects and the F3 crosshair inline, and
    // ends the world fog itself, after them, so the fog never outlives the level render. The hand is skipped while
    // tracing; the screen effects and the crosshair are drawn over the traced picture once it is in the target.
    @WrapOperation(method = "renderLevel", at = @At(value = "INVOKE",
        target = "Lnet/minecraft/client/renderer/GameRenderer;renderItemInHand(Lnet/minecraft/client/renderer/state/level/CameraRenderState;FLorg/joml/Matrix4fc;)V"))
    private void radiante$skipHandWhenTracing(GameRenderer gameRenderer,
        net.minecraft.client.renderer.state.level.CameraRenderState cameraState, float partialTicks,
        org.joml.Matrix4fc modelViewMatrix, Operation<Void> original) {
        if (!RadianteRenderer.isTracingLevel()) {
            original.call(gameRenderer, cameraState, partialTicks, modelViewMatrix);
        }
    }

    @WrapOperation(method = "renderLevel", at = @At(value = "INVOKE",
        target = "Lnet/minecraft/client/renderer/ScreenEffectRenderer;submit(ZZFLnet/minecraft/client/renderer/SubmitNodeCollector;Z)V"))
    private void radiante$skipScreenEffectsWhenTracing(net.minecraft.client.renderer.ScreenEffectRenderer renderer,
        boolean isFirstPerson, boolean isSleeping, float partialTicks,
        net.minecraft.client.renderer.SubmitNodeCollector collector, boolean hideGui, Operation<Void> original) {
        // Vanilla's hand pass already cleared the depth and set the hud projection, so over a picture traced inside
        // the level render they are drawn right here; otherwise after the trace (radiante$renderLevel).
        if (!RadianteRenderer.isTracingLevel() || RadianteRenderer.wasFrameTraced()) {
            original.call(renderer, isFirstPerson, isSleeping, partialTicks, collector, hideGui);
        }
    }

    @WrapOperation(method = "renderLevel", at = @At(value = "INVOKE",
        target = "Lnet/minecraft/client/renderer/DebugCrosshairRenderer;render(Lnet/minecraft/client/renderer/state/level/CameraRenderState;I)V"))
    private void radiante$crosshairWhenTracing(net.minecraft.client.renderer.DebugCrosshairRenderer renderer,
        net.minecraft.client.renderer.state.level.CameraRenderState cameraState, int guiScale,
        Operation<Void> original) {
        if (!RadianteRenderer.isTracingLevel()) {
            original.call(renderer, cameraState, guiScale);
        } else if (RadianteRenderer.wasFrameTraced()) {
            // The traced picture is already in the target; without the overlay pass it is not yet (see above).
            radiante$debugCrosshair();
        }
    }

    /**
     * The picture a world shows in the world list is taken once, a second or so after joining, and only when
     * Minecraft has drawn more than ten sections of terrain itself. With ray tracing on it draws none of them -
     * Radiante has the terrain - so the count stayed at zero, the picture was never taken and the world was left
     * without one. The renderer's own section count answers for it instead.
     */
    @ModifyExpressionValue(method = "takeAutoScreenshot", at = @At(value = "INVOKE",
        target = "Lnet/minecraft/client/renderer/extract/LevelExtractor;countRenderedSections()I"))
    private int radiante$countSectionsForWorldIcon(int original) {
        if (!RadianteRenderer.isRayTracingEnabled()) {
            return original;
        }

        // Only once the world has actually been on screen for a moment: the first frames after joining still show
        // the loading screen over it, and a picture taken then is a flat grey square.
        Minecraft minecraft = Minecraft.getInstance();
        if (minecraft.gui.screen() != null || RadianteRenderer.levelFrames() < WORLD_ICON_MIN_FRAMES) {
            return original;
        }

        return Math.max(original, ChunkManager.compiledSectionCount());
    }

    @ModifyExpressionValue(method = "takeAutoScreenshot", at = @At(value = "INVOKE",
        target = "Lnet/minecraft/client/renderer/LevelRenderer;hasRenderedAllSections()Z"))
    private boolean radiante$builtAllSectionsForWorldIcon(boolean original) {
        return RadianteRenderer.isRayTracingEnabled() ? ChunkManager.hasBuiltEverything() : original;
    }
}
