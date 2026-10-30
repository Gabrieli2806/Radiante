package com.g2806.radiante.client.render;

import com.g2806.radiante.client.option.Options;
import com.g2806.radiante.client.proxy.vulkan.TextureProxy;
import com.mojang.blaze3d.GpuFormat;
import com.mojang.blaze3d.vulkan.VulkanConst;
import java.nio.ByteBuffer;
import java.util.Arrays;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.ClientLevel;
import net.minecraft.core.BlockPos;
import net.minecraft.world.level.biome.Biome;
import net.minecraft.world.level.chunk.LevelChunk;
import net.minecraft.world.level.chunk.status.ChunkStatus;
import net.minecraft.world.level.levelgen.Heightmap;
import org.joml.Vector4f;
import org.lwjgl.system.MemoryStack;
import org.lwjgl.system.MemoryUtil;

/**
 * Where rain reaches the ground, for wet surfaces. A 512 by 512 texture
 * wraps around the camera, one texel per block column: the height of the column's top rain-blocking block (red and
 * green, offset by 32768), whether it rains there rather than snows (blue) and whether the column is known (alpha).
 * The shader wets upward faces that are the top of their column. Wetness itself follows the rain slowly, drying
 * well after it stops.
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
                String text = com.g2806.radiante.client.pipeline.Pipeline.getShaderPackValue(this.attribute);
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
        }
        long now = System.nanoTime();
        float dt = Math.min((now - lastTime) * 1e-9f, 0.1f);
        lastTime = now;
        if (!Options.rainWetness || world == null || !world.canHaveWeather()) {
            wetness = 0.0f;
            return new Vector4f(0.0f);
        }
        if (!minecraft.isPaused()) {
            float rain = world.getRainLevel(1.0f);
            // Soaks over the Wet-Up Time of steady rain (95 % by then, 150 s by default): the ground darkens within
            // seconds (the shaders follow the square root) while water gathers in patches that merge into one film
            // by the end (rain_wetness.glsl). Dries over a seventh of the Drying Time, the puddles standing on until
            // the end of it.
            float seconds = rain > wetness ? WET_UP.seconds() / 3.0f : DRYING.seconds() / 7.0f;
            wetness += (rain - wetness) * (1.0f - (float) Math.exp(-dt / Math.max(seconds, 0.5f)));
            if (wetness < 0.0001f) {
                wetness = 0.0f;
            }
        }
        if (wetness == 0.0f) {
            return new Vector4f(0.0f);
        }

        if (texture < 0) {
            texture = TextureProxy.generateTextureId();
            TextureProxy.prepareImage(texture, 1, SIZE, SIZE, VulkanConst.toVk(GpuFormat.RGBA8_UNORM));
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
        }
        return new Vector4f(wetness, texture, 0.0f, 0.0f);
    }

    private static void upload(ClientLevel world, int slot, ByteBuffer pixels, BlockPos.MutableBlockPos pos) {
        LevelChunk chunk = world.getChunkSource().getChunk(tileX[slot], tileZ[slot], ChunkStatus.FULL, false);
        chunks[slot] = chunk;
        MemoryUtil.memSet(MemoryUtil.memAddress(pixels), 0, pixels.capacity());
        if (chunk != null) {
            for (int z = 0; z < 16; z++) {
                for (int x = 0; x < 16; x++) {
                    int y = chunk.getHeight(Heightmap.Types.MOTION_BLOCKING, x, z);
                    pos.set(tileX[slot] * 16 + x, y + 1, tileZ[slot] * 16 + z);
                    int height = Math.clamp(y + 32768, 0, 65535);
                    int offset = (z * 16 + x) * 4;
                    pixels.put(offset, (byte) height);
                    pixels.put(offset + 1, (byte) (height >>> 8));
                    boolean rains = world.getBiome(pos).value().getPrecipitationAt(pos, world.getSeaLevel())
                        == Biome.Precipitation.RAIN;
                    // 128 where the column's top is leaves (the rain drips through to the ground below), 255 where
                    // it is anything else.
                    boolean leaves = chunk.getHeight(Heightmap.Types.MOTION_BLOCKING_NO_LEAVES, x, z) < y;
                    pixels.put(offset + 2, (byte) (rains ? (leaves ? 128 : 255) : 0));
                    pixels.put(offset + 3, (byte) 255);
                }
            }
        }
        TextureProxy.queueUpload(MemoryUtil.memAddress(pixels), pixels.capacity(), 16, texture, 0, 0,
            (slot & (TILES - 1)) * 16, (slot / TILES) * 16, 16, 16, 0);
    }
}
