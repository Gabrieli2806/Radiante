#include "prism/all_extern.hpp"
#include "prism/middleware/jni_support.hpp"
#include "prism/render/emission.hpp"
#include "prism/render/textures.hpp"

#include <cstdint>

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

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_vulkan_TextureProxy_setClamp(JNIEnv *,
                                                                                         jclass,
                                                                                         jint id,
                                                                                         jint addressMode) {
    jni::guard("[Radiante] setClamp", [=] {
        auto textures = jni::textures();
        if (textures == nullptr) return;
        textures->setAddressMode(static_cast<uint32_t>(id), static_cast<VkSamplerAddressMode>(addressMode));
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

} // extern "C"
