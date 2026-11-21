package com.g2806.radiante.client.render;

import com.g2806.radiante.client.option.Options;
import com.g2806.radiante.compat.VersionCompat;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.ClientLevel;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.core.BlockPos;
import net.minecraft.core.particles.ParticleTypes;
import net.minecraft.tags.FluidTags;
import net.minecraft.util.RandomSource;
import net.minecraft.world.level.material.FluidState;
import net.minecraft.world.phys.Vec3;

/**
 * Foam and mist on moving water, with Minecraft's own particles: a few splashes ride the surface of flowing water
 * along its current, and spray and a puff of mist rise where a waterfall lands in water. Still water and source
 * blocks stay calm. A handful of blocks near the player are looked at each tick, so a river costs no more than a pond.
 */
public final class WaterRapids {

    /** Blocks looked at each tick, and how far from the player. */
    private static final int SAMPLES = 48;
    private static final int RANGE = 16;
    /** How far down a waterfall is followed to where it lands. */
    private static final int FALL_SCAN = 12;

    private static final BlockPos.MutableBlockPos POS = new BlockPos.MutableBlockPos();
    private static final BlockPos.MutableBlockPos BELOW = new BlockPos.MutableBlockPos();

    private WaterRapids() {
    }

    private static long foamReadAt;
    private static boolean foam;

    /** Water Foam & Mist, the pack's attribute (RtAttributes.WATER_FOAM_ATTRIBUTE), read a few times a second. */
    private static boolean foamOn() {
        long now = System.nanoTime();
        if (now - foamReadAt > 250_000_000L) {
            foamReadAt = now;
            foam = com.g2806.radiante.client.pipeline.PipelineSettings.isShaderPackToggleOn(
                com.g2806.radiante.client.pipeline.RtAttributes.WATER_FOAM_ATTRIBUTE);
        }
        return foam;
    }

    /** Once a client tick. */
    public static void tick(Minecraft minecraft) {
        ClientLevel level = minecraft.level;
        LocalPlayer player = minecraft.player;
        if (level == null || player == null || !foamOn() || minecraft.isPaused()
            || !RadianteRenderer.isRayTracingEnabled()) {
            return;
        }
        RandomSource random = level.getRandom();
        BlockPos origin = player.blockPosition();
        for (int i = 0; i < SAMPLES; i++) {
            POS.set(origin.getX() + random.nextInt(RANGE * 2 + 1) - RANGE,
                origin.getY() + random.nextInt(RANGE + 1) - RANGE / 2,
                origin.getZ() + random.nextInt(RANGE * 2 + 1) - RANGE);
            FluidState fluid = level.getFluidState(POS);
            if (!fluid.is(FluidTags.WATER) || fluid.isSource()) {
                continue;
            }
            if (VersionCompat.isFalling(fluid)) {
                waterfall(level, random);
            } else if (random.nextFloat() < 0.35f) {
                current(level, fluid, random);
            }
        }
    }

    /** A splash on the surface of flowing water, carried along its current. */
    private static void current(ClientLevel level, FluidState fluid, RandomSource random) {
        BlockPos above = POS.above();
        if (!level.getFluidState(above).isEmpty() || !level.getBlockState(above).getCollisionShape(level, above).isEmpty()) {
            return;
        }
        Vec3 flow = fluid.getFlow(level, POS);
        if (flow.lengthSqr() < 0.01) {
            return;
        }
        double x = POS.getX() + random.nextDouble();
        double y = POS.getY() + fluid.getHeight(level, POS) + 0.02;
        double z = POS.getZ() + random.nextDouble();
        level.addParticle(random.nextFloat() < 0.8f ? ParticleTypes.SPLASH : ParticleTypes.BUBBLE_POP, x, y, z,
            flow.x * 0.12, 0.02, flow.z * 0.12);
    }

    /** Spray and mist where a falling column of water lands in water. */
    private static void waterfall(ClientLevel level, RandomSource random) {
        BELOW.set(POS);
        for (int i = 0; i < FALL_SCAN; i++) {
            BELOW.move(0, -1, 0);
            FluidState below = level.getFluidState(BELOW);
            if (below.is(FluidTags.WATER) && VersionCompat.isFalling(below)) {
                continue;
            }
            // Landed: in water (a pool, a river) it foams; on dry ground the falling water just spreads out.
            if (!below.is(FluidTags.WATER) || !VersionCompat.isFalling(level.getFluidState(BELOW.above()))) {
                return;
            }
            double surface = BELOW.getY() + below.getHeight(level, BELOW);
            double x = BELOW.getX() + 0.15 + random.nextDouble() * 0.7;
            double z = BELOW.getZ() + 0.15 + random.nextDouble() * 0.7;
            for (int spray = 0; spray < 2; spray++) {
                level.addParticle(ParticleTypes.SPLASH, x + random.nextGaussian() * 0.2, surface + 0.05,
                    z + random.nextGaussian() * 0.2, random.nextGaussian() * 0.06, 0.15, random.nextGaussian() * 0.06);
            }
            if (random.nextFloat() < 0.25f) {
                level.addParticle(ParticleTypes.CLOUD, x, surface + 0.2, z, random.nextGaussian() * 0.01, 0.03,
                    random.nextGaussian() * 0.01);
            }
            return;
        }
    }
}
