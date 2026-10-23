package com.g2806.radiante.mixin.world;

import com.g2806.radiante.client.render.RadianteRenderer;
import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import com.mojang.blaze3d.framegraph.FrameGraphBuilder;
import com.mojang.blaze3d.framegraph.FramePass;
import com.mojang.blaze3d.resource.GraphicsResourceAllocator;
import com.mojang.blaze3d.vertex.PoseStack;
import com.mojang.renderpearl.api.buffers.GpuBufferSlice;
import com.mojang.renderpearl.api.commands.CommandEncoder;
import com.mojang.renderpearl.api.commands.RenderPass;
import com.mojang.renderpearl.api.textures.GpuSampler;
import com.mojang.renderpearl.api.textures.GpuTextureView;
import java.util.Optional;
import java.util.OptionalDouble;
import java.util.function.Supplier;
import net.minecraft.client.renderer.GameRenderer;
import net.minecraft.client.renderer.LevelRenderer;
import net.minecraft.client.renderer.LevelTargetBundle;
import net.minecraft.client.renderer.SectionOcclusionGraph;
import net.minecraft.client.renderer.SubmitNodeCollector;
import net.minecraft.client.renderer.chunk.ChunkSectionsToRender;
import net.minecraft.client.renderer.feature.FeatureRenderDispatcher;
import net.minecraft.client.renderer.gizmos.DrawableGizmoPrimitives;
import net.minecraft.client.renderer.state.level.CameraRenderState;
import net.minecraft.client.renderer.state.level.ChunkLoadingRenderState;
import net.minecraft.client.renderer.state.level.LevelRenderState;
import net.minecraft.client.renderer.state.level.ParticlesRenderState;
import org.joml.Vector4f;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.ModifyVariable;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/**
 * While Radiante traces the world, LevelRenderer.render is still called every frame (GameRendererMixin), so that the
 * per-frame work other mods do at its head keeps running: Not Enough Animations hands its animator the frame's
 * partial tick there, and without it players freeze. The high priority applies this mixin after everyone else's,
 * which places this callback after theirs at the head of the method.
 *
 * <p>From there it goes one of two ways. With mod overlays on (the default) the tracer's work for the frame is done
 * right here, and Minecraft's level render then goes on with nothing of its own left to draw - no sky, terrain,
 * entities, particles, clouds or weather - over the traced picture and against the traced depth. Its passes are
 * where other mods draw into the world (schematics, selection boxes, waypoint beams, guides), so all of that lands
 * on the traced world, hidden where the world is in front. With mod overlays off the level render is stopped
 * outright, and nothing but the traced world shows.
 */
@Mixin(value = LevelRenderer.class, priority = 2000)
public class LevelRendererSkipMixin {

    @Shadow
    @Final
    private GameRenderer gameRenderer;

    @Shadow
    @Final
    private LevelRenderState levelRenderState;

    @Shadow
    @Final
    private LevelTargetBundle targets;

    @Inject(method = "render", at = @At("HEAD"), cancellable = true)
    private void radiante$skipWhenTracing(GraphicsResourceAllocator resourceAllocator, boolean renderOutline,
        CameraRenderState cameraState, GpuBufferSlice fogBuffer, boolean consistentDepthRequired, CallbackInfo ci) {
        this.radiante$traceAdded = false;
        if (!RadianteRenderer.isTracingLevel()) {
            return;
        }
        if (RadianteRenderer.usesOverlayPass()) {
            this.radiante$earlyModPass();
        }
        if (!RadianteRenderer.usesOverlayPass() || !RadianteRenderer.prepareFrame(this.gameRenderer,
            this.levelRenderState)) {
            ci.cancel();
            return;
        }
        // The tracer has read all of these; emptied, Minecraft submits and draws none of them again.
        this.levelRenderState.entityRenderStates.clear();
        this.levelRenderState.blockEntityRenderStates.clear();
        this.levelRenderState.blockBreakingRenderStates.clear();
        this.levelRenderState.weatherRenderState.reset();
        this.levelRenderState.cloudColor = 0;
    }

    @Shadow
    private void submitFeatures(LevelRenderState levelRenderState, SubmitNodeCollector submitNodeCollector,
        boolean renderOutline) {
    }

    /** The feature submission is being run before the frame is traced, for what other mods add to it (below). */
    @Unique
    private boolean radiante$earlyPass;
    @Unique
    private final LevelRenderState radiante$emptyState = new LevelRenderState();

    /**
     * What other mods add at the end of the feature submission is traced too, in the frame it is submitted, instead
     * of drawn flat over the traced picture: Animated Doors' swinging doors, which it takes out of the terrain while
     * they move. The submission is run once before the frame is traced, with none of Minecraft's own render states
     * in it (the tracer reads those itself), so only what mods add reaches the capture (EntityManager).
     */
    @Unique
    private void radiante$earlyModPass() {
        LevelRenderState empty = this.radiante$emptyState;
        empty.cameraRenderState = this.levelRenderState.cameraRenderState;
        empty.gameTime = this.levelRenderState.gameTime;
        empty.worldPartialTicks = this.levelRenderState.worldPartialTicks;
        empty.shouldShowEntityOutlines = this.levelRenderState.shouldShowEntityOutlines;
        this.radiante$earlyPass = true;
        try {
            this.submitFeatures(empty, com.g2806.radiante.client.render.EntityManager.beginModSubmits(
                this.levelRenderState.cameraRenderState.pos), false);
        } catch (RuntimeException e) {
            // A mod failing here still runs, and draws, in the level render after the trace.
        } finally {
            this.radiante$earlyPass = false;
            com.g2806.radiante.client.render.EntityManager.endModSubmits();
        }
    }

    /** The early pass leaves Minecraft's debug gizmos to the real submission: collected twice, they were emptied. */
    @WrapOperation(method = "submitFeatures", at = @At(value = "INVOKE",
        target = "Lnet/minecraft/client/renderer/LevelRenderer;finalizeGizmoCollection()V"))
    private void radiante$keepGizmos(LevelRenderer renderer, Operation<Void> original) {
        if (!this.radiante$earlyPass) {
            original.call(renderer);
        }
    }

    /**
     * In the level render over the traced picture, what mods submit at the end of the feature submission was traced
     * already (the early pass): their geometry is dropped there, and their lines and outlines drawn as before.
     */
    @ModifyVariable(method = "submitFeatures", at = @At(value = "INVOKE",
        target = "Lnet/minecraft/client/renderer/LevelRenderer;checkPoseStack(Lcom/mojang/blaze3d/vertex/PoseStack;)V",
        shift = At.Shift.AFTER), argsOnly = true)
    private SubmitNodeCollector radiante$captureModSubmits(SubmitNodeCollector collector) {
        if (this.radiante$earlyPass || !RadianteRenderer.isOverlayFrame()) {
            return collector;
        }
        return com.g2806.radiante.client.render.EntityManager.dropModSubmits(collector);
    }

    /** No block outline of Minecraft's: the tracer draws its own. */
    @ModifyVariable(method = "render", at = @At("HEAD"), argsOnly = true, ordinal = 0)
    private boolean radiante$noOutlineOverTrace(boolean renderOutline) {
        return RadianteRenderer.isOverlayFrame() ? false : renderOutline;
    }

    /** The traced picture is in Minecraft's target for this frame. */
    @Unique
    private boolean radiante$traceAdded;

    @Unique
    private void radiante$addTracePass(FrameGraphBuilder frame) {
        this.radiante$traceAdded = true;
        FramePass pass = frame.addPass("radiante");
        this.targets.main = pass.readsAndWrites(this.targets.main);
        pass.executes(() -> {
            RadianteRenderer.submitFrame();
            com.g2806.radiante.client.render.ModBackdrop.capture(this.gameRenderer.mainRenderTarget());
        });
    }

    /**
     * The traced picture and its depth go into Minecraft's target before its main pass, which is where the sky
     * (without improved transparency) and everything else other mods add are drawn. The sky runs with nothing of
     * Minecraft's own in it (SkyRendererMixin), for what other mods add to it, which so lands on the traced picture;
     * ModBackdrop then puts the traced world back over it.
     */
    @WrapOperation(method = "render", at = @At(value = "INVOKE",
        target = "Lnet/minecraft/client/renderer/LevelRenderer;addMainPass(Lcom/mojang/blaze3d/framegraph/FrameGraphBuilder;Lnet/minecraft/client/renderer/feature/FeatureRenderDispatcher$PreparedFrame;Lcom/mojang/renderpearl/api/buffers/GpuBufferSlice;Lnet/minecraft/client/renderer/chunk/ChunkSectionsToRender;Z)V"))
    private void radiante$traceBeforeMainPass(LevelRenderer levelRenderer, FrameGraphBuilder frame,
        FeatureRenderDispatcher.PreparedFrame featureFrame, GpuBufferSlice terrainFog,
        ChunkSectionsToRender chunkSectionsToRender, boolean consistentDepthRequired, Operation<Void> original) {
        if (RadianteRenderer.isOverlayFrame() && !this.radiante$traceAdded) {
            this.radiante$addTracePass(frame);
        }
        this.radiante$traceAdded = false;
        original.call(levelRenderer, frame, featureFrame, terrainFog, chunkSectionsToRender, consistentDepthRequired);
    }

    /**
     * Since 26.4 the main pass clears Minecraft's target itself (there is no separate clear pass), which would wipe
     * the traced picture; over it, the pass loads what is there instead.
     */
    @WrapOperation(method = "lambda$addMainPass$0", at = @At(value = "INVOKE",
        target = "Lcom/mojang/renderpearl/api/commands/CommandEncoder;createRenderPass(Ljava/util/function/Supplier;Lcom/mojang/renderpearl/api/textures/GpuTextureView;Ljava/util/Optional;Lcom/mojang/renderpearl/api/textures/GpuTextureView;Ljava/util/OptionalDouble;)Lcom/mojang/renderpearl/api/commands/RenderPass;"))
    private RenderPass radiante$keepTraceInMainPass(CommandEncoder encoder, Supplier<String> label,
        GpuTextureView color, Optional<Vector4f> clearColor, GpuTextureView depth, OptionalDouble clearDepth,
        Operation<RenderPass> original) {
        if (RadianteRenderer.isOverlayFrame()) {
            return original.call(encoder, label, color, Optional.empty(), depth, OptionalDouble.empty());
        }
        return original.call(encoder, label, color, clearColor, depth, clearDepth);
    }

    /**
     * With improved transparency the sky is drawn into a target of its own and blitted where nothing was drawn,
     * which over the traced picture would cover its sky with Minecraft's fog colour: the traced sky stays.
     */
    @WrapOperation(method = "lambda$addMainPass$0", at = @At(value = "INVOKE",
        target = "Lnet/minecraft/client/renderer/LevelRenderer;blitSky(Lcom/mojang/renderpearl/api/commands/RenderPass;Lcom/mojang/renderpearl/api/textures/GpuSampler;)V"))
    private void radiante$noSkyBlitOverTrace(LevelRenderer levelRenderer, RenderPass pass, GpuSampler sampler,
        Operation<Void> original) {
        if (!RadianteRenderer.isOverlayFrame()) {
            original.call(levelRenderer, pass, sampler);
        }
    }

    /** Minecraft compiles no terrain of its own while the tracer has it. */
    @WrapOperation(method = "render", at = @At(value = "INVOKE",
        target = "Lnet/minecraft/client/renderer/LevelRenderer;compileSections(Lnet/minecraft/client/renderer/state/level/CameraRenderState;)V"))
    private void radiante$noTerrainCompile(LevelRenderer levelRenderer, CameraRenderState cameraState,
        Operation<Void> original) {
        if (!RadianteRenderer.isOverlayFrame()) {
            original.call(levelRenderer, cameraState);
        }
    }

    /** Nor does it work out which of its sections are in view; the tracer keeps that graph fed itself. */
    @WrapOperation(method = "render", at = @At(value = "INVOKE",
        target = "Lnet/minecraft/client/renderer/SectionOcclusionGraph;update(Lnet/minecraft/client/renderer/state/level/CameraRenderState;ILnet/minecraft/client/renderer/state/level/ChunkLoadingRenderState;)V"))
    private void radiante$noOcclusionUpdate(SectionOcclusionGraph graph, CameraRenderState cameraState, int fov,
        ChunkLoadingRenderState chunkLoading, Operation<Void> original) {
        if (!RadianteRenderer.isOverlayFrame()) {
            original.call(graph, cameraState, fov, chunkLoading);
        }
    }

    /** Particles are traced. */
    @WrapOperation(method = "submitFeatures", at = @At(value = "INVOKE",
        target = "Lnet/minecraft/client/renderer/state/level/ParticlesRenderState;submit(Lnet/minecraft/client/renderer/SubmitNodeCollector;Lnet/minecraft/client/renderer/state/level/CameraRenderState;)V"))
    private void radiante$noParticlesOverTrace(ParticlesRenderState particles, SubmitNodeCollector collector,
        CameraRenderState cameraState, Operation<Void> original) {
        if (!RadianteRenderer.isOverlayFrame()) {
            original.call(particles, collector, cameraState);
        }
    }

    /** Blocks just placed, shown until their section is rebuilt: the tracer rebuilds its own sections. */
    @WrapOperation(method = "submitFeatures", at = @At(value = "INVOKE",
        target = "Lnet/minecraft/client/renderer/LevelRenderer;submitTransientBlocks(Lcom/mojang/blaze3d/vertex/PoseStack;Lnet/minecraft/client/renderer/SubmitNodeCollector;Lnet/minecraft/client/renderer/state/level/LevelRenderState;)V"))
    private void radiante$noTransientBlocksOverTrace(LevelRenderer levelRenderer, PoseStack poseStack,
        SubmitNodeCollector collector, LevelRenderState state, Operation<Void> original) {
        if (!RadianteRenderer.isOverlayFrame()) {
            original.call(levelRenderer, poseStack, collector, state);
        }
    }

    /** Debug gizmos (hitboxes, chunk borders) are traced too; see OverlayLines. */
    @WrapOperation(method = "submitFeatures", at = @At(value = "INVOKE",
        target = "Lnet/minecraft/client/renderer/gizmos/DrawableGizmoPrimitives;submit(Lnet/minecraft/client/renderer/SubmitNodeCollector;Lnet/minecraft/client/renderer/state/level/CameraRenderState;Z)V"))
    private void radiante$noGizmosOverTrace(DrawableGizmoPrimitives primitives, SubmitNodeCollector collector,
        CameraRenderState cameraState, boolean alwaysOnTop, Operation<Void> original) {
        if (!RadianteRenderer.isOverlayFrame()) {
            original.call(primitives, collector, cameraState, alwaysOnTop);
        }
    }
}
