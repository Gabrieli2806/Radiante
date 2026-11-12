package com.g2806.radiante.client.render;

import com.g2806.radiante.client.proxy.world.EntityProxy;
import java.util.Map;
import net.minecraft.core.BlockPos;
import net.minecraft.world.level.block.entity.BlockEntity;
import org.lwjgl.system.MemoryUtil;

import static com.g2806.radiante.client.render.EntityManager.*;

/**
 * Handing a frame's entities to the native renderer: the arena their vertices are copied into for the frame, the
 * names kept as native strings, and the one call that passes every pending entity and its layers across.
 *
 * <p>This was the end of {@code EntityManager}, which collects what is handed over here.
 */
final class EntityUpload {

    private EntityUpload() {
    }

    /**
     * The collector reuses its buffers between entities, so each layer keeps its own copy until the native renderer
     * has consumed it. Those copies used to be one allocation each, which in a busy world meant hundreds of
     * malloc and free calls every frame; they all live for exactly one frame, so one arena serves them all.
     * An offset is returned rather than an address because growing the arena moves it.
     */
    static long copyVertices(PBRVertexWriter writer) {
        long size = (long) writer.vertexCount() * PBRVertexWriter.STRIDE;
        return ARENA.append(writer.address(), size);
    }

    /** A copy of some of the writer's vertices: count of them from first on. */
    static long copyVertices(PBRVertexWriter writer, int first, int count) {
        return ARENA.append(writer.address() + (long) first * PBRVertexWriter.STRIDE,
            (long) count * PBRVertexWriter.STRIDE);
    }

    /** A bump allocator for everything that lives for one frame and is then handed to the renderer. */
    static final class Arena {

        private long address;
        private long capacity;
        private long used;

        long append(long source, long size) {
            reserve(this.used + size);
            long offset = this.used;
            MemoryUtil.memCopy(source, this.address + offset, size);
            this.used += size;
            return offset;
        }

        long addressOf(long offset) {
            return this.address + offset;
        }

        void reset() {
            this.used = 0L;
        }

        private void reserve(long required) {
            if (required <= this.capacity) {
                return;
            }
            long grown = Math.max(required, Math.max(this.capacity * 2L, 1L << 16));
            this.address = this.address == 0L ? MemoryUtil.nmemAllocChecked(grown)
                : MemoryUtil.nmemReallocChecked(this.address, grown);
            this.capacity = grown;
        }
    }

    static final Arena ARENA = new Arena();
    /** Block entities collected this frame from the chunks around the camera, by position; see BlockEntityCache. */
    static final Map<BlockPos, BlockEntity> CACHE_CANDIDATES = new java.util.HashMap<>();

    /**
     * Group names are a handful of fixed strings, but a native copy of one was being allocated for every layer of
     * every entity every frame. They never change, so each is encoded once and kept.
     */
    static long groupNameAddress(String name) {
        Long cached = GROUP_NAMES.get(name);
        if (cached != null) {
            return cached;
        }
        long address = MemoryUtil.memAddress(MemoryUtil.memUTF8(name, true));
        GROUP_NAMES.put(name, address);
        return address;
    }

    static final Map<String, Long> GROUP_NAMES = new java.util.HashMap<>();

    /** One native copy of the latest content name; the clouds' name changes with every rebuild of their mesh. */
    static String contentName;
    static java.nio.ByteBuffer contentNameBuffer;

    static long contentNameAddress(String name) {
        if (name.startsWith(BlockEntityCache.NAME_PREFIX)) {
            return BlockEntityCache.nameAddress(name);
        }
        if (!name.equals(contentName)) {
            if (contentNameBuffer != null) {
                MemoryUtil.memFree(contentNameBuffer);
            }
            contentNameBuffer = MemoryUtil.memUTF8(name, true);
            contentName = name;
        }
        return MemoryUtil.memAddress(contentNameBuffer);
    }

    /**
     * The fixed set of native arrays the upload hands over. Each slot keeps its buffer between frames and only
     * ever grows, so a frame costs no allocation at all once the world has settled.
     */
    static final class Scratch {

        private static final int SLOTS = 15;

        private final long[] addresses = new long[SLOTS];
        private final long[] sizes = new long[SLOTS];

        long ints(int slot, int count) {
            return reserve(slot, (long) count * Integer.BYTES);
        }

        long longs(int slot, int count) {
            return reserve(slot, (long) count * Long.BYTES);
        }

        long doubles(int slot, int count) {
            return reserve(slot, (long) count * Double.BYTES);
        }

        /** For an array the upload may leave unwritten: it has to read as zero, as a fresh allocation did. */
        long zeroedLongs(int slot, int count) {
            long size = (long) count * Long.BYTES;
            long address = reserve(slot, size);
            MemoryUtil.memSet(address, 0, size);
            return address;
        }

        private long reserve(int slot, long size) {
            if (size <= this.sizes[slot] && this.addresses[slot] != 0L) {
                return this.addresses[slot];
            }
            long grown = Math.max(size, Math.max(this.sizes[slot] * 2L, 256L));
            this.addresses[slot] = this.addresses[slot] == 0L ? MemoryUtil.nmemAllocChecked(grown)
                : MemoryUtil.nmemReallocChecked(this.addresses[slot], grown);
            this.sizes[slot] = grown;
            return this.addresses[slot];
        }
    }

    static final Scratch SCRATCH = new Scratch();

    static void upload(int coordinate) {
        if (PENDING.isEmpty()) {
            return;
        }

        queued = true;
        int entityCount = PENDING.size();
        int layerCount = 0;
        for (PendingEntity entity : PENDING) {
            layerCount += entity.layers().size();
        }

        // These fifteen arrays are the same shape every frame and only ever grow, so they are kept between
        // frames. Allocating and freeing them per frame was pure overhead on the render thread.
        long hashCodes = SCRATCH.ints(0, entityCount);
        long posXs = SCRATCH.doubles(1, entityCount);
        long posYs = SCRATCH.doubles(2, entityCount);
        long posZs = SCRATCH.doubles(3, entityCount);
        long rayTracingFlags = SCRATCH.ints(4, entityCount);
        long prebuiltBlas = SCRATCH.ints(5, entityCount);
        long layerCounts = SCRATCH.ints(6, entityCount);
        long geometryTypes = SCRATCH.ints(7, layerCount);
        long geometryGroupNames = SCRATCH.longs(8, layerCount);
        long geometryContentNames = SCRATCH.zeroedLongs(9, layerCount);
        long geometryTextures = SCRATCH.ints(10, layerCount);
        long vertexFormats = SCRATCH.ints(11, layerCount);
        long indexFormats = SCRATCH.ints(12, layerCount);
        long vertexCounts = SCRATCH.ints(13, layerCount);
        long vertices = SCRATCH.longs(14, layerCount);

        try {
            int layerIndex = 0;
            for (int i = 0; i < entityCount; i++) {
                PendingEntity entity = PENDING.get(i);
                MemoryUtil.memPutInt(hashCodes + (long) i * Integer.BYTES, entity.id());
                MemoryUtil.memPutDouble(posXs + (long) i * Double.BYTES, entity.x());
                MemoryUtil.memPutDouble(posYs + (long) i * Double.BYTES, entity.y());
                MemoryUtil.memPutDouble(posZs + (long) i * Double.BYTES, entity.z());
                MemoryUtil.memPutInt(layerCounts + (long) i * Integer.BYTES, entity.layers().size());
                MemoryUtil.memPutInt(rayTracingFlags + (long) i * Integer.BYTES, entity.rayTracingFlag());
                // A negative id means the renderer has to build the acceleration structure itself.
                MemoryUtil.memPutInt(prebuiltBlas + (long) i * Integer.BYTES, entity.prebuiltBlas());

                for (PendingLayer layer : entity.layers()) {
                    MemoryUtil.memPutInt(geometryTypes + (long) layerIndex * Integer.BYTES, layer.geometryType());
                    MemoryUtil.memPutAddress(geometryGroupNames + (long) layerIndex * Long.BYTES,
                        groupNameAddress(layer.name()));
                    MemoryUtil.memPutAddress(geometryContentNames + (long) layerIndex * Long.BYTES,
                        layer.contentName() == null ? 0L : contentNameAddress(layer.contentName()));
                    MemoryUtil.memPutInt(geometryTextures + (long) layerIndex * Integer.BYTES, layer.textureId());
                    MemoryUtil.memPutInt(vertexFormats + (long) layerIndex * Integer.BYTES,
                        NativeGeometry.VERTEX_FORMAT_PBR);
                    MemoryUtil.memPutInt(indexFormats + (long) layerIndex * Integer.BYTES,
                        NativeGeometry.DRAW_MODE_QUADS);
                    MemoryUtil.memPutInt(vertexCounts + (long) layerIndex * Integer.BYTES, layer.vertexCount());
                    MemoryUtil.memPutAddress(vertices + (long) layerIndex * Long.BYTES,
                        layer.directAddress() != 0L ? layer.directAddress() : ARENA.addressOf(layer.vertices()));
                    layerIndex++;
                }
            }

            EntityProxy.queue(0.0125f, coordinate, false, entityCount, hashCodes, posXs,
                posYs, posZs, rayTracingFlags, prebuiltBlas, layerCounts, geometryTypes,
                geometryGroupNames, geometryContentNames, geometryTextures, vertexFormats, indexFormats,
                vertexCounts, vertices);
        } finally {
            PENDING.clear();
        }
    }
}
