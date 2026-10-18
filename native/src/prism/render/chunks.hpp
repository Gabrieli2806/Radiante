#pragma once

#include "common/shared.hpp"
#include "common/singleton.hpp"
#include "prism/all_extern.hpp"
#include "prism/gpu/gpu.hpp"
#include "prism/render/emission.hpp"
#include "prism/render/world.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

class Framework;
class Chunks;
struct ChunkBuildData;

// One section's entry in the table of light lists the shaders read (block_light_data.glsl's VptChunkLightData).
struct ChunkPackedData {
    int32_t x;
    int32_t y;
    int32_t z;
    uint32_t geometryCount;
    uint32_t lightCount = 0;
    // Which of the 27 sections around this one, itself included, hold lights: bit (dz + 1) * 9 + (dy + 1) * 3 +
    // (dx + 1), plus kLightNeighboursKnown once worked out (Chunks::updateLightNeighbours). A shader gathering the
    // light lists around a point reads this one entry instead of all 27 (vptGatherLightSlots). It sits in the padding
    // the address below leaves, so the entry keeps its size and layout.
    uint32_t lightNeighbours = 0;
    VkDeviceAddress lightBufferAddress = 0;
};
static_assert(sizeof(ChunkPackedData) == 32);
static_assert(offsetof(ChunkPackedData, lightNeighbours) == 20 && offsetof(ChunkPackedData, lightBufferAddress) == 24);
// ChunkPackedData::lightNeighbours holds a mask worked out for the section the entry names.
constexpr uint32_t kLightNeighboursKnown = 1u << 31;

// A section's geometry as Java hands it over (ChunkProxy.rebuildSingle): per geometry its type, hit group name and
// vertices, four to a quad. Callers fill it with designated initializers, so the members keep this order.
struct ChunkBuildTask {
    int x = 0;
    int y = 0;
    int z = 0;
    int64_t id;
    int geometryCount;
    int *geometryTypes;
    const char **geometryGroupNames;
    int *geometryTextures;
    int *vertexFormats;
    int *vertexCounts;
    gpu::VertexFormat::PBRVertex **vertices;
    bool isImportant;
};

// A section structure built with compaction allowed, and the size its build reported it would have compacted.
struct ChunkCompaction {
    int64_t chunkId;
    std::shared_ptr<gpu::BLAS> source;
    VkDeviceSize compactedSize;
};

// One slot of the section grid and what it draws.
struct Chunk1 : public SharedObject<Chunk1> {
    int x = 0;
    int y = 0;
    int z = 0;
    // Builds are numbered as Java hands them over (latestVersion is the next number). A slot only swaps in a build
    // newer than the one it shows, so a slow build never replaces a later one.
    int64_t latestVersion = 0;
    int64_t blasVersion = -1;
    // When the slot last took a build; a slot that just took one waits a moment for its next (chunks.cpp).
    std::chrono::steady_clock::time_point lastUpdate;

    std::shared_ptr<gpu::BLAS> blas;
    uint32_t geometryCount = 0;
    std::shared_ptr<std::vector<std::string>> geometryGroupNames;
    // Per geometry, where its indices, positions and materials start, tagged in the lowest bit when they are in the
    // compact layout (chunks.cpp).
    std::shared_ptr<std::vector<VkDeviceAddress>> indexBufferAddresses;
    std::shared_ptr<std::vector<VkDeviceAddress>> positionBufferAddresses;
    std::shared_ptr<std::vector<VkDeviceAddress>> materialBufferAddresses;
    // The buffer those addresses point into, and the section's lights.
    std::shared_ptr<gpu::DeviceLocalBuffer> geometryBuffer;
    std::shared_ptr<gpu::DeviceLocalBuffer> lightBuffer;
    // This slot's entry in Chunks::occupied(), kept equal to (blas != nullptr).
    uint8_t *occupiedFlag = nullptr;
};

// Builds the queued sections in batches alongside the frames. A batch's uploads, structure builds and compacted-size
// queries go to the GPU in one submission with a fence of its own; its sections are swapped in once that signals.
class ChunkBuildScheduler : public SharedObject<ChunkBuildScheduler> {
  public:
    ChunkBuildScheduler(Chunks &chunks, uint32_t chunkBuildingBatchSize, uint32_t chunkBuildingTotalBatches);
    ~ChunkBuildScheduler();

    // Swaps in the sections of the batches the GPU has finished.
    void tryCheckBatchesFinish();
    // Waits for the batches in flight and swaps them in; the sections gathered for the next batch go back to the
    // queue.
    void waitAllBatchesFinish();
    // Gathers and submits batches of up to maxBatchSize sections while this frame's small time budget lasts.
    void tryScheduleBatches(uint32_t maxBatchSize);

    uint32_t chunkBuildingBatchSize();

  private:
    struct Batch;
    struct Submission {
        std::shared_ptr<gpu::Fence> fence;
        std::shared_ptr<gpu::CommandBuffer> commandBuffer;
    };
    struct InFlight {
        Submission submission;
        std::unique_ptr<Batch> batch;
    };

    bool submit(Batch &batch, const Submission &submission);
    void finish(InFlight &inFlight);
    void collectCompactions(Batch &batch);

    Chunks &chunks_;
    uint32_t batchSize_;
    // Builds go to the secondary queue when it shares the main queue's family, to the main queue otherwise.
    bool useSecondaryQueue_ = false;
    std::vector<Submission> idle_;
    std::vector<InFlight> inFlight_;
    // Sections taken from the queue for the next batch; a partial batch waits a few frames for more to join it.
    std::vector<std::shared_ptr<ChunkBuildData>> gathering_;
    uint32_t gatheringFrames_ = 0;
    // Measured render thread time to prepare one section of a batch, which sizes batches to the frame budget.
    double msPerChunk_ = 1.0;
    std::vector<VkAccelerationStructureKHR> queriedStructures_;
};

// The section grid Java builds into: per loaded section a slot with the structure, geometry and lights it draws, the
// builds on their way to the slots, and the tables the shaders read.
class Chunks : public SharedObject<Chunks> {
    friend World;
    friend ChunkBuildScheduler;

  public:
    explicit Chunks(std::shared_ptr<Framework> framework);

    // Replaces the grid (joining a world, a new render distance) once the builds in flight are done.
    void reset(uint32_t numChunks, uint32_t sizeX, uint32_t sizeY, uint32_t sizeZ, int32_t bottomSectionCoord);
    // Picks up new batch options.
    void resetScheduler();
    // Uploads this frame slot's copy of the light list table when it has changed.
    void resetFrame();
    void invalidateChunk(int id);
    void relocateChunk(int id, int x, int y, int z);
    // Takes over a section's geometry from Java. Copying, lights, packing and buffers happen on the calling thread,
    // outside the lock the render thread needs every frame.
    void queueChunkBuild(ChunkBuildTask task);
    void setCollectChunkEmission(bool collect);

    // No build of this slot still pending: what it draws is the latest geometry handed to it.
    bool isChunkCurrent(int64_t id);

    void close();

    std::recursive_mutex &mutex();
    std::vector<std::shared_ptr<Chunk1>> &chunks();
    // One byte per slot, set while that slot has geometry. Reading this flat array lets the per-frame walk skip the
    // empty slots (most of them, far out) without touching each chunk object.
    const std::vector<uint8_t> &occupied();
    // Replaces freshly built section structures with compacted copies, recorded into `commandBuffer` ahead of the
    // TLAS build. Call under mutex(), after the scheduler has collected finished batches.
    void compactFinishedBlases(std::shared_ptr<gpu::CommandBuffer> commandBuffer);

    // Bumped whenever any slot's geometry is replaced or dropped, so per-frame work can cache what depends on it.
    static uint64_t contentVersion();
    static void bumpContentVersion();
    // Development counters: bytes ever allocated for section geometry (indices, positions and materials share one
    // buffer, counted in the first) and for structures (the fourth). The others are no longer counted.
    static std::atomic<uint64_t> devBytes[5];

    std::shared_ptr<ChunkBuildScheduler> chunkBuildScheduler();
    // Structures of near-player rebuilds, recorded and cleared by the next frame. Use under mutex().
    std::vector<std::shared_ptr<gpu::BlasBuild>> &importantBlasBuilds();
    // This frame slot's copy of the light list table, or null while chunk emission is not collected.
    std::shared_ptr<gpu::DeviceLocalBuffer> chunkPackedData();
    glm::ivec4 chunkGridInfo();
    void setChunkStorageSectionPos(glm::ivec3 sectionPos);
    glm::ivec4 chunkStorageSectionPos();

    // Distant Horizons coverage: the section columns, in a square window round the camera, whose terrain Java has
    // built. Far terrain is hidden there per ray (util lod_coverage.glsl) instead of being remeshed, so it never needs
    // to catch up with sections loading. The buffer holds a header (origin, size, then two words world_prepare fills)
    // and one bit per column.
    static constexpr uint32_t kLodCoverageHeader = 8;
    static constexpr int32_t kLodCoverageMaxSize = 256;
    void setLodCoverage(int32_t originX, int32_t originZ, int32_t size, const uint32_t *words, size_t count);
    // Rewrites `out` (header then one bit per column) when Java has sent new coverage; false when unchanged.
    bool updateLodCoverage(std::vector<uint32_t> &out);
    std::shared_ptr<gpu::DeviceLocalBuffer> lodCoverageBuffer();
    // 0 until the buffer exists, which is after its first upload: shaders never read it uninitialised.
    VkDeviceAddress lodCoverageAddress();

  private:
    // The slots with a build waiting, as a flat list plus each slot's place in it: adding, removing and walking them
    // never allocates once grown. A slot is in it exactly while chunkBuildDatas_ holds a build for it.
    class PendingSlots {
      public:
        void reset(size_t slotCount);
        void insert(int64_t slot);
        void erase(int64_t slot);
        bool empty() const {
            return slots_.empty();
        }
        const std::vector<int64_t> &slots() const {
            return slots_;
        }

      private:
        static constexpr uint32_t kAbsent = ~0u;
        std::vector<int64_t> slots_;
        std::vector<uint32_t> places_;
    };

    // Everything below is called under mutex_.
    bool validSlot(int64_t id) const;
    void queueBuild(std::shared_ptr<ChunkBuildData> data);
    void dropQueuedBuild(int64_t id);
    void takeQueuedBuilds(uint32_t count, glm::vec3 cameraPos, std::vector<std::shared_ptr<ChunkBuildData>> &out);
    bool swapIn(ChunkBuildData &data);
    void clearSlot(Chunk1 &slot);
    void storePackedData(int64_t id, const ChunkPackedData &entry);
    void updateLightNeighbours(int64_t id);
    uint32_t lightNeighboursOf(const ChunkPackedData &entry) const;
    void allocateChunkPackedDataBuffers();
    void releaseEmissionResources();
    std::shared_ptr<ChunkBuildScheduler> makeScheduler();

    std::recursive_mutex mutex_;
    std::vector<std::shared_ptr<Chunk1>> chunks_;
    std::vector<uint8_t> occupied_;
    std::vector<ChunkCompaction> pendingCompactions_;

    std::vector<std::shared_ptr<ChunkBuildData>> chunkBuildDatas_;
    PendingSlots pending_;
    std::vector<std::pair<float, int64_t>> rankedScratch_;
    std::shared_ptr<ChunkBuildScheduler> chunkBuildScheduler_;
    std::vector<std::shared_ptr<gpu::BlasBuild>> importantBlasBuilds_;

    // The light list table, a copy per frame slot, and the revision of the table each copy holds.
    std::vector<ChunkPackedData> chunkPackedData_;
    uint64_t chunkPackedDataRevision_ = 1;
    std::vector<std::shared_ptr<gpu::DeviceLocalBuffer>> chunkPackedDataBuffers_;
    std::vector<uint64_t> chunkPackedDataUploaded_;

    int32_t sizeX_ = 0;
    int32_t sizeY_ = 0;
    int32_t sizeZ_ = 0;
    int32_t bottomSectionCoord_ = 0;
    // Bumped by reset(): builds numbered by an older grid are not taken.
    uint64_t generation_ = 0;
    glm::ivec3 chunkStorageSectionPos_ = glm::ivec3(0);

    std::vector<uint32_t> lodJavaWords_;
    int32_t lodOriginX_ = 0;
    int32_t lodOriginZ_ = 0;
    int32_t lodSize_ = 0;
    uint64_t lodJavaVersion_ = 0;
    uint64_t lodBuiltVersion_ = ~0ull;
    std::shared_ptr<gpu::DeviceLocalBuffer> lodCoverageBuffer_;
};
