#include "prism/render/chunks_internal.hpp"

// Packing a section's geometry and lights for the GPU (ChunkBuildData), on the thread that hands the section over.

namespace {

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
