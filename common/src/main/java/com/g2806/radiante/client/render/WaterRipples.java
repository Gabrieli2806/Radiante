package com.g2806.radiante.client.render;

import com.g2806.radiante.client.option.Options;
import com.g2806.radiante.client.pipeline.PipelineSettings;
import it.unimi.dsi.fastutil.ints.Int2ObjectOpenHashMap;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.ClientLevel;
import net.minecraft.core.BlockPos;
import net.minecraft.tags.FluidTags;
import net.minecraft.util.Mth;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.level.levelgen.Heightmap;
import net.minecraft.world.level.material.FluidState;
import net.minecraft.world.phys.Vec3;

/**
 * Tracks entities interacting with water surfaces and rain puddles near the camera, maintaining up to
 * {@link #MAX_RIPPLES} radial dispersive wave packets uploaded each frame in {@code SkyUBO.waterRipple*}.
 *
 * <p>Consecutive impulses emitted along a swimmer's or boat's path superpose into a natural Kelvin V-wake, while
 * splashes, bobbing items/bobbers and footsteps across rain-soaked ground produce concentric ring trains.
 * Simulation time follows level game time so ripples pause cleanly in the settings screen and Photo Mode.
 */
public final class WaterRipples {

    public static final int MAX_RIPPLES = 16;
    /** 1 control vec4 + 16 pos vec4 + 16 data vec4 = 33 vec4s = 132 floats (528 bytes). */
    public static final int PACKED_FLOATS = (1 + MAX_RIPPLES * 2) * 4;

    private static final String RIPPLES_ATTRIBUTE = "render_pipeline.module.ray_tracing.attribute.water_ripples";
    private static final double MAX_RANGE = 48.0;
    private static final double MAX_RANGE_SQ = MAX_RANGE * MAX_RANGE;

    private static final float KIND_WATER = 0.0f;
    private static final float KIND_PUDDLE = 1.0f;

    private static final class Ripple {
        double x;
        double y;
        double z;
        float speed;
        float spawnedAt;
        float amplitude;
        float maxAge;
        float kind;
        boolean active;
    }

    private static final class TrackedEntity {
        double prevX;
        double prevY;
        double prevZ;
        double waterHorizDist;
        double puddleHorizDist;
        double lastBobTime;
        boolean atWaterSurface;
        boolean wasOnGround;
        int lastSeenFrame;
    }

    private static final Ripple[] RIPPLES = new Ripple[MAX_RIPPLES];
    static {
        for (int i = 0; i < MAX_RIPPLES; i++) {
            RIPPLES[i] = new Ripple();
        }
    }

    private static final Int2ObjectOpenHashMap<TrackedEntity> TRACKED = new Int2ObjectOpenHashMap<>();
    private static final float[] PACKED = new float[PACKED_FLOATS];
    private static final BlockPos.MutableBlockPos SCAN_POS = new BlockPos.MutableBlockPos();

    private static ClientLevel lastLevel;
    private static double lastTimeSec = Double.NaN;
    private static int frameId;
    private static float cachedSetting = 1.0f;
    private static long settingReadAt;

    private WaterRipples() {
    }

    /** Updates active ripples and returns the packed float layout for {@code SkyUBO} (length {@link #PACKED_FLOATS}). */
    public static float[] update(Minecraft minecraft, Vec3 cameraPos, float groundWetness, long gameTime,
                                 float partialTick) {
        java.util.Arrays.fill(PACKED, 0.0f);
        ClientLevel level = minecraft.level;
        if (level == null || !isEnabled()) {
            if (level != lastLevel) {
                reset(level, Double.NaN);
            }
            return PACKED;
        }

        double nowSec = (double) gameTime / 20.0 + (double) partialTick / 20.0;
        if (level != lastLevel || Double.isNaN(lastTimeSec) || nowSec < lastTimeSec - 0.5
            || nowSec - lastTimeSec > 5.0) {
            reset(level, nowSec);
        }
        double dt = Math.max(0.0, nowSec - lastTimeSec);
        lastTimeSec = nowSec;
        frameId++;

        // Advance and expire active ripples first.
        for (Ripple r : RIPPLES) {
            if (!r.active) {
                continue;
            }
            float age = (float) (nowSec - r.spawnedAt);
            if (age < 0.0f || age >= r.maxAge) {
                r.active = false;
            }
        }

        // Scan nearby entities only when simulation time moved forward.
        if (dt > 1e-5) {
            scanEntities(level, cameraPos, groundWetness, nowSec, dt, partialTick);
        }

        // Periodically prune entities that left tracking range or despawned.
        if ((frameId & 63) == 0 && !TRACKED.isEmpty()) {
            int cutoff = frameId - 120;
            TRACKED.int2ObjectEntrySet().removeIf(entry -> entry.getValue().lastSeenFrame < cutoff);
        }

        // Pack active ripples relative to cameraPos.
        int count = 0;
        int posBase = 4;
        int dataBase = 4 + MAX_RIPPLES * 4;
        for (Ripple r : RIPPLES) {
            if (!r.active) {
                continue;
            }
            float age = (float) (nowSec - r.spawnedAt);
            if (age < 0.0f || age >= r.maxAge) {
                r.active = false;
                continue;
            }
            float dx = (float) (r.x - cameraPos.x);
            float dy = (float) (r.y - cameraPos.y);
            float dz = (float) (r.z - cameraPos.z);
            if (dx * dx + dy * dy + dz * dz > (float) ((MAX_RANGE + 12.0) * (MAX_RANGE + 12.0))) {
                continue;
            }
            int p = posBase + count * 4;
            PACKED[p] = dx;
            PACKED[p + 1] = dy;
            PACKED[p + 2] = dz;
            PACKED[p + 3] = r.speed;

            int d = dataBase + count * 4;
            PACKED[d] = age;
            PACKED[d + 1] = r.amplitude;
            PACKED[d + 2] = r.maxAge;
            PACKED[d + 3] = r.kind;

            count++;
            if (count >= MAX_RIPPLES) {
                break;
            }
        }
        PACKED[0] = (float) count;
        return PACKED;
    }

    private static void scanEntities(ClientLevel level, Vec3 cameraPos, float groundWetness, double nowSec,
                                     double dt, float partialTick) {
        boolean puddlesActive = Options.rainWetness && groundWetness > 0.12f;
        for (Entity entity : level.entitiesForRendering()) {
            if (entity.isSpectator()) {
                continue;
            }
            double x = Mth.lerp(partialTick, entity.xo, entity.getX());
            double y = Mth.lerp(partialTick, entity.yo, entity.getY());
            double z = Mth.lerp(partialTick, entity.zo, entity.getZ());
            double camDx = x - cameraPos.x;
            double camDy = y - cameraPos.y;
            double camDz = z - cameraPos.z;
            if (camDx * camDx + camDy * camDy + camDz * camDz > MAX_RANGE_SQ) {
                continue;
            }

            int id = entity.getId();
            TrackedEntity tracked = TRACKED.get(id);
            boolean firstSeen = tracked == null;
            if (firstSeen) {
                tracked = new TrackedEntity();
                tracked.prevX = x;
                tracked.prevY = y;
                tracked.prevZ = z;
                tracked.lastBobTime = nowSec;
                tracked.wasOnGround = entity.onGround();
                TRACKED.put(id, tracked);
            }
            tracked.lastSeenFrame = frameId;

            double moveHoriz = Math.hypot(x - tracked.prevX, z - tracked.prevZ);
            float bbWidth = Math.clamp(entity.getBbWidth(), 0.2f, 2.2f);
            float bbHeight = Math.max(entity.getBbHeight(), 0.2f);

            double surfaceY = findWaterSurfaceY(level, x, y, z);
            boolean hasSurface = !Double.isNaN(surfaceY);
            boolean atSurface = hasSurface && y <= surfaceY + 0.25 && (y + bbHeight) >= surfaceY - 0.35;
            boolean crossedSurface = hasSurface && !firstSeen && !tracked.atWaterSurface
                && tracked.prevY >= surfaceY - 0.05 && y <= surfaceY + 0.25;

            if (atSurface || crossedSurface) {
                boolean freshSplash = (!firstSeen && !tracked.atWaterSurface)
                    || (firstSeen && (entity.getDeltaMovement().y < -0.03 || entity.tickCount <= 4));
                if (freshSplash) {
                    float downSpeed = !firstSeen
                        ? (float) Math.max(0.0, (tracked.prevY - y) / Math.max(dt, 0.01))
                        : (float) Math.max(0.0, -entity.getDeltaMovement().y * 20.0);
                    float amp = Math.clamp(0.48f + 0.14f * downSpeed + 0.32f * bbWidth, 0.42f, 1.35f);
                    spawnRipple(cameraPos, nowSec, x, surfaceY, z, 2.25f, amp, 3.2f, KIND_WATER);
                    tracked.waterHorizDist = 0.0;
                    tracked.lastBobTime = nowSec;
                } else {
                    tracked.waterHorizDist += moveHoriz;
                    double wakeSpacing = Math.clamp(0.26 + 0.16 * bbWidth, 0.30, 0.55);
                    if (tracked.waterHorizDist >= wakeSpacing) {
                        float horizSpeed = (float) (moveHoriz / Math.max(dt, 0.01));
                        float amp = Math.clamp(0.36f + 0.09f * horizSpeed + 0.22f * bbWidth, 0.34f, 1.05f);
                        spawnRipple(cameraPos, nowSec, x, surfaceY, z, 2.05f, amp, 2.8f, KIND_WATER);
                        tracked.waterHorizDist = 0.0;
                        tracked.lastBobTime = nowSec;
                    } else if (nowSec - tracked.lastBobTime >= 1.45) {
                        float amp = Math.clamp(0.22f + 0.14f * bbWidth, 0.24f, 0.46f);
                        spawnRipple(cameraPos, nowSec, x, surfaceY, z, 1.65f, amp, 2.3f, KIND_WATER);
                        tracked.lastBobTime = nowSec;
                    }
                }
            } else {
                tracked.waterHorizDist = 0.0;
                if (puddlesActive) {
                    boolean onGround = entity.onGround();
                    if (onGround) {
                        int bx = Mth.floor(x);
                        int bz = Mth.floor(z);
                        int groundY = Mth.floor(y - 0.05);
                        SCAN_POS.set(bx, groundY, bz);
                        if (!level.getBlockState(SCAN_POS).isAir()
                            && !level.getFluidState(SCAN_POS).is(FluidTags.WATER)
                            && level.getHeight(Heightmap.Types.MOTION_BLOCKING, bx, bz) <= groundY + 1) {
                            double puddleY = groundY + 1.0;
                            if (!firstSeen && !tracked.wasOnGround && tracked.prevY - y > 0.02) {
                                float amp = Math.clamp(0.55f + 0.24f * bbWidth, 0.48f, 1.05f);
                                spawnRipple(cameraPos, nowSec, x, puddleY, z, 1.65f, amp, 1.7f, KIND_PUDDLE);
                                tracked.puddleHorizDist = 0.0;
                            } else {
                                tracked.puddleHorizDist += moveHoriz;
                                if (tracked.puddleHorizDist >= 0.88) {
                                    float amp = Math.clamp(0.46f + 0.20f * bbWidth, 0.42f, 0.88f);
                                    spawnRipple(cameraPos, nowSec, x, puddleY, z, 1.55f, amp, 1.5f, KIND_PUDDLE);
                                    tracked.puddleHorizDist = 0.0;
                                }
                            }
                        }
                    } else {
                        tracked.puddleHorizDist = 0.0;
                    }
                }
            }

            tracked.atWaterSurface = atSurface;
            tracked.wasOnGround = entity.onGround();
            tracked.prevX = x;
            tracked.prevY = y;
            tracked.prevZ = z;
        }
    }

    /** Returns the world Y of the water surface near (x, y, z), or {@code Double.NaN} if no surface water is near. */
    private static double findWaterSurfaceY(ClientLevel level, double x, double y, double z) {
        int bx = Mth.floor(x);
        int by = Mth.floor(y);
        int bz = Mth.floor(z);
        int waterY = Integer.MIN_VALUE;
        for (int dy = 1; dy >= -1; dy--) {
            SCAN_POS.set(bx, by + dy, bz);
            if (level.getFluidState(SCAN_POS).is(FluidTags.WATER)) {
                waterY = by + dy;
                break;
            }
        }
        if (waterY == Integer.MIN_VALUE) {
            return Double.NaN;
        }
        for (int step = 0; step < 3; step++) {
            SCAN_POS.set(bx, waterY + 1, bz);
            if (!level.getFluidState(SCAN_POS).is(FluidTags.WATER)) {
                SCAN_POS.set(bx, waterY, bz);
                FluidState state = level.getFluidState(SCAN_POS);
                float h = state.getHeight(level, SCAN_POS);
                return waterY + (h > 0.0f ? h : 0.8888889f);
            }
            waterY++;
        }
        // Deeper than 3 blocks beneath the surface: underwater movement does not disturb the top surface.
        return Double.NaN;
    }

    private static void spawnRipple(Vec3 cameraPos, double nowSec, double x, double y, double z,
                                    float speed, float amplitude, float maxAge, float kind) {
        // Coalesce impulses spawned nearly on top of an existing fresh impulse (< 0.18 blocks, < 0.08 s old).
        for (Ripple r : RIPPLES) {
            if (r.active && Math.abs(r.kind - kind) < 0.25 && (nowSec - r.spawnedAt) < 0.08) {
                double dx = r.x - x;
                double dy = r.y - y;
                double dz = r.z - z;
                if (dx * dx + dy * dy + dz * dz < 0.18 * 0.18) {
                    r.amplitude = Math.max(r.amplitude, amplitude);
                    return;
                }
            }
        }

        int slot = -1;
        float weakestScore = Float.MAX_VALUE;
        for (int i = 0; i < MAX_RIPPLES; i++) {
            Ripple r = RIPPLES[i];
            if (!r.active) {
                slot = i;
                break;
            }
            float age = (float) Math.max(0.0, nowSec - r.spawnedAt);
            float remaining = Math.max(0.0f, 1.0f - age / Math.max(r.maxAge, 0.1f));
            double dx = r.x - cameraPos.x;
            double dz = r.z - cameraPos.z;
            float score = (r.amplitude * remaining * remaining) / (float) (1.0 + 0.03 * (dx * dx + dz * dz));
            if (score < weakestScore) {
                weakestScore = score;
                slot = i;
            }
        }
        if (slot < 0) {
            return;
        }
        Ripple target = RIPPLES[slot];
        target.x = x;
        target.y = y;
        target.z = z;
        target.speed = speed;
        target.spawnedAt = (float) nowSec;
        target.amplitude = amplitude;
        target.maxAge = maxAge;
        target.kind = kind;
        target.active = true;
    }

    private static boolean isEnabled() {
        long now = System.nanoTime();
        if (now - settingReadAt > 250_000_000L) {
            settingReadAt = now;
            String value = PipelineSettings.getShaderPackValue(RIPPLES_ATTRIBUTE);
            try {
                cachedSetting = value == null ? 1.0f : Float.parseFloat(value.trim());
            } catch (NumberFormatException ignored) {
                cachedSetting = 1.0f;
            }
        }
        return cachedSetting > 0.001f;
    }

    private static void reset(ClientLevel level, double nowSec) {
        lastLevel = level;
        lastTimeSec = nowSec;
        TRACKED.clear();
        for (Ripple r : RIPPLES) {
            r.active = false;
        }
    }
}
