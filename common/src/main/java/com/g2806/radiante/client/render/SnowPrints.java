package com.g2806.radiante.client.render;

import com.g2806.radiante.client.option.Options;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.ClientLevel;
import net.minecraft.core.BlockPos;
import net.minecraft.util.Mth;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.level.biome.Biome;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.phys.Vec3;

/**
 * Footprints in snow: the players and mobs walking where it snows leave prints, left and right in turn, that the
 * snow fills back in over a couple of minutes. The shaders press them into the snow the rain_wetness code lays down
 * and into snow blocks (skyUBO.snowPrints, rain_wetness.glsl vptSnowPrint).
 *
 * <p>The newest {@link #MAX} prints are kept; laid out for the sky uniform after the water ripples.
 */
public final class SnowPrints {

    /** Mirrors PRISM_SNOW_PRINT_MAX (common/shared.hpp). */
    public static final int MAX = 32;
    /** Floats in the sky uniform: a vec4 of control, a vec4 a print, then the ages four to a vec4. */
    public static final int FLOATS = 4 + MAX * 4 + MAX;

    /** How far apart one foot's prints are from the other's, in blocks walked. */
    private static final double STRIDE = 0.55;
    /** How far each print sits to the side of the line walked. */
    private static final double SIDE_OFFSET = 0.13;
    /** Seconds the snow takes to fill a print back in; half that while it is snowing. */
    private static final double FILL_SECONDS = 150.0;
    private static final double RANGE_SQ = 48.0 * 48.0;
    /** Added to a print's age when it was left in a snow layer or block, whose own snow the shaders grey with it. */
    private static final float IN_SNOW_BLOCK = 2.0f;

    private static final double[] X = new double[MAX];
    private static final double[] Y = new double[MAX];
    private static final double[] Z = new double[MAX];
    private static final float[] YAW = new float[MAX];
    private static final boolean[] IN_BLOCK = new boolean[MAX];
    private static final double[] MADE = new double[MAX];
    private static int next;
    private static int count;
    private static final float[] PACKED = new float[FLOATS];
    private static final java.util.Map<Integer, Walker> WALKERS = new java.util.HashMap<>();
    private static ClientLevel lastLevel;
    private static long frame;

    private static final class Walker {
        double x;
        double z;
        double walked;
        boolean left;
        long seen;
    }

    private SnowPrints() {
    }

    /** The prints for this frame, relative to the camera, laid out for the sky uniform. */
    public static float[] update(Minecraft minecraft, Vec3 cameraPos, long gameTime, float partialTick) {
        java.util.Arrays.fill(PACKED, 0.0f);
        ClientLevel level = minecraft.level;
        if (level != lastLevel) {
            lastLevel = level;
            count = 0;
            next = 0;
            WALKERS.clear();
        }
        if (level == null || !Options.snowAccumulation || !RadianteRenderer.isRayTracingEnabled()) {
            return PACKED;
        }
        double now = (gameTime + partialTick) / 20.0;
        frame++;
        for (Entity entity : level.entitiesForRendering()) {
            if (!(entity instanceof net.minecraft.world.entity.LivingEntity) || entity.isSpectator()
                || !entity.onGround() || entity.getBbWidth() < 0.3f) {
                continue;
            }
            double x = Mth.lerp(partialTick, entity.xo, entity.getX());
            double y = Mth.lerp(partialTick, entity.yo, entity.getY());
            double z = Mth.lerp(partialTick, entity.zo, entity.getZ());
            if (cameraPos.distanceToSqr(x, y, z) > RANGE_SQ) {
                continue;
            }
            Walker walker = WALKERS.computeIfAbsent(entity.getId(), id -> {
                Walker fresh = new Walker();
                fresh.x = x;
                fresh.z = z;
                return fresh;
            });
            walker.seen = frame;
            double step = Math.hypot(x - walker.x, z - walker.z);
            walker.x = x;
            walker.z = z;
            if (step > 2.0) {
                walker.walked = 0.0; // teleported
                continue;
            }
            walker.walked += step;
            if (walker.walked < STRIDE) {
                continue;
            }
            walker.walked = 0.0;
            int snow = snowUnderfoot(level, x, y, z);
            if (snow == 0) {
                continue;
            }
            float yaw = (float) Math.toRadians(entity.getYRot());
            double side = walker.left ? SIDE_OFFSET : -SIDE_OFFSET;
            walker.left = !walker.left;
            // To the side of the way the walker faces: right is (cos, sin) of the yaw in Minecraft's frame.
            X[next] = x + Math.cos(yaw) * side;
            Y[next] = y;
            Z[next] = z + Math.sin(yaw) * side;
            YAW[next] = yaw;
            IN_BLOCK[next] = snow == 2;
            MADE[next] = now;
            next = (next + 1) % MAX;
            count = Math.min(count + 1, MAX);
        }
        if ((frame & 255) == 0) {
            WALKERS.values().removeIf(walker -> frame - walker.seen > 600);
        }

        double fill = level.isRaining() ? FILL_SECONDS * 0.5 : FILL_SECONDS;
        int packed = 0;
        for (int i = 0; i < count; i++) {
            double age = (now - MADE[i]) / fill;
            if (age < 0.0 || age >= 1.0) {
                continue;
            }
            int base = 4 + packed * 4;
            PACKED[base] = (float) (X[i] - cameraPos.x);
            PACKED[base + 1] = (float) (Y[i] - cameraPos.y);
            PACKED[base + 2] = (float) (Z[i] - cameraPos.z);
            PACKED[base + 3] = YAW[i];
            PACKED[4 + MAX * 4 + packed] = (float) age + (IN_BLOCK[i] ? IN_SNOW_BLOCK : 0.0f);
            packed++;
        }
        PACKED[0] = packed;
        return PACKED;
    }

    /** Snow to step in: 2 a snow layer or block underfoot, 1 ground where it snows, 0 none. */
    private static int snowUnderfoot(ClientLevel level, double x, double y, double z) {
        BlockPos feet = BlockPos.containing(x, y + 0.05, z);
        BlockPos below = BlockPos.containing(x, y - 0.05, z);
        BlockState at = level.getBlockState(feet);
        BlockState under = level.getBlockState(below);
        if (at.is(Blocks.SNOW) || under.is(Blocks.SNOW) || under.is(Blocks.SNOW_BLOCK)
            || under.is(Blocks.POWDER_SNOW)) {
            return 2;
        }
        return level.getBiome(feet).value().getPrecipitationAt(feet, level.getSeaLevel()) == Biome.Precipitation.SNOW
            ? 1 : 0;
    }
}
