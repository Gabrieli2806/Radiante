#include "prism/render/chunks_internal.hpp"

// ---- ChunkBuildScheduler

// Sections built together: one submission carries their uploads, structure builds and compacted-size queries.
struct ChunkBuildScheduler::Batch {
    explicit Batch(std::vector<std::shared_ptr<ChunkBuildData>> &&taken) : sections(std::move(taken)) {}
    Batch(const Batch &) = delete;
    Batch &operator=(const Batch &) = delete;
    ~Batch() {
        // Read and destroyed when the batch finishes; a batch dropped unfinished leaves it to the frames.
        if (compactedSizes != VK_NULL_HANDLE) {
            gpu::retire::later([device = queryDevice, pool = compactedSizes] {
                vkDestroyQueryPool(device->vkDevice(), pool, nullptr);
            });
        }
    }

    // Places the structures of the sections that have geometry, together in one buffer, and makes the pool their
    // compacted sizes are written into.
    void placeStructures(const std::shared_ptr<gpu::Device> &device, const std::shared_ptr<gpu::VMA> &vma);

    bool needsGpu() const {
        return structures != nullptr || !swaying.empty() ||
               std::any_of(sections.begin(), sections.end(),
                           [](const auto &section) { return section->lightBuffer != nullptr; });
    }

    std::vector<std::shared_ptr<ChunkBuildData>> sections;
    std::shared_ptr<gpu::BlasBatch> structures;
    // The sections whose structures are in `structures`, in the order they were placed, built and queried.
    std::vector<ChunkBuildData *> built;
    // The structures of sections whose foliage sways, each in storage of its own: they are refit every frame and never
    // compacted, and one in the batch's shared buffer would keep all of that buffer alive after the others had moved
    // into their compacted copies. The scratch memory of their builds is held until the batch has run.
    std::vector<std::shared_ptr<gpu::BlasBuild>> swaying;
    std::shared_ptr<gpu::DeviceLocalBuffer> swayingScratch;
    std::shared_ptr<gpu::Device> queryDevice;
    VkQueryPool compactedSizes = VK_NULL_HANDLE;
};

void ChunkBuildScheduler::Batch::placeStructures(const std::shared_ptr<gpu::Device> &device,
                                                 const std::shared_ptr<gpu::VMA> &vma) {
    structures = gpu::BlasBatch::create();
    for (const auto &section : sections) {
        if (section->geometryCount() == 0) continue;
        if (section->sways()) {
            section->blasBuild = gpu::BlasBuild::create(section->structureFlags(0));
            section->defineTriangles(*section->blasBuild, device, vma);
            section->blas = section->blasBuild->place(device, vma);
            Chunks::devBytes[3] += section->blas->size();
            swaying.push_back(section->blasBuild);
            continue;
        }
        section->blasBuild = structures->add(VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR |
                                             VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR);
        section->defineTriangles(*section->blasBuild, device, vma);
        built.push_back(section.get());
    }
    if (built.empty()) {
        structures = nullptr;
        return;
    }

    const auto placeStart = Clock::now();
    const std::vector<std::shared_ptr<gpu::BLAS>> placed = structures->place(device, vma);
    if (profiling()) {
        const double placeMs = millisecondsSince(placeStart);
        if (placeMs > 8.0) {
            std::cout << "[native profile] batch build: " << built.size() << " structures, alloc+size " << placeMs
                      << " ms" << std::endl;
        }
    }
    Chunks::devBytes[3] += structures->storageBytes();
    for (size_t i = 0; i < built.size(); i++) built[i]->blas = placed[i];

    VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    info.queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
    info.queryCount = static_cast<uint32_t>(built.size());
    // Without the pool the structures are simply not compacted.
    if (vkCreateQueryPool(device->vkDevice(), &info, nullptr, &compactedSizes) == VK_SUCCESS) {
        queryDevice = device;
    } else {
        compactedSizes = VK_NULL_HANDLE;
    }
}

ChunkBuildScheduler::ChunkBuildScheduler(Chunks &chunks,
                                         uint32_t chunkBuildingBatchSize,
                                         uint32_t chunkBuildingTotalBatches)
    : chunks_(chunks), batchSize_(chunkBuildingBatchSize) {
    auto framework = Renderer::instance().framework();
    auto device = framework->device();
    auto physicalDevice = framework->physicalDevice();
    // On a queue of the main family nothing the builds write needs an ownership transfer before the frames read it.
    useSecondaryQueue_ = physicalDevice->mainQueueIndex() == physicalDevice->secondaryQueueIndex();
    auto pool = useSecondaryQueue_ ? framework->asyncCommandPool() : framework->mainCommandPool();
    idle_.reserve(chunkBuildingTotalBatches);
    for (uint32_t i = 0; i < chunkBuildingTotalBatches; i++) {
        idle_.push_back({gpu::Fence::create(device), gpu::CommandBuffer::create(device, pool)});
    }
}

ChunkBuildScheduler::~ChunkBuildScheduler() = default;

uint32_t ChunkBuildScheduler::chunkBuildingBatchSize() {
    return batchSize_;
}

void ChunkBuildScheduler::tryCheckBatchesFinish() {
    auto device = Renderer::instance().framework()->device();
    std::unique_lock<std::recursive_mutex> lock(chunks_.mutex_);
    for (size_t i = 0; i < inFlight_.size();) {
        if (vkGetFenceStatus(device->vkDevice(), inFlight_[i].submission.fence->vkFence()) != VK_SUCCESS) {
            i++;
            continue;
        }
        finish(inFlight_[i]);
        inFlight_.erase(inFlight_.begin() + static_cast<std::ptrdiff_t>(i));
    }
}

void ChunkBuildScheduler::waitAllBatchesFinish() {
    auto device = Renderer::instance().framework()->device();
    std::unique_lock<std::recursive_mutex> lock(chunks_.mutex_);
    while (!inFlight_.empty()) {
        const VkResult result =
            vkWaitForFences(device->vkDevice(), 1, &inFlight_.front().submission.fence->vkFence(), VK_TRUE, UINT64_MAX);
        if (result != VK_SUCCESS) {
            // A lost device never signals the rest either: stop rather than wait forever.
            gpu::diagnostics::onResult(result, device->mainVkQueue(), device->secondaryQueue());
            break;
        }
        finish(inFlight_.front());
        inFlight_.erase(inFlight_.begin());
    }

    for (auto &section : gathering_) chunks_.queueBuild(std::move(section));
    gathering_.clear();
    gatheringFrames_ = 0;
}

void ChunkBuildScheduler::tryScheduleBatches(uint32_t maxBatchSize) {
    auto framework = Renderer::instance().framework();
    if (!framework->isRunning()) return;

    // Preparing a batch (placing its structures) happens right here on the render thread, and its builds then run on
    // the GPU alongside the frame. Scheduling every batch there was room for at once stalled a single frame for tens
    // of milliseconds - and the GPU for hundreds - whenever a burst of sections arrived, walking into a dense city or
    // teleporting. Batches are cut to what a small time budget allows by the measured cost per section (at least one
    // section, so loading always moves on), and no new batch starts once the budget is spent.
    constexpr double kBudgetMs = 3.0;
    constexpr uint32_t kMaxGatheringFrames = 3;
    const uint32_t batchLimit = std::clamp<uint32_t>(static_cast<uint32_t>(kBudgetMs / std::max(msPerChunk_, 0.05)), 1u,
                                                     std::max(maxBatchSize, 1u));
    const glm::vec3 cameraPos(Renderer::instance().world()->getCameraPos());
    const auto budgetStart = Clock::now();
    bool preparedOne = false;
    while (!preparedOne || millisecondsSince(budgetStart) <= kBudgetMs) {
        const auto gatherStart = Clock::now();
        Submission submission;
        std::unique_ptr<Batch> batch;
        {
            std::unique_lock<std::recursive_mutex> lock(chunks_.mutex_);
            if (idle_.empty()) return;
            if (gathering_.size() < batchLimit) {
                chunks_.takeQueuedBuilds(batchLimit - static_cast<uint32_t>(gathering_.size()), cameraPos, gathering_);
            }
            if (gathering_.empty()) return;
            // A partial batch waits a few frames for more sections to join it.
            if (gathering_.size() < batchLimit && ++gatheringFrames_ < kMaxGatheringFrames) return;
            gatheringFrames_ = 0;
            submission = std::move(idle_.back());
            idle_.pop_back();
            batch = std::make_unique<Batch>(std::exchange(gathering_, {}));
        }

        // Lights of sections that waited while emission was switched on, built here rather than under the lock.
        for (const auto &section : batch->sections) {
            if (!section->lightsStale) continue;
            section->lightsStale = false;
            auto textures = Renderer::instance().textures();
            section->buildLights(textures != nullptr ? textures->emission().get() : nullptr, framework->vma(),
                                 framework->device());
        }

        const auto buildStart = Clock::now();
        batch->placeStructures(framework->device(), framework->vma());
        preparedOne = true;
        const double perChunk = millisecondsSince(buildStart) / static_cast<double>(batch->sections.size());
        msPerChunk_ = msPerChunk_ * 0.8 + perChunk * 0.2;

        if (!batch->needsGpu()) {
            std::unique_lock<std::recursive_mutex> lock(chunks_.mutex_);
            idle_.push_back(std::move(submission));
            for (const auto &section : batch->sections) chunks_.swapIn(*section);
            continue;
        }

        if (!submit(*batch, submission)) {
            // The work never reached the GPU: the sections go back to the queue to be built again.
            submission.commandBuffer->reset();
            std::unique_lock<std::recursive_mutex> lock(chunks_.mutex_);
            idle_.push_back(std::move(submission));
            for (auto &section : batch->sections) chunks_.queueBuild(std::move(section));
            return;
        }

        if (profiling()) {
            const double totalMs = millisecondsSince(gatherStart);
            if (totalMs > 8.0) {
                std::cout << "[native profile] chunk batch " << batch->sections.size() << " chunks: select "
                          << std::chrono::duration<double, std::milli>(buildStart - gatherStart).count()
                          << " ms, build+submit " << millisecondsSince(buildStart) << " ms" << std::endl;
            }
        }
        std::unique_lock<std::recursive_mutex> lock(chunks_.mutex_);
        inFlight_.push_back({std::move(submission), std::move(batch)});
    }
}

bool ChunkBuildScheduler::submit(Batch &batch, const Submission &submission) {
    auto framework = Renderer::instance().framework();
    auto device = framework->device();
    const auto &commandBuffer = submission.commandBuffer;
    const VkCommandBuffer vkCommandBuffer = commandBuffer->vkCommandBuffer();

    commandBuffer->begin();
    bool uploaded = false;
    for (const auto &section : batch.sections) {
        if (section->geometryBuffer != nullptr) {
            section->geometryBuffer->uploadToBuffer(commandBuffer);
            uploaded = true;
        }
        if (section->lightBuffer != nullptr) {
            section->lightBuffer->uploadToBuffer(commandBuffer);
            uploaded = true;
        }
    }
    if (uploaded) {
        commandBuffer->memoryBarrier(VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
                                     VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR |
                                         VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR |
                                         VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                                         (gpu::opacityMicromapsEnabled() ? VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT
                                                                         : VK_PIPELINE_STAGE_2_NONE),
                                     VK_ACCESS_2_MEMORY_READ_BIT);
    }
    gpu::diagnostics::checkpoint(vkCommandBuffer, "chunks:batch-uploaded");

    if (batch.structures != nullptr) {
        batch.structures->record(commandBuffer, framework->physicalDevice(), device, framework->vma());
    }
    if (!batch.swaying.empty()) {
        batch.swayingScratch =
            gpu::recordBlasBuilds(commandBuffer, batch.swaying, framework->physicalDevice(), device, framework->vma());
    }
    if (batch.compactedSizes != VK_NULL_HANDLE) {
        queriedStructures_.clear();
        for (ChunkBuildData *section : batch.built) queriedStructures_.push_back(section->blas->handle());
        const uint32_t count = static_cast<uint32_t>(queriedStructures_.size());
        commandBuffer->memoryBarrier(VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                                     VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
                                     VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                                     VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR);
        vkCmdResetQueryPool(vkCommandBuffer, batch.compactedSizes, 0, count);
        vkCmdWriteAccelerationStructuresPropertiesKHR(vkCommandBuffer, count, queriedStructures_.data(),
                                                      VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR,
                                                      batch.compactedSizes, 0);
    }
    gpu::diagnostics::checkpoint(vkCommandBuffer, "chunks:batch-built");
    commandBuffer->end();

    VkSubmitInfo info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    info.commandBufferCount = 1;
    info.pCommandBuffers = &vkCommandBuffer;
    const VkQueue queue = useSecondaryQueue_ ? device->secondaryQueue() : device->mainVkQueue();
    const VkResult result = vkQueueSubmit(queue, 1, &info, submission.fence->vkFence());
    if (result == VK_SUCCESS) return true;
    gpu::diagnostics::onResult(result, device->mainVkQueue(), device->secondaryQueue());
    return false;
}

void ChunkBuildScheduler::finish(InFlight &inFlight) {
    auto device = Renderer::instance().framework()->device();
    vkResetFences(device->vkDevice(), 1, &inFlight.submission.fence->vkFence());
    inFlight.submission.commandBuffer->reset();
    idle_.push_back(std::move(inFlight.submission));

    Batch &batch = *inFlight.batch;
    collectCompactions(batch);
    for (const auto &section : batch.sections) {
        chunks_.swapIn(*section);
        section->releaseStaging();
    }
}

void ChunkBuildScheduler::collectCompactions(Batch &batch) {
    if (batch.compactedSizes == VK_NULL_HANDLE) return;
    const VkDevice device = batch.queryDevice->vkDevice();
    const uint32_t count = static_cast<uint32_t>(batch.built.size());
    std::vector<uint64_t> sizes(count, 0);
    const VkResult result =
        vkGetQueryPoolResults(device, batch.compactedSizes, 0, count, sizes.size() * sizeof(uint64_t), sizes.data(),
                              sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
    vkDestroyQueryPool(device, batch.compactedSizes, nullptr);
    batch.compactedSizes = VK_NULL_HANDLE;
    if (result != VK_SUCCESS) return;

    for (uint32_t i = 0; i < count; i++) {
        const ChunkBuildData &section = *batch.built[i];
        if (section.blas != nullptr && sizes[i] > 0) {
            chunks_.pendingCompactions_.push_back({section.id, section.blas, sizes[i]});
        }
    }
}
