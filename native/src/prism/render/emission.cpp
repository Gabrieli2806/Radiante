#include "prism/render/emission.hpp"

#include "prism/gpu/vertex.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace {

using PBRVertex = gpu::VertexFormat::PBRVertex;
constexpr int kGrid = Emission::kOccupancyGridSize;

uint64_t hashCombine64(uint64_t seed, uint64_t value) {
    return seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2));
}

// ---- texture space

// A rectangle in texture space with its corners in order.
struct UvRect {
    float minX;
    float minY;
    float maxX;
    float maxY;
};

UvRect orderedRect(const glm::vec2 &a, const glm::vec2 &b) {
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y)};
}

// Edges count: rectangles that only share an edge touch.
bool touches(const UvRect &a, const UvRect &b) {
    return !(a.maxX < b.minX || a.minX > b.maxX || a.maxY < b.minY || a.minY > b.maxY);
}

bool outsideTexture(const UvRect &rect) {
    return rect.maxX < 0.0f || rect.maxY < 0.0f || rect.minX > 1.0f || rect.minY > 1.0f;
}

// The occupancy grid column (or row) a texture coordinate falls in, clamped to the grid; NaN falls in the first.
int gridCoord(float uv) {
    const float cell = std::floor(uv * static_cast<float>(kGrid));
    if (!(cell >= 0.0f)) return 0;
    if (cell >= static_cast<float>(kGrid - 1)) return kGrid - 1;
    return static_cast<int>(cell);
}

struct GridRange {
    int x0;
    int y0;
    int x1;
    int y1;
};

GridRange gridRange(const UvRect &rect) {
    return {gridCoord(rect.minX), gridCoord(rect.minY), gridCoord(rect.maxX), gridCoord(rect.maxY)};
}

} // namespace

// The cells of one texture. Every cell is filed under each occupancy grid cell its rectangle covers (clamped to the
// texture, so cells and queries off its edge still meet), which is all a query needs to find the cells it touches.
struct Emission::TextureState {
    struct StoredCell {
        EmissionCell cell;
        UvRect rect;
        GridRange range;
        // Emissive and inside the texture: it marks the occupancy grid.
        bool marksOccupancy;
    };

    using Bucket = std::vector<const StoredCell *>;

    // Each tile's cells as uploaded. The cells never move once stored: the buckets point at them.
    std::unordered_map<uint64_t, std::vector<StoredCell>> tiles;
    std::vector<Bucket> buckets = std::vector<Bucket>(kOccupancyCellCount);
    // How many emissive cells mark each grid cell, and how many cells mark any.
    std::vector<uint32_t> marks = std::vector<uint32_t>(kOccupancyCellCount, 0u);
    uint32_t markingCells = 0;
    TextureOccupancy occupancy;

    void file(const StoredCell &stored) {
        for (int y = stored.range.y0; y <= stored.range.y1; y++) {
            for (int x = stored.range.x0; x <= stored.range.x1; x++) {
                const size_t bucket = static_cast<size_t>(y * kGrid + x);
                buckets[bucket].push_back(&stored);
                if (stored.marksOccupancy && marks[bucket]++ == 0) occupancy.occupiedCells.set(bucket);
            }
        }
        if (stored.marksOccupancy && markingCells++ == 0) occupancy.hasEmissionCells = true;
    }

    void unfile(const StoredCell &stored) {
        for (int y = stored.range.y0; y <= stored.range.y1; y++) {
            for (int x = stored.range.x0; x <= stored.range.x1; x++) {
                const size_t bucket = static_cast<size_t>(y * kGrid + x);
                auto &list = buckets[bucket];
                list.erase(std::find(list.begin(), list.end(), &stored));
                if (stored.marksOccupancy && --marks[bucket] == 0) occupancy.occupiedCells.reset(bucket);
            }
        }
        if (stored.marksOccupancy && --markingCells == 0) occupancy.hasEmissionCells = false;
    }
};

Emission::Emission(std::weak_ptr<Textures>) {}

Emission::~Emission() = default;

void Emission::reset() {
    std::unique_lock lock(mutex_);
    textures_.clear();
}

void Emission::resetTexture(uint32_t textureID) {
    if (textureID >= static_cast<uint32_t>(kMaxTextures)) return;
    std::unique_lock lock(mutex_);
    textures_.erase(textureID);
}

void Emission::updateTile(uint32_t textureID, uint64_t tileKey, const EmissionCellUpload *cells, int cellCount) {
    if (textureID >= static_cast<uint32_t>(kMaxTextures)) return;

    std::vector<TextureState::StoredCell> stored;
    if (cells != nullptr && cellCount > 0) {
        stored.reserve(static_cast<size_t>(cellCount));
        for (int i = 0; i < cellCount; i++) {
            const EmissionCellUpload &upload = cells[i];
            const EmissionCell cell{
                .uvMin = glm::vec2(upload.u0, upload.v0),
                .uvMax = glm::vec2(upload.u1, upload.v1),
                .avgEmission = std::abs(upload.avgEmission),
                .avgColor = glm::vec3(upload.avgR, upload.avgG, upload.avgB),
                .stableKey = hashCombine64(tileKey, static_cast<uint32_t>(i)),
                .exact = upload.avgEmission < 0.0f,
            };
            const UvRect rect = orderedRect(cell.uvMin, cell.uvMax);
            stored.push_back({cell, rect, gridRange(rect), !(cell.avgEmission <= 0.0f) && !outsideTexture(rect)});
        }
    }

    std::unique_lock lock(mutex_);
    auto &state = textures_[textureID];
    if (state == nullptr) state = std::make_unique<TextureState>();
    if (auto previous = state->tiles.find(tileKey); previous != state->tiles.end()) {
        for (const auto &cell : previous->second) state->unfile(cell);
        state->tiles.erase(previous);
    }
    if (stored.empty()) return;
    for (const auto &cell : state->tiles.emplace(tileKey, std::move(stored)).first->second) state->file(cell);
}

bool Emission::copyTextureOccupancy(uint32_t textureID, TextureOccupancy &occupancy) const {
    occupancy = {};
    if (textureID >= static_cast<uint32_t>(kMaxTextures)) return false;
    std::shared_lock lock(mutex_);
    if (auto state = textures_.find(textureID); state != textures_.end()) occupancy = state->second->occupancy;
    return occupancy.hasEmissionCells;
}

bool Emission::textureOccupancyOverlaps(const TextureOccupancy &occupancy,
                                        const glm::vec2 &uvMin,
                                        const glm::vec2 &uvMax) {
    if (!occupancy.hasEmissionCells) return false;
    const UvRect rect = orderedRect(uvMin, uvMax);
    if (outsideTexture(rect)) return false;
    const GridRange range = gridRange(rect);
    for (int y = range.y0; y <= range.y1; y++) {
        for (int x = range.x0; x <= range.x1; x++) {
            if (occupancy.occupiedCells.test(static_cast<size_t>(y * kGrid + x))) return true;
        }
    }
    return false;
}

void Emission::collectCells(uint32_t textureID,
                            const glm::vec2 &uvMin,
                            const glm::vec2 &uvMax,
                            std::vector<EmissionCell> &out) const {
    out.clear();
    if (textureID >= static_cast<uint32_t>(kMaxTextures)) return;

    std::shared_lock lock(mutex_);
    const auto state = textures_.find(textureID);
    if (state == textures_.end()) return;

    const UvRect query = orderedRect(uvMin, uvMax);
    const GridRange range = gridRange(query);
    for (int y = range.y0; y <= range.y1; y++) {
        for (int x = range.x0; x <= range.x1; x++) {
            for (const TextureState::StoredCell *stored : state->second->buckets[static_cast<size_t>(y * kGrid + x)]) {
                // A cell filed under several of the buckets visited is reported from the first of them only.
                if (x != std::max(stored->range.x0, range.x0) || y != std::max(stored->range.y0, range.y0)) continue;
                if (touches(stored->rect, query)) out.push_back(stored->cell);
            }
        }
    }
}

PackedLightData packLightAt(const LightInfo &light, const glm::vec3 &origin) {
    return {
        .p0Area = glm::vec4(light.p0 + origin, light.area),
        .p1 = glm::vec4(light.p1 + origin, 0.0f),
        .p2 = glm::vec4(light.p2 + origin, 0.0f),
        .p3 = glm::vec4(light.p3 + origin, 0.0f),
        .normal = glm::vec4(light.normal, 0.0f),
        .radiance = glm::vec4(light.radiance, light.kind),
        // The stable id's low and high halves, bit for bit.
        .sourceIDData = glm::vec4(std::bit_cast<float>(static_cast<uint32_t>(light.stableID)),
                                  std::bit_cast<float>(static_cast<uint32_t>(light.stableID >> 32u)), 0.0f, 0.0f),
    };
}

namespace {

// ---- lights cut out of emissive quads

uint64_t quadLightStableId(int64_t ownerId, uint32_t geometryIndex, uint32_t quadIndex, uint64_t cellKey) {
    uint64_t seed = 0xcbf29ce484222325ull;
    seed = hashCombine64(seed, static_cast<uint64_t>(ownerId));
    seed = hashCombine64(seed, geometryIndex);
    seed = hashCombine64(seed, quadIndex);
    return hashCombine64(seed, cellKey);
}

float triangleArea(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c) {
    return 0.5f * glm::length(glm::cross(b - a, c - a));
}

float cross2D(const glm::vec2 &a, const glm::vec2 &b) {
    return a.x * b.y - a.y * b.x;
}

// The colour a vertex tints the glow of its texture with.
glm::vec3 glowTint(const PBRVertex &vertex) {
    if (vertex.useColorLayer == 0) return glm::vec3(1.0f);
    return glm::clamp(glm::vec3(vertex.colorLayer), glm::vec3(0.0f), glm::vec3(1.0f));
}

// A convex polygon in texture space. A clipping pass gives at most two points per point it is given, so the four
// passes that cut a triangle to a rectangle stay within 3 * 2^4 points.
struct UvPolygon {
    std::array<glm::vec2, 48> points;
    size_t count = 0;
};

// Keeps the part of `in` where coordinate `axis` is at least (keepGreater) or at most `value` (Sutherland-Hodgman).
void clipPolygon(const UvPolygon &in, UvPolygon &out, int axis, float value, bool keepGreater) {
    out.count = 0;
    if (in.count == 0) return;
    const auto inside = [&](const glm::vec2 &point) {
        return keepGreater ? point[axis] >= value : point[axis] <= value;
    };

    glm::vec2 previous = in.points[in.count - 1];
    bool previousInside = inside(previous);
    for (size_t i = 0; i < in.count; i++) {
        const glm::vec2 current = in.points[i];
        const bool currentInside = inside(current);
        if (currentInside != previousInside) {
            const float delta = current[axis] - previous[axis];
            out.points[out.count++] =
                std::abs(delta) <= 1e-8f ?
                    previous :
                    glm::mix(previous, current, std::clamp((value - previous[axis]) / delta, 0.0f, 1.0f));
        }
        if (currentInside) out.points[out.count++] = current;
        previous = current;
        previousInside = currentInside;
    }
}

// The part of a texture space triangle inside the rectangle from rectMin to rectMax, into `result`.
void clipTriangleToRect(const std::array<glm::vec2, 3> &triangle,
                        const glm::vec2 &rectMin,
                        const glm::vec2 &rectMax,
                        UvPolygon &result,
                        UvPolygon &scratch) {
    std::copy(triangle.begin(), triangle.end(), result.points.begin());
    result.count = triangle.size();
    clipPolygon(result, scratch, 0, rectMin.x, true);
    clipPolygon(scratch, result, 0, rectMax.x, false);
    clipPolygon(result, scratch, 1, rectMin.y, true);
    clipPolygon(scratch, result, 1, rectMax.y, false);
}

// One triangle of a quad, mapping a point of its texture to the position and the tint at that point.
struct GlowTriangle {
    std::array<glm::vec2, 3> uv;
    std::array<glm::vec3, 3> position;
    std::array<glm::vec3, 3> tint;
    glm::vec2 edge0;
    glm::vec2 edge1;
    float denominator;

    GlowTriangle(const PBRVertex &a, const PBRVertex &b, const PBRVertex &c)
        : uv{a.textureUV, b.textureUV, c.textureUV},
          position{a.pos, b.pos, c.pos},
          tint{glowTint(a), glowTint(b), glowTint(c)},
          edge0(b.textureUV - a.textureUV),
          edge1(c.textureUV - a.textureUV),
          denominator(cross2D(edge0, edge1)) {}

    // Its texture coordinates span no area: no point of the texture maps onto it.
    bool degenerate() const {
        return std::abs(denominator) <= 1e-8f;
    }

    glm::vec3 barycentric(const glm::vec2 &point) const {
        const glm::vec2 relative = point - uv[0];
        const float b1 = cross2D(relative, edge1) / denominator;
        const float b2 = cross2D(edge0, relative) / denominator;
        return glm::vec3(1.0f - b1 - b2, b1, b2);
    }

    glm::vec3 positionAt(const glm::vec3 &weights) const {
        return position[0] * weights.x + position[1] * weights.y + position[2] * weights.z;
    }

    glm::vec3 tintAt(const glm::vec3 &weights) const {
        return tint[0] * weights.x + tint[1] * weights.y + tint[2] * weights.z;
    }
};

} // namespace

void collectEmissiveQuadLights(const Emission &emission,
                               const std::vector<gpu::VertexFormat::PBRVertex> &geometryVertices,
                               int64_t ownerId,
                               uint32_t geometryIndex,
                               std::unordered_map<uint32_t, Emission::TextureOccupancy> &textureOccupancyCache,
                               std::vector<LightInfo> &lightInfos) {
    thread_local std::vector<EmissionCell> cells;
    UvPolygon piece;
    UvPolygon scratch;

    uint32_t quadIndex = 0;
    for (size_t first = 0; first + 3 < geometryVertices.size(); first += 4, quadIndex++) {
        const PBRVertex &v0 = geometryVertices[first];
        const PBRVertex &v1 = geometryVertices[first + 1];
        const PBRVertex &v2 = geometryVertices[first + 2];
        const PBRVertex &v3 = geometryVertices[first + 3];

        if ((v0.alphaMode & gpu::Vertex::alphaModePointLightFlag) != 0u) {
            lightInfos.push_back(LightInfo{
                .p0 = v0.pos,
                .p1 = v0.pos,
                .p2 = v0.pos,
                .p3 = v0.pos,
                .normal = glm::vec3(v0.norm.x, 0.0f, 0.0f),
                .radiance = glm::vec3(v0.colorLayer) * v0.albedoEmission,
                .area = 1.0f,
                .kind = 2.0f,
                .textureID = v0.textureID,
                .stableID = quadLightStableId(ownerId, geometryIndex, quadIndex, 0u),
            });
            continue;
        }
        // A glow given per vertex that casts light too (powered redstone dust, RedstoneGlow.java): the quad's two
        // triangles are lights of their own, of the dust's colour times the light Java worked out for the quad from
        // its power and how much of it the sprite covers. Its texture's emission (it is unlit) plays no part.
        if ((v0.alphaMode & gpu::Vertex::alphaModeGlowLightFlag) != 0u) {
            const float cast = v0.postBase.y;
            if (!(cast > 0.0f)) continue;
            // postBase.z carries the light's colour as 0xRRGGBB (RedstoneGlow.castColor): dust is drawn with sprites
            // already coloured per power and no tint, so the vertex colour would cast white light.
            const uint32_t packed = std::bit_cast<uint32_t>(v0.postBase.z);
            const glm::vec3 tint = glm::vec3(float((packed >> 16u) & 0xFFu), float((packed >> 8u) & 0xFFu),
                                             float(packed & 0xFFu)) / 255.0f;
            const std::array<std::array<const PBRVertex *, 3>, 2> triangles = {{{&v0, &v1, &v2}, {&v0, &v2, &v3}}};
            uint64_t pieceIndex = 0;
            for (const auto &triangle : triangles) {
                const glm::vec3 &p0 = triangle[0]->pos;
                const glm::vec3 &p1 = triangle[1]->pos;
                const glm::vec3 &p2 = triangle[2]->pos;
                const float area = triangleArea(p0, p1, p2);
                if (area <= 1e-6f) continue;
                lightInfos.push_back(LightInfo{
                    .p0 = p0,
                    .p1 = p1,
                    .p2 = p2,
                    .p3 = p2,
                    .normal = glm::normalize(glm::cross(p1 - p0, p2 - p0)),
                    .radiance = tint * cast,
                    .area = area,
                    .kind = 0.0f,
                    .textureID = v0.textureID,
                    .stableID = hashCombine64(quadLightStableId(ownerId, geometryIndex, quadIndex, 0u), pieceIndex++),
                });
            }
            continue;
        }
        if (v0.useTexture == 0 || v1.useTexture == 0 || v2.useTexture == 0 || v3.useTexture == 0) continue;
        // A light block in a state that gives no light (unlit redstone ore) draws the sprite its lit state glows
        // with; it would otherwise cast light from it everywhere.
        if ((v0.alphaMode & gpu::Vertex::alphaModeUnlitFlag) != 0u) continue;

        const std::array<glm::vec2, 4> quadUv = {v0.textureUV, v1.textureUV, v2.textureUV, v3.textureUV};
        glm::vec2 uvMin(std::numeric_limits<float>::max());
        glm::vec2 uvMax(std::numeric_limits<float>::lowest());
        for (const glm::vec2 &uv : quadUv) {
            uvMin = glm::min(uvMin, uv);
            uvMax = glm::max(uvMax, uv);
        }
        const glm::vec2 uvSize = uvMax - uvMin;
        if (uvSize.x <= 1e-6f || uvSize.y <= 1e-6f) continue;

        // Most quads have no emissive texels at all: they are skipped without locking or searching the cells.
        auto [occupancy, inserted] = textureOccupancyCache.try_emplace(v0.textureID);
        if (inserted) emission.copyTextureOccupancy(v0.textureID, occupancy->second);
        if (!Emission::textureOccupancyOverlaps(occupancy->second, uvMin, uvMax)) continue;

        const glm::vec3 summedNormal =
            glm::cross(v1.pos - v0.pos, v2.pos - v0.pos) + glm::cross(v2.pos - v0.pos, v3.pos - v0.pos);
        const float summedNormalLength = glm::length(summedNormal);
        if (summedNormalLength <= 1e-6f) continue;
        const glm::vec3 quadNormal = summedNormal / summedNormalLength;
        if (triangleArea(v0.pos, v1.pos, v2.pos) + triangleArea(v0.pos, v2.pos, v3.pos) <= 1e-6f) continue;

        emission.collectCells(v0.textureID, uvMin, uvMax, cells);
        if (cells.empty()) continue;

        const std::array<GlowTriangle, 2> triangles = {GlowTriangle(v0, v1, v2), GlowTriangle(v0, v2, v3)};
        for (const EmissionCell &cell : cells) {
            const glm::vec2 overlapMin = glm::max(uvMin, cell.uvMin);
            const glm::vec2 overlapMax = glm::min(uvMax, cell.uvMax);
            const glm::vec2 overlapSize = overlapMax - overlapMin;
            if (overlapSize.x <= 1e-6f || overlapSize.y <= 1e-6f || cell.avgEmission <= 0.0f) continue;

            // Each piece of the cell on the quad is a light of its own: a fan of triangles over the part of each
            // triangle of the quad that the cell covers.
            const uint64_t cellStableId = quadLightStableId(ownerId, geometryIndex, quadIndex, cell.stableKey);
            uint64_t pieceIndex = 0;
            for (const GlowTriangle &triangle : triangles) {
                if (triangle.degenerate()) continue;
                clipTriangleToRect(triangle.uv, overlapMin, overlapMax, piece, scratch);
                if (piece.count < 3) continue;

                const glm::vec3 originWeights = triangle.barycentric(piece.points[0]);
                const glm::vec3 origin = triangle.positionAt(originWeights);
                const glm::vec3 originTint = triangle.tintAt(originWeights);
                for (size_t k = 1; k + 1 < piece.count; k++) {
                    const glm::vec3 weights1 = triangle.barycentric(piece.points[k]);
                    const glm::vec3 weights2 = triangle.barycentric(piece.points[k + 1]);
                    const glm::vec3 p1 = triangle.positionAt(weights1);
                    const glm::vec3 p2 = triangle.positionAt(weights2);

                    const float area = triangleArea(origin, p1, p2);
                    if (area <= 1e-6f) continue;

                    glm::vec3 normal = glm::cross(p1 - origin, p2 - origin);
                    const float normalLength = glm::length(normal);
                    normal = normalLength > 1e-6f ? normal / normalLength : quadNormal;
                    const glm::vec3 tint = glm::max(
                        (originTint + triangle.tintAt(weights1) + triangle.tintAt(weights2)) / 3.0f, glm::vec3(0.0f));

                    lightInfos.push_back(LightInfo{
                        .p0 = origin,
                        .p1 = p1,
                        .p2 = p2,
                        .p3 = p2,
                        .normal = normal,
                        .radiance = cell.avgColor * tint * cell.avgEmission,
                        .area = area,
                        .kind = cell.exact ? 1.0f : 0.0f,
                        .textureID = v0.textureID,
                        .stableID = hashCombine64(cellStableId, pieceIndex++),
                    });
                }
            }
        }
    }
}
