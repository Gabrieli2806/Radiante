#pragma once

#include "prism/render/textures.hpp"

#include <array>
#include <bitset>
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

// A light source sampled by the shaders: a triangle (p3 repeats p2) of an emissive surface, or a point light. The
// positions are in the space of the geometry it came from; packLightAt places it in the world.
struct LightInfo {
    glm::vec3 p0;
    glm::vec3 p1;
    glm::vec3 p2;
    glm::vec3 p3;

    glm::vec3 normal;
    glm::vec3 radiance;
    float area;
    // How the shaders scale the radiance (block_light.glsl, vptCastLight): 0 a light level's scale, 1 exactly the
    // glow its surface shows (a Bedrock pack's MER emission, for the experimental profile), 2 one of Bedrock RTX's
    // point lights: a sphere at p0 of radius normal.x whose radiance is its strength, falling off with distance.
    float kind = 0.0f;

    uint32_t textureID;
    uint64_t stableID;
};

// One emissive cell of a texture as Java uploads it (EmissionTiles.java): its texture rectangle, its mean emission
// and its mean colour. A negative emission marks a cell whose light is exactly what its texels show.
struct EmissionCellUpload {
    float u0;
    float v0;
    float u1;
    float v1;
    float avgEmission;
    float avgR;
    float avgG;
    float avgB;
};
static_assert(sizeof(EmissionCellUpload) == 8 * sizeof(float));

struct EmissionCell {
    glm::vec2 uvMin;
    glm::vec2 uvMax;
    float avgEmission;
    glm::vec3 avgColor;
    // Stays the same while the tile is not uploaded again, so the lights made from it keep their identity.
    uint64_t stableKey;
    // Uploaded with a negative emission: the cell holds the light its texels show (LightInfo::kind 1).
    bool exact = false;
};

// The emissive cells of every texture, uploaded by Java one tile (a sprite) at a time, and queried by the chunk and
// entity builders to turn emissive quads into lights. Safe to use from any thread.
class Emission : public SharedObject<Emission> {
  public:
    static constexpr int kMaxTextures = 4096;
    // A coarse map of where in each texture emissive cells lie. Builders copy it once per texture and skip, without
    // locking or searching, every quad whose texture coordinates miss it: almost all of them.
    static constexpr int kOccupancyGridSize = 64;
    static constexpr int kOccupancyCellCount = kOccupancyGridSize * kOccupancyGridSize;

    struct TextureOccupancy {
        bool hasEmissionCells = false;
        std::bitset<kOccupancyCellCount> occupiedCells;
    };

    // The textures are not needed to answer queries; the argument stays for the factory Textures calls.
    explicit Emission(std::weak_ptr<Textures> textures);
    ~Emission();

    void reset();
    void resetTexture(uint32_t textureID);
    // Replaces the cells of one tile of a texture (none when cells is null or cellCount is not positive).
    void updateTile(uint32_t textureID, uint64_t tileKey, const EmissionCellUpload *cells, int cellCount);

    // Copies a texture's occupancy into `occupancy`; true when it has emissive cells at all.
    bool copyTextureOccupancy(uint32_t textureID, TextureOccupancy &occupancy) const;
    static bool
    textureOccupancyOverlaps(const TextureOccupancy &occupancy, const glm::vec2 &uvMin, const glm::vec2 &uvMax);

    // Copies into `out` every cell of the texture whose rectangle touches the one from uvMin to uvMax, edges included.
    void collectCells(uint32_t textureID,
                      const glm::vec2 &uvMin,
                      const glm::vec2 &uvMax,
                      std::vector<EmissionCell> &out) const;

  private:
    struct TextureState;

    mutable std::shared_mutex mutex_;
    std::unordered_map<uint32_t, std::unique_ptr<TextureState>> textures_;
};

// One light as the shaders read it (block_light_data.glsl's VptPackedLight), at an absolute world position.
struct PackedLightData {
    glm::vec4 p0Area;
    glm::vec4 p1;
    glm::vec4 p2;
    glm::vec4 p3;
    glm::vec4 normal;
    glm::vec4 radiance;
    glm::vec4 sourceIDData;
};
static_assert(sizeof(PackedLightData) == 7 * sizeof(glm::vec4));

PackedLightData packLightAt(const LightInfo &light, const glm::vec3 &origin);

// The light sources in one geometry's quads: a light per emissive cell of the texture each quad shows, positioned
// in the geometry's own space, plus the point lights Java marks. Chunks call it per section, entities per entity (a
// dropped glowstone, an item frame). ownerId and geometryIndex go into each light's stable id.
void collectEmissiveQuadLights(const Emission &emission,
                               const std::vector<gpu::VertexFormat::PBRVertex> &geometryVertices,
                               int64_t ownerId,
                               uint32_t geometryIndex,
                               std::unordered_map<uint32_t, Emission::TextureOccupancy> &textureOccupancyCache,
                               std::vector<LightInfo> &lightInfos);
