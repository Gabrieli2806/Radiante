#pragma once

#include "common/shared.hpp"
#include "prism/all_extern.hpp"
#include "prism/gpu/gpu.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class Chunks;
class Framework;
class FrameworkContext;
struct Entity;
struct EntityBatch;

struct WorldPrepareContext;

// The scene as the ray tracing passes see it. Every frame records the bottom-level builds still pending, the
// top-level acceleration structure over every chunk and entity instance, and the tables the hit shaders read per
// instance and per geometry (set 1), and fills the hit records of the passes' shader binding tables.
//
// What the chunks contribute only changes when a chunk is built, dropped or compacted (Chunks::contentVersion), so
// it is gathered here once per change for every frame slot; only the chunks' camera-relative transforms are redone
// each frame. Entities are gathered every frame and follow the chunks, so their offsets start where the chunks end.
class WorldPrepare : public SharedObject<WorldPrepare> {
    friend WorldPrepareContext;

  public:
    WorldPrepare();

    void init(std::shared_ptr<Framework> framework);
    void build();

    std::shared_ptr<WorldPrepareContext> context(uint32_t frameIndex) const;

  private:
    struct ChunkInstance {
        std::shared_ptr<gpu::BLAS> blas;
        int x, y, z;
        uint32_t sbtOffset;
        // The blocks its geometry spans along x and z (Chunk1::minX).
        int minX, maxX, minZ, maxZ;
    };

    struct ChunkCache {
        uint64_t contentVersion = ~0ull;
        size_t slotCount = 0;
        // Counts the rebuilds; what is derived from the cache remembers the generation it was derived from.
        uint64_t generation = 0;
        std::vector<ChunkInstance> instances;
        // Instances from here on are Distant Horizons terrain (the slots past the chunk grid).
        size_t lodStart = 0;
        // Per instance its shadow slot, then one name per geometry. They point into the chunks' own name lists, which
        // the frame retainer keeps alive while a frame may still use them; copying them cost an allocation each.
        std::vector<const std::string *> hitGroupNames;
        std::vector<uint32_t> blasOffsets;
        std::vector<uint64_t> indexAddresses;
        std::vector<uint64_t> positionAddresses;
        std::vector<uint64_t> materialAddresses;
    };

    // A ray tracing pass's hit group table and every hit record's group as resolved against it: the chunks' part,
    // kept while the chunk cache is unchanged, then the entities' part of the frame set up last.
    struct HitGroupTable {
        const std::unordered_map<std::string, uint32_t> *groups = nullptr;
        size_t groupCount = 0;
        uint32_t fallback = 0;
        uint32_t shadow = 0;
        uint64_t generation = 0;
        std::vector<uint32_t> indices;
    };

    // The Distant Horizons columns the near terrain covers (lod_coverage.glsl), in blocks.
    struct LodWindow {
        int32_t size = 0;
        int32_t minX = 0, maxX = 0, minZ = 0, maxZ = 0;
        // The coverage itself: the window's first column, in chunks, and one bit per column (lod_coverage.glsl).
        int32_t originX = 0, originZ = 0;
        const uint32_t *columns = nullptr;

        // Whether the near terrain is built in every chunk column of these blocks (max exclusive).
        bool covers(int32_t blockMinX, int32_t blockMaxX, int32_t blockMinZ, int32_t blockMaxZ) const;
    };

    struct EntityRecord {
        int hashCode;
        uint32_t order;
        std::shared_ptr<Entity> entity;
        VkTransformMatrixKHR transform;
    };

    const ChunkCache &refreshChunkCache(Chunks &chunks);
    LodWindow refreshLodCoverage(Chunks &chunks, const std::shared_ptr<gpu::CommandBuffer> &commandBuffer);
    HitGroupTable &hitGroupTable(const std::unordered_map<std::string, uint32_t> &groups,
                                 uint32_t fallback,
                                 uint32_t shadow);

    // The entities of the last frame that had an entity batch, by hash code, with the transform they were drawn
    // with: an entity found there again gets its buffers and transform of then for its motion vectors.
    void beginEntityFrame();
    const EntityRecord *previousEntity(int hashCode) const;
    void recordEntity(int hashCode, const std::shared_ptr<Entity> &entity, const VkTransformMatrixKHR &transform);

    std::weak_ptr<Framework> framework_;

    ChunkCache chunkCache_;
    // Keyed by the address of a pass's group table, so they are dropped whenever the passes are made again.
    std::vector<HitGroupTable> hitGroupTables_;

    // The coverage last uploaded (Chunks::updateLodCoverage), and the instance range it was uploaded for.
    std::vector<uint32_t> lodCoverage_;
    size_t lodCoverageStart_ = ~size_t(0);
    size_t lodCoverageEnd_ = ~size_t(0);

    std::vector<EntityRecord> previousEntities_;
    std::vector<EntityRecord> currentEntities_;

    std::vector<std::shared_ptr<WorldPrepareContext>> contexts_;
};

struct WorldPrepareContext : public SharedObject<WorldPrepareContext> {
    // The tables of set 1 a frame uploads, in upload order.
    enum class Table : uint32_t {
        BlasOffsets,       // per instance: the index of its first geometry
        IndexAddresses,    // per geometry, this frame
        PositionAddresses,
        MaterialAddresses,
        LastIndexAddresses, // per geometry, last frame's, or 0 where there was none
        LastPositionAddresses,
        LastObjectToWorld, // per instance: the transform it had last frame
        Count,
    };

    WorldPrepareContext(std::shared_ptr<FrameworkContext> frameworkContext, std::shared_ptr<WorldPrepare> worldPrepare);

    // Records this frame's structure builds and table uploads into the frame's world command buffer. tlas is null
    // afterwards when there is nothing to trace, and the tables are then not uploaded.
    void render();

    // Fills the hit records of this frame's instances into the given tables (either may be null), each geometry's
    // group name resolved against a ray tracing pass's table: unknown names get the fallback group, every instance's
    // shadow slot the shadow group.
    void setupHitGroupSbt(const std::unordered_map<std::string, uint32_t> &hitGroupNameToIndex,
                          uint32_t fallbackHitGroupIndex,
                          uint32_t shadowHitGroupIndex,
                          const std::shared_ptr<gpu::CommandBuffer> &commandBuffer,
                          const std::shared_ptr<gpu::SBT> &updateSbt,
                          const std::shared_ptr<gpu::SBT> &querySbt);

    const std::shared_ptr<gpu::DeviceLocalBuffer> &table(Table table) const;

    std::shared_ptr<gpu::TLAS> tlas;

  private:
    // One table in a device buffer of this frame slot, filled from its staging memory every frame. The buffer only
    // grows (by half again), so frames do not allocate. The chunks' part of a table is written to the staging memory
    // only when the chunk cache changed since it last was; a grown buffer starts with nothing staged.
    class FrameTable {
      public:
        void resize(const std::shared_ptr<gpu::VMA> &vma, const std::shared_ptr<gpu::Device> &device, size_t bytes);
        // `bytes` from `data` at the start, or zeros for null data, unless the chunk cache of `generation` is staged.
        void stageChunks(const void *data, size_t bytes, uint64_t generation);
        void stage(const void *data, size_t bytes, size_t offset);
        // The staging memory, to write this frame's contents in place, flushed afterwards.
        template <typename T>
        T *mapped() {
            return static_cast<T *>(buffer_->mappedPtr());
        }
        void flush();
        void recordUpload(const std::shared_ptr<gpu::CommandBuffer> &commandBuffer) const;

        const std::shared_ptr<gpu::DeviceLocalBuffer> &buffer() const {
            return buffer_;
        }

      private:
        std::shared_ptr<gpu::DeviceLocalBuffer> buffer_;
        size_t bytes_ = 0;
        uint64_t stagedGeneration_ = 0;
    };

    FrameTable &frameTable(Table table) {
        return tables_[static_cast<size_t>(table)];
    }

    // Adds the chunks' instances, and writes their transforms (which are also last frame's) to lastTransforms.
    void addChunkInstances(const WorldPrepare::ChunkCache &cache,
                           const WorldPrepare::LodWindow &lod,
                           const glm::dvec3 &cameraPos,
                           glm::mat4 *lastTransforms);
    // Adds the entities' instances after the chunks' and gathers their part of the tables; writes the transform each
    // entity had last frame to lastTransforms.
    void addEntityInstances(WorldPrepare &prepare,
                            const EntityBatch &batch,
                            const WorldPrepare::ChunkCache &cache,
                            const glm::dvec3 &cameraPos,
                            glm::mat4 *lastTransforms);
    void stageTables(const WorldPrepare::ChunkCache &cache,
                     const std::shared_ptr<gpu::VMA> &vma,
                     const std::shared_ptr<gpu::Device> &device);
    void recordUploads(const std::shared_ptr<gpu::CommandBuffer> &commandBuffer, uint32_t queueIndex);

    std::weak_ptr<FrameworkContext> frameworkContext_;
    std::weak_ptr<WorldPrepare> worldPrepare_;

    std::shared_ptr<gpu::TlasBuilder> tlasBuilder_;
    std::array<FrameTable, static_cast<size_t>(Table::Count)> tables_;

    // This frame's entity part of the tables and of the hit group names; kept to reuse their memory.
    std::vector<const std::string *> entityHitGroupNames_;
    std::vector<uint32_t> entityBlasOffsets_;
    std::vector<uint64_t> entityIndexAddresses_;
    std::vector<uint64_t> entityPositionAddresses_;
    std::vector<uint64_t> entityMaterialAddresses_;
    std::vector<uint64_t> entityLastIndexAddresses_;
    std::vector<uint64_t> entityLastPositionAddresses_;

    std::vector<std::pair<const std::string *, uint32_t>> resolvedNames_;
    std::vector<gpu::CommandBuffer::BufferMemoryBarrier> uploadBarriers_;
};
