#include "prism/render/entity_geometry.hpp"

#include "prism/render/scene_stats.hpp"
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

namespace entity_geometry {

namespace {

namespace vf = gpu::VertexFormat;

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

} // namespace

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

namespace {

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

} // namespace

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

namespace {

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

} // namespace

Placement placementOf(const EntitiesBuildTask &task, const EntityInput &entity) {
    return {
        .coordinate = task.coordinate,
        .normalOffset = task.normalOffset,
        .lineWidth = task.lineWidth,
    };
}

void place(PBRVertex &vertex, const Placement &placement) {
    vertex.coordinate = (vertex.coordinate & ~0xFu) | static_cast<uint32_t>(placement.coordinate);
}

namespace {

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

} // namespace

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

} // namespace entity_geometry
