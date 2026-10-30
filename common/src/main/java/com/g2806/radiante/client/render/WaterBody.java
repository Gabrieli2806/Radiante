package com.g2806.radiante.client.render;

import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.ClientLevel;
import net.minecraft.core.BlockPos;
import net.minecraft.tags.FluidTags;
import net.minecraft.world.phys.Vec3;

/**
 * How big the body of water around the camera is, so that radiante-pt can tell a pool from the sea: the biome alone
 * coloured a 1x1 pool like the deep ocean once the head was in it. Read by the shaders through skyUBO's
 * waterBodyDepth and waterBodyWidth, and by shader packs through radianteWaterDepth and radianteWaterWidth
 * (common/pack_world.glsl).
 *
 * <p>Depth is the height of the water column the eye is in, from the floor to the surface. Width is the average,
 * over four lines through the eye (along x, along z and the two diagonals), of how far the water runs at eye level.
 * Both are measured again only when the eye moves into another block or every {@link #RESCAN_FRAMES} frames (the
 * water can be drained or poured), and eased with a half-life of {@link #HALF_LIFE} seconds so that swimming from a
 * pool into a river does not switch the look at once. Out of water both are 0.
 */
public final class WaterBody {

    /** Deepest column measured, in blocks: deeper water looks the same. */
    private static final int MAX_DEPTH = 64;
    /** Furthest the water is followed in each direction, in blocks. */
    private static final int MAX_REACH = 24;
    private static final int RESCAN_FRAMES = 20;
    private static final float HALF_LIFE = 0.4f;
    private static final int[][] DIRECTIONS = {{1, 0}, {0, 1}, {1, 1}, {1, -1}};

    private static float depth;
    private static float width;
    private static float targetDepth;
    private static float targetWidth;
    private static long lastBlock = Long.MIN_VALUE;
    private static int framesSinceScan;
    private static long lastNanos;
    private static Object level;

    private WaterBody() {
    }

    /** Call once per frame. */
    public static void update(Minecraft minecraft, Vec3 cameraPos) {
        long now = System.nanoTime();
        float seconds = lastNanos == 0L ? 0.0f : Math.min((now - lastNanos) / 1.0e9f, 1.0f);
        lastNanos = now;
        ClientLevel world = minecraft.level;
        BlockPos eye = BlockPos.containing(cameraPos);
        if (world != level) {
            level = world;
            lastBlock = Long.MIN_VALUE;
            depth = width = targetDepth = targetWidth = 0.0f;
        }
        if (world == null || !world.getFluidState(eye).is(FluidTags.WATER)) {
            // Out of the water it is gone at once: the next dive measures afresh rather than easing from the last.
            depth = width = targetDepth = targetWidth = 0.0f;
            lastBlock = Long.MIN_VALUE;
            return;
        }
        boolean entering = lastBlock == Long.MIN_VALUE;
        if (eye.asLong() != lastBlock || ++framesSinceScan >= RESCAN_FRAMES) {
            lastBlock = eye.asLong();
            framesSinceScan = 0;
            targetDepth = measureDepth(world, eye);
            targetWidth = measureWidth(world, eye);
        }
        if (entering) {
            depth = targetDepth;
            width = targetWidth;
        } else {
            float ease = 1.0f - (float) Math.pow(0.5, seconds / HALF_LIFE);
            depth += (targetDepth - depth) * ease;
            width += (targetWidth - width) * ease;
        }
    }

    /** Height of the water column at the eye, floor to surface, in blocks; 0 out of water. */
    public static float depth() {
        return depth;
    }

    /** How far the water runs across at eye level, in blocks (1 for a 1x1 pool); 0 out of water. */
    public static float width() {
        return width;
    }

    private static float measureDepth(ClientLevel world, BlockPos eye) {
        BlockPos.MutableBlockPos pos = eye.mutable();
        int top = eye.getY();
        while (top - eye.getY() < MAX_DEPTH && isWater(world, pos.setY(top + 1))) {
            top++;
        }
        int bottom = eye.getY();
        while (top - bottom < MAX_DEPTH && isWater(world, pos.setY(bottom - 1))) {
            bottom--;
        }
        return top - bottom + 1;
    }

    private static float measureWidth(ClientLevel world, BlockPos eye) {
        float total = 0.0f;
        BlockPos.MutableBlockPos pos = new BlockPos.MutableBlockPos();
        for (int[] direction : DIRECTIONS) {
            float span = 1.0f;
            for (int sign = -1; sign <= 1; sign += 2) {
                for (int step = 1; step <= MAX_REACH; step++) {
                    pos.set(eye.getX() + direction[0] * step * sign, eye.getY(),
                        eye.getZ() + direction[1] * step * sign);
                    if (!isWater(world, pos)) {
                        break;
                    }
                    // A diagonal step crosses about 1.4 blocks.
                    span += direction[0] != 0 && direction[1] != 0 ? 1.4142135f : 1.0f;
                }
            }
            total += span;
        }
        return total / DIRECTIONS.length;
    }

    private static boolean isWater(ClientLevel world, BlockPos pos) {
        return world.getFluidState(pos).is(FluidTags.WATER);
    }
}
