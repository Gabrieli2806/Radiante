#include "prism/render/chunks_internal.hpp"

// The grid of sections and its slots. Packing a section's geometry is in chunk_geometry.cpp, building the
// structures in batches in chunk_build_scheduler.cpp, the wind pass in chunks_wind.cpp.

namespace {

std::atomic<uint64_t> g_contentVersion{0};

} // namespace

// ---- Chunks

std::atomic<uint64_t> Chunks::devBytes[5] = {};

void Chunks::PendingSlots::reset(size_t slotCount) {
    slots_.clear();
    places_.assign(slotCount, kAbsent);
}

void Chunks::PendingSlots::insert(int64_t slot) {
    uint32_t &place = places_[static_cast<size_t>(slot)];
    if (place != kAbsent) return;
    place = static_cast<uint32_t>(slots_.size());
    slots_.push_back(slot);
}

void Chunks::PendingSlots::erase(int64_t slot) {
    uint32_t &place = places_[static_cast<size_t>(slot)];
    if (place == kAbsent) return;
    const int64_t last = slots_.back();
    slots_[place] = last;
    places_[static_cast<size_t>(last)] = place;
    slots_.pop_back();
    place = kAbsent;
}

Chunks::Chunks(std::shared_ptr<Framework>) {}

uint64_t Chunks::contentVersion() {
    return g_contentVersion.load(std::memory_order_acquire);
}

void Chunks::bumpContentVersion() {
    g_contentVersion.fetch_add(1, std::memory_order_acq_rel);
}

void Chunks::reset(uint32_t numChunks, uint32_t sizeX, uint32_t sizeY, uint32_t sizeZ, int32_t bottomSectionCoord) {
    std::unique_lock<std::recursive_mutex> lock(mutex_);
    auto device = Renderer::instance().framework()->device();

    // Rejoining a world replaces every section. Builds still running on the GPU and frames already handed to
    // Minecraft reference the old buffers and structures: finish the builds, and keep the old sections alive until
    // those frames are done.
    if (chunkBuildScheduler_ != nullptr) chunkBuildScheduler_->waitAllBatchesFinish();
    gpu::diagnostics::onResult(vkQueueWaitIdle(device->mainVkQueue()), device->mainVkQueue(),
                               device->secondaryQueue());
    gpu::diagnostics::onResult(vkQueueWaitIdle(device->secondaryQueue()), device->mainVkQueue(),
                               device->secondaryQueue());
    // The old slots point into occupied_, which is about to be replaced.
    for (const auto &slot : chunks_) {
        if (slot != nullptr) slot->occupiedFlag = nullptr;
    }
    retireWithFrames(std::exchange(chunks_, {}), std::exchange(chunkBuildDatas_, {}),
                     std::exchange(chunkPackedDataBuffers_, {}), std::exchange(chunkBuildScheduler_, nullptr));

    sizeX_ = static_cast<int32_t>(sizeX);
    sizeY_ = static_cast<int32_t>(sizeY);
    sizeZ_ = static_cast<int32_t>(sizeZ);
    bottomSectionCoord_ = bottomSectionCoord;
    chunkStorageSectionPos_ = glm::ivec3(0, bottomSectionCoord, 0);
    lodJavaWords_.clear();
    lodSize_ = 0;
    lodJavaVersion_++;
    // Builds still on their way from the old grid carry its versions: they are dropped on arrival.
    generation_++;
    importantBlasBuilds_.clear();
    pendingCompactions_.clear();

    occupied_.assign(numChunks, 0);
    chunks_.reserve(numChunks);
    for (uint32_t i = 0; i < numChunks; i++) {
        auto slot = Chunk1::create();
        slot->occupiedFlag = &occupied_[i];
        chunks_.push_back(std::move(slot));
    }
    chunkBuildDatas_.assign(numChunks, nullptr);
    pending_.reset(numChunks);
    windSlots_.reset(numChunks);
    windMoved_.clear();
    chunkPackedData_.assign(numChunks, ChunkPackedData{});
    chunkPackedDataRevision_++;
    chunkPackedDataUploaded_.clear();
    if (Renderer::options.collectChunkEmission) allocateChunkPackedDataBuffers();
    bumpContentVersion();

    chunkBuildScheduler_ = makeScheduler();
}

void Chunks::resetScheduler() {
    std::unique_lock<std::recursive_mutex> lock(mutex_);
    if (chunkBuildScheduler_ == nullptr) return;
    chunkBuildScheduler_->waitAllBatchesFinish();
    chunkBuildScheduler_ = makeScheduler();
}

void Chunks::setCollectChunkEmission(bool collect) {
    std::unique_lock<std::recursive_mutex> lock(mutex_);
    if (chunkBuildScheduler_ != nullptr) chunkBuildScheduler_->waitAllBatchesFinish();

    // The waiting sections only take the new setting here; their lights are built when a batch takes them, outside
    // the lock the render thread needs every frame.
    for (const auto &data : chunkBuildDatas_) {
        if (data == nullptr) continue;
        data->collectEmission = collect;
        data->lightBuffer = nullptr;
        data->lightCount = 0;
        data->lightsStale = collect;
    }

    if (collect) {
        if (chunkPackedDataBuffers_.empty()) allocateChunkPackedDataBuffers();
        return;
    }
    releaseEmissionResources();
}

void Chunks::resetFrame() {
    auto context = Renderer::instance().framework()->safeAcquireCurrentContext();
    std::unique_lock<std::recursive_mutex> lock(mutex_);
    if (!Renderer::options.collectChunkEmission || chunkPackedData_.empty()) return;
    const uint32_t frame = context->frameIndex;
    if (frame >= chunkPackedDataBuffers_.size() || chunkPackedDataBuffers_[frame] == nullptr) return;
    // Every frame slot reads its own copy of the table, written again only once something in it has changed.
    if (chunkPackedDataUploaded_[frame] == chunkPackedDataRevision_) return;

    const auto &buffer = chunkPackedDataBuffers_[frame];
    buffer->uploadToStagingBuffer(chunkPackedData_.data(), chunkPackedData_.size() * sizeof(ChunkPackedData), 0);
    Renderer::instance().buffers()->queueImportantWorldUpload(buffer);
    chunkPackedDataUploaded_[frame] = chunkPackedDataRevision_;
}

void Chunks::invalidateChunk(int id) {
    std::unique_lock<std::recursive_mutex> lock(mutex_);
    if (!validSlot(id)) return;
    dropQueuedBuild(id);
    windSlots_.erase(id);
    Chunk1 &slot = *chunks_[id];
    clearSlot(slot);
    storePackedData(id, {.x = slot.x, .y = slot.y, .z = slot.z, .geometryCount = 0});
}

void Chunks::relocateChunk(int id, int x, int y, int z) {
    std::unique_lock<std::recursive_mutex> lock(mutex_);
    if (!validSlot(id)) return;
    dropQueuedBuild(id);
    windSlots_.erase(id);
    Chunk1 &slot = *chunks_[id];
    slot.x = x;
    slot.y = y;
    slot.z = z;
    clearSlot(slot);
    storePackedData(id, {.x = x, .y = y, .z = z, .geometryCount = 0});
}

void Chunks::queueChunkBuild(ChunkBuildTask task) {
    auto data = std::make_shared<ChunkBuildData>();
    data->id = task.id;
    data->x = task.x;
    data->y = task.y;
    data->z = task.z;
    data->important = task.isImportant;
    for (int g = 0; g < task.geometryCount; g++) {
        // Whole quads only: a geometry without one has nothing to trace.
        const int vertexCount = task.vertexCounts[g];
        if (vertexCount < 4) continue;
        data->vertices.emplace_back(task.vertices[g], task.vertices[g] + vertexCount);
        data->geometryTypes.push_back(static_cast<World::GeometryTypes>(task.geometryTypes[g]));
        const char *name = task.geometryGroupNames != nullptr ? task.geometryGroupNames[g] : nullptr;
        data->geometryGroupNames.emplace_back(name != nullptr ? name : "default");
    }
    // Vertex positions are relative to the origin. How far a Distant Horizons section reaches is only known from its
    // geometry.
    float minX = 0.0f, maxX = 0.0f, minZ = 0.0f, maxZ = 0.0f;
    bool any = false;
    for (const auto &geometry : data->vertices) {
        for (const PBRVertex &vertex : geometry) {
            minX = any ? std::min(minX, vertex.pos.x) : vertex.pos.x;
            maxX = any ? std::max(maxX, vertex.pos.x) : vertex.pos.x;
            minZ = any ? std::min(minZ, vertex.pos.z) : vertex.pos.z;
            maxZ = any ? std::max(maxZ, vertex.pos.z) : vertex.pos.z;
            any = true;
        }
    }
    data->minX = data->x + static_cast<int>(std::floor(minX));
    data->maxX = data->x + static_cast<int>(std::ceil(maxX));
    data->minZ = data->z + static_cast<int>(std::floor(minZ));
    data->maxZ = data->z + static_cast<int>(std::ceil(maxZ));

    {
        std::unique_lock<std::recursive_mutex> lock(mutex_);
        if (!validSlot(data->id)) return;
        data->version = chunks_[data->id]->latestVersion++;
        data->generation = generation_;
    }

    // The light list of a section full of torches and lamps, and the packing of a detailed one, take milliseconds.
    // Both happen here, before taking the lock the render thread needs every frame: under it, or on the render thread,
    // loading sections made the frames stutter.
    auto framework = Renderer::instance().framework();
    const auto vma = framework->vma();
    const auto device = framework->device();
    data->collectEmission = Renderer::options.collectChunkEmission;
    if (data->collectEmission) {
        auto textures = Renderer::instance().textures();
        data->buildLights(textures != nullptr ? textures->emission().get() : nullptr, vma, device);
    }
    data->packGeometry(vma, device);

    if (!data->important) {
        std::unique_lock<std::recursive_mutex> lock(mutex_);
        queueBuild(std::move(data));
        return;
    }

    if (data->geometryCount() > 0) {
        data->blasBuild =
            gpu::BlasBuild::create(data->structureFlags(VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR));
        data->defineTriangles(*data->blasBuild, device, vma);
        data->blas = data->blasBuild->place(device, vma);
    }
    std::unique_lock<std::recursive_mutex> lock(mutex_);
    if (!validSlot(data->id) || data->generation != generation_) return;
    dropQueuedBuild(data->id);
    auto buffers = Renderer::instance().buffers();
    if (data->geometryBuffer != nullptr) buffers->queueImportantWorldUpload(data->geometryBuffer);
    if (data->lightBuffer != nullptr) buffers->queueImportantWorldUpload(data->lightBuffer);
    if (data->blasBuild != nullptr) importantBlasBuilds_.push_back(data->blasBuild);
    swapIn(*data);
}

bool Chunks::isChunkCurrent(int64_t id) {
    std::unique_lock<std::recursive_mutex> lock(mutex_);
    if (!validSlot(id)) return false;
    const Chunk1 &slot = *chunks_[id];
    return slot.blasVersion == slot.latestVersion - 1;
}

void Chunks::close() {
    std::unique_lock<std::recursive_mutex> lock(mutex_);
    if (chunkBuildScheduler_ != nullptr) {
        chunkBuildScheduler_->waitAllBatchesFinish();
        chunkBuildScheduler_ = nullptr;
    }

    pending_.reset(0);
    windSlots_.reset(0);
    windMoved_.clear();
    chunkBuildDatas_.clear();
    importantBlasBuilds_.clear();
    pendingCompactions_.clear();
    chunkPackedData_.clear();
    chunkPackedDataBuffers_.clear();
    chunkPackedDataUploaded_.clear();
    chunks_.clear();
    occupied_.clear();
    sizeX_ = 0;
    sizeY_ = 0;
    sizeZ_ = 0;
    bottomSectionCoord_ = 0;
    chunkStorageSectionPos_ = glm::ivec3(0);
    lodJavaWords_.clear();
    lodSize_ = 0;
    lodJavaVersion_++;
}

std::recursive_mutex &Chunks::mutex() {
    return mutex_;
}

std::vector<std::shared_ptr<Chunk1>> &Chunks::chunks() {
    return chunks_;
}

const std::vector<uint8_t> &Chunks::occupied() {
    return occupied_;
}

void Chunks::compactFinishedBlases(std::shared_ptr<gpu::CommandBuffer> commandBuffer) {
    std::unique_lock<std::recursive_mutex> lock(mutex_);
    if (pendingCompactions_.empty()) return;

    auto framework = Renderer::instance().framework();
    auto device = framework->device();
    auto vma = framework->vma();
    VkDeviceSize compactedBytes = 0;
    bool compactedAny = false;
    for (const ChunkCompaction &pending : pendingCompactions_) {
        // Only slots still showing the structure that was built; anything rebuilt or dropped since is skipped.
        if (!validSlot(pending.chunkId) || chunks_[pending.chunkId]->blas != pending.source) continue;

        // Each compacted structure gets its own buffer, so replacing one section frees exactly its memory.
        auto storage = gpu::allocateStructureStorage(device, vma, pending.compactedSize);
        auto compacted = gpu::BLAS::create(device, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, std::move(storage),
                                           0, pending.compactedSize);
        VkCopyAccelerationStructureInfoKHR copy{VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR};
        copy.src = pending.source->handle();
        copy.dst = compacted->handle();
        copy.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
        vkCmdCopyAccelerationStructureKHR(commandBuffer->vkCommandBuffer(), &copy);

        chunks_[pending.chunkId]->blas = std::move(compacted);
        compactedBytes += pending.compactedSize;
        compactedAny = true;
    }
    // The sources go now; destroying a structure waits for the frames that may still trace it.
    pendingCompactions_.clear();
    if (!compactedAny) return;
    bumpContentVersion();
    devBytes[3] += compactedBytes;
}

std::shared_ptr<ChunkBuildScheduler> Chunks::chunkBuildScheduler() {
    std::unique_lock<std::recursive_mutex> lock(mutex_);
    return chunkBuildScheduler_;
}

std::vector<std::shared_ptr<gpu::BlasBuild>> &Chunks::importantBlasBuilds() {
    return importantBlasBuilds_;
}

std::shared_ptr<gpu::DeviceLocalBuffer> Chunks::chunkPackedData() {
    auto context = Renderer::instance().framework()->safeAcquireCurrentContext();
    std::unique_lock<std::recursive_mutex> lock(mutex_);
    if (context->frameIndex >= chunkPackedDataBuffers_.size()) return nullptr;
    return chunkPackedDataBuffers_[context->frameIndex];
}

glm::ivec4 Chunks::chunkGridInfo() {
    std::unique_lock<std::recursive_mutex> lock(mutex_);
    return glm::ivec4(sizeX_, sizeY_, sizeZ_, bottomSectionCoord_);
}

void Chunks::setChunkStorageSectionPos(glm::ivec3 sectionPos) {
    std::unique_lock<std::recursive_mutex> lock(mutex_);
    chunkStorageSectionPos_ = sectionPos;
}

glm::ivec4 Chunks::chunkStorageSectionPos() {
    std::unique_lock<std::recursive_mutex> lock(mutex_);
    return glm::ivec4(chunkStorageSectionPos_, 0);
}

void Chunks::setLodCoverage(int32_t originX, int32_t originZ, int32_t size, const uint32_t *words, size_t count) {
    std::unique_lock<std::recursive_mutex> lock(mutex_);
    size = std::clamp(size, 0, kLodCoverageMaxSize);
    const size_t needed = (static_cast<size_t>(size) * static_cast<size_t>(size) + 31) / 32;
    lodOriginX_ = originX;
    lodOriginZ_ = originZ;
    lodSize_ = words != nullptr && count >= needed ? size : 0;
    if (words != nullptr) {
        lodJavaWords_.assign(words, words + std::min(count, needed));
    } else {
        lodJavaWords_.clear();
    }
    lodJavaVersion_++;
}

bool Chunks::updateLodCoverage(std::vector<uint32_t> &out) {
    std::unique_lock<std::recursive_mutex> lock(mutex_);
    if (lodBuiltVersion_ == lodJavaVersion_ && !out.empty()) return false;
    lodBuiltVersion_ = lodJavaVersion_;

    out.assign(kLodCoverageHeader + lodJavaWords_.size(), 0u);
    out[0] = static_cast<uint32_t>(lodOriginX_);
    out[1] = static_cast<uint32_t>(lodOriginZ_);
    out[2] = static_cast<uint32_t>(lodSize_);
    if (lodSize_ > 0) std::copy(lodJavaWords_.begin(), lodJavaWords_.end(), out.begin() + kLodCoverageHeader);
    return true;
}

std::shared_ptr<gpu::DeviceLocalBuffer> Chunks::lodCoverageBuffer() {
    std::unique_lock<std::recursive_mutex> lock(mutex_);
    if (lodCoverageBuffer_ == nullptr) {
        auto framework = Renderer::instance().framework();
        const size_t words = kLodCoverageHeader + static_cast<size_t>(kLodCoverageMaxSize) * kLodCoverageMaxSize / 32;
        lodCoverageBuffer_ = gpu::DeviceLocalBuffer::create(
            framework->vma(), framework->device(), false, words * sizeof(uint32_t),
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 16);
    }
    return lodCoverageBuffer_;
}

VkDeviceAddress Chunks::lodCoverageAddress() {
    std::unique_lock<std::recursive_mutex> lock(mutex_);
    return lodCoverageBuffer_ != nullptr ? lodCoverageBuffer_->bufferAddress() : 0;
}

bool Chunks::validSlot(int64_t id) const {
    return id >= 0 && static_cast<uint64_t>(id) < chunks_.size();
}

void Chunks::queueBuild(std::shared_ptr<ChunkBuildData> data) {
    // Builds of one slot can arrive out of order (several builder threads, a batch handed back): the newest waiting
    // build wins, and one no newer than what the slot shows is dropped.
    const int64_t id = data->id;
    if (!validSlot(id) || data->generation != generation_ || data->version <= chunks_[id]->blasVersion) return;
    auto &queued = chunkBuildDatas_[id];
    if (queued != nullptr && queued->version > data->version) return;
    queued = std::move(data);
    pending_.insert(id);
}

void Chunks::dropQueuedBuild(int64_t id) {
    pending_.erase(id);
    chunkBuildDatas_[id] = nullptr;
}

void Chunks::takeQueuedBuilds(uint32_t count,
                              glm::vec3 cameraPos,
                              std::vector<std::shared_ptr<ChunkBuildData>> &out) {
    if (count == 0 || pending_.empty()) return;

    // Each waiting section is scored once; only the best `count` are put in order.
    const auto now = Clock::now();
    rankedScratch_.clear();
    for (int64_t id : pending_.slots()) {
        const ChunkBuildData &data = *chunkBuildDatas_[id];
        rankedScratch_.emplace_back(buildPriority(*chunks_[id], now, cameraPos, glm::vec3(data.x, data.y, data.z)), id);
    }
    const auto taken = rankedScratch_.begin() + std::min<std::ptrdiff_t>(count, std::ssize(rankedScratch_));
    std::partial_sort(rankedScratch_.begin(), taken, rankedScratch_.end(), [](const auto &a, const auto &b) {
        return a.first > b.first || (a.first == b.first && a.second < b.second);
    });
    for (auto ranked = rankedScratch_.begin(); ranked != taken; ++ranked) {
        pending_.erase(ranked->second);
        out.push_back(std::exchange(chunkBuildDatas_[ranked->second], nullptr));
    }
}

bool Chunks::swapIn(ChunkBuildData &data) {
    if (!validSlot(data.id) || data.generation != generation_) return false;
    Chunk1 &slot = *chunks_[data.id];
    slot.lastUpdate = Clock::now();
    if (data.version <= slot.blasVersion) return false;

    retireDrawn(slot);
    slot.blasVersion = data.version;
    slot.x = data.x;
    slot.y = data.y;
    slot.z = data.z;
    slot.minX = data.minX;
    slot.maxX = data.maxX;
    slot.minZ = data.minZ;
    slot.maxZ = data.maxZ;
    slot.blas = data.blas;
    slot.geometryCount = data.geometryCount();
    slot.geometryGroupNames = std::make_shared<std::vector<std::string>>(std::move(data.geometryGroupNames));
    slot.indexBufferAddresses = std::make_shared<std::vector<VkDeviceAddress>>(std::move(data.indexAddresses));
    slot.positionBufferAddresses = std::make_shared<std::vector<VkDeviceAddress>>(std::move(data.positionAddresses));
    slot.materialBufferAddresses = std::make_shared<std::vector<VkDeviceAddress>>(std::move(data.materialAddresses));
    slot.geometryBuffer = data.geometryBuffer;
    slot.lightBuffer = data.lightBuffer;
    slot.micromap = data.blas != nullptr && data.micromapBuild != nullptr ? data.micromapBuild->micromap() : nullptr;
    slot.micromapInputs = slot.micromap != nullptr ? data.micromapInputs : nullptr;
    if (data.sways() && data.blas != nullptr && data.blasBuild != nullptr) {
        slot.windBuild = data.blasBuild;
        slot.windRegions = std::make_shared<std::vector<WindRegion>>(std::move(data.windRegions));
        windSlots_.insert(data.id);
    } else {
        windSlots_.erase(data.id);
    }
    if (slot.occupiedFlag != nullptr) *slot.occupiedFlag = slot.blas != nullptr ? 1 : 0;
    bumpContentVersion();

    storePackedData(data.id, {.x = slot.x,
                              .y = slot.y,
                              .z = slot.z,
                              .geometryCount = slot.geometryCount,
                              .lightCount = data.lightCount,
                              .lightBufferAddress =
                                  data.lightBuffer != nullptr ? data.lightBuffer->bufferAddress() : 0});
    return true;
}

void Chunks::clearSlot(Chunk1 &slot) {
    slot.lastUpdate = Clock::now();
    // Takes a version of its own, so that no build handed over before this is swapped in after it.
    slot.blasVersion = slot.latestVersion++;
    retireDrawn(slot);
    slot.geometryCount = 0;
    if (slot.occupiedFlag != nullptr) *slot.occupiedFlag = 0;
    bumpContentVersion();
}

void Chunks::storePackedData(int64_t id, const ChunkPackedData &entry) {
    if (!Renderer::options.collectChunkEmission) return;
    chunkPackedData_[id] = entry;
    updateLightNeighbours(id);
    chunkPackedDataRevision_++;
}

// The masks of the sections around a slot that changed. A mask reads only the slots the grid puts next to its own
// (the grid wraps in x and z), so these 27 are the only ones that can have changed, whatever section each holds.
void Chunks::updateLightNeighbours(int64_t id) {
    if (sizeX_ <= 0 || sizeY_ <= 0 || sizeZ_ <= 0) return;
    const int64_t layer = static_cast<int64_t>(sizeX_) * sizeY_;
    const int32_t ix = static_cast<int32_t>(id % sizeX_);
    const int32_t iy = static_cast<int32_t>((id / sizeX_) % sizeY_);
    const int32_t iz = static_cast<int32_t>(id / layer);
    for (int32_t dz = -1; dz <= 1; dz++) {
        for (int32_t dy = -1; dy <= 1; dy++) {
            const int32_t ny = iy + dy;
            if (ny < 0 || ny >= sizeY_) continue;
            for (int32_t dx = -1; dx <= 1; dx++) {
                const int64_t neighbour =
                    (static_cast<int64_t>(floorModInt(iz + dz, sizeZ_)) * sizeY_ + ny) * sizeX_ +
                    floorModInt(ix + dx, sizeX_);
                if (neighbour < 0 || neighbour >= static_cast<int64_t>(chunkPackedData_.size())) continue;
                chunkPackedData_[neighbour].lightNeighbours = lightNeighboursOf(chunkPackedData_[neighbour]);
            }
        }
    }
}

// The mask of one entry, for the section it names, by the rule vptSectionLightSlot applies to each neighbour: the
// slot the grid gives that section holds it, with lights.
uint32_t Chunks::lightNeighboursOf(const ChunkPackedData &entry) const {
    const int32_t sx = entry.x >> 4;
    const int32_t sy = entry.y >> 4;
    const int32_t sz = entry.z >> 4;
    uint32_t mask = kLightNeighboursKnown;
    for (int32_t dz = -1; dz <= 1; dz++) {
        for (int32_t dy = -1; dy <= 1; dy++) {
            const int32_t y = sy + dy;
            if (y < bottomSectionCoord_ || y >= bottomSectionCoord_ + sizeY_) continue;
            for (int32_t dx = -1; dx <= 1; dx++) {
                const int64_t slot = (static_cast<int64_t>(floorModInt(sz + dz, sizeZ_)) * sizeY_ +
                                      (y - bottomSectionCoord_)) *
                                         sizeX_ +
                                     floorModInt(sx + dx, sizeX_);
                if (slot < 0 || slot >= static_cast<int64_t>(chunkPackedData_.size())) continue;
                const ChunkPackedData &neighbour = chunkPackedData_[slot];
                if (neighbour.x != (sx + dx) * 16 || neighbour.y != y * 16 || neighbour.z != (sz + dz) * 16) continue;
                if (neighbour.lightCount == 0 || neighbour.lightBufferAddress == 0) continue;
                mask |= 1u << static_cast<uint32_t>((dz + 1) * 9 + (dy + 1) * 3 + (dx + 1));
            }
        }
    }
    return mask;
}

void Chunks::allocateChunkPackedDataBuffers() {
    auto framework = Renderer::instance().framework();
    const uint32_t frames = framework->swapchain()->imageCount();
    chunkPackedDataBuffers_.assign(frames, nullptr);
    chunkPackedDataUploaded_.assign(frames, 0);
    if (chunkPackedData_.empty()) return;

    const size_t bytes = chunkPackedData_.size() * sizeof(ChunkPackedData);
    for (auto &buffer : chunkPackedDataBuffers_) {
        buffer = gpu::DeviceLocalBuffer::create(framework->vma(), framework->device(), false, bytes,
                                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    }
}

void Chunks::releaseEmissionResources() {
    for (const auto &slot : chunks_) {
        if (slot != nullptr && slot->lightBuffer != nullptr) retireWithFrames(std::move(slot->lightBuffer));
    }
    for (const auto &data : chunkBuildDatas_) {
        if (data == nullptr) continue;
        data->lightBuffer = nullptr;
        data->lightCount = 0;
    }

    std::fill(chunkPackedData_.begin(), chunkPackedData_.end(), ChunkPackedData{});
    chunkPackedDataRevision_++;
    retireWithFrames(std::exchange(chunkPackedDataBuffers_, {}));
    chunkPackedDataUploaded_.clear();
}

std::shared_ptr<ChunkBuildScheduler> Chunks::makeScheduler() {
    return ChunkBuildScheduler::create(*this, Renderer::options.chunkBuildingBatchSize,
                                       Renderer::options.chunkBuildingTotalBatches);
}
