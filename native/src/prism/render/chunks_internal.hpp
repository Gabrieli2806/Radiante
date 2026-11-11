#pragma once

// What the files the sections' code is spread over share (chunks.cpp, chunk_geometry.cpp,
// chunk_build_scheduler.cpp, chunks_wind.cpp). Not for the rest of the renderer: that includes chunks.hpp.

#include "prism/render/chunks.hpp"

#include "prism/render/buffers.hpp"
#include "prism/render/gpu_profiler.hpp"
#include "prism/render/opacity_micromaps.hpp"
#include "prism/render/render_framework.hpp"
#include "prism/render/renderer.hpp"
#include "prism/render/textures.hpp"

#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <span>
#include <unordered_map>

namespace chunk_detail {

using Clock = std::chrono::steady_clock;
using PBRVertex = gpu::VertexFormat::PBRVertex;
using MaterialVertex = gpu::VertexFormat::PackedMaterialVertex;

inline double millisecondsSince(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// Development timings (RADIANTE_DEV_PROFILE): a line for each step that overran.
inline bool profiling() {
    static const bool enabled = std::getenv("RADIANTE_DEV_PROFILE") != nullptr;
    return enabled;
}

// Keeps what frames in flight may still read alive until none of them is left.
template <typename... Held>
void retireWithFrames(Held &&...held) {
    gpu::retire::later([... kept = std::forward<Held>(held)] { ((void)kept, ...); });
}

// Takes what a slot draws out of it, kept until the frames that may still trace it are done.
inline void retireDrawn(Chunk1 &slot) {
    retireWithFrames(std::move(slot.blas), std::move(slot.geometryGroupNames), std::move(slot.indexBufferAddresses),
                     std::move(slot.positionBufferAddresses), std::move(slot.materialBufferAddresses),
                     std::move(slot.geometryBuffer), std::move(slot.lightBuffer), std::move(slot.windBuild),
                     std::move(slot.windRegions), std::move(slot.micromap), std::move(slot.micromapInputs));
}

// How urgently a queued section is built: the nearer the camera the sooner, but a slot that just took a build waits a
// moment, so a section that keeps changing does not hold back the rest.
inline float buildPriority(const Chunk1 &slot, Clock::time_point now, glm::vec3 cameraPos, glm::vec3 sectionPos) {
    constexpr float kSettleMs = 200.0f;
    constexpr float kSettleWeight = 1.0f;
    constexpr float kHalfDistance = 128.0f; // blocks
    constexpr float kDistanceSensitivity = 1.35f;
    constexpr float kDistanceFloor = 0.4f;
    constexpr float kDistanceWeight = 1.1f;

    const double sinceUpdate = std::chrono::duration<double, std::milli>(now - slot.lastUpdate).count();
    const double distance = glm::distance(cameraPos, sectionPos);
    const double settled = 1.0 - std::exp(-sinceUpdate / kSettleMs);
    const double nearness = 1.0 / (1.0 + std::pow(distance / kHalfDistance, kDistanceSensitivity));
    const double closeness = kDistanceFloor + (1.0 - kDistanceFloor) * nearness;
    return static_cast<float>(std::pow(settled, kSettleWeight) * std::pow(closeness, kDistanceWeight));
}

// The compact form section geometry takes on the GPU is described in chunk_geometry.cpp.
inline constexpr VkDeviceAddress kCompactTag = 1;
inline constexpr uint32_t kMaxShortIndexedVertices = 65535;
// How far from the camera the wind moves foliage (WIND_FADE_END in util/wind.glsl), and the period its field repeats
// over (WIND_PERIOD).
inline constexpr double kWindReach = 48.0;
inline constexpr int32_t kWindPeriod = 4096;

// Where one geometry's regions lie in the packed buffer.
struct PackedRegions {
    size_t indexOffset = 0;
    size_t positionOffset = 0;
    size_t materialOffset = 0;
    // Only for a geometry that sways: its positions at rest.
    size_t restOffset = 0;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    bool shortIndices = false;
    bool compactMaterial = false;
    bool windy = false;
};

inline int32_t floorModInt(int32_t value, int32_t divisor) {
    const int32_t remainder = value % divisor;
    return remainder < 0 ? remainder + divisor : remainder;
}

} // namespace chunk_detail

using namespace chunk_detail;

// A section build on its way from Java to its slot: the geometry, packed into a buffer of its own by the thread that
// hands it over, its lights, and its structure once that has been placed.
struct ChunkBuildData {
    int64_t id = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    // The blocks the geometry spans along x and z (Chunk1::minX), worked out by queueChunkBuild.
    int minX = 0, maxX = 0, minZ = 0, maxZ = 0;
    int64_t version = 0;
    // The grid the version was taken from: reset() starts a new one, whose slots number their builds from 0 again.
    uint64_t generation = 0;
    // A near-player rebuild: built and swapped in by the call that hands it over, its uploads and structure recorded
    // by the next frame. The others wait in the queue and are built in batches.
    bool important = false;
    bool collectEmission = false;
    // Emission was switched on while this waited: the lights are built when it is taken into a batch.
    bool lightsStale = false;

    std::vector<World::GeometryTypes> geometryTypes;
    std::vector<std::string> geometryGroupNames;
    // Four vertices per quad, kept to rebuild the lights should emission be switched on while this waits.
    std::vector<std::vector<PBRVertex>> vertices;

    std::vector<PackedRegions> regions;
    std::shared_ptr<gpu::DeviceLocalBuffer> geometryBuffer;
    std::vector<VkDeviceAddress> indexAddresses;
    std::vector<VkDeviceAddress> positionAddresses;
    std::vector<VkDeviceAddress> materialAddresses;

    std::shared_ptr<gpu::DeviceLocalBuffer> lightBuffer;
    uint32_t lightCount = 0;

    std::shared_ptr<gpu::BlasBuild> blasBuild;
    std::shared_ptr<gpu::BLAS> blas;
    // The geometries with foliage that sways, filled by packGeometry; empty for a section where nothing sways.
    std::vector<WindRegion> windRegions;
    // The micromap inputs of its cut-out geometries, written after the geometry by packGeometry (null without any),
    // and the micromap the last defineTriangles placed for them.
    std::unique_ptr<omm::SectionMicromaps> micromaps;
    std::shared_ptr<gpu::MicromapBuild> micromapBuild;
    std::shared_ptr<void> micromapInputs;

    uint32_t geometryCount() const {
        return static_cast<uint32_t>(vertices.size());
    }
    // Foliage in it sways with the wind: its structure is built to be refit every frame (ALLOW_UPDATE) and is not
    // compacted.
    bool sways() const {
        return !windRegions.empty();
    }
    VkBuildAccelerationStructureFlagsKHR structureFlags(VkBuildAccelerationStructureFlagsKHR still) const {
        return sways() ? VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR |
                             VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR
                       : still;
    }
    // A batch's buffers keep their staging copy until the batch has run on its own queue (releaseStaging); an
    // important build's goes with the frame that records its upload.
    bool persistStaging() const {
        return !important;
    }

    // Collects the section's lights into a buffer of their own, at their world positions: none while emission is not
    // collected or without `emission`.
    void buildLights(const Emission *emission,
                     const std::shared_ptr<gpu::VMA> &vma,
                     const std::shared_ptr<gpu::Device> &device);
    void packGeometry(const std::shared_ptr<gpu::VMA> &vma, const std::shared_ptr<gpu::Device> &device);
    // Describes the geometry to a structure build, placing a micromap for its cut-out geometries where it has one.
    void defineTriangles(gpu::BlasBuild &build,
                         const std::shared_ptr<gpu::Device> &device,
                         const std::shared_ptr<gpu::VMA> &vma);
    void releaseStaging();
};
