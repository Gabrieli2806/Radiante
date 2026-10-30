#include "prism/render/entities.hpp"
#include "prism/render/hand_primary_bounds.hpp"

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

namespace {

namespace vf = gpu::VertexFormat;
using PBRVertex = vf::PBRVertex;

// ---- Static cache keys

// 64-bit hashing with xxHash64's construction: four independent multiply-rotate lanes over 32-byte stripes, so the
// vertices of every cacheable entity can be hashed each frame at close to memory speed. The keys never leave the
// process; only how well they spread matters.
constexpr uint64_t kPrime1 = 0x9E3779B185EBCA87ull;
constexpr uint64_t kPrime2 = 0xC2B2AE3D27D4EB4Full;
constexpr uint64_t kPrime3 = 0x165667B19E3779F9ull;
constexpr uint64_t kPrime4 = 0x85EBCA77C2B2AE63ull;
constexpr uint64_t kPrime5 = 0x27D4EB2F165667C5ull;

uint64_t load64(const uint8_t *bytes) {
    uint64_t value;
    std::memcpy(&value, bytes, sizeof(value));
    return value;
}

uint32_t load32(const uint8_t *bytes) {
    uint32_t value;
    std::memcpy(&value, bytes, sizeof(value));
    return value;
}

uint64_t mixLane(uint64_t lane, uint64_t input) {
    return std::rotl(lane + input * kPrime2, 31) * kPrime1;
}

uint64_t hash64(const void *data, size_t size, uint64_t seed) {
    const auto *p = static_cast<const uint8_t *>(data);
    const uint8_t *const end = p + size;
    uint64_t hash;
    if (size >= 32) {
        uint64_t lanes[4] = {seed + kPrime1 + kPrime2, seed + kPrime2, seed, seed - kPrime1};
        for (const uint8_t *last = end - 32; p <= last; p += 32) {
            for (int lane = 0; lane < 4; lane++) lanes[lane] = mixLane(lanes[lane], load64(p + 8 * lane));
        }
        hash = std::rotl(lanes[0], 1) + std::rotl(lanes[1], 7) + std::rotl(lanes[2], 12) + std::rotl(lanes[3], 18);
        for (uint64_t lane : lanes) hash = (hash ^ mixLane(0, lane)) * kPrime1 + kPrime4;
    } else {
        hash = seed + kPrime5;
    }
    hash += size;
    for (; end - p >= 8; p += 8) hash = std::rotl(hash ^ mixLane(0, load64(p)), 27) * kPrime1 + kPrime4;
    if (end - p >= 4) {
        hash = std::rotl(hash ^ (load32(p) * kPrime1), 23) * kPrime2 + kPrime3;
        p += 4;
    }
    for (; p < end; p++) hash = std::rotl(hash ^ (*p * kPrime5), 11) * kPrime1;
    hash ^= hash >> 33;
    hash *= kPrime2;
    hash ^= hash >> 29;
    hash *= kPrime3;
    hash ^= hash >> 32;
    return hash;
}

// Chains values into one key. Each value is hashed with the key so far as its seed, which keeps their boundaries
// apart: "ab" then "c" is not "a" then "bc".
class KeyHasher {
  public:
    template <typename T>
        requires(std::is_arithmetic_v<T> || std::is_enum_v<T>)
    void add(T value) {
        addBytes(&value, sizeof(value));
    }
    void add(std::string_view text) {
        addBytes(text.data(), text.size());
    }
    void addBytes(const void *data, size_t size) {
        key_ = hash64(data, size, key_);
    }
    uint64_t key() const {
        return key_;
    }

  private:
    uint64_t key_ = 0;
};

// ---- Reading the task

// One entity of a task, with where its geometries start in the geometry arrays.
struct EntityInput {
    int hashCode;
    double x, y, z;
    int rayTracingFlag;
    int prebuiltBLAS;
    size_t firstGeometry;
    int geometryCount;
};

EntityInput readEntity(const EntitiesBuildTask &task, int entity, size_t firstGeometry) {
    return {
        .hashCode = task.entityHashCodes[entity],
        .x = task.entityXs[entity],
        .y = task.entityYs[entity],
        .z = task.entityZs[entity],
        .rayTracingFlag = task.entityRayTracingFlags[entity],
        .prebuiltBLAS = task.entityPrebuiltBLASs[entity],
        .firstGeometry = firstGeometry,
        .geometryCount = std::max(task.entityGeometryCounts[entity], 0),
    };
}

size_t vertexCount(const EntitiesBuildTask &task, size_t geometry) {
    return static_cast<size_t>(std::max(task.vertexCounts[geometry], 0));
}

std::string_view groupName(const EntitiesBuildTask &task, size_t geometry) {
    if (task.geometryGroupNames != nullptr && task.geometryGroupNames[geometry] != nullptr) {
        return task.geometryGroupNames[geometry];
    }
    return "Entity";
}

std::string_view contentName(const EntitiesBuildTask &task, size_t geometry) {
    if (task.geometryContentNames != nullptr && task.geometryContentNames[geometry] != nullptr) {
        return task.geometryContentNames[geometry];
    }
    return {};
}

// Calls `visit` with the source vertex layout of a World::VertexFormats value (as a std::type_identity), or with
// void for formats whose vertices are not read: those become blank vertices that only carry the geometry's texture.
template <typename Visit>
decltype(auto) visitSourceLayout(int format, Visit &&visit) {
    switch (format) {
        case World::POSITION_COLOR_TEXTURE_LIGHT_NORMAL:
            return visit(std::type_identity<vf::PositionColorTexLightNormal>{});
        case World::POSITION_COLOR_TEXTURE_OVERLAY_LIGHT_NORMAL:
            return visit(std::type_identity<vf::PositionColorTexOverlayLightNormal>{});
        case World::POSITION_TEXTURE_COLOR_LIGHT: return visit(std::type_identity<vf::PositionTexColorLight>{});
        case World::POSITION: return visit(std::type_identity<vf::PositionOnly>{});
        case World::POSITION_COLOR: return visit(std::type_identity<vf::PositionColor>{});
        case World::LINES: return visit(std::type_identity<vf::PositionColorNormal>{});
        case World::POSITION_COLOR_LIGHT: return visit(std::type_identity<vf::PositionColorLight>{});
        case World::POSITION_TEXTURE: return visit(std::type_identity<vf::PositionTex>{});
        case World::POSITION_TEXTURE_COLOR: return visit(std::type_identity<vf::PositionTexColor>{});
        case World::POSITION_COLOR_TEXTURE_LIGHT: return visit(std::type_identity<vf::PositionColorTexLight>{});
        case World::POSITION_TEXTURE_LIGHT_COLOR: return visit(std::type_identity<vf::PositionTexLightColor>{});
        case World::POSITION_TEXTURE_COLOR_NORMAL: return visit(std::type_identity<vf::PositionTexColorNormal>{});
        case World::PBR_TRIANGLE: return visit(std::type_identity<PBRVertex>{});
        default: return visit(std::type_identity<void>{});
    }
}

size_t sourceStride(int format) {
    return visitSourceLayout(format, []<typename Source>(std::type_identity<Source>) -> size_t {
        if constexpr (std::is_void_v<Source>) {
            return 0;
        } else {
            return sizeof(Source);
        }
    });
}

// KEYED_BLAS: the Java side names the content, so the names and sizes are its identity. The position is left out:
// a match only moves the instance.
uint64_t keyedContentKey(const EntitiesBuildTask &task, const EntityInput &entity) {
    KeyHasher hasher;
    hasher.add(entity.rayTracingFlag);
    hasher.add(task.coordinate);
    for (int i = 0; i < entity.geometryCount; i++) {
        const size_t geometry = entity.firstGeometry + i;
        hasher.add(contentName(task, geometry));
        hasher.add(task.vertexCounts[geometry]);
    }
    return hasher.key();
}

// CACHEABLE_BLAS: everything the entity's triangles and instance are made from - its position, the batch settings
// that shape triangles, each geometry's description and its source vertices - so an equal key means the cached
// entity is exactly what this frame would build, and the vertices need not be converted to find out. Content names
// are left out: nothing traced reads them.
uint64_t cacheableContentKey(const EntitiesBuildTask &task, const EntityInput &entity) {
    KeyHasher hasher;
    hasher.add(entity.x);
    hasher.add(entity.y);
    hasher.add(entity.z);
    hasher.add(entity.rayTracingFlag);
    hasher.add(task.coordinate);
    hasher.add(task.normalOffset);
    hasher.add(task.lineWidth);
    for (int i = 0; i < entity.geometryCount; i++) {
        const size_t geometry = entity.firstGeometry + i;
        const int format = task.vertexFormats[geometry];
        const size_t count = vertexCount(task, geometry);
        hasher.add(task.geometryTypes[geometry]);
        hasher.add(groupName(task, geometry));
        hasher.add(format);
        hasher.add(task.indexFormats[geometry]);
        hasher.add(count);
        // PBR vertices carry their own texture.
        if (format != World::PBR_TRIANGLE) hasher.add(task.geometryTextures[geometry]);
        hasher.addBytes(task.vertices[geometry], count * sourceStride(format));
    }
    return hasher.key();
}

// ---- Vertices in the PBR layout

glm::vec4 unpackColor(uint32_t color) {
    return glm::vec4(color & 0xFF, (color >> 8) & 0xFF, (color >> 16) & 0xFF, (color >> 24) & 0xFF) / 255.0f;
}

glm::ivec2 unpackShorts(uint32_t packed) {
    return glm::ivec2(packed & 0xFFFF, (packed >> 16) & 0xFFFF);
}

glm::vec3 unpackNormal(uint32_t packed) {
    return glm::vec3(static_cast<int8_t>(packed & 0xFF), static_cast<int8_t>((packed >> 8) & 0xFF),
                     static_cast<int8_t>((packed >> 16) & 0xFF));
}

void setColor(PBRVertex &vertex, uint32_t color) {
    vertex.useColorLayer = 1;
    vertex.colorLayer = unpackColor(color);
}

void setTexture(PBRVertex &vertex, glm::vec2 uv) {
    vertex.useTexture = 1;
    vertex.textureUV = uv;
}

void setOverlay(PBRVertex &vertex, uint32_t uv) {
    vertex.useOverlay = 1;
    vertex.overlayUV = unpackShorts(uv);
}

void setLight(PBRVertex &vertex, uint32_t uv) {
    vertex.useLight = 1;
    vertex.lightUV = unpackShorts(uv);
}

void setNormal(PBRVertex &vertex, uint32_t normal) {
    vertex.useNorm = 1;
    vertex.norm = unpackNormal(normal);
}

void readVertex(PBRVertex &vertex, const vf::PositionColorTexLightNormal &source) {
    vertex.pos = source.position;
    setColor(vertex, source.color);
    setTexture(vertex, source.uv0);
    setLight(vertex, source.uv2);
    setNormal(vertex, source.normal);
}

void readVertex(PBRVertex &vertex, const vf::PositionColorTexOverlayLightNormal &source) {
    vertex.pos = source.position;
    setColor(vertex, source.color);
    setTexture(vertex, source.uv0);
    setOverlay(vertex, source.uv1);
    setLight(vertex, source.uv2);
    setNormal(vertex, source.normal);
}

void readVertex(PBRVertex &vertex, const vf::PositionTexColorLight &source) {
    vertex.pos = source.position;
    setTexture(vertex, source.uv0);
    setColor(vertex, source.color);
    setLight(vertex, source.uv2);
}

void readVertex(PBRVertex &vertex, const vf::PositionOnly &source) {
    vertex.pos = source.position;
}

void readVertex(PBRVertex &vertex, const vf::PositionColor &source) {
    vertex.pos = source.position;
    setColor(vertex, source.color);
}

void readVertex(PBRVertex &vertex, const vf::PositionColorNormal &source) {
    vertex.pos = source.position;
    setColor(vertex, source.color);
    setNormal(vertex, source.normal);
}

void readVertex(PBRVertex &vertex, const vf::PositionColorLight &source) {
    vertex.pos = source.position;
    setColor(vertex, source.color);
    setLight(vertex, source.uv2);
}

void readVertex(PBRVertex &vertex, const vf::PositionTex &source) {
    vertex.pos = source.position;
    setTexture(vertex, source.uv);
}

void readVertex(PBRVertex &vertex, const vf::PositionTexColor &source) {
    vertex.pos = source.position;
    setTexture(vertex, source.uv);
    setColor(vertex, source.color);
}

void readVertex(PBRVertex &vertex, const vf::PositionColorTexLight &source) {
    vertex.pos = source.position;
    setColor(vertex, source.color);
    setTexture(vertex, source.uv0);
    setLight(vertex, source.uv2);
}

void readVertex(PBRVertex &vertex, const vf::PositionTexLightColor &source) {
    vertex.pos = source.position;
    setTexture(vertex, source.uv0);
    setLight(vertex, source.uv2);
    setColor(vertex, source.color);
}

void readVertex(PBRVertex &vertex, const vf::PositionTexColorNormal &source) {
    vertex.pos = source.position;
    setTexture(vertex, source.uv0);
    setColor(vertex, source.color);
    setNormal(vertex, source.normal);
}

// PBR vertices are taken as they come; the other layouts carry the geometry's texture.
template <typename Source>
void readVertices(const void *source, size_t count, uint32_t texture, std::vector<PBRVertex> &out) {
    if constexpr (std::is_same_v<Source, PBRVertex>) {
        const auto *vertices = static_cast<const PBRVertex *>(source);
        out.assign(vertices, vertices + count);
    } else {
        out.assign(count, PBRVertex{});
        for (size_t i = 0; i < count; i++) {
            if constexpr (!std::is_void_v<Source>) readVertex(out[i], static_cast<const Source *>(source)[i]);
            out[i].textureID = texture;
        }
    }
}

// ---- Triangles

// What every vertex a geometry keeps is told about where it is.
struct Placement {
    World::Coordinates coordinate;
    bool normalOffset;
    float lineWidth;
};

Placement placementOf(const EntitiesBuildTask &task, const EntityInput &entity) {
    return {
        .coordinate = task.coordinate,
        .normalOffset = task.normalOffset,
        .lineWidth = task.lineWidth,
    };
}

void place(PBRVertex &vertex, const Placement &placement) {
    vertex.coordinate = placement.coordinate;
}

// Working memory for turning one geometry into triangles, reused from geometry to geometry and frame to frame.
struct TriangleScratch {
    std::vector<PBRVertex> vertices;
    std::vector<PBRVertex> assembled;
    std::vector<uint32_t> indices;
};

// Two triangles per complete quad. A trailing partial quad stays unindexed and unplaced: an acceleration structure
// build would read the vertices it lacks, and the device would be lost.
void indexQuads(std::vector<PBRVertex> &vertices,
                std::vector<uint32_t> &indices,
                const Placement &placement,
                bool offsetAlongNormals) {
    const size_t quads = vertices.size() / 4;
    indices.resize(quads * 6);
    for (size_t quad = 0; quad < quads; quad++) {
        const auto first = static_cast<uint32_t>(quad * 4);
        uint32_t *index = indices.data() + quad * 6;
        index[0] = first;
        index[1] = first + 1;
        index[2] = first + 2;
        index[3] = first + 2;
        index[4] = first + 3;
        index[5] = first;
        for (uint32_t corner = first; corner < first + 4; corner++) {
            PBRVertex &vertex = vertices[corner];
            if (offsetAlongNormals && vertex.useNorm) vertex.pos += 0.00001f * glm::normalize(vertex.norm);
            place(vertex, placement);
        }
    }
}

// A strip as a ladder of quads, one for each rung of two vertices after the first: a quad takes its attributes
// from the rung before it and its positions from both rungs.
void assembleStrip(TriangleScratch &scratch, const Placement &placement) {
    const std::vector<PBRVertex> &strip = scratch.vertices;
    std::vector<PBRVertex> &quads = scratch.assembled;
    quads.clear();
    for (size_t rung = 2; rung + 1 < strip.size(); rung += 2) {
        PBRVertex lower = strip[rung - 2];
        PBRVertex upper = strip[rung - 1];
        quads.push_back(lower);
        quads.push_back(upper);
        upper.pos = strip[rung + 1].pos;
        lower.pos = strip[rung].pos;
        quads.push_back(upper);
        quads.push_back(lower);
    }
    scratch.vertices.swap(quads);
    indexQuads(scratch.vertices, scratch.indices, placement, false);
}

constexpr std::array<uint32_t, 36> kBoxIndices = {
    0, 3, 2, 0, 2, 1, // around the start
    4, 5, 6, 4, 6, 7, // around the end
    0, 1, 5, 0, 5, 4, // the four sides
    1, 2, 6, 1, 6, 5, //
    2, 3, 7, 2, 7, 6, //
    3, 0, 4, 3, 4, 7, //
};

// A line segment is traced as a box: a square prism of side `width` around it, reaching half the width past either
// end. The corners around the start take its colour, those around the end the end's. Nothing for a segment of no
// length.
void appendSegmentBox(const PBRVertex &from,
                      const PBRVertex &to,
                      double width,
                      std::vector<PBRVertex> &vertices,
                      std::vector<uint32_t> &indices) {
    if (!(width > 0.0f)) throw std::invalid_argument("entity line width must be positive");
    const glm::dvec3 start = from.pos;
    const glm::dvec3 end = to.pos;
    const glm::dvec3 axis = end - start;
    if (!(glm::length(axis) > 0.0f)) return;

    const glm::dvec3 along = glm::normalize(axis);
    // The box's sides face across the segment from the up direction, or from +z when the segment is close to
    // vertical. The thresholds are single precision, as the boxes have always been built.
    glm::dvec3 reference(0.0, 1.0, 0.0);
    if (std::fabs(along.y) > 0.99f) reference = std::fabs(along.z) < 0.99f ? glm::dvec3(0, 0, 1) : glm::dvec3(0, 1, 0);
    const glm::dvec3 u = glm::normalize(glm::cross(along, reference));
    const glm::dvec3 v = glm::cross(along, u);

    const double half = 0.5f * width;
    const glm::dvec3 beyond = along * 0.5 * width;
    const glm::dvec3 uh = u * half;
    const glm::dvec3 vh = v * half;
    const std::array<glm::dvec3, 8> corners = {
        start - beyond - uh - vh, start - beyond + uh - vh, start - beyond + uh + vh, start - beyond - uh + vh,
        end + beyond - uh - vh,   end + beyond + uh - vh,   end + beyond + uh + vh,   end + beyond - uh + vh,
    };

    const auto base = static_cast<uint32_t>(vertices.size());
    for (size_t k = 0; k < corners.size(); k++) {
        PBRVertex vertex{};
        vertex.pos = corners[k];
        vertex.useColorLayer = 1;
        vertex.colorLayer = k < 4 ? from.colorLayer : to.colorLayer;
        vertices.push_back(vertex);
    }
    for (uint32_t index : kBoxIndices) indices.push_back(base + index);
}

void assembleLines(bool strip, TriangleScratch &scratch, const Placement &placement) {
    const std::vector<PBRVertex> &lines = scratch.vertices;
    std::vector<PBRVertex> &boxes = scratch.assembled;
    boxes.clear();
    scratch.indices.clear();
    const double width = placement.lineWidth;
    if (strip) {
        for (size_t i = 1; i < lines.size(); i++) {
            appendSegmentBox(lines[i - 1], lines[i], width, boxes, scratch.indices);
        }
    } else {
        // Each group of four vertices is traced as the segments 0-1, 2-3 and 2-1.
        for (size_t i = 0; i + 3 < lines.size(); i += 4) {
            appendSegmentBox(lines[i], lines[i + 1], width, boxes, scratch.indices);
            appendSegmentBox(lines[i + 2], lines[i + 3], width, boxes, scratch.indices);
            appendSegmentBox(lines[i + 2], lines[i + 1], width, boxes, scratch.indices);
        }
    }
    scratch.vertices.swap(boxes);
    for (PBRVertex &vertex : scratch.vertices) place(vertex, placement);
}

// Turns one geometry of the task into indexed triangles in the PBR layout, left in scratch.vertices and
// scratch.indices. False when it has none; such geometries are left out of the entity.
bool triangulate(const EntitiesBuildTask &task, size_t geometry, const Placement &placement, TriangleScratch &scratch) {
    const size_t count = vertexCount(task, geometry);
    const void *source = task.vertices[geometry];
    const auto texture = static_cast<uint32_t>(task.geometryTextures[geometry]);
    visitSourceLayout(task.vertexFormats[geometry], [&]<typename Source>(std::type_identity<Source>) {
        readVertices<Source>(source, count, texture, scratch.vertices);
    });

    scratch.indices.clear();
    switch (static_cast<World::DrawMode>(task.indexFormats[geometry])) {
        case World::DrawMode::QUADS:
            indexQuads(scratch.vertices, scratch.indices, placement, placement.normalOffset);
            break;
        case World::DrawMode::TRIANGLE_STRIP: assembleStrip(scratch, placement); break;
        case World::DrawMode::LINE_STRIP: assembleLines(true, scratch, placement); break;
        case World::DrawMode::LINES: assembleLines(false, scratch, placement); break;
        default: throw std::runtime_error("entity geometry in a draw mode that cannot be traced");
    }
    return !scratch.vertices.empty() && !scratch.indices.empty();
}

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
