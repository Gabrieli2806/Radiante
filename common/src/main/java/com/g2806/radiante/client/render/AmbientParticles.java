package com.g2806.radiante.client.render;

import com.g2806.radiante.client.option.Options;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.ClientLevel;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Holder;
import net.minecraft.core.particles.ParticleTypes;
import net.minecraft.tags.BiomeTags;
import net.minecraft.util.RandomSource;
import net.minecraft.world.level.Level;
import net.minecraft.world.level.biome.Biome;
import net.minecraft.world.level.biome.Biomes;
import net.minecraft.world.level.levelgen.Heightmap;
import org.joml.Vector3f;

/**
 * Things in the air that are there for the light to find, made of the game's own particles so they look like the rest
 * of it and the tracer lights them like any other:
 *
 * <ul>
 * <li>Fireflies, at night in swamps and forests: the firefly bush's own fireflies, drifting over the ground where no
 * bush is, a few in a forest and more in a swamp. Not in the rain.</li>
 * <li>Dust in shafts of light: motes where the sun reaches under a roof, through a window, a doorway or a hole in a
 * cave's ceiling. They are the end rod's glitter, a small pale speck that sinks slowly and is drawn at full light,
 * so it shines whichever way the sun stands (a mote lit by the sun alone showed black at noon: the game turns every
 * particle to face the camera, edge on to a sun overhead). Only where a ray from the mote to the sun is clear, so
 * they trace the shaft and nothing else.</li>
 * </ul>
 *
 * <p>Both are seen by this client only, each a few particles a second.
 */
public final class AmbientParticles {

    /** Night, in ticks of the day: from when the sun is down to before it comes up. */
    private static final long NIGHT_START = 13000L;
    private static final long NIGHT_END = 23000L;
    /** How far from the player fireflies appear, and how high over the ground they fly. */
    private static final double FIREFLY_RANGE = 20.0;
    private static final double FIREFLY_HEIGHT = 4.0;
    /** Fireflies a tick, on average: a forest's and a swamp's. */
    private static final float FOREST_RATE = 0.25f;
    private static final float SWAMP_RATE = 0.6f;

    /** How far from the player dust is looked for, the tries a tick, and how far towards the sun the way is checked. */
    private static final double DUST_RANGE = 10.0;
    private static final int DUST_TRIES = 32;
    private static final double SUN_REACH = 40.0;
    /** The sun has to stand this high (its direction's y) for its shafts to carry dust. */
    private static final float DUST_MIN_SUN = 0.12f;
    /** A mote's size against the end rod glitter it is made of. */
    private static final float DUST_SCALE = 0.3f;
    /** How far to each side shade is looked for: a shaft is sunlight with shade this near on two sides or more. */
    private static final double SHAFT_WIDTH = 2.5;

    /** The sun's direction as the renderer last traced it, towards the sun; null before a frame or with no sun. */
    private static volatile Vector3f sunDirection;

    private AmbientParticles() {
    }

    /** Told by the renderer each frame (RadianteRenderer): where the sun is, or null in a sky without one. */
    static void sun(Vector3f towardsSun) {
        sunDirection = towardsSun;
    }

    /** Once a client tick. */
    public static void tick(Minecraft minecraft) {
        ClientLevel level = minecraft.level;
        LocalPlayer player = minecraft.player;
        if (level == null || player == null || minecraft.isPaused() || !RadianteRenderer.isRayTracingEnabled()
            || com.g2806.radiante.client.photo.PhotoMode.isActive()) {
            return;
        }
        boolean raining = level.getRainLevel(1.0f) > 0.2f;
        RandomSource random = level.getRandom();
        if (Options.fireflies && !raining && level.dimension() == Level.OVERWORLD) {
            long time = Math.floorMod(level.getOverworldClockTime(), 24000L);
            if (time >= NIGHT_START && time <= NIGHT_END) {
                fireflies(level, player, random);
            }
        }
        Vector3f sun = sunDirection;
        if (Options.dustMotes && !raining && sun != null && sun.y() > DUST_MIN_SUN) {
            dust(level, player, random, sun);
        }
    }

    private static void fireflies(ClientLevel level, LocalPlayer player, RandomSource random) {
        float rate = rate(level.getBiome(player.blockPosition()));
        if (rate <= 0.0f) {
            return;
        }
        // The whole part of the rate, and its remainder by chance.
        int count = (int) rate + (random.nextFloat() < rate - (int) rate ? 1 : 0);
        BlockPos.MutableBlockPos at = new BlockPos.MutableBlockPos();
        for (int i = 0; i < count; i++) {
            double x = player.getX() + (random.nextDouble() * 2.0 - 1.0) * FIREFLY_RANGE;
            double z = player.getZ() + (random.nextDouble() * 2.0 - 1.0) * FIREFLY_RANGE;
            int ground = level.getHeight(Heightmap.Types.MOTION_BLOCKING_NO_LEAVES, (int) Math.floor(x),
                (int) Math.floor(z));
            double y = ground + 0.4 + random.nextDouble() * FIREFLY_HEIGHT;
            at.set(x, y, z);
            // Where they would be seen from here, over ground of their own biome, in the open air.
            if (Math.abs(y - player.getY()) > 16.0 || rate(level.getBiome(at)) <= 0.0f
                || !level.getBlockState(at).isAir()) {
                continue;
            }
            level.addParticle(ParticleTypes.FIREFLY, x, y, z, 0.0, 0.0, 0.0);
        }
    }

    /** Fireflies a tick in a biome: swamps most, forests some, anywhere else none. */
    private static float rate(Holder<Biome> biome) {
        if (biome.is(Biomes.SWAMP) || biome.is(Biomes.MANGROVE_SWAMP)) {
            return SWAMP_RATE;
        }
        return biome.is(BiomeTags.IS_FOREST) ? FOREST_RATE : 0.0f;
    }

    private static void dust(ClientLevel level, LocalPlayer player, RandomSource random, Vector3f sun) {
        BlockPos.MutableBlockPos at = new BlockPos.MutableBlockPos();
        for (int i = 0; i < DUST_TRIES; i++) {
            double x = player.getX() + (random.nextDouble() * 2.0 - 1.0) * DUST_RANGE;
            double y = player.getEyeY() + (random.nextDouble() * 2.0 - 1.0) * DUST_RANGE * 0.5;
            double z = player.getZ() + (random.nextDouble() * 2.0 - 1.0) * DUST_RANGE;
            at.set(x, y, z);
            // In the air, in the sun, and in a shaft of it: with shade to at least two sides. In the open the whole
            // air is lit and nothing shows as a shaft. (Not by whether the sky is overhead: under a hole in a roof
            // it is, and that shaft is the one most worth its dust.)
            if (!level.getBlockState(at).isAir() || !sunReaches(level, x, y, z, sun, at)) {
                continue;
            }
            int shaded = 0;
            for (int side = 0; side < 4; side++) {
                double ox = side == 0 ? SHAFT_WIDTH : side == 1 ? -SHAFT_WIDTH : 0.0;
                double oz = side == 2 ? SHAFT_WIDTH : side == 3 ? -SHAFT_WIDTH : 0.0;
                if (!sunReaches(level, x + ox, y, z + oz, sun, at)) {
                    shaded++;
                }
            }
            if (shaded < 2) {
                continue;
            }
            // A third of the glitter's size: a speck, not a spark.
            net.minecraft.client.particle.Particle mote = Minecraft.getInstance().particleEngine.createParticle(
                ParticleTypes.END_ROD, x, y, z, 0.0, 0.0, 0.0);
            if (mote != null) {
                mote.scale(DUST_SCALE);
            }
        }
    }

    /**
     * Whether nothing solid stands between a point and the sun, a block at a time. Glass, leaves and the like let
     * the sun through, as they let it through to the eye: a window's shaft is the one most often seen.
     */
    private static boolean sunReaches(ClientLevel level, double x, double y, double z, Vector3f sun,
        BlockPos.MutableBlockPos at) {
        for (double t = 0.75; t < SUN_REACH; t += 0.75) {
            double py = y + sun.y() * t;
            if (py >= level.getMaxY()) {
                return true;
            }
            at.set(x + sun.x() * t, py, z + sun.z() * t);
            if (level.getBlockState(at).canOcclude()) {
                return false;
            }
        }
        return true;
    }
}
