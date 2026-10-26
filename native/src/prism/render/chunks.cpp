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

namespace {

using Clock = std::chrono::steady_clock;
using PBRVertex = gpu::VertexFormat::PBRVertex;
using MaterialVertex = gpu::VertexFormat::PackedMaterialVertex;

std::atomic<uint64_t> g_contentVersion{0};

double millisecondsSince(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// Development timings (RADIANTE_DEV_PROFILE): a line for each step that overran.
bool profiling() {
    static const bool enabled = std::getenv("RADIANTE_DEV_PROFILE") != nullptr;
    return enabled;
}

// Keeps what frames in flight may still read alive until none of them is left.
template <typename... Held>
void retireWithFrames(Held &&...held) {
    gpu::retire::later([... kept = std::forward<Held>(held)] { ((void)kept, ...); });
}

// Takes what a slot draws out of it, kept until the frames that may still trace it are done.
void retireDrawn(Chunk1 &slot) {
    retireWithFrames(std::move(slot.blas), std::move(slot.geometryGroupNames), std::move(slot.indexBufferAddresses),
                     std::move(slot.positionBufferAddresses), std::move(slot.materialBufferAddresses),
                     std::move(slot.geometryBuffer), std::move(slot.lightBuffer), std::move(slot.windBuild),
                     std::move(slot.windRegions), std::move(slot.micromap), std::move(slot.micromapInputs));
}

// How urgently a queued section is built: the nearer the camera the sooner, but a slot that just took a build waits a
// moment, so a section that keeps changing does not hold back the rest.
float buildPriority(const Chunk1 &slot, Clock::time_point now, glm::vec3 cameraPos, glm::vec3 sectionPos) {
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

// ---- compact section geometry
//
// Section geometry goes to the GPU in a compact form of its own; entities keep the general layout. The address of a
// region in this form carries a tag in its lowest bit, free because every region is 8-byte aligned, and the shaders'
// loaders (util/vertex.glsl) read the compact form wherever they see it:
//   indices    16-bit when the geometry has at most 65535 vertices (tagged), otherwise 32-bit
//   positions  four halves per vertex, relative to the section origin (always tagged). Block geometry lies on a 1/16
//              grid, which half floats hold exactly over a section's range, so shared edges stay exactly shared.
//   materials  when every vertex shares its texture, flags, emission, overlay and glint (terrain always does): a
//              5-word header with those, then 5 words per vertex - normal, colour, u, v, light (tagged). Otherwise
//              40-byte PackedMaterialVertex records.
// A geometry with foliage that sways (a vertex with a wind code, util/wind.glsl) keeps its positions as four floats
// instead, untagged: x, y, z and the wind code, read as PositionVertex. The wind pass moves them every frame by a
// small, smoothly changing amount, which halves would round into visible steps. The same positions at rest follow
// the materials, for the pass to move them from.
// One buffer holds every geometry's indices, then every geometry's positions, then every geometry's materials, then
// the rest positions of the geometries that sway.

constexpr VkDeviceAddress kCompactTag = 1;
constexpr uint32_t kMaxShortIndexedVertices = 65535;
// How far from the camera the wind moves foliage (WIND_FADE_END in util/wind.glsl), and the period its field repeats
// over (WIND_PERIOD).
constexpr double kWindReach = 48.0;
constexpr int32_t kWindPeriod = 4096;

size_t alignTo8(size_t value) {
    return (value + 7) & ~size_t{7};
}

size_t alignTo16(size_t value) {
    return (value + 15) & ~size_t{15};
}

// The wind code Java gives a vertex (PBRVertex::postBase.x): 0 for one that stays put.
float windCodeOf(const PBRVertex &vertex) {
    const float code = vertex.postBase.x;
    return std::isfinite(code) && code > 0.0f ? code : 0.0f;
}

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


// Whether every vertex shares the fields the compact material header holds once for all of them.
bool sharesMaterial(std::span<const MaterialVertex> materials) {
    const MaterialVertex &first = materials.front();
    return std::all_of(materials.begin(), materials.end(), [&](const MaterialVertex &m) {
        return m.textures == first.textures && m.overlay == first.overlay && m.glintUV == first.glintUV &&
               m.packedData == first.packedData &&
               std::bit_cast<uint32_t>(m.albedoEmission) == std::bit_cast<uint32_t>(first.albedoEmission);
    });
}

// Writes the packed buffer front to back, padding included: staging memory is write-combined.
class PackedWriter {
  public:
    explicit PackedWriter(uint8_t *out) : out_(out) {}

    template <typename T>
    void put(T value) {
        std::memcpy(out_ + at_, &value, sizeof(T));
        at_ += sizeof(T);
    }
    void put(const void *bytes, size_t size) {
        std::memcpy(out_ + at_, bytes, size);
        at_ += size;
    }
    void padTo(size_t at) {
        std::memset(out_ + at_, 0, at - at_);
        at_ = at;
    }
    void align() {
        padTo(alignTo8(at_));
    }
    size_t at() const {
        return at_;
    }

  private:
    uint8_t *out_;
    size_t at_ = 0;
};

// One vertex of a swaying geometry: its position and its wind code.
void putWindyPosition(PackedWriter &writer, const PBRVertex &vertex) {
    writer.put(vertex.pos.x);
    writer.put(vertex.pos.y);
    writer.put(vertex.pos.z);
    writer.put(windCodeOf(vertex));
}

// Two triangles per quad of four vertices.
template <typename Index>
void putQuadIndices(PackedWriter &writer, uint32_t vertexCount) {
    for (uint32_t corner = 0; corner + 3 < vertexCount; corner += 4) {
        for (uint32_t offset : {0u, 1u, 2u, 2u, 3u, 0u}) writer.put(static_cast<Index>(corner + offset));
    }
}

void putPackedGeometry(const std::vector<std::vector<PBRVertex>> &vertices,
                       const std::vector<PackedRegions> &regions,
                       const std::vector<MaterialVertex> &materials,
                       size_t size,
                       uint8_t *out) {
    PackedWriter writer(out);
    for (const PackedRegions &region : regions) {
        assert(writer.at() == region.indexOffset);
        if (region.shortIndices) {
            putQuadIndices<uint16_t>(writer, region.vertexCount);
        } else {
            putQuadIndices<uint32_t>(writer, region.vertexCount);
        }
        writer.align();
    }
    for (size_t g = 0; g < regions.size(); g++) {
        if (regions[g].windy) writer.padTo(regions[g].positionOffset);
        assert(writer.at() == regions[g].positionOffset);
        for (const PBRVertex &vertex : vertices[g]) {
            if (regions[g].windy) {
                putWindyPosition(writer, vertex);
                continue;
            }
            writer.put(glm::packHalf2x16(glm::vec2(vertex.pos.x, vertex.pos.y)));
            writer.put(glm::packHalf2x16(glm::vec2(vertex.pos.z, 0.0f)));
        }
        writer.align();
    }
    const MaterialVertex *material = materials.data();
    for (const PackedRegions &region : regions) {
        assert(writer.at() == region.materialOffset);
        if (region.compactMaterial) {
            writer.put(material->textures);
            writer.put(material->overlay);
            writer.put(material->glintUV);
            writer.put(material->albedoEmission);
            writer.put(material->packedData);
            for (uint32_t v = 0; v < region.vertexCount; v++) {
                const MaterialVertex &m = material[v];
                writer.put(m.normal);
                writer.put(m.color);
                writer.put(m.u);
                writer.put(m.v);
                writer.put(m.light);
            }
        } else {
            writer.put(material, region.vertexCount * sizeof(MaterialVertex));
        }
        material += region.vertexCount;
        writer.align();
    }
    for (size_t g = 0; g < regions.size(); g++) {
        if (!regions[g].windy) continue;
        writer.padTo(regions[g].restOffset);
        for (const PBRVertex &vertex : vertices[g]) putWindyPosition(writer, vertex);
    }
    writer.padTo(size);
}


} // namespace

// A section build on its way from Java to its slot: the geometry, packed into a buffer of its own by the thread that
// hands it over, its lights, and its structure once that has been placed.
struct ChunkBuildData {
    int64_t id = 0;
    int x = 0;
    int y = 0;
    int z = 0;
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

void ChunkBuildData::buildLights(const Emission *emission,
                                 const std::shared_ptr<gpu::VMA> &vma,
                                 const std::shared_ptr<gpu::Device> &device) {
    lightBuffer = nullptr;
    lightCount = 0;
    if (!collectEmission || emission == nullptr) return;

    std::vector<LightInfo> lights;
    std::unordered_map<uint32_t, Emission::TextureOccupancy> occupancy;
    for (uint32_t g = 0; g < geometryCount(); g++) {
        collectEmissiveQuadLights(*emission, vertices[g], id, g, occupancy, lights);
    }
    if (lights.empty()) return;

    // A block with one of Bedrock's point lights (a torch, a lantern) casts its light through that alone: the glowing
    // texels of its flame stay off the list. As candidates they outnumbered the point light twenty to one, so most
    // surfaces picked a speck of flame and the light on the floor around a torch set down was grainy where the same
    // torch held, a single light, was clean. Their share of the light is in the point light's strength instead
    // (BedrockPointLights.CAST_STRENGTH).
    auto blockOf = [](const glm::vec3 &p) {
        const glm::ivec3 b = glm::ivec3(glm::floor(p));
        return (static_cast<uint32_t>(b.x & 15) << 8) | (static_cast<uint32_t>(b.y & 15) << 4) |
               static_cast<uint32_t>(b.z & 15);
    };
    std::array<uint64_t, 64> pointLightBlocks{};
    bool anyPointLight = false;
    for (const LightInfo &light : lights) {
        if (light.kind < 1.5f) continue;
        const uint32_t block = blockOf(light.p0);
        pointLightBlocks[block >> 6] |= 1ull << (block & 63u);
        anyPointLight = true;
    }

    std::vector<PackedLightData> packed;
    packed.reserve(lights.size());
    const glm::vec3 origin(x, y, z);
    for (const LightInfo &light : lights) {
        if (light.area <= 1e-6f || std::max({light.radiance.r, light.radiance.g, light.radiance.b}) <= 1e-6f) continue;
        if (anyPointLight && light.kind < 1.5f) {
            const uint32_t block = blockOf((light.p0 + light.p1 + light.p2) / 3.0f);
            if ((pointLightBlocks[block >> 6] >> (block & 63u)) & 1ull) continue;
        }
        packed.push_back(packLightAt(light, origin));
    }
    if (packed.empty()) return;

    // A shader picks a light of a section's list at random, each as likely as the next. Beside the hundreds of
    // glowing texels of a nether portal a torch was one among hundreds, all but never picked, and the floor around it
    // stayed dark. Each point light is listed that many times over, up to about as many entries as the rest, each
    // with its share of the light: the same light in all, picked as often as the others together.
    const auto isPoint = [](const PackedLightData &light) { return light.radiance.w > 1.5f; };
    const auto others = static_cast<size_t>(std::count_if(packed.begin(), packed.end(), std::not_fn(isPoint)));
    const size_t points = packed.size() - others;
    if (points > 0 && others > points) {
        constexpr size_t kMaxCopies = 64;
        const size_t copies = std::min((others + points - 1) / points, kMaxCopies);
        const size_t count = packed.size();
        packed.reserve(others + points * copies);
        for (size_t i = 0; i < count; i++) {
            if (!isPoint(packed[i])) continue;
            const glm::vec4 radiance = packed[i].radiance;
            packed[i].radiance = glm::vec4(glm::vec3(radiance) / static_cast<float>(copies), radiance.w);
            for (size_t copy = 1; copy < copies; copy++) packed.push_back(packed[i]);
        }
    }

    lightCount = static_cast<uint32_t>(packed.size());
    const size_t bytes = packed.size() * sizeof(PackedLightData);
    lightBuffer = gpu::DeviceLocalBuffer::create(vma, device, persistStaging(), bytes,
                                                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                 0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 16);
    lightBuffer->uploadToStagingBuffer(packed.data(), bytes, 0);
}

void ChunkBuildData::packGeometry(const std::shared_ptr<gpu::VMA> &vma, const std::shared_ptr<gpu::Device> &device) {
    if (vertices.empty()) return;

    // The micromaps of the cut-out geometries come first: quads one cannot describe move to a geometry of their own,
    // with the same type and hit group, which is then packed like the others (opacity_micromaps.hpp).
    micromaps = nullptr;
    if (gpu::opacityMicromapsEnabled()) {
        std::vector<bool> cutout(vertices.size());
        for (size_t g = 0; g < vertices.size(); g++) cutout[g] = geometryTypes[g] != World::WORLD_SOLID;
        micromaps = omm::bakeSection(vertices, cutout, device->opacityMicromapMaxSubdivisionLevel(),
                                     [this](size_t source, std::vector<PBRVertex> &&quads) {
                                         geometryTypes.push_back(geometryTypes[source]);
                                         geometryGroupNames.push_back(geometryGroupNames[source]);
                                         vertices.push_back(std::move(quads));
                                     });
    }

    // The material records are made once: the layout needs to know which geometries share one, the writer the records.
    size_t vertexTotal = 0;
    for (const auto &geometry : vertices) vertexTotal += geometry.size();
    std::vector<MaterialVertex> materials;
    materials.reserve(vertexTotal);
    for (const auto &geometry : vertices) {
        for (const PBRVertex &vertex : geometry) materials.push_back(gpu::Vertex::makeMaterialVertex(vertex));
    }

    regions.assign(vertices.size(), {});
    size_t size = 0;
    for (size_t g = 0; g < regions.size(); g++) {
        PackedRegions &region = regions[g];
        region.vertexCount = static_cast<uint32_t>(vertices[g].size());
        region.indexCount = region.vertexCount / 4 * 6;
        region.shortIndices = region.vertexCount <= kMaxShortIndexedVertices;
        region.indexOffset = size;
        size = alignTo8(size + size_t{region.indexCount} * (region.shortIndices ? 2 : 4));
    }
    for (size_t g = 0; g < regions.size(); g++) {
        PackedRegions &region = regions[g];
        region.windy = std::any_of(vertices[g].begin(), vertices[g].end(),
                                   [](const PBRVertex &vertex) { return windCodeOf(vertex) > 0.0f; });
        if (region.windy) size = alignTo16(size);
        region.positionOffset = size;
        size = alignTo8(size + size_t{region.vertexCount} * (region.windy ? 16 : 8));
    }
    size_t first = 0;
    for (PackedRegions &region : regions) {
        region.compactMaterial = sharesMaterial({materials.data() + first, region.vertexCount});
        region.materialOffset = size;
        size = alignTo8(size + (region.compactMaterial ? (5 + size_t{region.vertexCount} * 5) * sizeof(uint32_t) :
                                                         size_t{region.vertexCount} * sizeof(MaterialVertex)));
        first += region.vertexCount;
    }
    for (PackedRegions &region : regions) {
        if (!region.windy) continue;
        region.restOffset = size = alignTo16(size);
        size += size_t{region.vertexCount} * 16;
    }
    // The micromap inputs of the cut-out geometries follow (opacity_micromaps.hpp): states, triangle array and the
    // per-triangle indices, read by the micromap and structure builds only.
    const size_t packedSize = size;
    if (micromaps != nullptr) size = micromaps->layout(size);
    size = std::max<size_t>(size, 16);

    // One buffer per section holds all of its geometry. Sections used to share buffers with the rest of their build
    // batch, so rebuilding one left the whole batch's memory alive until every section in it was rebuilt too, and
    // video memory crept up over a long session.
    constexpr VkBufferUsageFlags kGeometryUsage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                                  VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (micromaps != nullptr) {
        // The micromap build reads from 256-byte aligned addresses, laid out from the buffer's start.
        geometryBuffer = gpu::DeviceLocalBuffer::create(
            vma, device, persistStaging(), size,
            kGeometryUsage | VK_BUFFER_USAGE_MICROMAP_BUILD_INPUT_READ_ONLY_BIT_EXT, 0,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 256);
    } else {
        geometryBuffer = gpu::DeviceLocalBuffer::create(vma, device, persistStaging(), size, kGeometryUsage);
    }
    const size_t geometryEnd = micromaps != nullptr ? packedSize : size;
    if (persistStaging()) {
        auto *out = static_cast<uint8_t *>(geometryBuffer->mappedPtr());
        putPackedGeometry(vertices, regions, materials, geometryEnd, out);
        if (micromaps != nullptr) micromaps->write(out);
        geometryBuffer->flushStagingBuffer();
    } else {
        std::vector<uint8_t> bytes(size);
        putPackedGeometry(vertices, regions, materials, geometryEnd, bytes.data());
        if (micromaps != nullptr) micromaps->write(bytes.data());
        geometryBuffer->uploadToStagingBuffer(bytes.data(), size, 0);
    }
    Chunks::devBytes[0] += size;
    micromapInputs = nullptr;
    if (micromaps != nullptr) {
        // The micromap inputs' share of the buffer, counted while a slot holds it.
        struct Footprint {
            explicit Footprint(uint64_t counted) : bytes(counted) {
                omm::stats().inputBytesLive.fetch_add(bytes, std::memory_order_relaxed);
            }
            Footprint(const Footprint &) = delete;
            Footprint &operator=(const Footprint &) = delete;
            ~Footprint() {
                omm::stats().inputBytesLive.fetch_sub(bytes, std::memory_order_relaxed);
            }
            uint64_t bytes;
        };
        micromapInputs = std::make_shared<Footprint>(size - packedSize);
    }

    const VkDeviceAddress base = geometryBuffer->bufferAddress();
    indexAddresses.clear();
    positionAddresses.clear();
    materialAddresses.clear();
    windRegions.clear();
    for (const PackedRegions &region : regions) {
        indexAddresses.push_back((base + region.indexOffset) | (region.shortIndices ? kCompactTag : 0));
        positionAddresses.push_back((base + region.positionOffset) | (region.windy ? 0 : kCompactTag));
        materialAddresses.push_back((base + region.materialOffset) | (region.compactMaterial ? kCompactTag : 0));
        if (region.windy) {
            windRegions.push_back({base + region.positionOffset, base + region.restOffset, region.vertexCount});
        }
    }
}

void ChunkBuildData::defineTriangles(gpu::BlasBuild &build,
                                     const std::shared_ptr<gpu::Device> &device,
                                     const std::shared_ptr<gpu::VMA> &vma) {
    const VkDeviceAddress base = geometryBuffer->bufferAddress();
    // A micromap of its own for each structure build: one placed for a build that never ran (a batch that failed to
    // submit and is built again) would be referenced unbuilt.
    micromapBuild = nullptr;
    if (micromaps != nullptr) {
        micromapBuild = gpu::MicromapBuild::create(device, vma, base + micromaps->statesOffset,
                                                   base + micromaps->trianglesOffset, micromaps->usage);
    }
    for (size_t g = 0; g < regions.size(); g++) {
        const PackedRegions &region = regions[g];
        gpu::OpacityMicromapAttachment attachment;
        const gpu::OpacityMicromapAttachment *micromap = nullptr;
        if (micromapBuild != nullptr && micromaps->geometries[g].used) {
            attachment.build = micromapBuild;
            attachment.indices = base + micromaps->geometries[g].indexOffset;
            attachment.usage = micromaps->geometries[g].usage;
            micromap = &attachment;
        }
        build.triangles(base + region.positionOffset,
                        region.windy ? VK_FORMAT_R32G32B32_SFLOAT : VK_FORMAT_R16G16B16A16_SFLOAT,
                        region.windy ? 16 : 8, region.vertexCount,
                        base + region.indexOffset, region.shortIndices ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32,
                        region.indexCount, geometryTypes[g] == World::WORLD_SOLID, micromap);
    }
}

void ChunkBuildData::releaseStaging() {
    if (geometryBuffer != nullptr) geometryBuffer->releaseStaging();
    if (lightBuffer != nullptr) lightBuffer->releaseStaging();
}

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
    vkQueueWaitIdle(device->mainVkQueue());
    vkQueueWaitIdle(device->secondaryQueue());
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

int32_t floorModInt(int32_t value, int32_t divisor);

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
    // section, as the fade in util/wind.glsl is measured to each vertex.
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

namespace {
int32_t floorModInt(int32_t value, int32_t divisor) {
    const int32_t remainder = value % divisor;
    return remainder < 0 ? remainder + divisor : remainder;
}
} // namespace

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
