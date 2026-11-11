#include "prism/render/chunks_internal.hpp"

// The wind pass: moves the positions of the sections whose foliage sways and refits their structures.

namespace {

// One section geometry the wind pass moves (world/wind/wind_displace.comp reads it as WindJob).
struct WindJob {
    VkDeviceAddress positions;
    VkDeviceAddress rest;
    uint32_t vertexCount;
    // 1 to move it by the wind, 0 to put it back to rest.
    float scale;
    // The section's origin, x and z wrapped into the wind's period; and relative to the camera.
    float wrappedX;
    float wrappedY;
    float wrappedZ;
    float relativeX;
    float relativeY;
    float relativeZ;
};
static_assert(sizeof(WindJob) == 48);

// The push constants of wind_displace.comp.
struct WindConstants {
    VkDeviceAddress jobs;
    uint32_t jobCount;
    float time;
    float strength;
    float weather;
};
static_assert(sizeof(WindConstants) == 24);

constexpr uint32_t kWindGroupSize = 64;

} // namespace

void Chunks::recordWind(const std::shared_ptr<gpu::CommandBuffer> &commandBuffer,
                        const glm::dvec3 &cameraPos,
                        const WindParams &wind) {
    std::unique_lock<std::recursive_mutex> lock(mutex_);
    const bool blowing = wind.strength > 0.0f;
    // With the wind off Java sends no wind codes, and once every section is rebuilt without them nothing here sways.
    if (windMoved_.empty() && (!blowing || windSlots_.empty())) return;

    auto framework = Renderer::instance().framework();
    auto device = framework->device();
    const uint32_t frame = framework->safeAcquireCurrentContext()->frameIndex;
    const uint64_t pass = ++windPassCount_;

    std::vector<WindJob> jobs;
    std::vector<std::shared_ptr<gpu::BlasBuild>> refits;
    std::vector<int64_t> moved;
    uint32_t maxVertices = 0;
    const auto addJobs = [&](const Chunk1 &slot, float scale) {
        const glm::dvec3 relative = glm::dvec3(slot.x, slot.y, slot.z) - cameraPos;
        for (const WindRegion &region : *slot.windRegions) {
            jobs.push_back({
                .positions = region.positions,
                .rest = region.rest,
                .vertexCount = region.vertexCount,
                .scale = scale,
                .wrappedX = static_cast<float>(floorModInt(slot.x, kWindPeriod)),
                .wrappedY = static_cast<float>(slot.y),
                .wrappedZ = static_cast<float>(floorModInt(slot.z, kWindPeriod)),
                .relativeX = static_cast<float>(relative.x),
                .relativeY = static_cast<float>(relative.y),
                .relativeZ = static_cast<float>(relative.z),
            });
            maxVertices = std::max(maxVertices, region.vertexCount);
        }
        refits.push_back(slot.windBuild);
    };

    // The sections within the wind's reach of the camera move. The reach is measured to the nearest point of the
    // section, as the fade in util/wind.glsl is measured to a point in it (windFadeAt).
    if (blowing) {
        for (int64_t id : windSlots_.slots()) {
            Chunk1 &slot = *chunks_[id];
            if (slot.windBuild == nullptr || slot.windRegions == nullptr || slot.blas != slot.windBuild->target()) {
                continue;
            }
            const glm::dvec3 low(slot.x, slot.y, slot.z);
            const glm::dvec3 nearest = glm::clamp(cameraPos, low, low + 16.0);
            if (glm::distance(nearest, cameraPos) >= kWindReach) continue;
            slot.windPass = pass;
            addJobs(slot, 1.0f);
            moved.push_back(id);
        }
    }
    // Those the last pass moved that have left the reach (or the wind stopped) go back to rest once, so nothing is
    // left frozen half way through a sway.
    for (int64_t id : windMoved_) {
        if (!validSlot(id)) continue;
        Chunk1 &slot = *chunks_[id];
        if (slot.windPass == pass || slot.windBuild == nullptr || slot.windRegions == nullptr ||
            slot.blas != slot.windBuild->target()) {
            continue;
        }
        addJobs(slot, 0.0f);
    }
    windMoved_ = std::move(moved);
    if (jobs.empty()) return;

    if (windPipeline_ == nullptr) {
        // The pass reads everything through device addresses; the one set only gives the layout something to hold.
        windTable_ = gpu::DescriptorTableBuilder{}
                         .beginDescriptorLayoutSet()
                         .beginDescriptorLayoutSetBinding()
                         .defineDescriptorLayoutSetBinding({
                             .binding = 0,
                             .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                             .descriptorCount = 1,
                             .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                         })
                         .endDescriptorLayoutSetBinding()
                         .endDescriptorLayoutSet()
                         .definePushConstant({
                             .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                             .offset = 0,
                             .size = sizeof(WindConstants),
                         })
                         .build(device);
        auto shader = gpu::Shader::create(
            device, (Renderer::folderPath / "shaders/world/wind/wind_displace_comp.spv").string());
        windPipeline_ = gpu::ComputePipelineBuilder{}.defineShader(shader).definePipelineLayout(windTable_).build(device);
    }

    // The job list, one buffer per frame slot: the frames before may still be reading theirs.
    if (windJobBuffers_.size() <= frame) windJobBuffers_.resize(frame + 1);
    auto &jobBuffer = windJobBuffers_[frame];
    const size_t jobBytes = jobs.size() * sizeof(WindJob);
    if (jobBuffer == nullptr || jobBuffer->size() < jobBytes) {
        jobBuffer = gpu::HostVisibleBuffer::create(framework->vma(), device, jobBytes + jobBytes / 2 + 16 * sizeof(WindJob),
                                                   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                   16);
    }
    jobBuffer->uploadToBuffer(jobs.data(), jobBytes, 0);

    const VkCommandBuffer commands = commandBuffer->vkCommandBuffer();
    GpuProfiler::mark(commands, "rt:prepare");
    // The frames before this one traced these positions and structures, and the last pass refit them with the same
    // scratch memory: all of that is done before they are written again.
    commandBuffer->memoryBarrier(
        VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR |
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR |
            VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR |
            VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR);

    const WindConstants constants{
        .jobs = jobBuffer->bufferAddress(),
        .jobCount = static_cast<uint32_t>(jobs.size()),
        .time = wind.time,
        .strength = wind.strength,
        .weather = wind.weather,
    };
    commandBuffer->bindComputePipeline(windPipeline_);
    vkCmdPushConstants(commands, windTable_->vkPipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants),
                       &constants);
    vkCmdDispatch(commands, (maxVertices + kWindGroupSize - 1) / kWindGroupSize, constants.jobCount, 1);

    // The moved positions are what the refits read, and what the hit shaders rebuild their hits from.
    commandBuffer->memoryBarrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
                                 VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR |
                                     VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                                 VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR);
    gpu::recordBlasUpdates(commandBuffer, refits, windScratch_, framework->physicalDevice(), device,
                           framework->vma());
    GpuProfiler::mark(commands, "rt:wind");
}
