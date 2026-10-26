#include "prism/middleware/jni_support.hpp"
#include "prism/render/entities.hpp"
#include "prism/render/world.hpp"

#include <memory>

namespace jni = prism::jni;

namespace {

std::shared_ptr<Entities> entities() {
    auto world = jni::world();
    return world != nullptr ? world->entities() : nullptr;
}

} // namespace

extern "C" {

// One batch of entities, in native arrays: the entity ones have an element per entity, the geometry ones an element
// per geometry of every entity in turn.
JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_world_EntityProxy_queueBuild(JNIEnv *,
                                                                                         jclass,
                                                                                         jfloat lineWidth,
                                                                                         jint coordinate,
                                                                                         jboolean normalOffset,
                                                                                         jint size,
                                                                                         jlong entityHashCodes,
                                                                                         jlong entityPosXs,
                                                                                         jlong entityPosYs,
                                                                                         jlong entityPosZs,
                                                                                         jlong entityRayTracingFlags,
                                                                                         jlong entityPrebuiltBLASs,
                                                                                         jlong entityLayerCounts,
                                                                                         jlong geometryTypes,
                                                                                         jlong geometryGroupNames,
                                                                                         jlong geometryContentNames,
                                                                                         jlong geometryTextures,
                                                                                         jlong vertexFormats,
                                                                                         jlong indexFormats,
                                                                                         jlong vertexCounts,
                                                                                         jlong vertices) {
    jni::guard("[Radiante] EntityProxy.queueBuild", [&] {
        auto queue = entities();
        if (queue == nullptr) return;
        queue->queueBuild(EntitiesBuildTask{
            .lineWidth = lineWidth,
            .coordinate = static_cast<World::Coordinates>(coordinate),
            .normalOffset = jni::isTrue(normalOffset),
            .entityCount = size,
            .entityHashCodes = jni::as<int *>(entityHashCodes),
            .entityXs = jni::as<double *>(entityPosXs),
            .entityYs = jni::as<double *>(entityPosYs),
            .entityZs = jni::as<double *>(entityPosZs),
            .entityRayTracingFlags = jni::as<int *>(entityRayTracingFlags),
            .entityPrebuiltBLASs = jni::as<int *>(entityPrebuiltBLASs),
            .entityGeometryCounts = jni::as<int *>(entityLayerCounts),
            .geometryTypes = jni::as<int *>(geometryTypes),
            .geometryGroupNames = jni::as<const char **>(geometryGroupNames),
            .geometryContentNames = jni::as<const char **>(geometryContentNames),
            .geometryTextures = jni::as<int *>(geometryTextures),
            .vertexFormats = jni::as<int *>(vertexFormats),
            .indexFormats = jni::as<int *>(indexFormats),
            .vertexCounts = jni::as<int *>(vertexCounts),
            .vertices = jni::as<void **>(vertices),
        });
    });
}

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_world_EntityProxy_build(JNIEnv *, jclass) {
    jni::guard("[Radiante] EntityProxy.build", [] {
        if (auto queue = entities()) queue->build();
    });
}

} // extern "C"
