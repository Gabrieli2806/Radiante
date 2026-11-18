package com.g2806.radiante.client.render;

import com.g2806.radiante.client.render.EntityManager.PendingLayer;
import com.mojang.blaze3d.vertex.PoseStack;
import java.lang.ref.ReferenceQueue;
import java.lang.ref.WeakReference;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import net.minecraft.client.model.geom.ModelPart;
import net.minecraft.util.LightCoordsUtil;
import org.jspecify.annotations.Nullable;
import org.lwjgl.system.MemoryUtil;

/**
 * Caches the untransformed local-space PBR geometry of individual {@link ModelPart}s so articulated mobs and players
 * reuse shared bottom-level acceleration structures and only update 3x4 TLAS instance transforms each frame.
 */
final class RigidPartCache {

    static final String NAME_PREFIX = "rp:";
    private static final int UNUSED_FRAMES = 600;
    /**
     * Baked variants kept for one part. A tint or overlay that changes every frame (jeb_'s rainbow sheep, a creeper
     * swelling) baked a new variant each frame, and up to 600 frames of them stayed, each found again by a scan of
     * the list. Past this the one used longest ago goes.
     */
    private static final int MAX_VARIANTS_PER_PART = 16;
    /** A variant used this recently may still be waiting to be uploaded, so it is never the one dropped. */
    private static final int IN_FLIGHT_FRAMES = 3;

    private static final PoseStack IDENTITY_STACK = new PoseStack();
    private static final PBRVertexWriter BAKE_WRITER = new PBRVertexWriter(256);

    private static final Map<PartKey, Bucket> BUCKETS = new HashMap<>();
    private static final ReferenceQueue<ModelPart> COLLECTED_PARTS = new ReferenceQueue<>();
    private static final Map<String, Long> NAME_ADDRESSES = new HashMap<>();

    private static long nextId = 1L;
    private static int frame;
    private static Object level;
    private static boolean dirty;

    private RigidPartCache() {
    }

    private static final class PartKey extends WeakReference<ModelPart> {
        final int hash;

        PartKey(ModelPart referent, @Nullable ReferenceQueue<ModelPart> queue) {
            super(referent, queue);
            this.hash = System.identityHashCode(referent);
        }

        @Override
        public int hashCode() {
            return this.hash;
        }

        @Override
        public boolean equals(Object other) {
            if (this == other) {
                return true;
            }
            if (other instanceof PartKey that) {
                if (this.hash != that.hash) {
                    return false;
                }
                ModelPart a = this.get();
                return a != null && a == that.get();
            }
            if (other instanceof ProbeKey probe) {
                return this.hash == probe.hash && this.get() == probe.part;
            }
            return false;
        }
    }

    /** Reusable lookup key so steady-frame cache hits allocate nothing on the heap. */
    private static final class ProbeKey {
        @Nullable ModelPart part;
        int hash;

        void set(ModelPart target) {
            this.part = target;
            this.hash = System.identityHashCode(target);
        }

        void clear() {
            this.part = null;
        }

        @Override
        public int hashCode() {
            return this.hash;
        }

        @Override
        public boolean equals(Object other) {
            return other instanceof PartKey key && key.hash == this.hash && key.get() == this.part;
        }
    }

    private static final ProbeKey PROBE = new ProbeKey();

    private static final class Variant {
        final int textureId;
        final int alphaMode;
        final int geometryType;
        final String groupName;
        final boolean useOverlay;
        final boolean needsNormals;
        final int overlayKey;
        final int tintedColor;
        final int colorOverride;
        final int colorTint;
        final int layer;
        final int emissionBits;
        final int vertexCount;
        long memory;
        String name;
        ByteBuffer nameBuffer;
        final PendingLayer pendingLayer;
        int lastUsed;

        Variant(int textureId, int alphaMode, int geometryType, String groupName, boolean useOverlay,
                boolean needsNormals, int overlayKey, int tintedColor, int colorOverride, int colorTint, int layer,
                int emissionBits, int vertexCount, long memory, String name, ByteBuffer nameBuffer, int frame) {
            this.textureId = textureId;
            this.alphaMode = alphaMode;
            this.geometryType = geometryType;
            this.groupName = groupName;
            this.useOverlay = useOverlay;
            this.needsNormals = needsNormals;
            this.overlayKey = overlayKey;
            this.tintedColor = tintedColor;
            this.colorOverride = colorOverride;
            this.colorTint = colorTint;
            this.layer = layer;
            this.emissionBits = emissionBits;
            this.vertexCount = vertexCount;
            this.memory = memory;
            this.name = name;
            this.nameBuffer = nameBuffer;
            this.pendingLayer =
                new PendingLayer(geometryType, textureId, vertexCount, 0L, groupName, memory, name);
            this.lastUsed = frame;
        }

        boolean matches(int textureId, int alphaMode, int geometryType, String groupName, boolean useOverlay,
                        boolean needsNormals, int overlayKey, int tintedColor, int colorOverride, int colorTint,
                        int layer, int emissionBits) {
            return this.textureId == textureId
                && this.alphaMode == alphaMode
                && this.geometryType == geometryType
                && this.useOverlay == useOverlay
                && this.needsNormals == needsNormals
                && this.overlayKey == overlayKey
                && this.tintedColor == tintedColor
                && this.colorOverride == colorOverride
                && this.colorTint == colorTint
                && this.layer == layer
                && this.emissionBits == emissionBits
                && this.groupName.equals(groupName);
        }

        void free() {
            if (this.memory != 0L) {
                MemoryUtil.nmemFree(this.memory);
                this.memory = 0L;
            }
            if (this.nameBuffer != null) {
                NAME_ADDRESSES.remove(this.name);
                MemoryUtil.memFree(this.nameBuffer);
                this.nameBuffer = null;
                this.name = null;
            }
        }
    }

    private static final class Bucket {
        final List<Variant> variants = new ArrayList<>(2);

        void freeAll() {
            for (Variant variant : this.variants) {
                variant.free();
            }
            this.variants.clear();
        }
    }

    static long nameAddress(String name) {
        Long address = NAME_ADDRESSES.get(name);
        return address == null ? 0L : address;
    }

    static void invalidate() {
        dirty = true;
    }

    static void beginFrame(Object currentLevel) {
        frame++;
        if (dirty || currentLevel != level) {
            dirty = false;
            level = currentLevel;
            for (Bucket bucket : BUCKETS.values()) {
                bucket.freeAll();
            }
            BUCKETS.clear();
            while (COLLECTED_PARTS.poll() != null) {
                // Drain stale weak keys.
            }
            return;
        }
        PartKey collected;
        while ((collected = (PartKey) COLLECTED_PARTS.poll()) != null) {
            Bucket removed = BUCKETS.remove(collected);
            if (removed != null) {
                removed.freeAll();
            }
        }
        if ((frame & 255) == 0) {
            for (Iterator<Bucket> bucketIt = BUCKETS.values().iterator(); bucketIt.hasNext();) {
                Bucket bucket = bucketIt.next();
                for (Iterator<Variant> varIt = bucket.variants.iterator(); varIt.hasNext();) {
                    Variant variant = varIt.next();
                    if (frame - variant.lastUsed > UNUSED_FRAMES) {
                        variant.free();
                        varIt.remove();
                    }
                }
                if (bucket.variants.isEmpty()) {
                    bucketIt.remove();
                }
            }
        }
    }

    static @Nullable PendingLayer getOrBake(ModelPart part, List<ModelPart.Cube> cubes, RenderTypeInfo info,
                                            int layer, float layerOffset, float effectiveEmission, int overlayCoords,
                                            int tintedColor, int colorOverride, int colorTint) {
        int textureId = info.textureId();
        int alphaMode = info.alphaMode();
        int geometryType = info.geometryType();
        String groupName = info.groupName();
        boolean useOverlay = info.useOverlay();
        boolean needsNormals = info.needsComputedNormals();
        int overlayKey = useOverlay ? overlayCoords : 0;
        int emissionBits = Float.floatToRawIntBits(effectiveEmission);

        PROBE.set(part);
        Bucket bucket = BUCKETS.get(PROBE);
        PROBE.clear();
        if (bucket != null) {
            List<Variant> list = bucket.variants;
            for (int i = 0, n = list.size(); i < n; i++) {
                Variant v = list.get(i);
                if (v.matches(textureId, alphaMode, geometryType, groupName, useOverlay, needsNormals, overlayKey,
                    tintedColor, colorOverride, colorTint, layer, emissionBits)) {
                    v.lastUsed = frame;
                    return v.vertexCount > 0 ? v.pendingLayer : null;
                }
            }
        }

        BAKE_WRITER.reset();
        BAKE_WRITER.textureId(textureId)
            .glintTextureId(0)
            .glintEnabled(false)
            .alphaMode(alphaMode)
            .coordinate(NativeGeometry.COORDINATE_CAMERA)
            .albedoEmission(effectiveEmission)
            .overlayEnabled(useOverlay)
            .computeQuadNormals(needsNormals)
            .colorOverride(colorOverride)
            .colorTint(colorTint)
            .held(false)
            .glass(false)
            .uvOffset(info.uOffset(), info.vOffset())
            .layerOffset(layerOffset);

        IDENTITY_STACK.setIdentity();
        PoseStack.Pose identityPose = IDENTITY_STACK.last();
        for (int i = 0, n = cubes.size(); i < n; i++) {
            cubes.get(i).compile(identityPose, BAKE_WRITER, LightCoordsUtil.FULL_BRIGHT, overlayCoords, tintedColor);
        }
        BAKE_WRITER.layerOffset(0.0f);
        BAKE_WRITER.finish();

        int vertexCount = BAKE_WRITER.vertexCount();
        if (bucket == null) {
            bucket = new Bucket();
            BUCKETS.put(new PartKey(part, COLLECTED_PARTS), bucket);
        } else if (bucket.variants.size() >= MAX_VARIANTS_PER_PART) {
            evictOldest(bucket);
        }
        if (vertexCount == 0 || vertexCount % 4 != 0) {
            Variant empty = new Variant(textureId, alphaMode, geometryType, groupName, useOverlay, needsNormals,
                overlayKey, tintedColor, colorOverride, colorTint, layer, emissionBits, 0, 0L, "", null, frame);
            bucket.variants.add(empty);
            return null;
        }

        long bytes = (long) vertexCount * PBRVertexWriter.STRIDE;
        long memory = MemoryUtil.nmemAllocChecked(bytes);
        MemoryUtil.memCopy(BAKE_WRITER.address(), memory, bytes);
        String name = NAME_PREFIX + Long.toHexString(nextId++);
        ByteBuffer nameBuf = MemoryUtil.memUTF8(name, true);
        NAME_ADDRESSES.put(name, MemoryUtil.memAddress(nameBuf));

        Variant created = new Variant(textureId, alphaMode, geometryType, groupName, useOverlay, needsNormals,
            overlayKey, tintedColor, colorOverride, colorTint, layer, emissionBits, vertexCount, memory, name,
            nameBuf, frame);
        bucket.variants.add(created);
        return created.pendingLayer;
    }

    private static void evictOldest(Bucket bucket) {
        Variant oldest = null;
        for (Variant variant : bucket.variants) {
            if (frame - variant.lastUsed >= IN_FLIGHT_FRAMES && (oldest == null || variant.lastUsed < oldest.lastUsed)) {
                oldest = variant;
            }
        }
        if (oldest != null) {
            oldest.free();
            bucket.variants.remove(oldest);
        }
    }
}
