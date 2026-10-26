#include "prism/render/modules/world/ray_tracing/submodules/world_prepare.hpp"

#include "prism/render/buffers.hpp"
#include "prism/render/chunks.hpp"
#include "prism/render/entities.hpp"
#include "prism/render/opacity_micromaps.hpp"
#include "prism/render/render_framework.hpp"
#include "prism/render/renderer.hpp"
#include "prism/render/world.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <type_traits>

namespace {

using Clock = std::chrono::steady_clock;

// Hit group names of the slots that are not a geometry's own: every instance's shadow slot, and geometries without
// a name. Compared by address.
const std::string kShadowGroup = "shadow";
const std::string kDefaultGroup = "default";

// Development timings (RADIANTE_DEV_PROFILE): where preparing the scene spends its time, averaged over 300 frames.
struct PrepareProfile {
    bool enabled = std::getenv("RADIANTE_DEV_PROFILE") != nullptr;
    double schedule = 0, entities = 0, chunks = 0, tlas = 0, upload = 0, sbt = 0;
    int frames = 0, instances = 0, rebuilds = 0;
};
PrepareProfile g_profile;

double millisecondsSince(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// Milliseconds since `last`, which moves on to now.
double lap(Clock::time_point &last) {
    const auto now = Clock::now();
    const double ms = std::chrono::duration<double, std::milli>(now - last).count();
    last = now;
    return ms;
}

constexpr VkBufferUsageFlags kTableUsage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                           VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

VkTransformMatrixKHR translation(float x, float y, float z) {
    return {
        1, 0, 0, x, //
        0, 1, 0, y, //
        0, 0, 1, z, //
    };
}

// Where an entity is placed relative to the camera: world geometry at its position, camera-space geometry (the held
// item) through the inverse view, and camera-shifted geometry at the camera's offset.
VkTransformMatrixKHR entityTransform(const Entity &entity, const glm::dvec3 &camera, const gpu::Data::WorldUBO *ubo) {
    if (entity.coordinate == World::Coordinates::WORLD || ubo == nullptr) {
        return translation(static_cast<float>(entity.x - camera.x), static_cast<float>(entity.y - camera.y),
                           static_cast<float>(entity.z - camera.z));
    }
    if (entity.coordinate == World::Coordinates::CAMERA) {
        const glm::mat4 rows = glm::transpose(ubo->cameraViewMatInv);
        return {
            rows[0][0], rows[0][1], rows[0][2], rows[0][3], //
            rows[1][0], rows[1][1], rows[1][2], rows[1][3], //
            rows[2][0], rows[2][1], rows[2][2], rows[2][3], //
        };
    }
    const glm::vec3 shift(ubo->cameraViewMatInv[3]);
    return translation(shift.x, shift.y, shift.z);
}

glm::mat4 toMatrix(const VkTransformMatrixKHR &transform) {
    return glm::transpose(glm::mat4(glm::make_vec4(transform.matrix[0]), glm::make_vec4(transform.matrix[1]),
                                    glm::make_vec4(transform.matrix[2]), glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)));
}

// Records the bottom-level work the frame's top-level build depends on: compaction of freshly built chunks, the
// chunk builds that must not wait, the foliage the wind moves, and the entities' structures.
void recordStructureBuilds(Chunks &chunks,
                           Entities &entities,
                           const glm::dvec3 &cameraPos,
                           const std::shared_ptr<gpu::CommandBuffer> &commandBuffer,
                           const std::shared_ptr<gpu::PhysicalDevice> &physicalDevice,
                           const std::shared_ptr<gpu::Device> &device,
                           const std::shared_ptr<gpu::VMA> &vma) {
    // Freshly built chunks trade their acceleration structures for compacted copies, typically half the size. At far
    // render distances these structures are gigabytes; left uncompacted they pushed the renderer past the card's
    // memory, and once video memory spills the frame rate collapses.
    chunks.compactFinishedBlases(commandBuffer);
    gpu::diagnostics::checkpoint(commandBuffer->vkCommandBuffer(), "prepare:compacted");

    auto &importantBuilds = chunks.importantBlasBuilds();
    if (!importantBuilds.empty()) {
        // The scratch memory is dropped here and retired with this frame. The builds stay queued until a frame
        // records them: a chunk whose structure was never built would be traced from uninitialised memory.
        (void)gpu::recordBlasBuilds(commandBuffer, importantBuilds, physicalDevice, device, vma);
        importantBuilds.clear();
    }

    // After the builds above: a section built this frame is refit from the structure they make (recordWind's own
    // barrier orders the two). Nothing is recorded while no section sways.
    if (auto buffers = Renderer::instance().buffers(); buffers != nullptr) {
        const gpu::Data::WorldUBO ubo = buffers->lastWrittenWorldUniform();
        chunks.recordWind(commandBuffer, cameraPos,
                          {.time = ubo.windTime, .strength = ubo.windStrength, .weather = ubo.windWeather});
    }
    gpu::diagnostics::checkpoint(commandBuffer->vkCommandBuffer(), "prepare:wind");

    if (auto batch = entities.blasBatch(); batch != nullptr) batch->record(commandBuffer, physicalDevice, device, vma);
    for (auto &batch : entities.staticBlasBatches()) batch->record(commandBuffer, physicalDevice, device, vma);
    entities.staticBatchesSubmitted();

    commandBuffer->memoryBarrier(
        VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
        VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR);
}

} // namespace

// ---- WorldPrepare

WorldPrepare::WorldPrepare() {}

void WorldPrepare::init(std::shared_ptr<Framework> framework) {
    framework_ = framework;
}

void WorldPrepare::build() {
    auto framework = framework_.lock();
    const uint32_t frameCount = framework->swapchain()->imageCount();
    // The ray tracing passes are made again after this: tables keyed by the old passes' addresses must go.
    hitGroupTables_.clear();
    contexts_.clear();
    contexts_.reserve(frameCount);
    for (uint32_t i = 0; i < frameCount; i++) {
        contexts_.push_back(WorldPrepareContext::create(framework->contexts()[i], shared_from_this()));
    }
}

std::shared_ptr<WorldPrepareContext> WorldPrepare::context(uint32_t frameIndex) const {
    return contexts_.at(frameIndex);
}

const WorldPrepare::ChunkCache &WorldPrepare::refreshChunkCache(Chunks &chunks) {
    ChunkCache &cache = chunkCache_;
    const auto &slots = chunks.chunks();
    const uint64_t version = Chunks::contentVersion();
    if (cache.contentVersion == version && cache.slotCount == slots.size()) return cache;

    cache.instances.clear();
    cache.hitGroupNames.clear();
    cache.blasOffsets.clear();
    cache.indexAddresses.clear();
    cache.positionAddresses.clear();
    cache.materialAddresses.clear();

    // The flat occupancy array lets the walk skip the empty slots (most of them, far out) without touching them.
    const auto &occupied = chunks.occupied();
    const bool useOccupied = occupied.size() == slots.size();
    const glm::ivec4 grid = chunks.chunkGridInfo();
    const size_t gridCount = static_cast<size_t>(grid.x) * grid.y * grid.z;

    uint32_t geometryBase = 0;
    uint32_t sbtOffset = 0;
    cache.lodStart = ~size_t(0);
    for (size_t i = 0; i < slots.size(); i++) {
        if (useOccupied && occupied[i] == 0) continue;
        const Chunk1 *chunk = slots[i].get();
        if (chunk == nullptr || chunk->blas == nullptr) continue;

        if (i >= gridCount && cache.lodStart == ~size_t(0)) cache.lodStart = cache.instances.size();
        cache.instances.push_back({chunk->blas, chunk->x, chunk->y, chunk->z, sbtOffset});
        cache.blasOffsets.push_back(geometryBase);
        cache.hitGroupNames.push_back(&kShadowGroup);
        const auto *groupNames = chunk->geometryGroupNames.get();
        for (uint32_t j = 0; j < chunk->geometryCount; j++) {
            cache.hitGroupNames.push_back(groupNames != nullptr && j < groupNames->size() ? &(*groupNames)[j]
                                                                                        : &kDefaultGroup);
            cache.indexAddresses.push_back((*chunk->indexBufferAddresses)[j]);
            cache.positionAddresses.push_back((*chunk->positionBufferAddresses)[j]);
            cache.materialAddresses.push_back((*chunk->materialBufferAddresses)[j]);
        }
        geometryBase += chunk->geometryCount;
        sbtOffset += chunk->geometryCount + 1;
    }
    if (cache.lodStart == ~size_t(0)) cache.lodStart = cache.instances.size();

    cache.contentVersion = version;
    cache.slotCount = slots.size();
    cache.generation++;
    g_profile.rebuilds++;
    return cache;
}

WorldPrepare::LodWindow WorldPrepare::refreshLodCoverage(Chunks &chunks,
                                                         const std::shared_ptr<gpu::CommandBuffer> &commandBuffer) {
    bool changed = chunks.updateLodCoverage(lodCoverage_);
    const size_t lodEnd = chunkCache_.instances.size();
    if (lodCoverageStart_ != chunkCache_.lodStart || lodCoverageEnd_ != lodEnd) {
        lodCoverageStart_ = chunkCache_.lodStart;
        lodCoverageEnd_ = lodEnd;
        changed = true;
    }
    if (lodCoverage_.size() < Chunks::kLodCoverageHeader) return {};

    if (changed) {
        // The shaders find the far terrain's instances by this range.
        lodCoverage_[3] = static_cast<uint32_t>(chunkCache_.lodStart);
        lodCoverage_[4] = static_cast<uint32_t>(lodEnd);
        auto buffer = chunks.lodCoverageBuffer();
        commandBuffer->memoryBarrier(VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR, VK_ACCESS_2_SHADER_READ_BIT,
                                     VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        vkCmdUpdateBuffer(commandBuffer->vkCommandBuffer(), buffer->vkBuffer(), 0,
                          lodCoverage_.size() * sizeof(uint32_t), lodCoverage_.data());
        commandBuffer->memoryBarrier(VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                     VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR, VK_ACCESS_2_SHADER_READ_BIT);
    }

    // The widest far section that can still reach into the window.
    constexpr int32_t kReach = 1024;
    const int32_t originX = static_cast<int32_t>(lodCoverage_[0]);
    const int32_t originZ = static_cast<int32_t>(lodCoverage_[1]);
    LodWindow window;
    window.size = static_cast<int32_t>(lodCoverage_[2]);
    window.minX = originX * 16 - kReach;
    window.minZ = originZ * 16 - kReach;
    window.maxX = (originX + window.size) * 16;
    window.maxZ = (originZ + window.size) * 16;
    return window;
}

WorldPrepare::HitGroupTable &WorldPrepare::hitGroupTable(const std::unordered_map<std::string, uint32_t> &groups,
                                                         uint32_t fallback,
                                                         uint32_t shadow) {
    for (auto &table : hitGroupTables_) {
        if (table.groups == &groups && table.groupCount == groups.size() && table.fallback == fallback &&
            table.shadow == shadow) {
            return table;
        }
    }
    return hitGroupTables_.emplace_back(HitGroupTable{&groups, groups.size(), fallback, shadow});
}

void WorldPrepare::beginEntityFrame() {
    previousEntities_.swap(currentEntities_);
    currentEntities_.clear();

    // Records come in batch order. Of equal hash codes the last one stands, as each replaced the one before.
    std::sort(previousEntities_.begin(), previousEntities_.end(), [](const EntityRecord &a, const EntityRecord &b) {
        return a.hashCode != b.hashCode ? a.hashCode < b.hashCode : a.order < b.order;
    });
    size_t kept = 0;
    for (size_t i = 0; i < previousEntities_.size(); i++) {
        if (i + 1 < previousEntities_.size() && previousEntities_[i + 1].hashCode == previousEntities_[i].hashCode) {
            continue;
        }
        if (kept != i) previousEntities_[kept] = std::move(previousEntities_[i]);
        kept++;
    }
    previousEntities_.erase(previousEntities_.begin() + static_cast<ptrdiff_t>(kept), previousEntities_.end());
}

const WorldPrepare::EntityRecord *WorldPrepare::previousEntity(int hashCode) const {
    auto it = std::lower_bound(previousEntities_.begin(), previousEntities_.end(), hashCode,
                               [](const EntityRecord &record, int code) { return record.hashCode < code; });
    return it != previousEntities_.end() && it->hashCode == hashCode ? &*it : nullptr;
}

void WorldPrepare::recordEntity(int hashCode,
                                const std::shared_ptr<Entity> &entity,
                                const VkTransformMatrixKHR &transform) {
    currentEntities_.push_back({hashCode, static_cast<uint32_t>(currentEntities_.size()), entity, transform});
}

// ---- WorldPrepareContext::FrameTable

void WorldPrepareContext::FrameTable::resize(const std::shared_ptr<gpu::VMA> &vma,
                                             const std::shared_ptr<gpu::Device> &device,
                                             size_t bytes) {
    const size_t needed = std::max<size_t>(bytes, 16);
    if (buffer_ == nullptr || buffer_->size() < needed) {
        buffer_ = gpu::DeviceLocalBuffer::create(vma, device, true, needed + needed / 2, kTableUsage);
        stagedGeneration_ = 0;
    }
    bytes_ = bytes;
}

void WorldPrepareContext::FrameTable::stageChunks(const void *data, size_t bytes, uint64_t generation) {
    if (stagedGeneration_ == generation) return;
    if (bytes > 0) {
        if (data != nullptr) {
            buffer_->uploadToStagingBuffer(const_cast<void *>(data), bytes, 0);
        } else {
            std::memset(buffer_->mappedPtr(), 0, bytes);
            buffer_->flushStagingBuffer();
        }
    }
    stagedGeneration_ = generation;
}

void WorldPrepareContext::FrameTable::stage(const void *data, size_t bytes, size_t offset) {
    if (bytes > 0) buffer_->uploadToStagingBuffer(const_cast<void *>(data), bytes, offset);
}

void WorldPrepareContext::FrameTable::flush() {
    buffer_->flushStagingBuffer();
}

void WorldPrepareContext::FrameTable::recordUpload(const std::shared_ptr<gpu::CommandBuffer> &commandBuffer) const {
    if (bytes_ > 0) buffer_->uploadToBuffer(commandBuffer, bytes_, 0, 0);
}

// ---- WorldPrepareContext

WorldPrepareContext::WorldPrepareContext(std::shared_ptr<FrameworkContext> frameworkContext,
                                         std::shared_ptr<WorldPrepare> worldPrepare)
    : frameworkContext_(frameworkContext), worldPrepare_(worldPrepare) {}

const std::shared_ptr<gpu::DeviceLocalBuffer> &WorldPrepareContext::table(Table table) const {
    return tables_[static_cast<size_t>(table)].buffer();
}

void WorldPrepareContext::render() {
    auto profileClock = Clock::now();
    auto prepare = worldPrepare_.lock();
    auto frameworkContext = frameworkContext_.lock();
    auto world = Renderer::instance().world();
    if (prepare == nullptr || frameworkContext == nullptr || world == nullptr) {
        tlas = nullptr;
        return;
    }

    auto framework = Renderer::instance().framework();
    auto physicalDevice = framework->physicalDevice();
    auto device = framework->device();
    auto vma = framework->vma();
    const std::shared_ptr<gpu::CommandBuffer> &commandBuffer = frameworkContext->worldCommandBuffer;
    auto chunks = world->chunks();
    auto entities = world->entities();
    const glm::dvec3 cameraPos = world->getCameraPos();

    double checkMs = 0.0;
    double buildMs = 0.0;
    if (auto scheduler = chunks->chunkBuildScheduler(); scheduler != nullptr) {
        const auto start = Clock::now();
        scheduler->tryCheckBatchesFinish();
        checkMs = millisecondsSince(start);
        scheduler->tryScheduleBatches(scheduler->chunkBuildingBatchSize());
        buildMs = millisecondsSince(start) - checkMs;
    }

    std::unique_lock<std::recursive_mutex> chunksLock(chunks->mutex());
    if (g_profile.enabled) {
        const double stepMs = lap(profileClock);
        g_profile.schedule += stepMs;
        if (stepMs > 8.0) {
            std::cout << "[native profile] spike prepare schedule=" << stepMs << "ms (check " << checkMs
                      << ", build " << buildMs << ", lock " << (stepMs - checkMs - buildMs) << ")" << std::endl;
        }
    }

    recordStructureBuilds(*chunks, *entities, cameraPos, commandBuffer, physicalDevice, device, vma);

    const WorldPrepare::ChunkCache &cache = prepare->refreshChunkCache(*chunks);
    const WorldPrepare::LodWindow lod = prepare->refreshLodCoverage(*chunks, commandBuffer);
    auto entityBatch = entities->entityBatch();
    const size_t chunkInstances = cache.instances.size();
    const size_t instanceCount = chunkInstances + (entityBatch != nullptr ? entityBatch->entities.size() : 0);

    if (tlasBuilder_ == nullptr) tlasBuilder_ = gpu::TlasBuilder::create(physicalDevice, device, vma);
    tlasBuilder_->clear();
    tlasBuilder_->reserve(instanceCount);
    entityHitGroupNames_.clear();
    entityBlasOffsets_.clear();
    entityIndexAddresses_.clear();
    entityPositionAddresses_.clear();
    entityMaterialAddresses_.clear();
    entityLastIndexAddresses_.clear();
    entityLastPositionAddresses_.clear();

    // Every instance's transform of last frame is written straight into the table's staging memory.
    glm::mat4 *lastTransforms = nullptr;
    if (instanceCount > 0) {
        FrameTable &lastObjectToWorld = frameTable(Table::LastObjectToWorld);
        lastObjectToWorld.resize(vma, device, instanceCount * sizeof(glm::mat4));
        lastTransforms = lastObjectToWorld.mapped<glm::mat4>();
    }

    addChunkInstances(cache, lod, cameraPos, lastTransforms);
    if (g_profile.enabled) {
        const double stepMs = lap(profileClock);
        g_profile.chunks += stepMs;
        if (stepMs > 8.0) std::cout << "[native profile] spike prepare chunks=" << stepMs << "ms" << std::endl;
    }

    if (entityBatch != nullptr) {
        addEntityInstances(*prepare, *entityBatch, cache, cameraPos, lastTransforms + chunkInstances);
    }
    if (g_profile.enabled) {
        g_profile.entities += lap(profileClock);
        g_profile.instances += static_cast<int>(tlasBuilder_->count());
    }
    if (instanceCount == 0) {
        tlas = nullptr;
        return;
    }

    gpu::diagnostics::checkpoint(commandBuffer->vkCommandBuffer(), "prepare:blas-built");
    tlas = tlasBuilder_->record(commandBuffer);
    gpu::diagnostics::checkpoint(commandBuffer->vkCommandBuffer(), "prepare:tlas-built");
    commandBuffer->memoryBarrier(VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                                 VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
                                 VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                                 VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR);
    if (g_profile.enabled) {
        const double stepMs = lap(profileClock);
        g_profile.tlas += stepMs;
        if (stepMs > 8.0) std::cout << "[native profile] spike prepare tlas=" << stepMs << "ms" << std::endl;
    }

    stageTables(cache, vma, device);
    recordUploads(commandBuffer, physicalDevice->mainQueueIndex());
    if (g_profile.enabled) {
        g_profile.upload += lap(profileClock);
        if (++g_profile.frames == 300) {
            std::cout << "[prepare profile] schedule=" << g_profile.schedule / 300
                      << "ms entities=" << g_profile.entities / 300 << "ms chunks=" << g_profile.chunks / 300
                      << "ms tlas=" << g_profile.tlas / 300 << "ms upload=" << g_profile.upload / 300
                      << "ms sbt=" << g_profile.sbt / 300 << "ms instances=" << g_profile.instances / 300
                      << " rebuilds=" << g_profile.rebuilds
                      << " MB pos/mat/idx/blas=" << Chunks::devBytes[0] / 1048576 << "/"
                      << Chunks::devBytes[1] / 1048576 << "/" << Chunks::devBytes[2] / 1048576 << "/"
                      << Chunks::devBytes[3] / 1048576 << " tris=" << Chunks::devBytes[4] << std::endl;
            if (gpu::opacityMicromapsEnabled()) {
                // Cut-out triangles baked so far, by what their micromap made of them (moved: left to the shaders in a
                // geometry of their own; known: the share of micro-triangles camera rays need no shader for), and the
                // memory the micromaps of the sections drawn now take.
                const omm::Stats &omm = omm::stats();
                const uint64_t known = omm.knownMicroTriangles.load(), unknown = omm.unknownMicroTriangles.load();
                std::cout << "[micromap profile] micromaps=" << gpu::Micromap::liveBytes() / 1024
                          << "KiB inputs=" << omm.inputBytesLive.load() / 1024 << "KiB triangles=" << omm.triangles.load()
                          << " opaque=" << omm.fullyOpaque.load() << " transparent=" << omm.fullyTransparent.load()
                          << " states=" << omm.withStates.load() << " unknown=" << omm.fullyUnknown.load()
                          << " left=" << omm.moved.load() << " (not cut-out "
                          << omm.left[omm::Stats::kNotCutout].load() << ", no area "
                          << omm.left[omm::Stats::kNoArea].load() << ", animated "
                          << omm.left[omm::Stats::kAnimated].load() << ", straddling "
                          << omm.left[omm::Stats::kStraddles].load() << ")"
                          << " micro-triangles known=" << (known + unknown > 0 ? 100.0 * known / (known + unknown) : 0.0)
                          << "%" << std::endl;
            }
            g_profile = PrepareProfile{};
        }
    }
}

void WorldPrepareContext::addChunkInstances(const WorldPrepare::ChunkCache &cache,
                                            const WorldPrepare::LodWindow &lod,
                                            const glm::dvec3 &cameraPos,
                                            glm::mat4 *lastTransforms) {
    for (size_t i = 0; i < cache.instances.size(); i++) {
        const auto &instance = cache.instances[i];
        const float tx = static_cast<float>(static_cast<double>(instance.x) - cameraPos.x);
        const float ty = static_cast<float>(static_cast<double>(instance.y) - cameraPos.y);
        const float tz = static_cast<float>(static_cast<double>(instance.z) - cameraPos.z);

        // Distant Horizons terrain is hidden per ray in the columns the near terrain covers (lod_coverage.glsl),
        // which needs its any-hit shader: far terrain that may reach into the covered window is made non-opaque.
        VkGeometryInstanceFlagsKHR flags = 0;
        if (lod.size > 0 && i >= cache.lodStart && instance.x >= lod.minX && instance.x < lod.maxX &&
            instance.z >= lod.minZ && instance.z < lod.maxZ) {
            flags = VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR;
        }
        tlasBuilder_->add(translation(tx, ty, tz), static_cast<uint32_t>(i), 0x01, instance.sbtOffset, flags,
                          instance.blas->address());

        // Chunks do not move: last frame's transform is this frame's.
        glm::mat4 objectToWorld(1.0f);
        objectToWorld[3] = glm::vec4(tx, ty, tz, 1.0f);
        lastTransforms[i] = objectToWorld;
    }
}

void WorldPrepareContext::addEntityInstances(WorldPrepare &prepare,
                                             const EntityBatch &batch,
                                             const WorldPrepare::ChunkCache &cache,
                                             const glm::dvec3 &cameraPos,
                                             glm::mat4 *lastTransforms) {
    prepare.beginEntityFrame();
    const auto *ubo =
        static_cast<const gpu::Data::WorldUBO *>(Renderer::instance().buffers()->worldUniformBuffer()->mappedPtr());

    uint32_t instanceIndex = static_cast<uint32_t>(cache.instances.size());
    uint32_t geometryBase = static_cast<uint32_t>(cache.indexAddresses.size());
    uint32_t sbtOffset = static_cast<uint32_t>(cache.hitGroupNames.size());
    for (const auto &entity : batch.entities) {
        if (entity->prebuiltBLAS >= 0) throw std::runtime_error("prebuilt blas not implemented yet!");

        const VkTransformMatrixKHR transform = entityTransform(*entity, cameraPos, ubo);
        tlasBuilder_->add(transform, instanceIndex, static_cast<uint8_t>(entity->rayTracingFlag), sbtOffset,
                          VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR, entity->blas->address());

        const uint32_t geometryCount = entity->geometryCount;
        const auto *groupNames = entity->geometryGroupNames.get();
        entityHitGroupNames_.push_back(&kShadowGroup);
        for (uint32_t j = 0; j < geometryCount; j++) {
            entityHitGroupNames_.push_back(groupNames != nullptr && j < groupNames->size() ? &(*groupNames)[j]
                                                                                         : &kDefaultGroup);
            entityIndexAddresses_.push_back((*entity->indexBufferAddresses)[j]);
            entityPositionAddresses_.push_back((*entity->positionBufferAddresses)[j]);
            entityMaterialAddresses_.push_back((*entity->materialBufferAddresses)[j]);
        }
        if (entity->hashCode != 0) prepare.recordEntity(entity->hashCode, entity, transform);

        // The motion vectors read last frame's geometry where it kept its size, and last frame's transform.
        const WorldPrepare::EntityRecord *previous = prepare.previousEntity(entity->hashCode);
        const Entity *last = previous != nullptr ? previous->entity.get() : nullptr;
        const bool sameGeometries = last != nullptr && last->geometryCount == geometryCount;
        for (uint32_t j = 0; j < geometryCount; j++) {
            const bool kept = sameGeometries && (*last->vertexCounts)[j] == (*entity->vertexCounts)[j] &&
                              (*last->indexCounts)[j] == (*entity->indexCounts)[j];
            entityLastIndexAddresses_.push_back(kept ? (*last->indexBufferAddresses)[j] : 0);
            entityLastPositionAddresses_.push_back(kept ? (*last->positionBufferAddresses)[j] : 0);
        }
        *lastTransforms++ = previous != nullptr ? toMatrix(previous->transform) : glm::mat4(1.0f);

        entityBlasOffsets_.push_back(geometryBase);
        geometryBase += geometryCount;
        sbtOffset += geometryCount + 1;
        instanceIndex++;
    }
}

void WorldPrepareContext::stageTables(const WorldPrepare::ChunkCache &cache,
                                      const std::shared_ptr<gpu::VMA> &vma,
                                      const std::shared_ptr<gpu::Device> &device) {
    // Each table holds the chunks' part (zeros without data: chunks have no last frame's geometry), then this
    // frame's entities.
    auto stage = [&](Table id, const auto *chunkData, size_t chunkCount, const auto &entityPart) {
        using Element = std::remove_cvref_t<decltype(*chunkData)>;
        const size_t chunkBytes = chunkCount * sizeof(Element);
        FrameTable &table = frameTable(id);
        table.resize(vma, device, chunkBytes + entityPart.size() * sizeof(Element));
        table.stageChunks(chunkData, chunkBytes, cache.generation);
        table.stage(entityPart.data(), entityPart.size() * sizeof(Element), chunkBytes);
    };
    const size_t chunkGeometries = cache.indexAddresses.size();
    const uint64_t *noData = nullptr;
    stage(Table::BlasOffsets, cache.blasOffsets.data(), cache.blasOffsets.size(), entityBlasOffsets_);
    stage(Table::IndexAddresses, cache.indexAddresses.data(), chunkGeometries, entityIndexAddresses_);
    stage(Table::PositionAddresses, cache.positionAddresses.data(), chunkGeometries, entityPositionAddresses_);
    stage(Table::MaterialAddresses, cache.materialAddresses.data(), chunkGeometries, entityMaterialAddresses_);
    stage(Table::LastIndexAddresses, noData, chunkGeometries, entityLastIndexAddresses_);
    stage(Table::LastPositionAddresses, noData, chunkGeometries, entityLastPositionAddresses_);
    frameTable(Table::LastObjectToWorld).flush();
}

void WorldPrepareContext::recordUploads(const std::shared_ptr<gpu::CommandBuffer> &commandBuffer,
                                        uint32_t queueIndex) {
    uploadBarriers_.clear();
    for (const auto &table : tables_) {
        uploadBarriers_.push_back({
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
            .srcQueueFamilyIndex = queueIndex,
            .dstQueueFamilyIndex = queueIndex,
            .buffer = table.buffer(),
        });
    }
    commandBuffer->barriersBufferImage(uploadBarriers_, {});

    for (const auto &table : tables_) table.recordUpload(commandBuffer);

    for (auto &barrier : uploadBarriers_) {
        barrier.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                               VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR |
                               VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
        barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    }
    commandBuffer->barriersBufferImage(uploadBarriers_, {});
}

void WorldPrepareContext::setupHitGroupSbt(const std::unordered_map<std::string, uint32_t> &hitGroupNameToIndex,
                                           uint32_t fallbackHitGroupIndex,
                                           uint32_t shadowHitGroupIndex,
                                           const std::shared_ptr<gpu::CommandBuffer> &commandBuffer,
                                           const std::shared_ptr<gpu::SBT> &updateSbt,
                                           const std::shared_ptr<gpu::SBT> &querySbt) {
    const auto start = Clock::now();
    auto prepare = worldPrepare_.lock();
    if (prepare == nullptr) return;

    // A handful of names repeat over every geometry: the ones met before are matched by address (or by value, for
    // equal names in different lists) before the table is asked.
    resolvedNames_.clear();
    auto resolve = [&](const std::string *name) -> uint32_t {
        if (name == &kShadowGroup) return shadowHitGroupIndex;
        for (const auto &[known, index] : resolvedNames_) {
            if (known == name || *known == *name) return index;
        }
        auto it = hitGroupNameToIndex.find(*name);
        const uint32_t index = it == hitGroupNameToIndex.end() ? fallbackHitGroupIndex : it->second;
        resolvedNames_.emplace_back(name, index);
        return index;
    };

    // The chunks' part is resolved once per chunk change and table; the entities' part every time.
    const WorldPrepare::ChunkCache &cache = prepare->chunkCache_;
    WorldPrepare::HitGroupTable &table =
        prepare->hitGroupTable(hitGroupNameToIndex, fallbackHitGroupIndex, shadowHitGroupIndex);
    const size_t chunkSlots = cache.hitGroupNames.size();
    if (table.generation != cache.generation) {
        table.indices.resize(chunkSlots);
        for (size_t i = 0; i < chunkSlots; i++) table.indices[i] = resolve(cache.hitGroupNames[i]);
        table.generation = cache.generation;
    }
    table.indices.resize(chunkSlots);
    for (const std::string *name : entityHitGroupNames_) table.indices.push_back(resolve(name));

    if (updateSbt != nullptr) updateSbt->setupHitSBT(table.indices, commandBuffer);
    if (querySbt != nullptr) querySbt->setupHitSBT(table.indices, commandBuffer);
    if (g_profile.enabled) g_profile.sbt += millisecondsSince(start);
}
