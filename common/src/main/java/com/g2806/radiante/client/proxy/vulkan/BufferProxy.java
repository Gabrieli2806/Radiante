package com.g2806.radiante.client.proxy.vulkan;

import static org.lwjgl.system.MemoryStack.stackPush;
import static org.lwjgl.system.MemoryUtil.memAddress;
import static org.lwjgl.system.MemoryUtil.memSet;

import java.nio.ByteBuffer;
import org.joml.Matrix4f;
import org.joml.Vector3fc;
import org.joml.Vector4fc;
import org.lwjgl.system.MemoryStack;

/** Uploads the uniform blocks the ray tracing shaders read (see native common/shared.hpp). */
public class BufferProxy {

    // The last fields (frameCounter and the entity lights) are written natively.
    // The part of WorldUBO that Java fills: everything before packParams (offsetof(WorldUBO, packParams) natively).
    // The pack parameters after it are written by the renderer itself.
    private static final int WORLD_UBO_SIZE = 736;
    /** Where WorldUBO.offHandLightPos starts; offHandLightColor follows it. */
    private static final int OFF_HAND_LIGHT_OFFSET = 704;
    /** Where WorldUBO.windTime starts; windStrength and windWeather follow it, ending at the off hand's light. */
    private static final int WIND_OFFSET = 692;
    private static final int SKY_UBO_SIZE = 352;
    private static final int TEXTURE_MAPPING_ENTRIES = 4096;

    private static native void updateWorldUniform(long ptr);

    private static native void updateSkyUniform(long ptr);

    private static native void updateMapping(long ptr);

    public record WorldUniform(Matrix4f viewMatrix,
                               Matrix4f effectedViewMatrix,
                               Matrix4f projectionMatrix,
                               Matrix4f glintTextureMatrix,
                               float gameTime,
                               int overlayTextureId,
                               boolean firstPerson,
                               boolean handsDrawn,
                               float fogStart,
                               float fogEnd,
                               Vector4fc fogColor,
                               int skyType,
                               int endSkyTextureId,
                               int endPortalTextureId,
                               int lightMapTextureId,
                               float handFovScale,
                               boolean glowOutline,
                               boolean blockLightSampling,
                               Vector4fc heldLightPos,
                               Vector4fc heldLightColor,
                               Vector4fc offHandLightPos,
                               Vector4fc offHandLightColor,
                               boolean parallaxTransparentEdges,
                               float sunBrightness,
                               float moonBrightness,
                               float emissionBrightness,
                               boolean pixelLighting,
                               float rainFallPerFrame,
                               float traceDistance,
                               float heldLightBrightness,
                               float windTime,
                               float windStrength,
                               float windWeather) {
    }

    public static void updateWorldUniform(WorldUniform uniform) {
        try (MemoryStack stack = stackPush()) {
            ByteBuffer bb = stack.calloc(WORLD_UBO_SIZE);
            long addr = memAddress(bb);
            int offset = 0;

            uniform.viewMatrix().get(offset, bb);
            offset += Float.BYTES * 16;
            uniform.effectedViewMatrix().get(offset, bb);
            offset += Float.BYTES * 16;
            uniform.projectionMatrix().get(offset, bb);
            offset += Float.BYTES * 16;

            // The three inverse matrices and the jitter are filled in natively.
            offset += Float.BYTES * 16 * 3;
            offset += Float.BYTES * 2;

            bb.putFloat(offset, uniform.gameTime());
            offset += Float.BYTES;
            offset += Integer.BYTES; // seed, filled in natively

            uniform.glintTextureMatrix().get(offset, bb);
            offset += Float.BYTES * 16;

            bb.putInt(offset, uniform.overlayTextureId());
            offset += Integer.BYTES;
            // Bit 0 first person, bit 1 the first person hands are drawn (the ray generation traces them only then).
            bb.putInt(offset, uniform.firstPerson() ? (uniform.handsDrawn() ? 3 : 1) : 0);
            offset += Integer.BYTES;
            bb.putFloat(offset, uniform.fogStart());
            offset += Float.BYTES;
            bb.putFloat(offset, uniform.fogEnd());
            offset += Float.BYTES;

            bb.putFloat(offset, uniform.fogColor().x());
            offset += Float.BYTES;
            bb.putFloat(offset, uniform.fogColor().y());
            offset += Float.BYTES;
            bb.putFloat(offset, uniform.fogColor().z());
            offset += Float.BYTES;
            bb.putFloat(offset, uniform.fogColor().w());
            offset += Float.BYTES;

            bb.putInt(offset, 0); // fog shape: sphere
            offset += Integer.BYTES;
            bb.putInt(offset, uniform.skyType());
            offset += Integer.BYTES;
            bb.putInt(offset, uniform.glowOutline() ? 1 : 0);
            offset += Integer.BYTES;
            bb.putInt(offset, uniform.blockLightSampling() ? 1 : 0);
            offset += Integer.BYTES;

            // cameraPos (dvec4), chunkGridInfo and chunkStorageSectionPos are filled in natively.
            offset += Double.BYTES * 4;
            offset += Integer.BYTES * 4;
            offset += Integer.BYTES * 4;

            bb.putInt(offset, uniform.endSkyTextureId());
            offset += Integer.BYTES;
            bb.putInt(offset, uniform.endPortalTextureId());
            offset += Integer.BYTES;
            bb.putInt(offset, uniform.lightMapTextureId());
            offset += Integer.BYTES;
            bb.putFloat(offset, uniform.handFovScale());
            offset += Float.BYTES;

            uniform.heldLightPos().get(offset, bb);
            offset += Float.BYTES * 4;
            uniform.heldLightColor().get(offset, bb);
            offset += Float.BYTES * 4;

            bb.putInt(offset, uniform.parallaxTransparentEdges() ? 1 : 0);
            offset += Integer.BYTES;
            bb.putFloat(offset, uniform.sunBrightness());
            offset += Float.BYTES;
            bb.putFloat(offset, uniform.moonBrightness());
            offset += Float.BYTES;
            bb.putFloat(offset, uniform.emissionBrightness());
            offset += Float.BYTES;
            bb.putInt(offset, uniform.pixelLighting() ? 1 : 0);
            offset += Integer.BYTES;
            bb.putFloat(offset, uniform.traceDistance());
            offset += Float.BYTES;
            bb.putFloat(offset, uniform.rainFallPerFrame());
            offset += Float.BYTES;
            bb.putFloat(offset, uniform.heldLightBrightness());

            // After the natively filled tail (frameCounter ... lodCoveragePad1): the wind, then the off hand's light.
            bb.putFloat(WIND_OFFSET, uniform.windTime());
            bb.putFloat(WIND_OFFSET + Float.BYTES, uniform.windStrength());
            bb.putFloat(WIND_OFFSET + Float.BYTES * 2, uniform.windWeather());
            uniform.offHandLightPos().get(OFF_HAND_LIGHT_OFFSET, bb);
            uniform.offHandLightColor().get(OFF_HAND_LIGHT_OFFSET + Float.BYTES * 4, bb);
            updateWorldUniform(addr);
        }
    }

    public record SkyUniform(Vector3fc baseColor,
                             Vector4fc horizonColor,
                             Vector3fc sunDirection,
                             int skyType,
                             boolean sunRisingOrSetting,
                             boolean skyDark,
                             boolean blindnessOrDarkness,
                             int submersionType,
                             int moonPhase,
                             float rainGradient,
                             int sunTextureId,
                             int moonTextureId,
                             Vector4fc sunUvRect,
                             Vector4fc moonUvRect,
                             Vector4fc biomeFog,
                             float nightVision,
                             Vector4fc celestialAxis,
                             Vector4fc biomeFogChroma,
                             Vector4fc biomeFogHeights,
                             Vector4fc waterExtinction,
                             Vector4fc waterAlbedo,
                             Vector4fc fogControls,
                             Vector4fc rainWetness,
                             Vector4fc waterFog,
                             Vector4fc moonDirection,
                             Vector4fc moonAxis,
                             Vector4fc spriteRotation,
                             Vector4fc biomeClimate,
                             float thunderGradient,
                             int biomeId,
                             int previousBiomeId,
                             float biomeIdBlend,
                             Vector4fc packWeather) {
    }

    public static void updateSkyUniform(SkyUniform uniform) {
        try (MemoryStack stack = stackPush()) {
            ByteBuffer bb = stack.calloc(SKY_UBO_SIZE);
            long addr = memAddress(bb);
            int offset = 0;

            bb.putFloat(offset, uniform.baseColor().x());
            offset += Float.BYTES;
            bb.putFloat(offset, uniform.baseColor().y());
            offset += Float.BYTES;
            bb.putFloat(offset, uniform.baseColor().z());
            offset += Float.BYTES;
            bb.putInt(offset, uniform.skyType());
            offset += Integer.BYTES;

            bb.putFloat(offset, uniform.horizonColor().x());
            offset += Float.BYTES;
            bb.putFloat(offset, uniform.horizonColor().y());
            offset += Float.BYTES;
            bb.putFloat(offset, uniform.horizonColor().z());
            offset += Float.BYTES;
            bb.putFloat(offset, uniform.horizonColor().w());
            offset += Float.BYTES;

            bb.putFloat(offset, uniform.sunDirection().x());
            offset += Float.BYTES;
            bb.putFloat(offset, uniform.sunDirection().y());
            offset += Float.BYTES;
            bb.putFloat(offset, uniform.sunDirection().z());
            offset += Float.BYTES;
            bb.putInt(offset, uniform.sunRisingOrSetting() ? 1 : 0);
            offset += Integer.BYTES;

            bb.putInt(offset, uniform.skyDark() ? 1 : 0);
            offset += Integer.BYTES;
            bb.putInt(offset, uniform.blindnessOrDarkness() ? 1 : 0);
            offset += Integer.BYTES;
            bb.putInt(offset, uniform.submersionType());
            offset += Integer.BYTES;
            bb.putInt(offset, uniform.moonPhase());
            offset += Integer.BYTES;
            bb.putFloat(offset, uniform.rainGradient());
            offset += Float.BYTES;
            bb.putInt(offset, uniform.sunTextureId());
            offset += Integer.BYTES;
            bb.putInt(offset, uniform.moonTextureId());
            offset += Integer.BYTES;
            bb.putFloat(offset, uniform.nightVision());
            offset += Float.BYTES;

            bb.putFloat(offset, uniform.sunUvRect().x());
            offset += Float.BYTES;
            bb.putFloat(offset, uniform.sunUvRect().y());
            offset += Float.BYTES;
            bb.putFloat(offset, uniform.sunUvRect().z());
            offset += Float.BYTES;
            bb.putFloat(offset, uniform.sunUvRect().w());
            offset += Float.BYTES;

            bb.putFloat(offset, uniform.moonUvRect().x());
            offset += Float.BYTES;
            bb.putFloat(offset, uniform.moonUvRect().y());
            offset += Float.BYTES;
            bb.putFloat(offset, uniform.moonUvRect().z());
            offset += Float.BYTES;
            bb.putFloat(offset, uniform.moonUvRect().w());
            offset += Float.BYTES;

            bb.putFloat(offset, uniform.biomeFog().x());
            offset += Float.BYTES;
            bb.putFloat(offset, uniform.biomeFog().y());
            offset += Float.BYTES;
            bb.putFloat(offset, uniform.biomeFog().z());
            offset += Float.BYTES;
            bb.putFloat(offset, uniform.biomeFog().w());
            offset += Float.BYTES;

            bb.putFloat(offset, uniform.celestialAxis().x());
            offset += Float.BYTES;
            bb.putFloat(offset, uniform.celestialAxis().y());
            offset += Float.BYTES;
            bb.putFloat(offset, uniform.celestialAxis().z());
            offset += Float.BYTES;
            bb.putFloat(offset, uniform.celestialAxis().w());
            offset += Float.BYTES;

            offset = putVec4(bb, offset, uniform.biomeFogChroma());
            offset = putVec4(bb, offset, uniform.biomeFogHeights());
            offset = putVec4(bb, offset, uniform.waterExtinction());
            offset = putVec4(bb, offset, uniform.waterAlbedo());
            offset = putVec4(bb, offset, uniform.fogControls());
            offset = putVec4(bb, offset, uniform.rainWetness());
            offset = putVec4(bb, offset, uniform.waterFog());
            offset = putVec4(bb, offset, uniform.moonDirection());
            offset = putVec4(bb, offset, uniform.moonAxis());
            offset = putVec4(bb, offset, uniform.spriteRotation());

            // The weather and the biome for shader packs (BiomeClimate).
            offset = putVec4(bb, offset, uniform.biomeClimate());
            bb.putFloat(offset, uniform.thunderGradient());
            bb.putInt(offset + Integer.BYTES, uniform.biomeId());
            bb.putInt(offset + 2 * Integer.BYTES, uniform.previousBiomeId());
            bb.putFloat(offset + 3 * Integer.BYTES, uniform.biomeIdBlend());
            offset += 4 * Integer.BYTES;

            // The air's wetness after rain for shader packs (PackWeather), then the size of the body of water the camera
            // is in (WaterBody), which radiante-pt reads too.
            putVec4(bb, offset, uniform.packWeather());

            updateSkyUniform(addr);
        }
    }

    private static int putVec4(ByteBuffer bb, int offset, Vector4fc value) {
        bb.putFloat(offset, value.x());
        bb.putFloat(offset + Float.BYTES, value.y());
        bb.putFloat(offset + 2 * Float.BYTES, value.z());
        bb.putFloat(offset + 3 * Float.BYTES, value.w());
        return offset + 4 * Float.BYTES;
    }

    /** Uploads the specular/normal/flag texture mapping used by PBR resource packs. */
    public static void updateMapping() {
        // 64 KiB can exhaust LWJGL's thread stack when called from another stack frame.
        int size = TEXTURE_MAPPING_ENTRIES * Integer.BYTES * 4;
        ByteBuffer bb = org.lwjgl.system.MemoryUtil.memAlloc(size);
        try {
            long addr = memAddress(bb);
            memSet(addr, -1, size);
            int blocks = com.g2806.radiante.client.render.TextureTracker.idOf(
                net.minecraft.client.renderer.texture.TextureAtlas.LOCATION_BLOCKS);
            if (blocks > 0 && blocks < TEXTURE_MAPPING_ENTRIES) {
                int entry = blocks * 4 * Integer.BYTES;
                int specular = com.g2806.radiante.client.render.PbrAtlases.specularTextureFor(blocks);
                if (specular < 0) {
                    specular = com.g2806.radiante.client.render.EmissionTiles.specularTextureFor(blocks);
                }
                if (specular >= 0) {
                    bb.putInt(entry, specular);
                }
                bb.putInt(entry + 3 * Integer.BYTES,
                    com.g2806.radiante.client.render.PbrAtlases.rawMerTextureFor(blocks));
                int normal = com.g2806.radiante.client.render.PbrAtlases.normalTextureFor(blocks);
                if (normal >= 0) {
                    bb.putInt(entry + Integer.BYTES, normal);
                }
            }
            com.g2806.radiante.client.render.EntityPbr.writeMapping(bb, TEXTURE_MAPPING_ENTRIES);
            updateMapping(addr);
        } finally {
            org.lwjgl.system.MemoryUtil.memFree(bb);
        }
    }
}
