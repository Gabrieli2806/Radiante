package com.g2806.radiante.client.render;

import com.g2806.radiante.client.option.Options;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.ClientLevel;
import net.minecraft.core.BlockPos;
import net.minecraft.util.Mth;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.level.biome.Biome;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.phys.Vec3;

/**
 * Footprints: the players and mobs walking where it snows leave prints where their feet come down in the walk, the
 * square of the leg itself, left and right in turn. Snow falling on them fills them back in; out of the snowfall
 * they stay, settling slowly. Feet still wet from the water tread the snow into slush for a few steps, and feet that
 * come off the snow carry it onto the next floor, a house's, for a few steps more, where it melts and dries within
 * half a minute; wet feet leave water on a floor the same way. The shaders draw them (skyUBO.snowPrints,
 * rain_wetness.glsl vptSnowPrints).
 *
 * <p>Up to {@link #MAX} prints are kept; laid out for the sky uniform after the water ripples.
 */
public final class SnowPrints {

    /** Mirrors PRISM_SNOW_PRINT_MAX (common/shared.hpp). */
    public static final int MAX = 128;
    /** Floats in the sky uniform: a vec4 of control, a vec4 a print, then the ages four to a vec4. */
    public static final int FLOATS = 4 + MAX * 4 + MAX;

    // The kinds of print, as rain_wetness.glsl reads them (SNOW_PRINT_*).
    private static final int PRESSED = 0;
    private static final int PACKED = 1;
    private static final int SLUSH = 2;
    private static final int CARRIED_SNOW = 3;
    private static final int WET = 4;

    /** How far round the walk animation goes between one foot coming down and the other: half a stride. */
    private static final double HALF_STRIDE = Math.PI / 0.6662;
    /** A player's leg: how far it is from the middle of the body, and how long it is, in blocks. */
    private static final double LEG_SIDE = 1.9 / 16.0;
    private static final double LEG_LENGTH = 12.0 / 16.0;
    /** Seconds of snowfall that fill a print back in, and of no snowfall that settle one away. */
    private static final double FILL_SECONDS = 45.0;
    private static final double SETTLE_SECONDS = 1200.0;
    /** Seconds the snow carried onto a floor takes to melt and the water it leaves to dry. */
    private static final double CARRIED_SECONDS = 30.0;
    /** Seconds the water left on a floor by wet feet takes to dry. */
    private static final double WET_SECONDS = 40.0;
    /** Steps the snow on the feet lasts for off the snow, and the water on them out of the water. */
    private static final int CARRIED_STEPS = 6;
    private static final int WET_STEPS = 8;
    /** How much older each of those prints starts than the one before, as the feet run out of snow or water. */
    private static final double CARRIED_FADE = 0.09;
    private static final double WET_FADE = 0.06;
    /** Seconds after the snow stops that the snow it laid on the ground is still taken to be there. */
    private static final double SNOW_LINGERS = 200.0;
    private static final double RANGE_SQ = 48.0 * 48.0;

    private static final double[] X = new double[MAX];
    private static final double[] Y = new double[MAX];
    private static final double[] Z = new double[MAX];
    private static final float[] YAW = new float[MAX];
    private static final int[] KIND = new int[MAX];
    private static final int[] SIZE = new int[MAX];
    /** How far each print has filled back in, melted or dried: 0 fresh, 1 gone. */
    private static final double[] AGE = new double[MAX];
    /** Whether snow falls on the print: open to the sky in a biome where it snows. */
    private static final boolean[] SNOWED_ON = new boolean[MAX];
    private static int count;
    private static final float[] PACKED_FLOATS = new float[FLOATS];
    private static final java.util.Map<Integer, Walker> WALKERS = new java.util.HashMap<>();
    private static ClientLevel lastLevel;
    private static long frame;
    private static double lastNow = Double.NaN;
    private static double lastSnowfall = Double.NEGATIVE_INFINITY;

    private static final class Walker {
        /** The last time a foot came down, counted in half strides of the walk animation. */
        long step;
        long seen;
        /** Steps left of the snow on the feet, and of the water. */
        int snowSteps;
        int wetSteps;
    }

    private SnowPrints() {
    }

    /** The prints for this frame, relative to the camera, laid out for the sky uniform. */
    public static float[] update(Minecraft minecraft, Vec3 cameraPos, long gameTime, float partialTick) {
        java.util.Arrays.fill(PACKED_FLOATS, 0.0f);
        ClientLevel level = minecraft.level;
        if (level != lastLevel) {
            lastLevel = level;
            count = 0;
            WALKERS.clear();
            lastNow = Double.NaN;
            lastSnowfall = Double.NEGATIVE_INFINITY;
        }
        if (level == null || !Options.snowAccumulation || !RadianteRenderer.isRayTracingEnabled()) {
            return PACKED_FLOATS;
        }
        double now = (gameTime + partialTick) / 20.0;
        // Game time, so a paused game fills nothing in; at most half a second at once, after a hitch.
        double dt = Double.isNaN(lastNow) ? 0.0 : Mth.clamp(now - lastNow, 0.0, 0.5);
        lastNow = now;
        float snowfall = level.getRainLevel(partialTick);
        if (snowfall > 0.0f) {
            lastSnowfall = now;
        }
        frame++;
        for (Entity entity : level.entitiesForRendering()) {
            if (!(entity instanceof LivingEntity living) || entity.isSpectator() || entity.getBbWidth() < 0.3f) {
                continue;
            }
            double x = Mth.lerp(partialTick, entity.xo, entity.getX());
            double y = Mth.lerp(partialTick, entity.yo, entity.getY());
            double z = Mth.lerp(partialTick, entity.zo, entity.getZ());
            if (cameraPos.distanceToSqr(x, y, z) > RANGE_SQ) {
                continue;
            }
            // A foot comes down each time the legs swing to the end of their stride (HumanoidModel and
            // QuadrupedModel swing them by the cosine of 0.6662 times the walk animation's position).
            float walk = living.walkAnimation.position(partialTick);
            long step = Math.round(walk / HALF_STRIDE);
            Walker walker = WALKERS.get(entity.getId());
            if (walker == null) {
                walker = new Walker();
                walker.step = step;
                WALKERS.put(entity.getId(), walker);
            }
            walker.seen = frame;
            if (entity.isInWater()) {
                // Out of the water the feet are wet, and the water has washed the snow off them.
                walker.wetSteps = WET_STEPS;
                walker.snowSteps = 0;
                walker.step = step;
                continue;
            }
            if (step == walker.step) {
                continue;
            }
            walker.step = step;
            float swing = living.walkAnimation.speed(partialTick);
            if (!entity.onGround() || swing < 0.05f) {
                continue;
            }
            // Where the foot is: to its side of the body, and ahead of it by as far as the leg swings.
            float bodyYaw = (float) Math.toRadians(Mth.rotLerp(partialTick, living.yBodyRotO, living.yBodyRot));
            double scale = entity.getBbHeight() / 1.8;
            boolean left = (step & 1L) == 0L;
            double side = (left ? 1.0 : -1.0) * LEG_SIDE * entity.getBbWidth() / 0.6;
            double ahead = Math.sin(Math.min(1.4 * swing, 1.2)) * LEG_LENGTH * scale;
            // Minecraft's frame: the way faced is (-sin, cos) of the yaw, and (cos, sin) is the left.
            double footX = x - Math.sin(bodyYaw) * ahead + Math.cos(bodyYaw) * side;
            double footZ = z + Math.cos(bodyYaw) * ahead + Math.sin(bodyYaw) * side;
            int kind = printKind(level, walker, footX, y, footZ, now);
            if (kind < 0) {
                continue;
            }
            // Snow and water off the feet run out: each print starts a little older than the one before.
            double startAge = kind == CARRIED_SNOW ? (CARRIED_STEPS - 1 - walker.snowSteps) * CARRIED_FADE
                : kind == WET ? (WET_STEPS - 1 - walker.wetSteps) * WET_FADE : 0.0;
            int slot = freeSlot();
            BlockPos at = BlockPos.containing(footX, y + 0.05, footZ);
            X[slot] = footX;
            Y[slot] = y;
            Z[slot] = footZ;
            YAW[slot] = bodyYaw;
            KIND[slot] = kind;
            // A leg is four pixels square; a chicken's foot two.
            SIZE[slot] = entity.getBbWidth() >= 0.5f ? 4 : 2;
            AGE[slot] = startAge;
            SNOWED_ON[slot] = level.canSeeSky(at)
                && level.getBiome(at).value().getPrecipitationAt(at, level.getSeaLevel()) == Biome.Precipitation.SNOW;
        }
        if ((frame & 255) == 0) {
            WALKERS.values().removeIf(walker -> frame - walker.seen > 600);
        }

        int packed = 0;
        for (int i = 0; i < count; i++) {
            AGE[i] += dt * fillRate(KIND[i], SNOWED_ON[i], snowfall);
            if (AGE[i] >= 1.0) {
                continue;
            }
            double dx = X[i] - cameraPos.x;
            double dy = Y[i] - cameraPos.y;
            double dz = Z[i] - cameraPos.z;
            if (dx * dx + dy * dy + dz * dz > RANGE_SQ * 4.0) {
                continue;
            }
            int base = 4 + packed * 4;
            PACKED_FLOATS[base] = (float) dx;
            PACKED_FLOATS[base + 1] = (float) dy;
            PACKED_FLOATS[base + 2] = (float) dz;
            PACKED_FLOATS[base + 3] = YAW[i];
            // The age, plus twice the kind, plus sixteen times the size in pixels (rain_wetness.glsl reads them
            // back); kept below 1 so the rest read back whole.
            PACKED_FLOATS[4 + MAX * 4 + packed] = (float) Math.min(AGE[i], 0.999) + 2.0f * KIND[i] + 16.0f * SIZE[i];
            packed++;
        }
        PACKED_FLOATS[0] = packed;
        return PACKED_FLOATS;
    }

    /** A slot for a new print: a free one, else the one most nearly gone (filled in, melted or dried). */
    private static int freeSlot() {
        int slot = -1;
        double oldest = -1.0;
        for (int i = 0; i < count; i++) {
            if (AGE[i] >= 1.0) {
                return i;
            }
            if (AGE[i] > oldest) {
                oldest = AGE[i];
                slot = i;
            }
        }
        return count < MAX ? count++ : slot;
    }

    /** How fast a print goes, a share of it a second. */
    private static double fillRate(int kind, boolean snowedOn, float snowfall) {
        return switch (kind) {
            case CARRIED_SNOW -> 1.0 / CARRIED_SECONDS;
            case WET -> 1.0 / WET_SECONDS;
            // The snow falling on it fills it in; out of the snowfall it only settles, over a long while.
            default -> (snowedOn ? snowfall / FILL_SECONDS : 0.0) + 1.0 / SETTLE_SECONDS;
        };
    }

    /** What this step leaves, or -1 for nothing; spends the snow or water on the feet it uses. */
    private static int printKind(ClientLevel level, Walker walker, double x, double y, double z, double now) {
        BlockPos feet = BlockPos.containing(x, y + 0.05, z);
        BlockPos below = BlockPos.containing(x, y - 0.05, z);
        BlockState at = level.getBlockState(feet);
        BlockState under = level.getBlockState(below);
        boolean snowBlock = at.is(Blocks.SNOW) || under.is(Blocks.SNOW) || under.is(Blocks.SNOW_BLOCK)
            || under.is(Blocks.POWDER_SNOW);
        // Ground where it snows has the snow the shaders lay on it while it snows and for a while after.
        boolean snowyGround = !snowBlock && now - lastSnowfall < SNOW_LINGERS && level.canSeeSky(feet)
            && level.getBiome(feet).value().getPrecipitationAt(feet, level.getSeaLevel()) == Biome.Precipitation.SNOW;
        if (snowBlock || snowyGround) {
            walker.snowSteps = CARRIED_STEPS;
            if (walker.wetSteps > 0) {
                walker.wetSteps--;
                return SLUSH;
            }
            return snowBlock ? PACKED : PRESSED;
        }
        // A floor to leave the snow or water on: something to stand on, not a liquid.
        if (under.isAir() || !under.getFluidState().isEmpty() || under.getCollisionShape(level, below).isEmpty()) {
            return -1;
        }
        if (walker.wetSteps > 0) {
            walker.wetSteps--;
            return WET;
        }
        if (walker.snowSteps > 0) {
            walker.snowSteps--;
            return CARRIED_SNOW;
        }
        return -1;
    }
}
