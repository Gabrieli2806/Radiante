package com.g2806.radiante.mixin.world;

import com.g2806.radiante.client.render.ChunkManager;
import com.g2806.radiante.client.render.EntityManager;
import com.g2806.radiante.client.render.LevelExtractorAccess;
import com.g2806.radiante.client.render.RadianteRenderer;
import com.g2806.radiante.client.render.SectionTrackerAccess;
import net.minecraft.client.SectionUpdateTracker;
import net.minecraft.client.renderer.culling.Frustum;
import net.minecraft.client.renderer.extract.LevelExtractor;
import net.minecraft.world.entity.Entity;
import org.jspecify.annotations.Nullable;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(LevelExtractor.class)
public class LevelExtractorMixin implements LevelExtractorAccess {

    @Shadow
    private @Nullable SectionUpdateTracker sectionUpdateTracker;

    @Override
    public SectionUpdateTracker radiante$sectionUpdateTracker() {
        return this.sectionUpdateTracker;
    }

    @Inject(method = "allChanged", at = @At("TAIL"))
    private void radiante$onSectionGridCreated(CallbackInfo ci) {
        if (!RadianteRenderer.isActive() || this.sectionUpdateTracker == null) {
            return;
        }

        SectionTrackerAccess access = (SectionTrackerAccess) this.sectionUpdateTracker;
        ChunkManager.onTrackerCreated(this.sectionUpdateTracker, access.radiante$radius(),
            access.radiante$minSectionY(), access.radiante$maxSectionY());
    }

    /** Light-giving entities near the camera stay in the frame when vanilla culls them (EntityManager). */
    @Inject(method = "isEntityVisible", at = @At("RETURN"), cancellable = true)
    private void radiante$keepLightOutOfView(Entity entity, Frustum frustum, double cameraX, double cameraY,
        double cameraZ, float partialTick, long frame, CallbackInfoReturnable<Boolean> cir) {
        if (!cir.getReturnValueZ() && RadianteRenderer.isRayTracingEnabled()
            && EntityManager.keepsLightOutOfView(entity, cameraX, cameraY, cameraZ, partialTick)) {
            cir.setReturnValue(true);
        }
    }
}
