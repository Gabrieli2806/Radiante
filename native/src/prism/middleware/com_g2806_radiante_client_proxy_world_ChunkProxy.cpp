#include "prism/all_extern.hpp"
#include "prism/middleware/jni_support.hpp"
#include "prism/render/chunks.hpp"

#include <cstdint>
#include <exception>
#include <iostream>
#include <new>
#include <vector>

namespace jni = prism::jni;

extern "C" {

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_world_ChunkProxy_initNative(
    JNIEnv *, jclass, jint chunkNum, jint sizeX, jint sizeY, jint sizeZ, jint bottomSectionCoord) {
    jni::guard("[Chunks] initNative", [=] {
        auto chunks = jni::chunks();
        if (chunks == nullptr) return;
        chunks->reset(static_cast<uint32_t>(chunkNum), static_cast<uint32_t>(sizeX), static_cast<uint32_t>(sizeY),
                      static_cast<uint32_t>(sizeZ), bottomSectionCoord);
    });
}

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_world_ChunkProxy_updateSectionPosNative(
    JNIEnv *, jclass, jint sectionX, jint sectionY, jint sectionZ) {
    jni::guard("[Chunks] updateSectionPosNative", [=] {
        if (auto chunks = jni::chunks()) chunks->setChunkStorageSectionPos(glm::ivec3(sectionX, sectionY, sectionZ));
    });
}

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_world_ChunkProxy_rebuildSingle(JNIEnv *,
                                                                                           jclass,
                                                                                           jint originX,
                                                                                           jint originY,
                                                                                           jint originZ,
                                                                                           jlong index,
                                                                                           jint geometryCount,
                                                                                           jlong geometryTypes,
                                                                                           jlong geometryGroupNames,
                                                                                           jlong geometryTextures,
                                                                                           jlong vertexFormats,
                                                                                           jlong vertexCounts,
                                                                                           jlong vertexAddrs,
                                                                                           jboolean important) {
    // Runs on the section builder threads. What has failed here is the system running out of memory under a huge Java
    // heap, and losing this one section until it next changes is far better than the game going down. The lines go
    // straight to stderr: the error log needs memory to keep a line.
    try {
        auto chunks = jni::chunks();
        if (chunks == nullptr) return;
        chunks->queueChunkBuild(ChunkBuildTask{
            .x = originX,
            .y = originY,
            .z = originZ,
            .id = index,
            .geometryCount = geometryCount,
            .geometryTypes = jni::as<int *>(geometryTypes),
            .geometryGroupNames = jni::as<const char **>(geometryGroupNames),
            .geometryTextures = jni::as<int *>(geometryTextures),
            .vertexFormats = jni::as<int *>(vertexFormats),
            .vertexCounts = jni::as<int *>(vertexCounts),
            .vertices = jni::as<gpu::VertexFormat::PBRVertex **>(vertexAddrs),
            .isImportant = jni::isTrue(important),
        });
    } catch (const std::bad_alloc &) {
        std::cerr << "[Chunks] out of memory queueing section " << index << "; skipped" << std::endl;
    } catch (const std::exception &e) {
        std::cerr << "[Chunks] could not queue section " << index << ": " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "[Chunks] could not queue section " << index << std::endl;
    }
}

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_world_ChunkProxy_relocateSingle(
    JNIEnv *, jclass, jlong index, jint originX, jint originY, jint originZ) {
    jni::guard("[Chunks] relocateSingle", [=] {
        if (auto chunks = jni::chunks()) chunks->relocateChunk(static_cast<int>(index), originX, originY, originZ);
    });
}

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_world_ChunkProxy_invalidateSingle(JNIEnv *,
                                                                                              jclass,
                                                                                              jlong index) {
    jni::guard("[Chunks] invalidateSingle", [index] {
        if (auto chunks = jni::chunks()) chunks->invalidateChunk(static_cast<int>(index));
    });
}

// The chunk columns, in a size x size window from originX, originZ, whose terrain is built: a bit each, row by row
// along x. Distant Horizons terrain is hidden in them. No words, or a size of 0, clears it.
JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_world_ChunkProxy_setLodCoverage(
    JNIEnv *env, jclass, jint originX, jint originZ, jint size, jintArray words) {
    jni::guard("[Chunks] setLodCoverage", [&] {
        auto chunks = jni::chunks();
        if (chunks == nullptr) return;
        if (words == nullptr || size <= 0) {
            chunks->setLodCoverage(originX, originZ, 0, nullptr, 0);
            return;
        }
        // Read into a buffer kept from call to call, as the coverage changes all the time while terrain loads; the
        // chunks keep a copy of their own.
        thread_local std::vector<uint32_t> coverage;
        coverage.resize(static_cast<size_t>(env->GetArrayLength(words)));
        env->GetIntArrayRegion(words, 0, static_cast<jsize>(coverage.size()),
                               reinterpret_cast<jint *>(coverage.data()));
        chunks->setLodCoverage(originX, originZ, size, coverage.data(), coverage.size());
    });
}

JNIEXPORT jboolean JNICALL Java_com_g2806_radiante_client_proxy_world_ChunkProxy_isSlotCurrent(JNIEnv *,
                                                                                               jclass,
                                                                                               jlong index) {
    return jni::boolean(jni::guard("[Chunks] isSlotCurrent", false, [index] {
        auto chunks = jni::chunks();
        return chunks != nullptr && chunks->isChunkCurrent(index);
    }));
}

} // extern "C"
