#include "prism/render/opacity_micromaps.hpp"

#include "prism/util/logging.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <optional>

namespace omm {
namespace {

using PBRVertex = gpu::VertexFormat::PBRVertex;
using Vertex = gpu::Vertex;

// The deepest level baked. A vanilla block face is 16 texels a side and needs level 4; level 6 keeps 64-texel packs
// describable, and larger packs' triangles, whose micro-triangles straddle texels, keep the shader.
constexpr uint32_t kMaxLevel = 6;
// How far, in texels, a micro-triangle has to reach into a texel to count as covering it. Block faces put micro-
// triangle edges exactly on texel edges, give or take the rounding of their texture coordinates.
constexpr double kOverlapEpsilon = 1.0e-3;
// Baked triangles kept before the cache starts over (a pack with unusually many distinct cut-out faces).
constexpr size_t kMaxCachedTriangles = 1u << 18;
// Mip levels kept; the atlas has at most Minecraft's mipmap setting plus one.
constexpr uint32_t kMaxMipLevels = 8;

// Mirrors util/alpha_mode.glsl: the cut-out mode, and the surface flags whose any-hit behaviour is more than the alpha
// test (water, rain, held items, glass and panes, ice/slime/honey, lava, light markers).
constexpr uint32_t kAlphaModeCutout = 1u;
constexpr uint32_t kSpecialSurfaces = Vertex::alphaModeWaterFlag | Vertex::alphaModeRainFlag |
                                      Vertex::alphaModeHeldFlag | Vertex::alphaModeGlassFlag |
                                      Vertex::alphaModePaneFlag | Vertex::alphaModeBlendFlag |
                                      Vertex::alphaModeLavaFlag | Vertex::alphaModePointLightFlag;

std::ostream &micromapLog() {
    return radiante::out() << "[Micromaps] ";
}

// ---- the micro-triangle layout

uint32_t evenBits(uint32_t x) {
    x &= 0x55555555u;
    x = (x | (x >> 1)) & 0x33333333u;
    x = (x | (x >> 2)) & 0x0f0f0f0fu;
    x = (x | (x >> 4)) & 0x00ff00ffu;
    x = (x | (x >> 8)) & 0x0000ffffu;
    return x;
}

// Each bit becomes the exclusive or of itself and every bit above it.
uint32_t prefixXor(uint32_t x) {
    x ^= x >> 1;
    x ^= x >> 2;
    x ^= x >> 4;
    x ^= x >> 8;
    return x;
}

// ---- texel space geometry

// Whether a triangle and the square of side `side` at (x, y) share more than a sliver: separated along no axis of
// either.
bool overlaps(const glm::dvec2 t[3], double x, double y, double side) {
    const double epsilon = kOverlapEpsilon * side;
    const double minX = std::min({t[0].x, t[1].x, t[2].x});
    const double maxX = std::max({t[0].x, t[1].x, t[2].x});
    const double minY = std::min({t[0].y, t[1].y, t[2].y});
    const double maxY = std::max({t[0].y, t[1].y, t[2].y});
    if (std::min(maxX, x + side) - std::max(minX, x) <= epsilon) return false;
    if (std::min(maxY, y + side) - std::max(minY, y) <= epsilon) return false;
    const glm::dvec2 square[4] = {{x, y}, {x + side, y}, {x, y + side}, {x + side, y + side}};
    for (int e = 0; e < 3; e++) {
        const glm::dvec2 edge = t[(e + 1) % 3] - t[e];
        const double length = glm::length(edge);
        if (!(length > 1e-12)) return false;
        const glm::dvec2 normal(edge.y / length, -edge.x / length);
        double triangleMin = INFINITY, triangleMax = -INFINITY, squareMin = INFINITY, squareMax = -INFINITY;
        for (int i = 0; i < 3; i++) {
            const double d = glm::dot(normal, t[i]);
            triangleMin = std::min(triangleMin, d);
            triangleMax = std::max(triangleMax, d);
        }
        for (const glm::dvec2 &corner : square) {
            const double d = glm::dot(normal, corner);
            squareMin = std::min(squareMin, d);
            squareMax = std::max(squareMax, d);
        }
        if (std::min(triangleMax, squareMax) - std::max(triangleMin, squareMin) <= epsilon) return false;
    }
    return true;
}

// A triangle whose any-hit test is the plain alpha test on the atlas, and whose vertex colour leaves it alone.
bool eligible(const PBRVertex &vertex, uint32_t textureId) {
    if ((vertex.alphaMode & 0xFu) != kAlphaModeCutout || (vertex.alphaMode & kSpecialSurfaces) != 0) return false;
    if (vertex.useTexture == 0 || (vertex.textureID & 0xFFFFu) != textureId) return false;
    // The shaders multiply the texel's alpha by the vertex colour's (stored as eight bits): only a full one leaves the
    // texel's answer alone.
    if (vertex.useColorLayer != 0 && std::lround(std::clamp(vertex.colorLayer.a, 0.0f, 1.0f) * 255.0f) != 255) {
        return false;
    }
    return true;
}

size_t alignUp(size_t value, size_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

void countUsage(std::vector<VkMicromapUsageEXT> &usage, uint32_t level, uint32_t format) {
    for (VkMicromapUsageEXT &entry : usage) {
        if (entry.subdivisionLevel == level && entry.format == format) {
            entry.count++;
            return;
        }
    }
    usage.push_back({1, level, format});
}

bool bitAt(const std::vector<uint64_t> &bits, uint64_t bit) {
    return (bits[bit >> 6] >> (bit & 63u)) & 1u;
}

void setBit(std::vector<uint64_t> &bits, uint64_t bit, bool value) {
    if (value) {
        bits[bit >> 6] |= 1ull << (bit & 63u);
    } else {
        bits[bit >> 6] &= ~(1ull << (bit & 63u));
    }
}

// ---- the published coverage

struct Pending {
    uint32_t textureId = 0;
    std::vector<CoverageAtlas::Level> levels;
};

std::mutex g_mutex;
std::optional<Pending> g_pending;
std::shared_ptr<CoverageAtlas> g_current;
// The id begin() was last called for, so uploads into every other texture return at once.
std::atomic<uint32_t> g_watched{~0u};
// Detailed Distant Foliage, as the shader pack's attribute last said (on by default, as in the pack).
std::atomic<bool> g_detailed{true};

} // namespace

Stats &stats() {
    static Stats instance;
    return instance;
}

void microTriangleCorners(uint32_t index, uint32_t level, glm::vec2 corners[3]) {
    if (level == 0) {
        corners[0] = {0.0f, 0.0f};
        corners[1] = {1.0f, 0.0f};
        corners[2] = {0.0f, 1.0f};
        return;
    }
    // The discrete barycentric coordinates of the micro-triangle along the curve: two bits of the index per level,
    // turned into where it lies and which way it points.
    const uint32_t b0 = evenBits(index);
    const uint32_t b1 = evenBits(index >> 1);
    const uint32_t fx = prefixXor(b0);
    const uint32_t fy = prefixXor(b0 & ~b1);
    const uint32_t t = fy ^ b1;
    const uint32_t mask = (1u << level) - 1u;
    uint32_t u = ((fx & ~t) | (b0 & ~t) | (~b0 & ~fx & t)) & mask;
    uint32_t v = (fy ^ b0) & mask;
    const uint32_t w = ((~fx & ~t) | (b0 & ~t) | (~b0 & fx & t)) & mask;

    const bool upright = ((u ^ v ^ w) & 1u) != 0;
    if (!upright) {
        u += 1;
        v += 1;
    }
    const float scale = 1.0f / static_cast<float>(1u << level);
    const float step = upright ? scale : -scale;
    const glm::vec2 origin(static_cast<float>(u) * scale, static_cast<float>(v) * scale);
    corners[0] = origin;
    corners[1] = origin + glm::vec2(step, 0.0f);
    corners[2] = origin + glm::vec2(0.0f, step);
}

// ---- CoverageAtlas

CoverageAtlas::CoverageAtlas(uint32_t textureId, std::vector<Level> levels, std::vector<uint64_t> animated)
    : textureId_(textureId), levels_(std::move(levels)), animated_(std::move(animated)) {}

bool CoverageAtlas::Key::operator==(const Key &other) const {
    return std::memcmp(bits, other.bits, sizeof(bits)) == 0 && maxLevel == other.maxLevel;
}

size_t CoverageAtlas::KeyHash::operator()(const Key &key) const {
    uint64_t hash = 1469598103934665603ull ^ key.maxLevel;
    for (uint32_t word : key.bits) {
        hash ^= word;
        hash *= 1099511628211ull;
    }
    return static_cast<size_t>(hash ^ (hash >> 29));
}

bool CoverageAtlas::opaqueAt(uint32_t mipLevel, int64_t x, int64_t y) const {
    const Level &level = levels_[mipLevel];
    // The sampler repeats.
    x = ((x % level.width) + level.width) % level.width;
    y = ((y % level.height) + level.height) % level.height;
    return bitAt(level.opaque, static_cast<uint64_t>(y) * level.width + static_cast<uint64_t>(x));
}

bool CoverageAtlas::animatedAt(int64_t x, int64_t y) const {
    const Level &level = levels_[0];
    x = ((x % level.width) + level.width) % level.width;
    y = ((y % level.height) + level.height) % level.height;
    return bitAt(animated_, static_cast<uint64_t>(y) * level.width + static_cast<uint64_t>(x));
}

bool CoverageAtlas::animatedOnly(int64_t x, int64_t y, uint32_t width, uint32_t height) const {
    for (int64_t row = y; row < y + static_cast<int64_t>(height); row++) {
        for (int64_t column = x; column < x + static_cast<int64_t>(width); column++) {
            if (!animatedAt(column, row)) return false;
        }
    }
    return true;
}

CoverageAtlas::Coverage CoverageAtlas::cover(const glm::dvec2 corners[3], uint32_t mipLevel) const {
    // In this level's texels, which halve with each level (the shaders read it with nearest filtering).
    const double scale = 1.0 / static_cast<double>(1u << mipLevel);
    const glm::dvec2 t[3] = {corners[0] * scale, corners[1] * scale, corners[2] * scale};
    const double minX = std::min({t[0].x, t[1].x, t[2].x});
    const double maxX = std::max({t[0].x, t[1].x, t[2].x});
    const double minY = std::min({t[0].y, t[1].y, t[2].y});
    const double maxY = std::max({t[0].y, t[1].y, t[2].y});
    const int64_t x0 = static_cast<int64_t>(std::floor(minX + kOverlapEpsilon));
    const int64_t x1 = std::max(static_cast<int64_t>(std::ceil(maxX - kOverlapEpsilon)), x0 + 1);
    const int64_t y0 = static_cast<int64_t>(std::floor(minY + kOverlapEpsilon));
    const int64_t y1 = std::max(static_cast<int64_t>(std::ceil(maxY - kOverlapEpsilon)), y0 + 1);
    Coverage coverage;
    for (int64_t y = y0; y < y1; y++) {
        for (int64_t x = x0; x < x1; x++) {
            if (!overlaps(t, static_cast<double>(x), static_cast<double>(y), 1.0)) continue;
            if (mipLevel == 0 && animatedAt(x, y)) coverage.animated = true;
            if (opaqueAt(mipLevel, x, y)) {
                coverage.opaque = true;
            } else {
                coverage.transparent = true;
            }
        }
    }
    return coverage;
}

BakedTriangle CoverageAtlas::bakeUncached(const glm::vec2 uv[3], uint32_t maxLevel, bool detailed) const {
    BakedTriangle baked;
    const Level &full = levels_[0];
    const glm::dvec2 size(static_cast<double>(full.width), static_cast<double>(full.height));
    const glm::dvec2 p[3] = {glm::dvec2(uv[0]) * size, glm::dvec2(uv[1]) * size, glm::dvec2(uv[2]) * size};
    const glm::dvec2 e1 = p[1] - p[0];
    const glm::dvec2 e2 = p[2] - p[0];
    const glm::dvec2 e3 = p[2] - p[1];

    // A triangle with (almost) no area in the texture reads texels along a line or a point: the shaders decide it.
    const double area = std::abs(e1.x * e2.y - e1.y * e2.x);
    const double extent = std::max({std::abs(e1.x), std::abs(e1.y), std::abs(e2.x), std::abs(e2.y), std::abs(e3.x),
                                    std::abs(e3.y)});
    if (!(area > 1e-4) || !std::isfinite(extent) || !std::isfinite(p[0].x) || !std::isfinite(p[0].y)) {
        baked.reason = Stats::kNoArea;
        return baked;
    }

    // Deep enough that every micro-triangle spans at most a texel along u and v; for a face whose sides run along
    // them, each then lies within one texel.
    uint32_t level = 0;
    while (level < std::min(maxLevel, kMaxLevel) && extent / static_cast<double>(1u << level) > 1.0 + kOverlapEpsilon) {
        level++;
    }

    const uint32_t count = 1u << (2 * level);
    std::vector<uint8_t> states(count);
    uint32_t known[2] = {0, 0};
    uint32_t hinted[2] = {0, 0};
    for (uint32_t i = 0; i < count; i++) {
        glm::vec2 bary[3];
        microTriangleCorners(i, level, bary);
        glm::dvec2 corners[3];
        for (int k = 0; k < 3; k++) corners[k] = p[0] + static_cast<double>(bary[k].x) * e1 + static_cast<double>(bary[k].y) * e2;

        // The full-size answer, which shadow rays read: one answer, or the triangle cannot take a micromap.
        const Coverage fullSize = cover(corners, 0);
        if (fullSize.animated || fullSize.opaque == fullSize.transparent) {
            baked.reason = fullSize.animated ? Stats::kAnimated : Stats::kStraddles;
            return baked;
        }
        const bool opaque = fullSize.opaque;
        // Camera rays read the full size too with Detailed distant foliage; otherwise the answer is known only where
        // every mip level they may read gives the same one.
        bool consistent = true;
        for (uint32_t mip = 1; !detailed && mip < levels_.size() && consistent; mip++) {
            const Coverage atMip = cover(corners, mip);
            consistent = opaque ? !atMip.transparent : !atMip.opaque;
        }
        if (consistent) {
            states[i] = opaque ? kOpaque : kTransparent;
            known[opaque ? 1 : 0]++;
        } else {
            states[i] = opaque ? kUnknownOpaque : kUnknownTransparent;
            hinted[opaque ? 1 : 0]++;
        }
    }

    baked.describable = true;
    baked.level = static_cast<uint16_t>(level);
    baked.unknown = hinted[0] + hinted[1];
    if (known[1] == count) {
        baked.special = VK_OPACITY_MICROMAP_SPECIAL_INDEX_FULLY_OPAQUE_EXT;
    } else if (known[0] == count) {
        baked.special = VK_OPACITY_MICROMAP_SPECIAL_INDEX_FULLY_TRANSPARENT_EXT;
    } else if (hinted[1] == count) {
        baked.special = VK_OPACITY_MICROMAP_SPECIAL_INDEX_FULLY_UNKNOWN_OPAQUE_EXT;
    } else if (hinted[0] == count) {
        baked.special = VK_OPACITY_MICROMAP_SPECIAL_INDEX_FULLY_UNKNOWN_TRANSPARENT_EXT;
    } else if (baked.unknown == 0) {
        // Packed from the lowest bit of the first byte on: one bit per micro-triangle when every state is known, two
        // otherwise.
        baked.format = VK_OPACITY_MICROMAP_FORMAT_2_STATE_EXT;
        baked.states.assign((count + 7) / 8, 0);
        for (uint32_t i = 0; i < count; i++) {
            if (states[i] == kOpaque) baked.states[i >> 3] |= static_cast<uint8_t>(1u << (i & 7u));
        }
    } else {
        baked.format = VK_OPACITY_MICROMAP_FORMAT_4_STATE_EXT;
        baked.states.assign((count + 3) / 4, 0);
        for (uint32_t i = 0; i < count; i++) {
            baked.states[i >> 2] |= static_cast<uint8_t>(states[i] << ((i & 3u) * 2));
        }
    }
    return baked;
}

std::shared_ptr<const BakedTriangle> CoverageAtlas::bake(const glm::vec2 uv[3], uint32_t maxLevel) {
    Key key{};
    std::memcpy(key.bits, uv, sizeof(key.bits));
    const bool detailedNow = coverage::detailed();
    key.maxLevel = maxLevel | (detailedNow ? 0x80000000u : 0u);
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        if (auto found = cache_.find(key); found != cache_.end()) return found->second;
    }
    auto baked = std::make_shared<const BakedTriangle>(bakeUncached(uv, maxLevel, detailedNow));
    std::lock_guard<std::mutex> lock(cacheMutex_);
    if (cache_.size() >= kMaxCachedTriangles) cache_.clear();
    return cache_.emplace(key, std::move(baked)).first->second;
}

// ---- coverage

namespace coverage {

void begin(uint32_t textureId, uint32_t width, uint32_t height) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_current != nullptr && g_current->textureId() == textureId) g_current = nullptr;
    if (width == 0 || height == 0) {
        g_pending.reset();
        g_watched = ~0u;
        return;
    }
    Pending pending;
    pending.textureId = textureId;
    for (uint32_t mip = 0; mip < kMaxMipLevels && (width >> mip) > 0 && (height >> mip) > 0; mip++) {
        CoverageAtlas::Level &level = pending.levels.emplace_back();
        level.width = width >> mip;
        level.height = height >> mip;
        level.opaque.assign((static_cast<size_t>(level.width) * level.height + 63) / 64, 0);
    }
    g_pending = std::move(pending);
    g_watched = textureId;
}

void upload(uint32_t textureId,
            uint32_t mipLevel,
            const uint8_t *firstTexel,
            size_t rowPixels,
            int32_t dstX,
            int32_t dstY,
            uint32_t width,
            uint32_t height) {
    if (textureId != g_watched.load(std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_pending.has_value() && g_pending->textureId == textureId) {
        if (mipLevel >= g_pending->levels.size() || dstX < 0 || dstY < 0) return;
        CoverageAtlas::Level &level = g_pending->levels[mipLevel];
        level.written = true;
        for (uint32_t row = 0; row < height; row++) {
            const uint8_t *texel = firstTexel + static_cast<size_t>(row) * rowPixels * 4;
            const uint64_t y = static_cast<uint64_t>(dstY) + row;
            if (y >= level.height) break;
            for (uint32_t column = 0; column < width; column++, texel += 4) {
                const uint64_t x = static_cast<uint64_t>(dstX) + column;
                if (x >= level.width) break;
                // CUTOUT_ALPHA_THRESHOLD: a/255 >= 0.5.
                setBit(level.opaque, y * level.width + x, texel[3] >= 128);
            }
        }
        return;
    }
    if (g_current != nullptr && g_current->textureId() == textureId) {
        // Animation frames are left to the shaders anyway (their mip levels too, which cover the same sprites).
        if (g_current->animatedOnly(static_cast<int64_t>(dstX) << mipLevel, static_cast<int64_t>(dstY) << mipLevel,
                                    width << mipLevel, height << mipLevel)) {
            return;
        }
        // Anything else written into the atlas after it was mirrored would leave the micromaps wrong.
        micromapLog() << "texture " << textureId << " was written to after its coverage was taken (level "
                      << mipLevel << ", " << width << "x" << height << " at " << dstX << "," << dstY
                      << "); cut-out geometry built from now on goes without micromaps" << std::endl;
        g_current = nullptr;
        g_watched = ~0u;
    }
}

void finish(uint32_t textureId, std::span<const int32_t> animatedRects) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_pending.has_value() || g_pending->textureId != textureId) return;
    Pending pending = std::move(*g_pending);
    g_pending.reset();
    // The levels the atlas has are the ones Java filled (Minecraft's mipmap setting plus one).
    const auto missing = std::find_if(pending.levels.begin(), pending.levels.end(),
                                      [](const CoverageAtlas::Level &level) { return !level.written; });
    pending.levels.erase(missing, pending.levels.end());
    if (pending.levels.empty()) {
        g_watched = ~0u;
        return;
    }

    const CoverageAtlas::Level &full = pending.levels[0];
    std::vector<uint64_t> animated(full.opaque.size(), 0);
    size_t animatedSprites = 0;
    for (size_t i = 0; i + 3 < animatedRects.size(); i += 4) {
        // With the texel of border round each that the atlas repeats a sprite's edge into (AtlasMirror).
        const int64_t x0 = std::max<int64_t>(static_cast<int64_t>(animatedRects[i]) - 1, 0);
        const int64_t y0 = std::max<int64_t>(static_cast<int64_t>(animatedRects[i + 1]) - 1, 0);
        const int64_t x1 =
            std::min<int64_t>(static_cast<int64_t>(animatedRects[i]) + animatedRects[i + 2] + 1, full.width);
        const int64_t y1 =
            std::min<int64_t>(static_cast<int64_t>(animatedRects[i + 1]) + animatedRects[i + 3] + 1, full.height);
        for (int64_t y = y0; y < y1; y++) {
            for (int64_t x = x0; x < x1; x++) setBit(animated, static_cast<uint64_t>(y) * full.width + x, true);
        }
        animatedSprites++;
    }

    size_t opaqueTexels = 0;
    for (uint64_t word : full.opaque) opaqueTexels += static_cast<size_t>(std::popcount(word));
    micromapLog() << "coverage of texture " << textureId << " (" << full.width << "x" << full.height << ", "
                  << pending.levels.size() << " levels) taken: " << opaqueTexels << " opaque texels, "
                  << animatedSprites << " animated sprites left to the shaders" << std::endl;
    g_current = CoverageAtlas::create(textureId, std::move(pending.levels), std::move(animated));
    g_watched = textureId;
}

void forget(uint32_t textureId) {
    if (textureId != g_watched.load(std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_pending.has_value() && g_pending->textureId == textureId) return; // begun for this very image
    if (g_current != nullptr && g_current->textureId() == textureId) g_current = nullptr;
    g_watched = ~0u;
}

std::shared_ptr<CoverageAtlas> current() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_current;
}

void setDetailed(bool detailed) {
    g_detailed.store(detailed, std::memory_order_relaxed);
}

bool detailed() {
    return g_detailed.load(std::memory_order_relaxed);
}

} // namespace coverage

// ---- SectionMicromaps

size_t SectionMicromaps::layout(size_t offset) {
    // The micromap build reads its data and triangle array from 256-byte aligned addresses.
    statesOffset = alignUp(offset, 256);
    trianglesOffset = alignUp(statesOffset + states.size(), 256);
    size_t end = trianglesOffset + triangles.size() * sizeof(VkMicromapTriangleEXT);
    for (Geometry &geometry : geometries) {
        if (!geometry.used) continue;
        geometry.indexOffset = alignUp(end, 8);
        end = geometry.indexOffset + geometry.indices.size() * sizeof(int32_t);
    }
    return end;
}

void SectionMicromaps::write(uint8_t *buffer) const {
    std::memcpy(buffer + statesOffset, states.data(), states.size());
    std::memcpy(buffer + trianglesOffset, triangles.data(), triangles.size() * sizeof(VkMicromapTriangleEXT));
    for (const Geometry &geometry : geometries) {
        if (!geometry.used) continue;
        std::memcpy(buffer + geometry.indexOffset, geometry.indices.data(), geometry.indices.size() * sizeof(int32_t));
    }
}

std::unique_ptr<SectionMicromaps> bakeSection(
    std::vector<std::vector<PBRVertex>> &geometries,
    const std::vector<bool> &cutout,
    uint32_t maxLevel,
    const std::function<void(size_t source, std::vector<PBRVertex> &&quads)> &split) {
    std::shared_ptr<CoverageAtlas> atlas = coverage::current();
    if (atlas == nullptr) return nullptr;
    const uint32_t textureId = atlas->textureId();
    // The order putQuadIndices writes a quad's triangles in: 0 1 2, then 2 3 0.
    static constexpr int kCorners[2][3] = {{0, 1, 2}, {2, 3, 0}};

    auto section = std::make_unique<SectionMicromaps>();
    std::unordered_map<const BakedTriangle *, uint32_t> entries;
    std::vector<std::shared_ptr<const BakedTriangle>> held;
    Stats &counters = stats();
    bool anyUsed = false;
    const size_t sourceCount = geometries.size();
    section->geometries.resize(sourceCount);

    for (size_t g = 0; g < sourceCount; g++) {
        if (!cutout[g]) continue;
        const size_t quads = geometries[g].size() / 4;

        // Each quad's two triangles, baked; a quad with a triangle no micromap describes keeps the shader.
        std::vector<std::shared_ptr<const BakedTriangle>> baked(quads * 2);
        std::vector<bool> keep(quads, false);
        size_t kept = 0;
        for (size_t q = 0; q < quads; q++) {
            const PBRVertex *quad = geometries[g].data() + q * 4;
            bool describable = true;
            for (int half = 0; half < 2 && describable; half++) {
                const PBRVertex &a = quad[kCorners[half][0]];
                const PBRVertex &b = quad[kCorners[half][1]];
                const PBRVertex &c = quad[kCorners[half][2]];
                if (!eligible(a, textureId) || !eligible(b, textureId) || !eligible(c, textureId)) {
                    counters.left[Stats::kNotCutout].fetch_add(2, std::memory_order_relaxed);
                    describable = false;
                    break;
                }
                const glm::vec2 uv[3] = {a.textureUV, b.textureUV, c.textureUV};
                baked[q * 2 + half] = atlas->bake(uv, maxLevel);
                describable = baked[q * 2 + half]->describable;
                if (!describable) counters.left[baked[q * 2 + half]->reason].fetch_add(2, std::memory_order_relaxed);
            }
            keep[q] = describable;
            kept += describable ? 1 : 0;
        }
        counters.triangles.fetch_add(quads * 2, std::memory_order_relaxed);
        counters.moved.fetch_add((quads - kept) * 2, std::memory_order_relaxed);
        if (kept == 0) continue;

        // The quads that keep the shader go to a geometry of their own, so that this one holds only quads its
        // micromap describes.
        if (kept < quads) {
            std::vector<PBRVertex> stays;
            std::vector<PBRVertex> moves;
            std::vector<std::shared_ptr<const BakedTriangle>> stayingBakes;
            stays.reserve(kept * 4);
            moves.reserve((quads - kept) * 4);
            stayingBakes.reserve(kept * 2);
            for (size_t q = 0; q < quads; q++) {
                const PBRVertex *quad = geometries[g].data() + q * 4;
                (keep[q] ? stays : moves).insert((keep[q] ? stays : moves).end(), quad, quad + 4);
                if (keep[q]) {
                    stayingBakes.push_back(baked[q * 2]);
                    stayingBakes.push_back(baked[q * 2 + 1]);
                }
            }
            geometries[g] = std::move(stays);
            baked = std::move(stayingBakes);
            split(g, std::move(moves));
        }

        SectionMicromaps::Geometry &geometry = section->geometries[g];
        geometry.used = true;
        geometry.indices.resize(baked.size());
        for (size_t t = 0; t < baked.size(); t++) {
            const BakedTriangle &triangle = *baked[t];
            const uint32_t microTriangles = 1u << (2 * triangle.level);
            counters.unknownMicroTriangles.fetch_add(triangle.unknown, std::memory_order_relaxed);
            counters.knownMicroTriangles.fetch_add(microTriangles - triangle.unknown, std::memory_order_relaxed);
            if (triangle.special != 0) {
                geometry.indices[t] = triangle.special;
                if (triangle.special == VK_OPACITY_MICROMAP_SPECIAL_INDEX_FULLY_OPAQUE_EXT) {
                    counters.fullyOpaque.fetch_add(1, std::memory_order_relaxed);
                } else if (triangle.special == VK_OPACITY_MICROMAP_SPECIAL_INDEX_FULLY_TRANSPARENT_EXT) {
                    counters.fullyTransparent.fetch_add(1, std::memory_order_relaxed);
                } else {
                    counters.fullyUnknown.fetch_add(1, std::memory_order_relaxed);
                }
                continue;
            }
            auto [entry, added] = entries.try_emplace(&triangle, static_cast<uint32_t>(section->triangles.size()));
            if (added) {
                const size_t offset = alignUp(section->states.size(), 4);
                section->states.resize(offset + triangle.states.size());
                std::memcpy(section->states.data() + offset, triangle.states.data(), triangle.states.size());
                section->triangles.push_back({static_cast<uint32_t>(offset), triangle.level, triangle.format});
                countUsage(section->usage, triangle.level, triangle.format);
                held.push_back(baked[t]);
            }
            geometry.indices[t] = static_cast<int32_t>(entry->second);
            countUsage(geometry.usage, triangle.level, triangle.format);
            counters.withStates.fetch_add(1, std::memory_order_relaxed);
        }
        anyUsed = true;
    }
    // The geometries split off come after the sources and take no micromap.
    section->geometries.resize(geometries.size());
    if (!anyUsed) return nullptr;

    // A micromap needs at least one triangle, even when every triangle of the section takes a special index.
    if (section->triangles.empty()) {
        section->states.assign(1, 0);
        section->triangles.push_back({0, 0, VK_OPACITY_MICROMAP_FORMAT_2_STATE_EXT});
        countUsage(section->usage, 0, VK_OPACITY_MICROMAP_FORMAT_2_STATE_EXT);
    }
    return section;
}

} // namespace omm
