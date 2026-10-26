#pragma once

#include "prism/all_extern.hpp"
#include "prism/gpu/gpu.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <unordered_map>
#include <vector>

// Opacity micromaps for cut-out terrain (leaves, grass, flowers, rails...).
//
// A cut-out surface is solid where its texture's alpha reaches one half and absent elsewhere, and the any-hit shaders
// find out which for every ray that meets one: in a forest a shadow ray runs the shader once per leaf block it passes.
// A micromap holds that answer per micro-triangle (VK_EXT_opacity_micromap): each triangle is cut into 4^level smaller
// ones, and the traversal hardware answers the ones marked opaque or transparent itself, running the any-hit shader
// only on those marked unknown.
//
// The micromaps must answer exactly as the shaders do, and the shaders do not all ask the same question:
// - Camera and bounce rays (default.rahit) test the full-size texel with Detailed distant foliage (the default), and
//   read it like shadow rays: every micro-triangle the full-size texture decides is known. With Smooth distant foliage
//   they test the texel at the mip level their ray cone reaches; Minecraft's cut-out mip levels keep the coverage of
//   the full-size texture on average but not texel for texel, so a micro-triangle is then marked opaque (transparent)
//   only where every texel it covers is opaque (transparent) at every mip level, and the rest are unknown and go to
//   the shader.
// - Shadow rays (shadow.rahit) test the full-size texture only. They trace with
//   gl_RayFlagsForceOpacityMicromap2StateEXT, which reads "unknown opaque" as opaque and "unknown transparent" as
//   transparent, and each unknown micro-triangle is given the full-size answer that way: these rays never need the
//   shader on terrain with a micromap.
// The forced reading has to be right for every micro-triangle of a geometry with a micromap, so a triangle that one
// state cannot describe at full size - a micro-triangle straddling texels that disagree, an animated sprite, or a
// surface whose any-hit test is more than the alpha test (glass, a tinted vertex) - is moved, with its quad, to a
// geometry of its own without one (bakeSection).
//
// Minecraft's block faces map whole texels onto their quads, and a triangle whose sides run along u and v is cut at
// subdivision level log2(side) into micro-triangles that each lie within one texel, so nearly every leaf, grass and
// flower triangle can be described.
//
// The coverage comes from a copy of the block atlas kept here, one bit per texel and mip level, filled from the uploads
// Java makes when it mirrors the atlas (AtlasMirror), plus the texels of animated sprites.
namespace omm {

// The micro-triangle states (VkOpacityMicromapStateEXT values).
enum State : uint8_t {
    kTransparent = 0,
    kOpaque = 1,
    kUnknownTransparent = 2,
    kUnknownOpaque = 3,
};

// The corners, in barycentric coordinates (u weighting the triangle's second vertex, v its third), of micro-triangle
// `index` of a triangle cut at subdivision level `level`, in the order the Vulkan specification lays out micromap data
// (a hierarchical space-filling curve over the micro-triangles).
void microTriangleCorners(uint32_t index, uint32_t level, glm::vec2 corners[3]);

// One triangle's micromap: a special index for the whole triangle, or packed states at a subdivision level.
struct BakedTriangle {
    // Every micro-triangle has one answer at the texture's full size: the triangle can take a micromap.
    bool describable = false;
    // A VK_OPACITY_MICROMAP_SPECIAL_INDEX_* value, or 0 when `states` holds the triangle's micromap.
    int32_t special = 0;
    uint16_t level = 0;
    // VK_OPACITY_MICROMAP_FORMAT_2_STATE_EXT or _4_STATE_EXT.
    uint16_t format = 0;
    std::vector<uint8_t> states;
    // How many of its micro-triangles camera rays still ask the shader about, of 4^level.
    uint32_t unknown = 0;
    // Why it cannot take a micromap (Stats::Left), when it cannot.
    uint8_t reason = 0;
};

// The coverage of the block atlas once Java has finished mirroring it: per mip level one bit per texel, and which
// texels of the full-size level belong to animated sprites. Immutable but for its cache of baked triangles, and shared
// by the threads that build sections.
class CoverageAtlas : public SharedObject<CoverageAtlas> {
  public:
    struct Level {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint64_t> opaque;
        // Java uploaded into it: the atlas image has this level.
        bool written = false;
    };

    CoverageAtlas(uint32_t textureId, std::vector<Level> levels, std::vector<uint64_t> animated);

    uint32_t textureId() const {
        return textureId_;
    }

    // The micromap of a triangle with these texture coordinates (normalised, in the order of its vertices). Baked on
    // first use and kept: a world's leaves, grass and flowers share a few hundred distinct triangles.
    std::shared_ptr<const BakedTriangle> bake(const glm::vec2 uv[3], uint32_t maxLevel);
    // Whether every full-size texel of the rectangle belongs to an animated sprite (or its border).
    bool animatedOnly(int64_t x, int64_t y, uint32_t width, uint32_t height) const;

  private:
    struct Key {
        uint32_t bits[6];
        uint32_t maxLevel;
        bool operator==(const Key &other) const;
    };
    struct KeyHash {
        size_t operator()(const Key &key) const;
    };
    // What the texels a micro-triangle covers say at one mip level.
    struct Coverage {
        bool opaque = false;
        bool transparent = false;
        bool animated = false;
    };

    Coverage cover(const glm::dvec2 corners[3], uint32_t mipLevel) const;
    BakedTriangle bakeUncached(const glm::vec2 uv[3], uint32_t maxLevel, bool detailed) const;
    bool opaqueAt(uint32_t mipLevel, int64_t x, int64_t y) const;
    bool animatedAt(int64_t x, int64_t y) const;

    uint32_t textureId_;
    std::vector<Level> levels_;
    std::vector<uint64_t> animated_;

    std::mutex cacheMutex_;
    std::unordered_map<Key, std::shared_ptr<const BakedTriangle>, KeyHash> cache_;
};

// The atlas whose coverage micromaps are baked from. Java calls begin, its uploads arrive through Textures::queueUpload,
// and finish publishes the result; until then (and after anything but an animation writes into the atlas) no
// micromaps are made.
namespace coverage {
void begin(uint32_t textureId, uint32_t width, uint32_t height);
// The texels of an RGBA8 upload into mip level `mipLevel`: rows of `rowPixels` texels from `firstTexel`, written at
// (dstX, dstY).
void upload(uint32_t textureId,
            uint32_t mipLevel,
            const uint8_t *firstTexel,
            size_t rowPixels,
            int32_t dstX,
            int32_t dstY,
            uint32_t width,
            uint32_t height);
// `animatedRects` holds x, y, width and height of each animated sprite, in full-size texels.
void finish(uint32_t textureId, std::span<const int32_t> animatedRects);
// The texture under this id was replaced: whatever was known of it no longer applies.
void forget(uint32_t textureId);
// The published atlas, or null.
std::shared_ptr<CoverageAtlas> current();
// Whether camera rays test cut-out coverage at full resolution (the Detailed Distant Foliage attribute): sections
// baked from now on follow it, and Java builds every section again when it changes.
void setDetailed(bool detailed);
bool detailed();
} // namespace coverage

// The micromap inputs of one chunk section: the states and triangle array of one micromap shared by its cut-out
// geometries, and per geometry the index of each triangle's micromap triangle (or a special index). Written after the
// section's geometry in its buffer.
struct SectionMicromaps {
    struct Geometry {
        bool used = false;
        std::vector<int32_t> indices;
        std::vector<VkMicromapUsageEXT> usage;
        // Where its indices lie in the section's buffer.
        size_t indexOffset = 0;
    };

    std::vector<uint8_t> states;
    std::vector<VkMicromapTriangleEXT> triangles;
    std::vector<VkMicromapUsageEXT> usage;
    std::vector<Geometry> geometries;
    // Where the states and the triangle array lie in the section's buffer.
    size_t statesOffset = 0;
    size_t trianglesOffset = 0;

    // Lays the regions out from `offset` on; returns the end.
    size_t layout(size_t offset);
    void write(uint8_t *buffer) const;
};

// Bakes the micromaps of a section's cut-out geometries (quads of four vertices, two triangles each: 0 1 2 and 2 3 0).
// The quads of a cut-out geometry that a micromap cannot describe are moved to a new geometry appended after the
// others, which `split` is called with (source geometry, its quads) so the caller can give it the source's type and
// hit group. Null when no geometry takes a micromap.
std::unique_ptr<SectionMicromaps> bakeSection(
    std::vector<std::vector<gpu::VertexFormat::PBRVertex>> &geometries,
    const std::vector<bool> &cutout,
    uint32_t maxLevel,
    const std::function<void(size_t source, std::vector<gpu::VertexFormat::PBRVertex> &&quads)> &split);

// Counters for the development report: triangles baked into sections, by what their micromap made of them.
struct Stats {
    std::atomic<uint64_t> triangles{0};
    std::atomic<uint64_t> fullyOpaque{0};
    std::atomic<uint64_t> fullyTransparent{0};
    std::atomic<uint64_t> fullyUnknown{0};
    std::atomic<uint64_t> withStates{0};
    // Left to the shaders, in a geometry without a micromap, and why: not a plain cut-out surface of the atlas (a
    // translucent layer, glass, a tinted vertex...), no area in the texture, an animated sprite, or a micro-triangle
    // over texels that disagree.
    enum Left { kNotCutout = 1, kNoArea, kAnimated, kStraddles, kLeftReasons };
    std::atomic<uint64_t> moved{0};
    std::atomic<uint64_t> left[kLeftReasons] = {};
    std::atomic<uint64_t> knownMicroTriangles{0};
    std::atomic<uint64_t> unknownMicroTriangles{0};
    std::atomic<uint64_t> inputBytesLive{0};
};
Stats &stats();

} // namespace omm
