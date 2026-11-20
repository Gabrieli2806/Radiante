package com.g2806.radiante.client.render;

import com.g2806.radiante.client.option.Options;
import com.g2806.radiante.client.proxy.vulkan.TextureProxy;
import com.g2806.radiante.compat.VersionCompat;
import java.nio.ByteBuffer;
import java.util.Arrays;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.ClientLevel;
import net.minecraft.core.BlockPos;
import net.minecraft.core.particles.BlockParticleOption;
import net.minecraft.core.particles.ParticleTypes;
import net.minecraft.tags.FluidTags;
import net.minecraft.util.RandomSource;
import net.minecraft.world.item.BlockItem;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.Items;
import net.minecraft.world.level.biome.Biome;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.SlabBlock;
import net.minecraft.world.level.block.StairBlock;
import net.minecraft.world.level.block.TrapDoorBlock;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.block.state.properties.BlockStateProperties;
import net.minecraft.world.level.block.state.properties.Half;
import net.minecraft.world.level.block.state.properties.SlabType;
import net.minecraft.world.level.chunk.LevelChunk;
import net.minecraft.world.level.chunk.status.ChunkStatus;
import net.minecraft.world.level.levelgen.Heightmap;
import org.joml.Vector4f;
import org.lwjgl.system.MemoryStack;
import org.lwjgl.system.MemoryUtil;

/**
 * Where rain reaches the ground, for wet surfaces. A 512 by 512 texture
 * wraps around the camera, one texel per block column: the height of the column's top rain-blocking block (red and
 * green's low 12 bits, offset by 2048; green's top bit says the last change was water or lava coming or going, the
 * next three what the top has half a block up, see halfTopOf),
 * whether it rains there rather than snows (blue), and in alpha whether the column is known
 * (0 not) and how long its top has been out in the rain. The shader wets upward faces that are the top of their
 * column. Wetness itself follows the rain slowly, drying well after it stops.
 *
 * <p>A column whose top changes (a block broken off it, or one put on it) has a new, dry surface under the sky. Its
 * alpha then holds the rain clock (below) at that moment, 1 to {@link #STAMPS}; 255 is a top that has been out long
 * enough to be as wet as the rest. The rain clock counts the rain that has fallen, in steps of {@link #CLOCK_UNIT}
 * time constants of the ground's soaking, and stands still while it is dry, so the shader can tell how far the new
 * surface has soaked: {@code 1 - exp(-rain since)}, never more than the ground around it. Broken out in the rain, the
 * block under it shows dry and darkens over the Wet-Up Time; broken after the rain, it stays dry.
 *
 * <p>Blue's two low bits (where it rains) say how hot the column's top is: 3 for fire, lava, magma and a lit
 * campfire, furnace, blast furnace or smoker, 2 for a torch or a lantern standing on it, 0 for nothing. The ground round
 * such a block stays drier in the rain and dries sooner after (rain_wetness.glsl).
 *
 * <p>Where it snows, blue's bit 3 says the column's top is water: the floor under it holds no snow; bit 4 says a
 * bright light is on the top, which melts the snow round it as a torch does.
 */
public final class RainExposure {

    private static final int TILES = 32;
    private static final int SIZE = TILES * 16;
    /** Columns re-read each frame round the ring, so roofs built or broken show up within a few seconds. */
    private static final int REFRESH_PER_FRAME = 6;

    /** The Wet-Up Time and Drying Time settings, read from radiante-pt's attributes. */
    private static final Setting WET_UP = new Setting("render_pipeline.module.ray_tracing.attribute.rain_wet_up_time", 150.0f);
    private static final Setting DRYING = new Setting("render_pipeline.module.ray_tracing.attribute.rain_drying_time", 180.0f);

    /** A pack attribute in seconds, looked up again about once a second: it is read every frame. */
    private static final class Setting {
        private final String attribute;
        private final float fallback;
        private float value;
        private long readAt;

        Setting(String attribute, float fallback) {
            this.attribute = attribute;
            this.fallback = fallback;
            this.value = fallback;
        }

        float seconds() {
            long now = System.nanoTime();
            if (now - this.readAt > 1_000_000_000L) {
                this.readAt = now;
                String text = com.g2806.radiante.client.pipeline.PipelineSettings.getShaderPackValue(this.attribute);
                try {
                    this.value = text == null ? this.fallback : Float.parseFloat(text.trim());
                } catch (NumberFormatException notANumber) {
                    this.value = this.fallback;
                }
            }
            return this.value;
        }
    }

    private static final LevelChunk[] chunks = new LevelChunk[TILES * TILES];
    private static final int[] tileX = new int[TILES * TILES];
    private static final int[] tileZ = new int[TILES * TILES];
    private static ClientLevel level;
    private static int texture = -1;
    private static boolean cleared;
    private static int centerX = Integer.MIN_VALUE;
    private static int centerZ = Integer.MIN_VALUE;
    private static int scan;
    private static float wetness;
    private static long lastTime = System.nanoTime();

    /** Rain clock steps a stamp can hold (1..STAMPS), and how much soaking one step is, in time constants. */
    private static final int STAMPS = 253;
    private static final float CLOCK_UNIT = 0.025f;
    /** A stamp this much soaking old reads as the rest of the ground (alpha 255): exp(-5) is under 1 %. */
    private static final float SOAKED = 5.0f;
    /** The rain clock, in time constants of soaking; it only runs while it rains. */
    private static double rainClock;
    /**
     * Per column (the texture's texel): the top's height last read, Integer.MIN_VALUE before it is known, and when it
     * changed on the rain clock (in steps), NaN once it has been out long enough.
     */
    private static final int[] heights = new int[SIZE * SIZE];
    private static final float[] exposedAt = new float[SIZE * SIZE];
    /**
     * Per column: how hot its top was last read (0..3), and when it became hot (System.nanoTime), 0 once it has been
     * hot long enough. A campfire lit in the rain dries the ground round it a step at a time, HEAT_STEP_SECONDS each,
     * instead of at once; one already there when the chunk comes in is hot from the start.
     */
    private static final byte[] heatTarget = new byte[SIZE * SIZE];
    /** Per column: whether its last change lowered its top (a block taken away), which opens up new faces. */
    private static final boolean[] lowered = new boolean[SIZE * SIZE];
    private static final long[] heatSince = new long[SIZE * SIZE];
    private static final float HEAT_STEP_SECONDS = 15.0f;
    /**
     * Per column: whether its top was water or lava when last read, and whether its last change was water or lava
     * coming or going. Snow comes back on such a top evenly, where a block put down or taken away keeps its own way.
     */
    private static final boolean[] fluidTop = new boolean[SIZE * SIZE];
    private static final boolean[] fluidChange = new boolean[SIZE * SIZE];
    /** Per column: 0 when its top is not a trapdoor (or not read yet), 1 a shut one, 2 an open one. */
    private static final byte[] trapdoorState = new byte[SIZE * SIZE];
    /**
     * Per column: its top block became another kind of block since the column was last read
     * (onBlockChanged), and per tile: it has such a column to read again on the next frame. Read by its height alone,
     * a block broken and put back between two reads (a stair swapped for another) left the column as it was, and the
     * new block came out under the old one's snow.
     */
    private static final boolean[] topChanged = new boolean[SIZE * SIZE];
    private static final boolean[] changedTiles = new boolean[TILES * TILES];

    /** Tiles whose chunk was not loaded when they were read: read again every frame until it is. */
    private static final boolean[] missing = new boolean[TILES * TILES];
    private static int nearRefresh;
    private static boolean lastRainEnabled = true;
    private static boolean lastSnowEnabled = true;

    static {
        Arrays.fill(heights, Integer.MIN_VALUE);
        Arrays.fill(exposedAt, Float.NaN);
    }

    private RainExposure() {
    }

    /** x: wetness 0 to 1, y: the texture's id (0 when there is nothing wet). */
    public static Vector4f update(Minecraft minecraft, double cameraX, double cameraZ) {
        ClientLevel world = minecraft.level;
        if (world != level) {
            level = world;
            Arrays.fill(chunks, null);
            Arrays.fill(tileX, Integer.MIN_VALUE);
            centerX = Integer.MIN_VALUE;
            cleared = false;
            wetness = 0.0f;
            rainClock = 0.0;
            Arrays.fill(heights, Integer.MIN_VALUE);
            Arrays.fill(exposedAt, Float.NaN);
            Arrays.fill(missing, false);
            Arrays.fill(trapdoorState, (byte) 0);
        }
        long now = System.nanoTime();
        float dt = Math.min((now - lastTime) * 1e-9f, 0.1f);
        lastTime = now;
        boolean rainEnabled = Options.rainWetness;
        boolean snowEnabled = Options.snowAccumulation;
        if (!rainEnabled && !snowEnabled || world == null || !world.canHaveWeather()) {
            wetness = 0.0f;
            return new Vector4f(0.0f);
        }
        if (rainEnabled != lastRainEnabled || snowEnabled != lastSnowEnabled) {
            lastRainEnabled = rainEnabled;
            lastSnowEnabled = snowEnabled;
            Arrays.fill(tileX, Integer.MIN_VALUE);
            centerX = Integer.MIN_VALUE;
        }
        if (!minecraft.isPaused()) {
            float rain = world.getRainLevel(1.0f);
            // Soaks over the Wet-Up Time of steady rain (95 % by then, 150 s by default): the ground darkens within
            // seconds (the shaders follow the square root) while water gathers in patches that merge into one film
            // by the end (rain_wetness.glsl). Dries over a seventh of the Drying Time, the puddles standing on until
            // the end of it.
            float seconds = rain > wetness ? WET_UP.seconds() / 3.0f : DRYING.seconds() / 7.0f;
            wetness += (rain - wetness) * (1.0f - (float) Math.exp(-dt / Math.max(seconds, 0.5f)));
            if (rain > 0.2f) {
                rainClock += dt / Math.max(WET_UP.seconds() / 3.0f, 0.5f);
            }
            if (wetness < 0.0001f) {
                wetness = 0.0f;
            }
        }
        if (wetness == 0.0f) {
            return new Vector4f(0.0f);
        }

        if (texture < 0) {
            texture = TextureProxy.generateTextureId();
            TextureProxy.prepareImage(texture, 1, SIZE, SIZE, VersionCompat.vkFormatRgba8());
            TextureProxy.setFilter(texture, 0, 0);
            cleared = false;
        }
        if (!cleared) {
            ByteBuffer zeros = MemoryUtil.memCalloc(SIZE * SIZE * 4);
            try {
                TextureProxy.queueUpload(MemoryUtil.memAddress(zeros), zeros.capacity(), SIZE, texture, 0, 0, 0, 0,
                    SIZE, SIZE, 0);
            } finally {
                MemoryUtil.memFree(zeros);
            }
            cleared = true;
        }

        int cx = Math.floorDiv((int) Math.floor(cameraX), 16);
        int cz = Math.floorDiv((int) Math.floor(cameraZ), 16);
        try (MemoryStack stack = MemoryStack.stackPush()) {
            ByteBuffer pixels = stack.calloc(16 * 16 * 4);
            BlockPos.MutableBlockPos pos = new BlockPos.MutableBlockPos();
            if (cx != centerX || cz != centerZ) {
                centerX = cx;
                centerZ = cz;
                for (int z = cz - TILES / 2; z < cz + TILES / 2; z++) {
                    for (int x = cx - TILES / 2; x < cx + TILES / 2; x++) {
                        int slot = (z & (TILES - 1)) * TILES + (x & (TILES - 1));
                        if (tileX[slot] != x || tileZ[slot] != z) {
                            tileX[slot] = x;
                            tileZ[slot] = z;
                            chunks[slot] = null;
                            forgetTile(slot);
                            upload(world, slot, pixels, pos);
                        }
                    }
                }
            }
            for (int i = 0; i < REFRESH_PER_FRAME; i++) {
                int slot = scan++ & (chunks.length - 1);
                if (tileX[slot] != Integer.MIN_VALUE) {
                    upload(world, slot, pixels, pos);
                }
            }
            // Chunks that were not loaded yet, every frame until they are. Read only by the round above, a chunk
            // that came in a moment after its tile showed dry for seconds and then went wet all at once, which in a
            // walk looked like the wet ground changing at every chunk crossed.
            for (int slot = 0; slot < missing.length; slot++) {
                if (missing[slot] && world.getChunkSource().getChunk(tileX[slot], tileZ[slot], ChunkStatus.FULL,
                    false) != null) {
                    upload(world, slot, pixels, pos);
                }
            }
            // Tiles a block changed in (onBlockChanged), on the next frame.
            for (int slot = 0; slot < changedTiles.length; slot++) {
                if (changedTiles[slot]) {
                    changedTiles[slot] = false;
                    if (tileX[slot] != Integer.MIN_VALUE) {
                        upload(world, slot, pixels, pos);
                    }
                }
            }
            // The tiles round the camera every few frames, so a block broken or put down nearby shows within a
            // fraction of a second.
            if (++nearRefresh >= 8) {
                nearRefresh = 0;
                for (int dz = -1; dz <= 1; dz++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        int slot = ((cz + dz) & (TILES - 1)) * TILES + ((cx + dx) & (TILES - 1));
                        if (tileX[slot] == cx + dx && tileZ[slot] == cz + dz) {
                            upload(world, slot, pixels, pos);
                        }
                    }
                }
            }
        }
        // y carries the rain clock too, under the texture's id: id * 256 + the clock in steps (0..STAMPS).
        float clockSteps = (float) ((rainClock / CLOCK_UNIT) % STAMPS);
        // Negative with Snow on Paths & Farmland on (rain_wetness.glsl vptSnowOnPaths).
        float idAndClock = texture * 256.0f + clockSteps;
        return new Vector4f(wetness, Options.snowOnPaths ? -idAndClock : idAndClock, 0.0f, 0.0f);
    }

    /**
     * A block of the level changed (ClientLevel's block updates, as the world renderer hears of them). The top block of
     * a column becoming another kind of block marks the column as a new top for the next read, even if its height
     * comes out the same. A change of state alone (a leaf's distance, a crop's age, a grass block turning
     * snowy) is not a new top.
     */
    public static void onBlockChanged(BlockPos pos, BlockState before, BlockState after) {
        if (before.getBlock() == after.getBlock()) {
            return;
        }
        int slot = ((pos.getZ() >> 4) & (TILES - 1)) * TILES + ((pos.getX() >> 4) & (TILES - 1));
        if (tileX[slot] != pos.getX() >> 4 || tileZ[slot] != pos.getZ() >> 4) {
            return;
        }
        int column = (pos.getZ() & (SIZE - 1)) * SIZE + (pos.getX() & (SIZE - 1));
        // The top itself: a block put on top raises the column, which its next read sees anyway, and a flower or a
        // torch set down on the ground is not a new top for the ground under it.
        if (heights[column] == Integer.MIN_VALUE || pos.getY() != heights[column]) {
            return;
        }
        topChanged[column] = true;
        changedTiles[slot] = true;
    }

    /** The tile's columns start over unknown: a tile that comes into the ring is not ground that just changed. */
    private static void forgetTile(int slot) {
        int baseX = (slot & (TILES - 1)) * 16;
        int baseZ = (slot / TILES) * 16;
        for (int z = 0; z < 16; z++) {
            int row = (baseZ + z) * SIZE + baseX;
            Arrays.fill(heights, row, row + 16, Integer.MIN_VALUE);
            Arrays.fill(exposedAt, row, row + 16, Float.NaN);
            Arrays.fill(trapdoorState, row, row + 16, (byte) 0);
            Arrays.fill(topChanged, row, row + 16, false);
        }
    }

    /** How hot an item is in the hand: as its block would be set down, a lava bucket as lava, a bright light as a torch. */
    static int heldHeatOf(ItemStack stack) {
        if (stack.isEmpty()) {
            return 0;
        }
        if (stack.is(Items.LAVA_BUCKET)) {
            return 3;
        }
        if (!(stack.getItem() instanceof BlockItem blockItem)) {
            return 0;
        }
        BlockState state = blockItem.getBlock().defaultBlockState();
        int heat = heatOf(state);
        return heat == 0 && state.getLightEmission() > 11 ? 2 : heat;
    }

    /** Snow sliding off a trapdoor swung open: clumps of it falling from where its top was, and a little powder. */
    private static void shakeOffSnow(ClientLevel world, BlockState trapdoor, int x, int y, int z) {
        double top = y + (trapdoor.getValue(BlockStateProperties.HALF) == Half.TOP ? 1.0 : 0.1875);
        BlockParticleOption snow = new BlockParticleOption(ParticleTypes.BLOCK, Blocks.SNOW_BLOCK.defaultBlockState());
        RandomSource random = world.getRandom();
        for (int i = 0; i < 14; i++) {
            world.addParticle(snow, x + 0.1 + random.nextDouble() * 0.8, top + 0.02, z + 0.1 + random.nextDouble() * 0.8,
                random.nextGaussian() * 0.02, -0.05, random.nextGaussian() * 0.02);
        }
        for (int i = 0; i < 4; i++) {
            world.addParticle(ParticleTypes.SNOWFLAKE, x + random.nextDouble(), top, z + random.nextDouble(),
                random.nextGaussian() * 0.01, -0.02, random.nextGaussian() * 0.01);
        }
    }

    /** How hot the top of a column is (see the class comment): the block at pos, its top, or the one standing on it. */
    private static int heatOf(LevelChunk chunk, BlockPos.MutableBlockPos pos) {
        int heat = heatOf(chunk.getBlockState(pos));
        return heat == 3 ? 3 : Math.max(heat, heatOf(chunk.getBlockState(pos.move(0, 1, 0))));
    }

    /**
     * What a top that is not a whole block has half a block up, as rain_wetness.glsl reads green's bits 4 to 6: 0
     * nothing, 1 a bottom slab (its sides' top edge all open), 2 to 5 a bottom stair facing north, east, south or west
     * (the edge open on the step's half, away from where it faces).
     */
    private static int halfTopOf(BlockState state) {
        if (state.getBlock() instanceof SlabBlock) {
            return state.getValue(SlabBlock.TYPE) == SlabType.BOTTOM ? 1 : 0;
        }
        if (state.getBlock() instanceof StairBlock && state.getValue(StairBlock.HALF) == Half.BOTTOM) {
            return switch (state.getValue(StairBlock.FACING)) {
                case NORTH -> 2;
                case EAST -> 3;
                case SOUTH -> 4;
                default -> 5;
            };
        }
        return 0;
    }

    /** The brightest light of the column's top and the block standing on it. */
    private static int lightOf(LevelChunk chunk, BlockPos.MutableBlockPos pos) {
        int light = chunk.getBlockState(pos).getLightEmission();
        return Math.max(light, chunk.getBlockState(pos.move(0, 1, 0)).getLightEmission());
    }

    private static int heatOf(net.minecraft.world.level.block.state.BlockState state) {
        if (state.isAir()) {
            return 0;
        }
        boolean lit = !state.hasProperty(net.minecraft.world.level.block.state.properties.BlockStateProperties.LIT)
            || state.getValue(net.minecraft.world.level.block.state.properties.BlockStateProperties.LIT);
        if (state.is(net.minecraft.tags.BlockTags.FIRE) || state.is(net.minecraft.world.level.block.Blocks.LAVA)
            || state.is(net.minecraft.world.level.block.Blocks.MAGMA_BLOCK)) {
            return 3;
        }
        if ((state.is(net.minecraft.tags.BlockTags.CAMPFIRES) || state.is(net.minecraft.world.level.block.Blocks.FURNACE)
            || state.is(net.minecraft.world.level.block.Blocks.BLAST_FURNACE)
            || state.is(net.minecraft.world.level.block.Blocks.SMOKER)) && lit) {
            return 3;
        }
        if (state.is(net.minecraft.world.level.block.Blocks.TORCH)
            || state.is(net.minecraft.world.level.block.Blocks.SOUL_TORCH)
            || state.is(net.minecraft.world.level.block.Blocks.LANTERN)
            || state.is(net.minecraft.world.level.block.Blocks.SOUL_LANTERN)
            || (state.is(net.minecraft.tags.BlockTags.CANDLES) && lit)) {
            return 2;
        }
        return 0;
    }

    private static void upload(ClientLevel world, int slot, ByteBuffer pixels, BlockPos.MutableBlockPos pos) {
        LevelChunk chunk = world.getChunkSource().getChunk(tileX[slot], tileZ[slot], ChunkStatus.FULL, false);
        chunks[slot] = chunk;
        missing[slot] = chunk == null;
        MemoryUtil.memSet(MemoryUtil.memAddress(pixels), 0, pixels.capacity());
        if (chunk == null) {
            forgetTile(slot);
        } else {
            int baseX = (slot & (TILES - 1)) * 16;
            int baseZ = (slot / TILES) * 16;
            float clock = (float) (rainClock / CLOCK_UNIT);
            for (int z = 0; z < 16; z++) {
                for (int x = 0; x < 16; x++) {
                    int y = chunk.getHeight(Heightmap.Types.MOTION_BLOCKING, x, z);
                    int column = (baseZ + z) * SIZE + baseX + x;
                    int before = heights[column];
                    heights[column] = y;
                    // Water or lava on top: water poured on or drained off, lava flowing in or out.
                    boolean fluid = !chunk.getFluidState(pos.set(tileX[slot] * 16 + x, y, tileZ[slot] * 16 + z))
                        .isEmpty();
                    boolean replaced = topChanged[column];
                    topChanged[column] = false;
                    if (before != Integer.MIN_VALUE && (before != y || fluid != fluidTop[column] || replaced)) {
                        // A new top: dry, from now on the rain clock.
                        exposedAt[column] = clock;
                        lowered[column] = y < before || fluidTop[column] && !fluid;
                        fluidChange[column] = fluid || fluidTop[column];
                    } else if (!Float.isNaN(exposedAt[column]) && (clock - exposedAt[column]) * CLOCK_UNIT > SOAKED) {
                        exposedAt[column] = Float.NaN;
                    }
                    fluidTop[column] = fluid;
                    pos.set(tileX[slot] * 16 + x, y + 1, tileZ[slot] * 16 + z);
                    // Green's top bit: the top changed lately with water or lava coming or going (see fluidChange);
                    // the next three: what is half a block up on a top that is not a whole block, for the band of
                    // snow along the top edge of its sides (halfTopOf).
                    BlockState topState = chunk.getBlockState(pos.set(tileX[slot] * 16 + x, y, tileZ[slot] * 16 + z));
                    int height = Math.clamp(y + 2048, 0, 4095)
                        | (fluidChange[column] && !Float.isNaN(exposedAt[column]) ? 32768 : 0)
                        | (halfTopOf(topState) << 12);
                    int offset = (z * 16 + x) * 4;
                    pixels.put(offset, (byte) height);
                    pixels.put(offset + 1, (byte) (height >>> 8));
                    Biome.Precipitation precipitation =
                        world.getBiome(pos).value().getPrecipitationAt(pos, world.getSeaLevel());
                    boolean rains = Options.rainWetness && precipitation == Biome.Precipitation.RAIN;
                    boolean snows = Options.snowAccumulation && precipitation == Biome.Precipitation.SNOW;
                    // Rain: 128 under leaves (drips through), 248 open. Snow: 32 under leaves, 96 open.
                    boolean leaves = chunk.getHeight(Heightmap.Types.MOTION_BLOCKING_NO_LEAVES, x, z) < y;
                    int target = (rains || snows)
                        ? heatOf(chunk, pos.set(tileX[slot] * 16 + x, y, tileZ[slot] * 16 + z)) : 0;
                    long now = System.nanoTime();
                    if (target > heatTarget[column] && before != Integer.MIN_VALUE) {
                        heatSince[column] = now;
                    } else if (target == 0 || before == Integer.MIN_VALUE) {
                        heatSince[column] = 0L;
                    }
                    heatTarget[column] = (byte) target;
                    int heat = target;
                    if (heatSince[column] != 0L) {
                        int steps = (int) ((now - heatSince[column]) * 1e-9f / HEAT_STEP_SECONDS);
                        if (steps >= target) {
                            heatSince[column] = 0L;
                        } else {
                            heat = steps;
                        }
                    }
                    // A trapdoor on top opened or shut: whichever face is now on top was out of the weather, so it
                    // starts bare like a top laid bare, and the snow on a trapdoor swung open falls off it. The snow
                    // stayed on the face as it turned, and on the top it showed again when shut.
                    BlockState top = chunk.getBlockState(pos.set(tileX[slot] * 16 + x, y, tileZ[slot] * 16 + z));
                    byte trapdoor = top.getBlock() instanceof TrapDoorBlock
                        ? (byte) (top.getValue(BlockStateProperties.OPEN) ? 2 : 1) : 0;
                    if (trapdoor != 0 && trapdoorState[column] != 0 && trapdoor != trapdoorState[column]
                        && before == y && (rains || snows)) {
                        boolean snowLay = snows && trapdoor == 2 && wetness > 0.2f && (Float.isNaN(exposedAt[column])
                            || (clock - exposedAt[column]) * CLOCK_UNIT > 0.7f);
                        exposedAt[column] = clock;
                        lowered[column] = true;
                        if (snowLay) {
                            shakeOffSnow(world, top, tileX[slot] * 16 + x, y, tileZ[slot] * 16 + z);
                        }
                    }
                    trapdoorState[column] = trapdoor;
                    // Bit 2: the column's recent change lowered its top (see lowered), for the faces it opened.
                    int down = lowered[column] && !Float.isNaN(exposedAt[column]) ? 4 : 0;
                    int kind = rains ? (leaves ? 128 : 248) : snows ? (leaves ? 32 : 96) : 0;
                    // Bit 3 where it snows: the column's top is water, so the floor under it holds no snow.
                    if (snows && chunk.getFluidState(pos.set(tileX[slot] * 16 + x, y, tileZ[slot] * 16 + z))
                        .is(FluidTags.WATER)) {
                        kind |= 8;
                    }
                    // Bit 4 where it snows: a bright light on the top or standing on it (glowstone, a froglight, a
                    // sea lantern), which Minecraft melts snow beside too (block light over 11); without it the snow
                    // stopped dead at the light's edge, as only the light's own face kept clear.
                    if (snows && heat == 0 && (lightOf(chunk, pos.set(tileX[slot] * 16 + x, y, tileZ[slot] * 16 + z))
                        > 11)) {
                        kind |= 16;
                    }
                    pixels.put(offset + 2, (byte) (kind == 0 ? 0 : kind | down | heat));
                    float stamp = exposedAt[column];
                    pixels.put(offset + 3, (byte) (Float.isNaN(stamp) ? 255
                        : 1 + (int) (((long) Math.floor(stamp)) % STAMPS)));
                }
            }
        }
        TextureProxy.queueUpload(MemoryUtil.memAddress(pixels), pixels.capacity(), 16, texture, 0, 0,
            (slot & (TILES - 1)) * 16, (slot / TILES) * 16, 16, 16, 0);
    }
}
