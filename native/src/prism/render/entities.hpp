#pragma once

#include "common/shared.hpp"
#include "common/singleton.hpp"
#include "prism/all_extern.hpp"
#include "prism/gpu/gpu.hpp"

#include "prism/render/emission.hpp"
#include "prism/render/world.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class Framework;

// One batch of entities as the Java side hands it over (EntityProxy.queueBuild). The entity arrays have an element
// per entity; the geometry arrays have one per geometry of every entity in turn, entityGeometryCounts[e] of them for
// entity e. The JNI entry point fills it with designated initializers, so the field order is part of the interface.
struct EntitiesBuildTask {
    // Side of the square boxes lines are traced as.
    float lineWidth;
    // Space of every vertex in the batch.
    World::Coordinates coordinate;
    // Pushes each quad vertex that has a normal a hair along it.
    bool normalOffset;
    int entityCount;
    // Identity of each entity from frame to frame: motion vectors and the static cache follow it.
    int *entityHashCodes;
    double *entityXs;
    double *entityYs;
    double *entityZs;
    int *entityRayTracingFlags;
    // Negative: the structure is built here. CACHEABLE_BLAS and KEYED_BLAS allow it to be kept between frames.
    int *entityPrebuiltBLASs;
    int *entityGeometryCounts;
    int *geometryTypes;
    // Hit group of each geometry; "Entity" where null.
    const char **geometryGroupNames;
    const char **geometryContentNames;
    // Texture of every vertex, for the vertex formats that do not carry one.
    int *geometryTextures;
    int *vertexFormats;
    int *indexFormats;
    int *vertexCounts;
    void **vertices;
};

// An entity in the frame's acceleration structure: a bottom-level structure of its own, instanced at its position.
// Nothing in it changes once it is built, except the position of a keyed entity the static cache moves.
struct Entity : public SharedObject<Entity> {
    int hashCode = 0;
    double x = 0.0, y = 0.0, z = 0.0;
    int rayTracingFlag = 0;
    int prebuiltBLAS = -1;
    World::Coordinates coordinate = World::WORLD;

    std::shared_ptr<gpu::BLAS> blas;
    // Where each geometry's indices, positions and materials start; the shaders read the geometry through these.
    std::shared_ptr<std::vector<VkDeviceAddress>> indexBufferAddresses;
    std::shared_ptr<std::vector<VkDeviceAddress>> positionBufferAddresses;
    std::shared_ptr<std::vector<VkDeviceAddress>> materialBufferAddresses;
    // The buffers those addresses point into, alive as long as the entity: the ray tracing pass keeps the entities of
    // earlier frames, and reads their geometry again for motion vectors.
    std::shared_ptr<gpu::DeviceLocalBuffer> indexBuffer;
    std::shared_ptr<gpu::DeviceLocalBuffer> positionBuffer;
    std::shared_ptr<gpu::DeviceLocalBuffer> materialBuffer;

    // Final float positions uploaded to the BLAS, before its instance transform; projected at TLAS preparation.
    // Retained with cached entities; only HAND_MASK entities need these.
    std::vector<glm::vec3> handPositions;
    bool handBoundsValid = false;

    uint32_t geometryCount = 0;
    std::shared_ptr<std::vector<std::string>> geometryGroupNames;
    std::shared_ptr<std::vector<std::string>> geometryContentNames;
    std::shared_ptr<std::vector<uint32_t>> vertexCounts;
    std::shared_ptr<std::vector<uint32_t>> indexCounts;
    // Lights in this entity's emissive quads (a dropped glowstone, an item frame holding a sea lantern), relative to
    // x, y, z. Null when it has none.
    std::shared_ptr<std::vector<LightInfo>> lightInfos;
};

// The entities traced this frame. The buffers are those of the entities built this frame; entities reused from
// the static cache keep buffers of their own.
struct EntityBatch : public SharedObject<EntityBatch> {
    std::vector<std::shared_ptr<Entity>> entities;

    std::shared_ptr<gpu::DeviceLocalBuffer> indexBuffer;
    std::shared_ptr<gpu::DeviceLocalBuffer> positionBuffer;
    std::shared_ptr<gpu::DeviceLocalBuffer> materialBuffer;
};

// The entities of each frame. The Java side queues them (queueBuild) and then has them built (build); the ray
// tracing pass records the structure builds and instances them.
//
// Block entities and others that stand still are kept between frames with their structure (the static cache),
// instead of being built again every frame with everything else.
class Entities : public SharedObject<Entities> {
  public:
    explicit Entities(std::shared_ptr<Framework> framework);
    ~Entities();

    void resetFrame();
    void queueBuild(const EntitiesBuildTask &task);
    void build();
    void close();
    std::shared_ptr<EntityBatch> entityBatch();
    // The structure builds of the entities built this frame; null when there are none.
    std::shared_ptr<gpu::BlasBatch> blasBatch();
    // Builds for newly cached entities, submitted alongside the frame's batch.
    std::vector<std::shared_ptr<gpu::BlasBatch>> &staticBlasBatches();
    // Called once the static builds are recorded into a frame; until then they are kept.
    void staticBatchesSubmitted();

    // The lights of the latest build's emissive entities (WorldUBO.entityLightAddress*, read by block_light.glsl),
    // and how many there are. 0 and 0 without any.
    VkDeviceAddress lightBufferAddress() const {
        return lightBufferAddress_;
    }
    uint32_t lightCount() const {
        return lightCount_;
    }

    // Lights further from the camera than Renderer::options.entityLightReach are left out, and of the rest at most
    // this many, the nearest.
    static constexpr uint32_t MAX_ENTITY_LIGHTS = 1024;

    // Value the Java side puts in prebuiltBLAS for geometry that may be cached (see EntityManager).
    static constexpr int CACHEABLE_BLAS = -2;
    // Like CACHEABLE_BLAS, but the Java side names the content (in the geometry content names) and moves it by
    // position alone: a match reuses the structure without reading the vertices at all, and only moves the instance.
    // For large meshes that drift but rarely change shape - the vanilla clouds.
    static constexpr int KEYED_BLAS = -3;
    // Frames an entity may go unseen before its cached structure is dropped.
    static constexpr uint64_t STATIC_CACHE_EVICT_FRAMES = 120;

  private:
    struct State;

    void buildLightBuffer();

    std::unique_ptr<State> state_;
    VkDeviceAddress lightBufferAddress_ = 0;
    uint32_t lightCount_ = 0;
};
