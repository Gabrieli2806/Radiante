#include "prism/all_extern.hpp"
#include "prism/middleware/jni_support.hpp"
#include "prism/render/emission.hpp"
#include "prism/render/opacity_micromaps.hpp"
#include "prism/render/textures.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace jni = prism::jni;

extern "C" {

JNIEXPORT jint JNICALL Java_com_g2806_radiante_client_proxy_vulkan_TextureProxy_generateTextureId(JNIEnv *, jclass) {
    return jni::guard("[Radiante] generateTextureId", 0, [] {
        auto textures = jni::textures();
        return textures != nullptr ? static_cast<jint>(textures->allocateTexture()) : 0;
    });
}

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_vulkan_TextureProxy_prepareImage(
    JNIEnv *, jclass, jint id, jint maxLevel, jint width, jint height, jint format) {
    jni::guard("[Radiante] prepareImage", [=] {
        auto textures = jni::textures();
        if (textures == nullptr) return;
        textures->initializeTexture(static_cast<uint32_t>(id), static_cast<uint32_t>(maxLevel),
                                    static_cast<uint32_t>(width), static_cast<uint32_t>(height),
                                    static_cast<VkFormat>(format));
        // The image under this id is new: emitters found in the one it replaces no longer apply.
        if (auto emission = textures->emission()) emission->resetTexture(static_cast<uint32_t>(id));
    });
}

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_vulkan_TextureProxy_setFilter(
    JNIEnv *, jclass, jint id, jint samplingMode, jint mipmapMode) {
    jni::guard("[Radiante] setFilter", [=] {
        auto textures = jni::textures();
        if (textures == nullptr) return;
        textures->setSamplingMode(static_cast<uint32_t>(id), static_cast<VkFilter>(samplingMode),
                                  static_cast<VkSamplerMipmapMode>(mipmapMode));
    });
}

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_vulkan_TextureProxy_queueUpload(JNIEnv *,
                                                                                            jclass,
                                                                                            jlong srcPointer,
                                                                                            jint srcSizeInBytes,
                                                                                            jint srcRowPixels,
                                                                                            jint dstId,
                                                                                            jint srcOffsetX,
                                                                                            jint srcOffsetY,
                                                                                            jint dstOffsetX,
                                                                                            jint dstOffsetY,
                                                                                            jint width,
                                                                                            jint height,
                                                                                            jint level) {
    jni::guard("[Radiante] queueUpload", [=] {
        auto textures = jni::textures();
        if (textures == nullptr) return;
        textures->queueUpload(jni::as<uint8_t *>(srcPointer), static_cast<uint32_t>(srcSizeInBytes),
                              static_cast<uint32_t>(srcRowPixels), static_cast<uint32_t>(dstId), srcOffsetX, srcOffsetY,
                              dstOffsetX, dstOffsetY, static_cast<uint32_t>(width), static_cast<uint32_t>(height),
                              static_cast<uint32_t>(level));
    });
}

// Emitter cells found in one tile of a texture, eight floats each (TextureProxy.uploadEmissionTile).
JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_vulkan_TextureProxy_uploadEmissionTileNative(
    JNIEnv *, jclass, jint textureId, jlong tileKey, jlong cellsPtr, jint cellCount) {
    jni::guard("[Radiante] uploadEmissionTile", [=] {
        auto textures = jni::textures();
        auto emission = textures != nullptr ? textures->emission() : nullptr;
        if (emission == nullptr) return;
        emission->updateTile(static_cast<uint32_t>(textureId), static_cast<uint64_t>(tileKey),
                             jni::as<const EmissionCellUpload *>(cellsPtr), cellCount);
    });
}

// The block atlas is about to be mirrored: its level 0 uploads from now on are its cut-out coverage.
JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_vulkan_TextureProxy_beginCutoutCoverage(
    JNIEnv *, jclass, jint textureId, jint width, jint height) {
    jni::guard("[Radiante] beginCutoutCoverage", [=] {
        omm::coverage::begin(static_cast<uint32_t>(textureId), static_cast<uint32_t>(std::max(width, 0)),
                             static_cast<uint32_t>(std::max(height, 0)));
    });
}

// The mirror is complete; `animatedRects` holds x, y, width and height of each animated sprite, in level 0 texels.
JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_vulkan_TextureProxy_finishCutoutCoverage(
    JNIEnv *env, jclass, jint textureId, jintArray animatedRects) {
    jni::guard("[Radiante] finishCutoutCoverage", [=] {
        std::vector<int32_t> rects;
        if (animatedRects != nullptr) {
            const jsize length = env->GetArrayLength(animatedRects);
            rects.resize(static_cast<size_t>(length));
            if (length > 0) env->GetIntArrayRegion(animatedRects, 0, length, reinterpret_cast<jint *>(rects.data()));
        }
        omm::coverage::finish(static_cast<uint32_t>(textureId), rects);
    });
}

} // extern "C"
