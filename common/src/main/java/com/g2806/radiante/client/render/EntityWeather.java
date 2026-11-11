package com.g2806.radiante.client.render;

import java.util.ArrayList;
import java.util.List;
import net.minecraft.client.Minecraft;
import net.minecraft.world.phys.Vec3;
import net.minecraft.client.renderer.state.level.CameraRenderState;
import net.minecraft.client.renderer.state.level.LevelRenderState;
import net.minecraft.util.Mth;

import static com.g2806.radiante.client.render.EntityUpload.*;
import static com.g2806.radiante.client.render.EntityManager.*;

/**
 * Rain, snow and Minecraft's own clouds as traced geometry: the weather under a mask camera rays see and shadow rays
 * skip, the clouds as one mesh kept between frames.
 *
 * <p>This was part of {@code EntityManager}.
 */
public final class EntityWeather {

    private EntityWeather() {
    }

    /**
     * Blocks vanilla rain falls per tick: its texture scrolls (3 to 4) / 32 of a texture height per tick, and a
     * texture height covers four blocks. Each column picks its own speed in that range; the middle one is used.
     */
    static final float RAIN_FALL_PER_TICK = 4.0f * 3.5f / 32.0f;
    static double lastRainTime = Double.NaN;
    static float rainFallPerFrame;
    /** Dev switch for comparing with and without rain motion vectors. */
    public static boolean rainMotion = true;

    /** How far rain fell since the previous frame, for its motion vectors; see WorldUBO.rainFallPerFrame. */
    public static float rainFallPerFrame(LevelRenderState levelRenderState) {
        double now = levelRenderState.gameTime + PlayerFrame.worldPartialTicks();
        double ticks = Double.isNaN(lastRainTime) ? 0.0 : now - lastRainTime;
        lastRainTime = now;
        // A pause or a jump in time is not rain falling.
        rainFallPerFrame = rainMotion && ticks > 0.0 && ticks < 2.0 ? (float) ticks * RAIN_FALL_PER_TICK : 0.0f;
        return rainFallPerFrame;
    }

    /**
     * Rain and snow: the same sheets vanilla draws, built from the columns Minecraft already extracted. They go in
     * under the weather mask, which camera rays see and shadow rays skip, and use the stochastic alpha mode so a
     * streak is a faint visible surface rather than clear glass.
     */
    static void collectWeather(LevelRenderState levelRenderState, CameraRenderState cameraState) {
        net.minecraft.client.renderer.state.level.WeatherRenderState weather = levelRenderState.weatherRenderState;
        if (weather.intensity <= 0.0f || (weather.rainColumns.isEmpty() && weather.snowColumns.isEmpty())) {
            return;
        }

        Vec3 camera = cameraState.pos;
        List<PendingLayer> layers = new ArrayList<>();
        addWeatherLayer(layers, weather.rainColumns, RAIN_TEXTURE, camera, 1.0f, weather.radius, weather.intensity,
            true);
        addWeatherLayer(layers, weather.snowColumns, SNOW_TEXTURE, camera, 0.8f, weather.radius, weather.intensity,
            false);
        if (!layers.isEmpty()) {
            PENDING.add(new PendingEntity(WEATHER_ID, camera.x(), camera.y(), camera.z(), RAY_TRACING_WEATHER,
                layers));
        }
    }

    static void addWeatherLayer(List<PendingLayer> layers,
        List<net.minecraft.client.renderer.WeatherEffectRenderer.ColumnInstance> columns,
        net.minecraft.resources.Identifier texture, Vec3 camera, float maxAlpha, int radius, float intensity,
        boolean rain) {
        if (columns.isEmpty()) {
            return;
        }
        int textureId = TextureTracker.idOf(texture);
        if (textureId == 0) {
            return;
        }

        PBRVertexWriter writer = WEATHER_WRITER.textureId(textureId)
            .glintTextureId(0)
            .alphaMode(PBRVertexWriter.ALPHA_MODE_STOCHASTIC)
            .coordinate(NativeGeometry.COORDINATE_WORLD)
            .albedoEmission(0.0f)
            .overlayEnabled(false)
            .computeQuadNormals(true)
            .rain(rain);
        writer.reset();

        float radiusSq = Math.max(radius * radius, 1);
        int cameraX = Mth.floor(camera.x());
        int cameraZ = Mth.floor(camera.z());
        for (net.minecraft.client.renderer.WeatherEffectRenderer.ColumnInstance column : columns) {
            int indexX = column.x() - cameraX + 16;
            int indexZ = column.z() - cameraZ + 16;
            if (indexX < 0 || indexX >= 32 || indexZ < 0 || indexZ >= 32) {
                continue;
            }
            float relativeX = (float) (column.x() + 0.5 - camera.x());
            float relativeZ = (float) (column.z() + 0.5 - camera.z());
            float distanceSq = relativeX * relativeX + relativeZ * relativeZ;
            float alpha = Mth.lerp(Math.min(distanceSq / radiusSq, 1.0f), maxAlpha, 0.5f) * intensity;
            int color = net.minecraft.util.ARGB.white(alpha);
            float halfX = WEATHER_COLUMN_X[indexZ * 32 + indexX] / 2.0f;
            float halfZ = WEATHER_COLUMN_Z[indexZ * 32 + indexX] / 2.0f;
            float x0 = relativeX - halfX;
            float x1 = relativeX + halfX;
            float z0 = relativeZ - halfZ;
            float z1 = relativeZ + halfZ;
            float y1 = (float) (column.topY() - camera.y());
            float y0 = (float) (column.bottomY() - camera.y());
            float u0 = column.uOffset();
            float u1 = column.uOffset() + 1.0f;
            float v0 = column.bottomY() * 0.25f + column.vOffset();
            float v1 = column.topY() * 0.25f + column.vOffset();
            writer.addVertex(x0, y1, z0).setColor(color).setUv(u0, v0).setLight(column.lightCoords());
            writer.addVertex(x1, y1, z1).setColor(color).setUv(u1, v0).setLight(column.lightCoords());
            writer.addVertex(x1, y0, z1).setColor(color).setUv(u1, v1).setLight(column.lightCoords());
            writer.addVertex(x0, y0, z0).setColor(color).setUv(u0, v1).setLight(column.lightCoords());
        }

        writer.finish();
        if (writer.vertexCount() == 0 || writer.vertexCount() % 4 != 0) {
            return;
        }
        layers.add(new PendingLayer(NativeGeometry.GEOMETRY_TYPE_WORLD_TRANSPARENT, textureId, writer.vertexCount(),
            copyVertices(writer), "Entity"));
    }

    static boolean cloudsShownLastFrame;
    static int cloudPauseFrames;

    /** The render pipeline was just rebuilt; see collectClouds. */
    public static void onPipelineRebuilt() {
        BlockEntityCache.invalidate();
        cloudPauseFrames = 3;
        cloudsShownLastFrame = false;
    }

    /** Vanilla's clouds, when the shader pack's cloud mode asks for them rather than its ray marched ones. */
    static void collectClouds(Minecraft minecraft, LevelRenderState levelRenderState,
        CameraRenderState cameraState) {
        String mode = com.g2806.radiante.client.pipeline.PipelineSettings.getCloudMode();
        if (mode == null || !mode.endsWith(".vanilla") || net.minecraft.util.ARGB.alpha(levelRenderState.cloudColor) == 0) {
            cloudsShownLastFrame = false;
            return;
        }
        // Clouds that were off for a while come back under a new key: the renderer let go of the old acceleration
        // structure once nothing used it, and handing it the old key again (clouds off, then back to vanilla) made
        // it trace a structure that no longer existed - the device was lost.
        // Nor right after the pipeline was rebuilt: a structure first built in those frames was traced before its
        // vertices had reached the GPU. A few frames without clouds are not noticed.
        if (cloudPauseFrames > 0) {
            cloudPauseFrames--;
            cloudsShownLastFrame = false;
            return;
        }
        if (!cloudsShownLastFrame) {
            CLOUDS.invalidate();
        }
        cloudsShownLastFrame = true;
        net.minecraft.client.renderer.CloudRenderer renderer =
            ((LevelRendererGizmoAccess) minecraft.levelRenderer).radiante$cloudRenderer();
        PBRVertexWriter writer = CLOUDS.update(
            ((com.g2806.radiante.mixin.world.CloudRendererAccessor) renderer).radiante$texture(),
            minecraft.options.getCloudStatus(), levelRenderState.cloudColor,
            levelRenderState.cloudHeight + com.g2806.radiante.client.option.Options.cloudHeightOffset,
            minecraft.options.cloudRange().get(), cameraState.pos, levelRenderState.gameTime,
            PlayerFrame.worldPartialTicks());
        if (writer == null || writer.vertexCount() % 4 != 0) {
            return;
        }
        // The mesh only changes when the camera crosses into another cloud cell; in between it just drifts. So its
        // acceleration structure is kept and only moved, and its vertices are handed over from the mesh itself
        // rather than copied: rebuilding it every frame cost more than everything else on the render thread.
        List<PendingLayer> layers = new ArrayList<>();
        layers.add(new PendingLayer(NativeGeometry.GEOMETRY_TYPE_WORLD_CLOUD, 0, writer.vertexCount(), 0L, "clouds",
            writer.address(), "clouds#" + CLOUDS.version()));
        PENDING.add(new PendingEntity(CLOUD_ID, CLOUDS.originX, CLOUDS.originY, CLOUDS.originZ, RAY_TRACING_CLOUD,
            layers, PREBUILT_BLAS_KEYED));
    }
}
