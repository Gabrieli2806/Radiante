#include "prism/render/entities.hpp"
#include "prism/render/scene_stats.hpp"
#include "prism/render/hand_primary_bounds.hpp"
#include "prism/render/entity_geometry.hpp"

#include "prism/render/buffers.hpp"
#include "prism/render/chunks.hpp"
#include "prism/render/render_framework.hpp"
#include "prism/render/renderer.hpp"
#include "prism/render/textures.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

using namespace entity_geometry;

namespace {

namespace vf = gpu::VertexFormat;

// ---- Packed geometry and its buffers

// Where one geometry's triangles are in the PackedGeometry it went into.
struct GeometryRange {
    uint32_t firstVertex;
    uint32_t vertexCount;
    uint32_t firstIndex;
    uint32_t indexCount;
    bool opaque;
};

// The triangles of entities built together, in the layouts the shaders read: filled as entities are queued, then
// uploaded into one set of buffers.
struct PackedGeometry {
    std::vector<vf::PositionVertex> positions;
    std::vector<vf::PackedMaterialVertex> materials;
    std::vector<uint32_t> indices;
    std::vector<GeometryRange> ranges;

    void clear() {
        positions.clear();
        materials.clear();
        indices.clear();
        ranges.clear();
    }

    void add(const std::vector<PBRVertex> &vertices, const std::vector<uint32_t> &geometryIndices, bool opaque) {
        ranges.push_back({
            .firstVertex = static_cast<uint32_t>(positions.size()),
            .vertexCount = static_cast<uint32_t>(vertices.size()),
            .firstIndex = static_cast<uint32_t>(indices.size()),
            .indexCount = static_cast<uint32_t>(geometryIndices.size()),
            .opaque = opaque,
        });
        for (const PBRVertex &vertex : vertices) {
            positions.push_back(gpu::Vertex::makePositionVertex(vertex));
            materials.push_back(gpu::Vertex::makeMaterialVertex(vertex));
        }
        indices.insert(indices.end(), geometryIndices.begin(), geometryIndices.end());
    }
};

// The buffers a PackedGeometry goes into, staged and waiting to be queued for upload.
struct GeometryBuffers {
    std::shared_ptr<gpu::DeviceLocalBuffer> index;
    std::shared_ptr<gpu::DeviceLocalBuffer> position;
    std::shared_ptr<gpu::DeviceLocalBuffer> material;

    explicit operator bool() const {
        return index != nullptr;
    }
};

// No buffers when there are no triangles: buffers of no size cannot be made.
GeometryBuffers stageGeometry(PackedGeometry &packed) {
    if (packed.positions.empty() || packed.indices.empty()) return {};

    auto framework = Renderer::instance().framework();
    auto vma = framework->vma();
    auto device = framework->device();
    constexpr VkBufferUsageFlags kShaderRead =
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    constexpr VkBufferUsageFlags kBuildInput = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    const size_t positionBytes = packed.positions.size() * sizeof(vf::PositionVertex);
    const size_t materialBytes = packed.materials.size() * sizeof(vf::PackedMaterialVertex);
    const size_t indexBytes = packed.indices.size() * sizeof(uint32_t);

    GeometryBuffers buffers;
    buffers.position = gpu::DeviceLocalBuffer::create(vma, device, false, positionBytes, kShaderRead | kBuildInput);
    buffers.material = gpu::DeviceLocalBuffer::create(vma, device, false, materialBytes, kShaderRead);
    buffers.index = gpu::DeviceLocalBuffer::create(vma, device, false, indexBytes, kShaderRead | kBuildInput);
    buffers.position->uploadToStagingBuffer(packed.positions.data(), positionBytes, 0);
    buffers.material->uploadToStagingBuffer(packed.materials.data(), materialBytes, 0);
    buffers.index->uploadToStagingBuffer(packed.indices.data(), indexBytes, 0);
    return buffers;
}

void queueUploads(Buffers &uploads, const GeometryBuffers &buffers) {
    uploads.queueImportantWorldUpload(buffers.index);
    uploads.queueImportantWorldUpload(buffers.position);
    uploads.queueImportantWorldUpload(buffers.material);
}

// Points the entity at its geometries in `buffers` and, unless its structure is prebuilt, describes that structure
// on `batch`; the entities waiting for one are listed in `awaitingStructure`, in the order of their builds.
void attachGeometry(Entity &entity,
                    const PackedGeometry &packed,
                    uint32_t firstRange,
                    const GeometryBuffers &buffers,
                    gpu::BlasBatch &batch,
                    std::vector<Entity *> &awaitingStructure) {
    std::shared_ptr<gpu::BlasBuild> build;
    if (entity.prebuiltBLAS < 0) {
        build = batch.add(VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
        awaitingStructure.push_back(&entity);
    }

    const VkDeviceAddress indexBase = buffers.index->bufferAddress();
    const VkDeviceAddress positionBase = buffers.position->bufferAddress();
    const VkDeviceAddress materialBase = buffers.material->bufferAddress();
    auto &indexAddresses = *entity.indexBufferAddresses;
    auto &positionAddresses = *entity.positionBufferAddresses;
    auto &materialAddresses = *entity.materialBufferAddresses;
    indexAddresses.clear();
    positionAddresses.clear();
    materialAddresses.clear();
    for (uint32_t geometry = 0; geometry < entity.geometryCount; geometry++) {
        const GeometryRange &range = packed.ranges[firstRange + geometry];
        const VkDeviceAddress indices = indexBase + range.firstIndex * sizeof(uint32_t);
        const VkDeviceAddress positions = positionBase + range.firstVertex * sizeof(vf::PositionVertex);
        const VkDeviceAddress materials = materialBase + range.firstVertex * sizeof(vf::PackedMaterialVertex);
        indexAddresses.push_back(indices);
        positionAddresses.push_back(positions);
        materialAddresses.push_back(materials);
        if (build != nullptr) {
            build->triangles<vf::PositionVertex>(positions, range.vertexCount, indices, range.indexCount, range.opaque);
        }
    }
    entity.indexBuffer = buffers.index;
    entity.positionBuffer = buffers.position;
    entity.materialBuffer = buffers.material;
}

// Places every structure described on `batch` in one buffer and hands each to its entity.
void placeStructures(gpu::BlasBatch &batch, const std::vector<Entity *> &awaitingStructure) {
    SceneStats::entityBlasThisFrame.fetch_add(static_cast<uint32_t>(awaitingStructure.size()), std::memory_order_relaxed);
    auto framework = Renderer::instance().framework();
    std::vector<std::shared_ptr<gpu::BLAS>> structures = batch.place(framework->device(), framework->vma());
    for (size_t i = 0; i < awaitingStructure.size(); i++) awaitingStructure[i]->blas = std::move(structures[i]);
}

// Everything an Entity lists per geometry, in one allocation that its list pointers share.
struct EntityLists {
    std::vector<VkDeviceAddress> indexBufferAddresses;
    std::vector<VkDeviceAddress> positionBufferAddresses;
    std::vector<VkDeviceAddress> materialBufferAddresses;
    std::vector<std::string> geometryGroupNames;
    std::vector<std::string> geometryContentNames;
    std::vector<uint32_t> vertexCounts;
    std::vector<uint32_t> indexCounts;
};

std::shared_ptr<Entity> makeEntity(const EntityInput &input, World::Coordinates coordinate, int prebuiltBLAS) {
    auto entity = Entity::create();
    entity->hashCode = input.hashCode;
    entity->x = input.x;
    entity->y = input.y;
    entity->z = input.z;
    entity->rayTracingFlag = input.rayTracingFlag;
    entity->prebuiltBLAS = prebuiltBLAS;
    entity->coordinate = coordinate;

    auto lists = std::make_shared<EntityLists>();
    const auto geometries = static_cast<size_t>(input.geometryCount);
    lists->indexBufferAddresses.reserve(geometries);
    lists->positionBufferAddresses.reserve(geometries);
    lists->materialBufferAddresses.reserve(geometries);
    lists->geometryGroupNames.reserve(geometries);
    lists->geometryContentNames.reserve(geometries);
    lists->vertexCounts.reserve(geometries);
    lists->indexCounts.reserve(geometries);
    entity->indexBufferAddresses = {lists, &lists->indexBufferAddresses};
    entity->positionBufferAddresses = {lists, &lists->positionBufferAddresses};
    entity->materialBufferAddresses = {lists, &lists->materialBufferAddresses};
    entity->geometryGroupNames = {lists, &lists->geometryGroupNames};
    entity->geometryContentNames = {lists, &lists->geometryContentNames};
    entity->vertexCounts = {lists, &lists->vertexCounts};
    entity->indexCounts = {lists, &lists->indexCounts};
    return entity;
}

// Textures::emission() makes the emission state on first use, under a lock. It is asked at most once per batch, and
// only once an entity has triangles that may glow.
class EmissionLookup {
  public:
    const Emission *get() {
        if (!resolved_) {
            resolved_ = true;
            if (auto textures = Renderer::instance().textures(); textures != nullptr) emission_ = textures->emission();
        }
        return emission_.get();
    }

  private:
    bool resolved_ = false;
    std::shared_ptr<Emission> emission_;
};

// ---- Development timings (RADIANTE_DEV_PROFILE): where queueBuild spends its time, averaged over 300 calls.

struct QueueProfile {
    bool enabled = std::getenv("RADIANTE_DEV_PROFILE") != nullptr;
    double acquire = 0, kept = 0, built = 0;
    long keptCount = 0, builtCount = 0;
    int calls = 0;
};
QueueProfile g_queueProfile;

class QueueTimer {
  public:
    QueueTimer() {
        if (g_queueProfile.enabled) last_ = Clock::now();
    }

    QueueTimer(const QueueTimer &) = delete;
    QueueTimer &operator=(const QueueTimer &) = delete;

    ~QueueTimer() {
        if (!g_queueProfile.enabled || ++g_queueProfile.calls < 300) return;
        std::cout << "[native profile] entities: acquire(wait)=" << g_queueProfile.acquire / 300
                  << "ms kept=" << g_queueProfile.kept / 300 << "ms (" << g_queueProfile.keptCount / 300
                  << ") built=" << g_queueProfile.built / 300 << "ms (" << g_queueProfile.builtCount / 300 << ")"
                  << std::endl;
        g_queueProfile = QueueProfile{};
    }

    // After the frame context is acquired, which may wait for the GPU.
    void acquired() {
        if (g_queueProfile.enabled) g_queueProfile.acquire += lap();
    }

    // After an entity: moved in the cache by its key alone (kept), or anything else.
    void entity(bool kept) {
        if (!g_queueProfile.enabled) return;
        const double ms = lap();
        if (kept) {
            g_queueProfile.kept += ms;
            g_queueProfile.keptCount++;
        } else {
            g_queueProfile.built += ms;
            g_queueProfile.builtCount++;
        }
    }

  private:
    using Clock = std::chrono::steady_clock;

    double lap() {
        const Clock::time_point now = Clock::now();
        const double ms = std::chrono::duration<double, std::milli>(now - last_).count();
        last_ = now;
        return ms;
    }

    Clock::time_point last_{};
};

} // namespace

// ---- Entities

struct Entities::State {
    // A cached entity, kept with its structure while its content stays the same.
    struct CacheEntry {
        uint64_t contentHash = 0;
        bool seen = false;
        uint64_t lastSeenFrame = 0;
        std::shared_ptr<Entity> entity;
    };

    // A traced entity built this frame: its geometries are ranges of `packed` from firstRange on.
    struct PendingEntity {
        std::shared_ptr<Entity> entity;
        uint32_t firstRange;
    };

    // The buffers and structure build of an entity cached this frame.
    struct StaticBuild {
        GeometryBuffers buffers;
        std::shared_ptr<gpu::BlasBatch> blasBatch;
    };

    struct LightCandidate {
        double distance2;
        const LightInfo *light;
        glm::vec3 origin;
    };

    // What the frame has queued.
    PackedGeometry packed;
    std::vector<PendingEntity> pending;
    std::vector<std::shared_ptr<Entity>> reused;
    std::vector<StaticBuild> newStaticBuilds;

    // What build() made of it.
    std::shared_ptr<EntityBatch> entityBatch;
    std::shared_ptr<gpu::BlasBatch> blasBatch;
    std::shared_ptr<gpu::DeviceLocalBuffer> lightBuffer;
    // Kept until a frame records them, across frames that are prepared but never rendered.
    std::vector<std::shared_ptr<gpu::BlasBatch>> staticBlasBatches;

    std::unordered_map<int, CacheEntry> staticCache;
    uint64_t frameCounter = 0;

    // Working memory, reused.
    TriangleScratch triangles;
    PackedGeometry single;
    std::vector<Entity *> awaitingStructure;
    std::unordered_map<uint32_t, Emission::TextureOccupancy> occupancy;
    std::vector<LightCandidate> lightCandidates;
    std::vector<PackedLightData> packedLights;

    std::shared_ptr<Entity> gatherTraced(const EntitiesBuildTask &task,
                                         const EntityInput &input,
                                         int prebuiltBLAS,
                                         PackedGeometry &target,
                                         EmissionLookup &emission);
    std::optional<StaticBuild> buildCached(Entity &entity);
};

// Triangulates a traced entity's geometries into `target` and lists them on a new Entity, with the lights in its
// emissive quads (as a chunk's: only for entities placed in the world). Null when no geometry has triangles.
std::shared_ptr<Entity> Entities::State::gatherTraced(const EntitiesBuildTask &task,
                                                      const EntityInput &input,
                                                      int prebuiltBLAS,
                                                      PackedGeometry &target,
                                                      EmissionLookup &emission) {
    const Placement placement = placementOf(task, input);
    const bool collectLights = Renderer::options.collectChunkEmission && task.coordinate == World::WORLD;
    std::shared_ptr<Entity> entity;
    std::vector<LightInfo> lights;
    uint32_t kept = 0;
    for (int i = 0; i < input.geometryCount; i++) {
        const size_t geometry = input.firstGeometry + i;
        if (!triangulate(task, geometry, placement, triangles)) continue;
        if (entity == nullptr) {
            entity = makeEntity(input, task.coordinate, prebuiltBLAS);
            entity->handBoundsValid = true;
        }
        if ((input.rayTracingFlag & hand_primary::mask) != 0) {
            for (const auto &vertex : triangles.vertices) {
                const glm::vec3 p(gpu::Vertex::makePositionVertex(vertex).pos);
                if (!hand_primary::finite(p)) entity->handBoundsValid = false;
                entity->handPositions.push_back(p);
            }
        }

        if (collectLights) {
            if (const Emission *source = emission.get(); source != nullptr) {
                collectEmissiveQuadLights(*source, triangles.vertices, input.hashCode, kept, occupancy, lights);
            }
        }
        target.add(triangles.vertices, triangles.indices, task.geometryTypes[geometry] == World::WORLD_SOLID);
        entity->geometryGroupNames->emplace_back(groupName(task, geometry));
        entity->geometryContentNames->emplace_back(contentName(task, geometry));
        entity->vertexCounts->push_back(static_cast<uint32_t>(triangles.vertices.size()));
        entity->indexCounts->push_back(static_cast<uint32_t>(triangles.indices.size()));
        kept++;
    }
    if (entity == nullptr) return nullptr;
    entity->geometryCount = kept;
    if (!lights.empty()) entity->lightInfos = std::make_shared<std::vector<LightInfo>>(std::move(lights));
    return entity;
}

// Builds an entity gathered into `single` in buffers and a structure of its own, for the static cache. Nothing when
// there was nothing to upload.
std::optional<Entities::State::StaticBuild> Entities::State::buildCached(Entity &entity) {
    GeometryBuffers buffers = stageGeometry(single);
    if (!buffers) return std::nullopt;
    auto batch = gpu::BlasBatch::create();
    awaitingStructure.clear();
    attachGeometry(entity, single, 0, buffers, *batch, awaitingStructure);
    placeStructures(*batch, awaitingStructure);
    return StaticBuild{std::move(buffers), std::move(batch)};
}

Entities::Entities(std::shared_ptr<Framework>) : state_(std::make_unique<State>()) {}

Entities::~Entities() = default;

void Entities::resetFrame() {
    SceneStats::entityBlasLastFrame.store(SceneStats::entityBlasThisFrame.exchange(0, std::memory_order_relaxed),
                                          std::memory_order_relaxed);
    Renderer::instance().framework()->safeAcquireCurrentContext();
    State &s = *state_;

    // What the previous frame built retires itself once no frame in flight uses it.
    s.packed.clear();
    s.pending.clear();
    s.entityBatch = nullptr;
    s.blasBatch = nullptr;
    s.reused.clear();
    s.newStaticBuilds.clear();

    // staticBlasBatches stays: builds of newly cached entities wait for a frame that actually records them
    // (staticBatchesSubmitted). Frames that are prepared but never rendered - while a world loads, say - used to
    // drop them, and the cached entity then stayed without an acceleration structure: invisible until its content
    // changed.
    s.frameCounter++;
    std::erase_if(s.staticCache, [&](const auto &item) {
        return s.frameCounter - item.second.lastSeenFrame > STATIC_CACHE_EVICT_FRAMES;
    });
}

void Entities::queueBuild(const EntitiesBuildTask &task) {
    QueueTimer timer;
    Renderer::instance().framework()->safeAcquireCurrentContext();
    timer.acquired();

    State &s = *state_;
    s.occupancy.clear();
    EmissionLookup emission;

    size_t firstGeometry = 0;
    for (int e = 0; e < task.entityCount; e++) {
        const EntityInput input = readEntity(task, e, firstGeometry);
        firstGeometry += static_cast<size_t>(input.geometryCount);

        // The static cache. A cacheable entity is cached only once its content has stayed the same for two frames
        // in a row, so geometry that animates every frame (a waving banner, a chest while it opens) never pays for
        // a build of its own that would be thrown away the next frame. Keyed content is cached at once: its name
        // only changes when its shape does.
        const bool keyed = input.prebuiltBLAS == KEYED_BLAS;
        const bool cacheable = keyed || input.prebuiltBLAS == CACHEABLE_BLAS;
        uint64_t contentKey = 0;
        bool stable = false;
        if (keyed) {
            contentKey = keyedContentKey(task, input);
            State::CacheEntry &entry = s.staticCache[input.hashCode];
            entry.lastSeenFrame = s.frameCounter;
            if (entry.entity != nullptr && entry.contentHash == contentKey) {
                entry.entity->x = input.x;
                entry.entity->y = input.y;
                entry.entity->z = input.z;
                s.reused.push_back(entry.entity);
                timer.entity(true);
                continue;
            }
            stable = true;
        } else if (cacheable) {
            contentKey = cacheableContentKey(task, input);
            if (auto found = s.staticCache.find(input.hashCode); found != s.staticCache.end()) {
                State::CacheEntry &entry = found->second;
                if (entry.entity != nullptr && entry.contentHash == contentKey) {
                    entry.lastSeenFrame = s.frameCounter;
                    s.reused.push_back(entry.entity);
                    timer.entity(false);
                    continue;
                }
                stable = entry.seen && entry.contentHash == contentKey;
            }
        }

        PackedGeometry &target = stable ? s.single : s.packed;
        if (stable) s.single.clear();
        const auto firstRange = static_cast<uint32_t>(target.ranges.size());
        std::shared_ptr<Entity> entity =
            s.gatherTraced(task, input, cacheable ? -1 : input.prebuiltBLAS, target, emission);
        if (entity == nullptr) continue;

        if (cacheable) {
            State::CacheEntry &entry = s.staticCache[input.hashCode];
            entry.lastSeenFrame = s.frameCounter;
            entry.seen = true;
            entry.contentHash = contentKey;
            entry.entity = nullptr;
            if (stable) {
                if (auto build = s.buildCached(*entity); build.has_value()) {
                    entry.entity = entity;
                    s.reused.push_back(std::move(entity));
                    s.newStaticBuilds.push_back(std::move(*build));
                }
                continue;
            }
        }
        s.pending.push_back({std::move(entity), firstRange});
        timer.entity(false);
    }
}

void Entities::build() {
    Renderer::instance().framework()->safeAcquireCurrentContext();
    State &s = *state_;
    Buffers &uploads = *Renderer::instance().buffers();

    // Everything queued since the frame began, in the order it was queued.
    const GeometryBuffers frameBuffers = stageGeometry(s.packed);
    s.blasBatch = nullptr;
    if (frameBuffers) {
        queueUploads(uploads, frameBuffers);
        s.blasBatch = gpu::BlasBatch::create();
        s.awaitingStructure.clear();
        for (const State::PendingEntity &pending : s.pending) {
            attachGeometry(*pending.entity, s.packed, pending.firstRange, frameBuffers, *s.blasBatch,
                           s.awaitingStructure);
        }
        placeStructures(*s.blasBatch, s.awaitingStructure);
    }

    s.entityBatch = EntityBatch::create();
    s.entityBatch->entities.reserve(s.pending.size() + s.reused.size());
    for (const State::PendingEntity &pending : s.pending) s.entityBatch->entities.push_back(pending.entity);
    s.entityBatch->indexBuffer = frameBuffers.index;
    s.entityBatch->positionBuffer = frameBuffers.position;
    s.entityBatch->materialBuffer = frameBuffers.material;

    for (State::StaticBuild &build : s.newStaticBuilds) {
        queueUploads(uploads, build.buffers);
        s.staticBlasBatches.push_back(std::move(build.blasBatch));
    }
    s.newStaticBuilds.clear();
    // Cached entities come after the ones built this frame.
    s.entityBatch->entities.insert(s.entityBatch->entities.end(), s.reused.begin(), s.reused.end());
    buildLightBuffer();
}

// The lights of the entities just built, in the buffer WorldUBO points the shaders at. The world uniform of a frame
// is written before its entities are built, so it carries the buffer of the build before.
void Entities::buildLightBuffer() {
    State &s = *state_;
    // That previous buffer is still read by the frames in flight; it retires itself once they are done.
    s.lightBuffer = nullptr;
    lightBufferAddress_ = 0;
    lightCount_ = 0;
    if (s.entityBatch == nullptr || !Renderer::options.collectChunkEmission) return;

    const glm::dvec3 cameraPos = Renderer::instance().world()->getCameraPos();
    const double reach = Renderer::options.entityLightReach;
    const double reach2 = reach * reach;
    auto &candidates = s.lightCandidates;
    candidates.clear();
    for (const auto &entity : s.entityBatch->entities) {
        if (entity == nullptr || entity->lightInfos == nullptr || entity->coordinate != World::WORLD) continue;
        const glm::dvec3 origin(entity->x, entity->y, entity->z);
        for (const LightInfo &light : *entity->lightInfos) {
            const glm::dvec3 center = origin + glm::dvec3((light.p0 + light.p1 + light.p2) / 3.0f);
            const glm::dvec3 delta = center - cameraPos;
            const double distance2 = glm::dot(delta, delta);
            if (distance2 > reach2) continue;
            candidates.push_back({distance2, &light, glm::vec3(origin)});
        }
    }
    if (candidates.empty()) return;
    if (candidates.size() > MAX_ENTITY_LIGHTS) {
        std::nth_element(
            candidates.begin(), candidates.begin() + MAX_ENTITY_LIGHTS, candidates.end(),
            [](const State::LightCandidate &a, const State::LightCandidate &b) { return a.distance2 < b.distance2; });
        candidates.resize(MAX_ENTITY_LIGHTS);
    }

    auto &packed = s.packedLights;
    packed.clear();
    for (const State::LightCandidate &candidate : candidates) {
        const LightInfo &light = *candidate.light;
        const float maxRadiance = std::max({light.radiance.r, light.radiance.g, light.radiance.b});
        if (light.area <= 1e-6f || maxRadiance <= 1e-6f) continue;
        packed.push_back(packLightAt(light, candidate.origin));
    }
    if (packed.empty()) return;

    auto framework = Renderer::instance().framework();
    const size_t bytes = packed.size() * sizeof(PackedLightData);
    s.lightBuffer =
        gpu::DeviceLocalBuffer::create(framework->vma(), framework->device(), false, bytes,
                                       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                       0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 16);
    s.lightBuffer->uploadToStagingBuffer(packed.data(), bytes, 0);
    Renderer::instance().buffers()->queueImportantWorldUpload(s.lightBuffer);
    lightBufferAddress_ = s.lightBuffer->bufferAddress();
    lightCount_ = static_cast<uint32_t>(packed.size());
}

void Entities::close() {
    state_ = std::make_unique<State>();
    lightBufferAddress_ = 0;
    lightCount_ = 0;
}

std::shared_ptr<EntityBatch> Entities::entityBatch() {
    Renderer::instance().framework()->safeAcquireCurrentContext();
    return state_->entityBatch;
}

std::shared_ptr<gpu::BlasBatch> Entities::blasBatch() {
    return state_->blasBatch;
}

std::vector<std::shared_ptr<gpu::BlasBatch>> &Entities::staticBlasBatches() {
    return state_->staticBlasBatches;
}

void Entities::staticBatchesSubmitted() {
    // Recorded into the frame: their scratch and storage retire themselves once it has run.
    state_->staticBlasBatches.clear();
}
