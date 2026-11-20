package com.g2806.radiante.client.render;

import com.g2806.radiante.client.option.Options;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.ClientLevel;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.core.BlockPos;
import net.minecraft.world.level.biome.Biome;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.phys.Vec3;

/**
 * Footprints: each foot that comes down where it snows (Footsteps) leaves its print, the square of the leg itself.
 * Snow falling on them fills them back in; out of the snowfall they stay, settling slowly. Feet still wet from the
 * water tread the snow into slush, and feet that come off the snow carry it onto the next floor, a house's, for a few
 * steps more, where it melts and dries within half a minute; wet feet leave water on a floor the same way. The shaders
 * draw them (skyUBO.snowPrints, rain_wetness.glsl vptSnowPrints).
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
    private static final int MELT = 5;

    /** Seconds standing still with a heat source before the snow melts, how fast its round grows (blocks a second),
     * and how far the player may sway without having moved (blocks). */
    private static final double MELT_DELAY = 2.0;
    private static final double MELT_GROWTH = 0.5;
    private static final double MELT_STILL = 0.3;
    /** The print the held heat source is melting now (-1 none), how long the player has stood still, and where. */
    private static int meltSlot = -1;
    private static double meltStill;
    private static double meltX;
    private static double meltY;
    private static double meltZ;

    /** Seconds of snowfall that fill a print back in, and of no snowfall that settle one away. */
    private static final double FILL_SECONDS = 45.0;
    private static final double SETTLE_SECONDS = 1200.0;
    /** Seconds the snow carried onto a floor takes to melt and the water it leaves to dry. */
    private static final double CARRIED_SECONDS = 30.0;
    /** Seconds the water left on a floor by wet feet takes to dry. */
    private static final double WET_SECONDS = 40.0;
    /** Steps the snow on the feet lasts for off the snow. */
    private static final int CARRIED_STEPS = 6;
    /** How much older each of those prints starts than the one before, as the feet run out of snow or water. */
    private static final double CARRIED_FADE = 0.09;
    private static final double WET_FADE = 0.06;
    /** Seconds after the snow stops that the snow it laid on the ground is still taken to be there. */
    private static final double SNOW_LINGERS = 200.0;
    private static final double RANGE_SQ = 96.0 * 96.0;

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
    /** Per walker: the steps the snow on the feet lasts for, and whether the step being made still carries some. */
    private static final java.util.Map<Integer, int[]> SNOW_ON_FEET = new java.util.HashMap<>();
    private static ClientLevel lastLevel;
    private static double lastNow = Double.NaN;
    private static double lastSnowfall = Double.NEGATIVE_INFINITY;

    private SnowPrints() {
    }

    /** The prints for this frame, relative to the camera, laid out for the sky uniform; after Footsteps.update. */
    public static float[] update(Minecraft minecraft, Vec3 cameraPos, long gameTime, float partialTick) {
        java.util.Arrays.fill(PACKED_FLOATS, 0.0f);
        ClientLevel level = minecraft.level;
        if (level != lastLevel) {
            lastLevel = level;
            count = 0;
            SNOW_ON_FEET.clear();
            lastNow = Double.NaN;
            lastSnowfall = Double.NEGATIVE_INFINITY;
        }
        if (level == null || !Options.snowAccumulation || !RadianteRenderer.isRayTracingEnabled()) {
            return PACKED_FLOATS;
        }
        double now = (gameTime + partialTick) / 20.0;
        // Game time, so a paused game fills nothing in; at most half a second at once, after a hitch.
        double dt = Double.isNaN(lastNow) ? 0.0 : Math.min(Math.max(now - lastNow, 0.0), 0.5);
        lastNow = now;
        float snowfall = level.getRainLevel(partialTick);
        if (snowfall > 0.0f) {
            lastSnowfall = now;
        }
        for (Footsteps.Footfall fall : Footsteps.footfalls()) {
            int[] snowOnFeet = SNOW_ON_FEET.computeIfAbsent(fall.entityId, id -> new int[2]);
            int kind = printKind(level, snowOnFeet, fall, now);
            if (kind < 0) {
                continue;
            }
            // Snow and water off the feet run out: each step's prints start a little older than the last's.
            double startAge = kind == CARRIED_SNOW ? (CARRIED_STEPS - 1 - snowOnFeet[0]) * CARRIED_FADE
                : kind == WET ? (Footsteps.WET_STEPS - fall.wetLeft) * WET_FADE : 0.0;
            int slot = freeSlot();
            BlockPos at = BlockPos.containing(fall.x, fall.y + 0.05, fall.z);
            X[slot] = fall.x;
            Y[slot] = fall.y;
            Z[slot] = fall.z;
            YAW[slot] = fall.yaw;
            KIND[slot] = kind;
            SIZE[slot] = fall.size;
            AGE[slot] = startAge;
            SNOWED_ON[slot] = level.canSeeSky(at)
                && level.getBiome(at).value().getPrecipitationAt(at, level.getSeaLevel()) == Biome.Precipitation.SNOW;
        }
        if (SNOW_ON_FEET.size() > 256) {
            SNOW_ON_FEET.values().removeIf(state -> state[0] == 0);
        }
        heldMelt(minecraft, level, now, dt);

        int packed = 0;
        for (int i = 0; i < count; i++) {
            AGE[i] += dt * fillRate(KIND[i], SNOWED_ON[i], snowfall);
            if (AGE[i] >= 1.0) {
                continue;
            }
            double dx = X[i] - cameraPos.x;
            double dy = Y[i] - cameraPos.y;
            double dz = Z[i] - cameraPos.z;
            if (dx * dx + dy * dy + dz * dz > RANGE_SQ) {
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

    /**
     * A torch, lantern, campfire, lava bucket or bright light held standing still on the snow: after MELT_DELAY
     * seconds a round melts out from the feet, growing at MELT_GROWTH blocks a second up to what the block would melt
     * set down. Walked off or put away, the round stays and fills back in from its rim as the snow falls on it, as a
     * footprint does. It was the column under the feet warmed like a block, which put the melted ground back at once
     * and left the columns it had reached bare as squares when the player walked on.
     */
    private static void heldMelt(Minecraft minecraft, ClientLevel level, double now, double dt) {
        LocalPlayer player = minecraft.player;
        int heat = 0;
        if (player != null && player.onGround() && !player.isSpectator()) {
            heat = Math.max(RainExposure.heldHeatOf(player.getMainHandItem()),
                RainExposure.heldHeatOf(player.getOffhandItem()));
        }
        boolean moved = player == null || Math.abs(player.getX() - meltX) > MELT_STILL
            || Math.abs(player.getZ() - meltZ) > MELT_STILL || Math.abs(player.getY() - meltY) > 0.3;
        if (heat == 0 || moved) {
            meltSlot = -1;
            meltStill = 0.0;
            if (player != null) {
                meltX = player.getX();
                meltY = player.getY();
                meltZ = player.getZ();
            }
            return;
        }
        meltStill += dt;
        BlockPos feet = BlockPos.containing(meltX, meltY + 0.05, meltZ);
        boolean snowyGround = now - lastSnowfall < SNOW_LINGERS && level.canSeeSky(feet)
            && level.getBiome(feet).value().getPrecipitationAt(feet, level.getSeaLevel()) == Biome.Precipitation.SNOW;
        if (meltStill < MELT_DELAY || !snowyGround) {
            return;
        }
        if (meltSlot < 0 || KIND[meltSlot] != MELT) {
            meltSlot = freeSlot();
            X[meltSlot] = meltX;
            Y[meltSlot] = meltY;
            Z[meltSlot] = meltZ;
            YAW[meltSlot] = 0.0f;
            KIND[meltSlot] = MELT;
            SNOWED_ON[meltSlot] = true;
        }
        double reach = heat >= 3 ? 2.4 : 1.5;
        double radius = Math.min(reach, 0.3 + (meltStill - MELT_DELAY) * MELT_GROWTH);
        SIZE[meltSlot] = (int) Math.round(radius * 2.0 * 16.0);
        AGE[meltSlot] = 0.0;
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

    /** What a footfall leaves, or -1 for nothing; spends the snow on the feet a step uses. */
    private static int printKind(ClientLevel level, int[] snowOnFeet, Footsteps.Footfall fall, double now) {
        BlockPos feet = BlockPos.containing(fall.x, fall.y + 0.05, fall.z);
        BlockPos below = BlockPos.containing(fall.x, fall.y - 0.05, fall.z);
        BlockState at = level.getBlockState(feet);
        BlockState under = level.getBlockState(below);
        boolean snowBlock = at.is(Blocks.SNOW) || under.is(Blocks.SNOW) || under.is(Blocks.SNOW_BLOCK)
            || under.is(Blocks.POWDER_SNOW);
        // Ground where it snows has the snow the shaders lay on it while it snows and for a while after.
        boolean snowyGround = !snowBlock && now - lastSnowfall < SNOW_LINGERS && level.canSeeSky(feet)
            && level.getBiome(feet).value().getPrecipitationAt(feet, level.getSeaLevel()) == Biome.Precipitation.SNOW;
        if (snowBlock || snowyGround) {
            snowOnFeet[0] = CARRIED_STEPS;
            snowOnFeet[1] = 0;
            return fall.wetLeft > 0 ? SLUSH : snowBlock ? PACKED : PRESSED;
        }
        // A floor to leave the snow or water on: something to stand on, not a liquid.
        if (under.isAir() || !under.getFluidState().isEmpty() || under.getCollisionShape(level, below).isEmpty()) {
            return -1;
        }
        if (fall.wetLeft > 0) {
            // The water has washed the snow off the feet.
            snowOnFeet[0] = 0;
            return WET;
        }
        // The feet that come down together in a step, or a landing, share its snow: it is spent once.
        if (fall.first) {
            snowOnFeet[1] = snowOnFeet[0] > 0 ? 1 : 0;
            snowOnFeet[0] = Math.max(0, snowOnFeet[0] - 1);
        }
        return snowOnFeet[1] != 0 ? CARRIED_SNOW : -1;
    }
}
