#include "common/shared.hpp"
#include "prism/middleware/jni_support.hpp"
#include "prism/render/buffers.hpp"

#include <cstddef>
#include <cstring>

namespace jni = prism::jni;

// Java fills these blocks in buffers of its own (BufferProxy.java) sized by these numbers, which must follow any change
// to the structs.
static_assert(offsetof(gpu::Data::WorldUBO, packParams) == 736, "BufferProxy.WORLD_UBO_SIZE");
static_assert(sizeof(gpu::Data::SkyUBO) == 256, "BufferProxy.SKY_UBO_SIZE");
static_assert(sizeof(gpu::Data::TextureMapping) == 4096 * 16, "BufferProxy.TEXTURE_MAPPING_ENTRIES");

extern "C" {

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_vulkan_BufferProxy_updateWorldUniform(JNIEnv *,
                                                                                                  jclass,
                                                                                                  jlong ptr) {
    jni::guard("[Radiante] updateWorldUniform", [ptr] {
        auto buffers = jni::buffers();
        if (buffers == nullptr) return;
        // Java's block ends where packParams begins (the renderer sets those itself), so it is copied into a whole
        // struct rather than read in place past the end of what Java allocated.
        gpu::Data::WorldUBO ubo{};
        std::memcpy(&ubo, jni::as<const void *>(ptr), offsetof(gpu::Data::WorldUBO, packParams));
        buffers->setAndUploadWorldUniformBuffer(ubo);
    });
}

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_vulkan_BufferProxy_updateSkyUniform(JNIEnv *,
                                                                                                jclass,
                                                                                                jlong ptr) {
    jni::guard("[Radiante] updateSkyUniform", [ptr] {
        if (auto buffers = jni::buffers()) buffers->setAndUploadSkyUniformBuffer(*jni::as<gpu::Data::SkyUBO *>(ptr));
    });
}

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_vulkan_BufferProxy_updateMapping(JNIEnv *,
                                                                                             jclass,
                                                                                             jlong ptr) {
    jni::guard("[Radiante] updateMapping", [ptr] {
        auto buffers = jni::buffers();
        if (buffers != nullptr) buffers->setAndUploadTextureMappingBuffer(*jni::as<gpu::Data::TextureMapping *>(ptr));
    });
}

} // extern "C"
