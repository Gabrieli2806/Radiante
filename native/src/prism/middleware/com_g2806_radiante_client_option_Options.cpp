#include "prism/middleware/jni_support.hpp"
#include "prism/render/chunks.hpp"
#include "prism/render/renderer.hpp"
#include "prism/render/textures.hpp"

#include <algorithm>
#include <cstdint>

namespace jni = prism::jni;

namespace {

// The chunk build scheduler is made with the batch limits, so changing one takes a new scheduler.
void restartChunkScheduler() {
    if (auto chunks = jni::chunks()) chunks->resetScheduler();
}

} // namespace

extern "C" {

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_option_Options_nativeSetChunkBuildingBatchSize(JNIEnv *,
                                                                                                     jclass,
                                                                                                     jint size,
                                                                                                     jboolean write) {
    Renderer::options.chunkBuildingBatchSize = static_cast<uint32_t>(size);
    if (jni::isTrue(write)) jni::guard("[Radiante] nativeSetChunkBuildingBatchSize", restartChunkScheduler);
}

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_option_Options_nativeSetChunkBuildingTotalBatches(
    JNIEnv *, jclass, jint batches, jboolean write) {
    Renderer::options.chunkBuildingTotalBatches = static_cast<uint32_t>(batches);
    if (jni::isTrue(write)) jni::guard("[Radiante] nativeSetChunkBuildingTotalBatches", restartChunkScheduler);
}

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_option_Options_nativeSetEntityLightReach(JNIEnv *,
                                                                                               jclass,
                                                                                               jint reach,
                                                                                               jboolean) {
    Renderer::options.entityLightReach = std::clamp(static_cast<double>(reach), 8.0, 256.0);
}

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_option_Options_nativeSetCollectChunkEmission(
    JNIEnv *, jclass, jboolean collectChunkEmission, jboolean) {
    const bool collect = jni::isTrue(collectChunkEmission);
    if (Renderer::options.collectChunkEmission == collect) return;
    Renderer::options.collectChunkEmission = collect;
    jni::guard("[Radiante] nativeSetCollectChunkEmission", [collect] {
        if (auto chunks = jni::chunks()) chunks->setCollectChunkEmission(collect);
        if (collect) return;
        if (auto textures = jni::textures()) textures->releaseEmission();
    });
}

} // extern "C"
