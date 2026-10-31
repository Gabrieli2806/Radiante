#pragma once

#include "common/shared.hpp"
#include "prism/all_extern.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/gtc/packing.hpp>
#include <vector>

namespace gpu {

// How a graphics pipeline reads one vertex format: binding 0, attribute i at location i.
struct VertexLayoutInfo {
    VkVertexInputBindingDescription bindingDescription;
    std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
};

struct VertexAttribute {
    VkFormat format;
    uint32_t offset;
};

// Vertex layouts for graphics pipelines, and the packing of the PBRVertex Java sends into the position and material
// buffers the ray tracer reads (PositionVertex and PackedMaterialVertex, common/shared.hpp).
struct Vertex {
    // The material word (PackedMaterialVertex::packedData) as the shaders unpack it:
    //   bits 0-5    which of the vertex's inputs are used
    //   bit 6       frost (ice and snow, see alphaModeFrostFlag)
    //   bits 8-11   the alpha mode (the low four bits of PBRVertex::alphaMode)
    //   bits 12-15  the coordinate system
    //   bit 16      an HDR emission tint in the spare light and overlay UV bytes
    //   bits 17-31  the surface flags Java sets in alphaMode above the mode (below)
    static constexpr uint32_t useColorLayerBit = 1u << 0u;
    static constexpr uint32_t useTextureBit = 1u << 1u;
    static constexpr uint32_t useOverlayBit = 1u << 2u;
    static constexpr uint32_t useGlintBit = 1u << 3u;
    static constexpr uint32_t useNormBit = 1u << 4u;
    static constexpr uint32_t useLightBit = 1u << 5u;
    static constexpr uint32_t alphaModeShift = 8u;
    static constexpr uint32_t coordinateShift = 12u;
    static constexpr uint32_t emissionOverrideBit = 1u << 16u;
    static constexpr uint32_t alphaModeEmissionTintFlag = 1u << 21u;

    // Water surfaces.
    static constexpr uint32_t alphaModeWaterFlag = 0x10u;
    static constexpr uint32_t waterSurfaceBit = 1u << 17u;
    // Rain and snow sheets, whose texture falls while the sheet stands still.
    static constexpr uint32_t alphaModeRainFlag = 0x20u;
    static constexpr uint32_t rainSurfaceBit = 1u << 18u;
    // What the player holds in first person, which glows by its own brightness setting.
    static constexpr uint32_t alphaModeHeldFlag = 0x40u;
    static constexpr uint32_t heldSurfaceBit = 1u << 19u;
    // Glass blocks and panes: their see-through texels are clear glass, not holes.
    static constexpr uint32_t alphaModeGlassFlag = 0x80u;
    static constexpr uint32_t glassSurfaceBit = 1u << 20u;
    // Small plants, which the tracer may leave out far away.
    static constexpr uint32_t alphaModePlantFlag = 0x100u;
    static constexpr uint32_t plantSurfaceBit = 1u << 21u;
    // Glass panes, and whether their top and bottom caps face air (seamless glass).
    static constexpr uint32_t alphaModePaneFlag = 0x200u;
    static constexpr uint32_t alphaModeCapUpFlag = 0x400u;
    static constexpr uint32_t alphaModeCapDownFlag = 0x800u;
    static constexpr uint32_t paneSurfaceBit = 1u << 22u;
    static constexpr uint32_t paneCapUpBit = 1u << 23u;
    static constexpr uint32_t paneCapDownBit = 1u << 24u;
    // Ice, slime and honey blocks, which the experimental Bedrock profile sees through as it does glass.
    static constexpr uint32_t alphaModeBlendFlag = 0x1000u;
    static constexpr uint32_t blendSurfaceBit = 1u << 25u;
    // A light block in a state that gives no light but still draws the sprites its lit states glow with (unlit
    // redstone ore): it neither glows nor casts light.
    static constexpr uint32_t alphaModeUnlitFlag = 0x2000u;
    static constexpr uint32_t unlitSurfaceBit = 1u << 26u;
    // Not a surface: the four corners of a light Bedrock RTX adds at a small light block (a torch, a lantern...),
    // read into the section's light list and never traced. Colour, strength (albedoEmission) and radius (norm.x)
    // are the light's. Never packed into the material word.
    static constexpr uint32_t alphaModePointLightFlag = 0x4000u;
    // The surface of lava: shown by its colour at a steady brightness, not by the light on it.
    static constexpr uint32_t alphaModeLavaFlag = 0x8000u;
    static constexpr uint32_t lavaSurfaceBit = 1u << 28u;
    // A layer drawn over a face of the same model and lifted off it (the grass block's side overlay): not carved by
    // height maps.
    static constexpr uint32_t alphaModeLayerFlag = 0x10000u;
    static constexpr uint32_t layerSurfaceBit = 1u << 29u;
    // Leaves, which a shader pack may let light through.
    static constexpr uint32_t alphaModeLeavesFlag = 0x20000u;
    static constexpr uint32_t leavesSurfaceBit = 1u << 30u;
    // A glow given per vertex (albedoEmission) that is also cast as a light of the section's list (powered redstone
    // dust, RedstoneGlow.java): postBase.y holds the light each quad casts, postBase.z its colour (0xRRGGBB bits).
    static constexpr uint32_t alphaModeGlowLightFlag = 0x40000u;
    static constexpr uint32_t glowLightSurfaceBit = 1u << 31u;
    // Ice, packed ice, blue ice and snow, lit a little past the terminator and from behind. The flags above fill the
    // word's top fifteen bits, so this one goes to a free low bit (6) instead of thirteen bits up.
    static constexpr uint32_t alphaModeFrostFlag = 0x80000u;
    static constexpr uint32_t frostSurfaceBit = 1u << 6u;

    // The layout of vertex format T: one per format, made on first use and kept for the process, so pipelines may
    // point into it. Only the formats specialized below have one; asking for any other does not compile.
    template <typename T>
    static VertexLayoutInfo &vertexLayoutInfo() = delete;

    // A layout of format T that reads attributes[i] at location i.
    template <typename T>
    static VertexLayoutInfo initVertexLayout(const std::vector<VertexAttribute> &attributes);

    // Points binding 0 at format T and adds attributes[i] at location i.
    template <typename T>
    static void buildVertexLayoutInfo(VertexLayoutInfo &vertexLayoutInfo,
                                      const std::vector<VertexAttribute> &attributes);

    // The make/pack functions run for every vertex of every chunk section built. They are defined inline below so
    // that the chunk builder's loops inline them, and callers that keep only part of a material vertex do not pay
    // for the rest of it.
    static uint32_t packMaterialFlags(const VertexFormat::PBRVertex &vertex);
    static VertexFormat::PositionVertex makePositionVertex(const VertexFormat::PBRVertex &vertex);
    static VertexFormat::PackedMaterialVertex makeMaterialVertex(const VertexFormat::PBRVertex &vertex);

  private:
    static uint32_t packNormal(glm::vec3 normal);
    static uint32_t packInt16Pair(int32_t x, int32_t y);
    static uint32_t packUint16Pair(int32_t x, int32_t y);
};

template <typename T>
VertexLayoutInfo Vertex::initVertexLayout(const std::vector<VertexAttribute> &attributes) {
    VertexLayoutInfo layout{};
    buildVertexLayoutInfo<T>(layout, attributes);
    return layout;
}

template <typename T>
void Vertex::buildVertexLayoutInfo(VertexLayoutInfo &vertexLayoutInfo, const std::vector<VertexAttribute> &attributes) {
    vertexLayoutInfo.bindingDescription = {
        .binding = 0,
        .stride = sizeof(T),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    vertexLayoutInfo.attributeDescriptions.reserve(vertexLayoutInfo.attributeDescriptions.size() + attributes.size());
    for (uint32_t location = 0; location < attributes.size(); location++) {
        vertexLayoutInfo.attributeDescriptions.push_back({
            .location = location,
            .binding = 0,
            .format = attributes[location].format,
            .offset = attributes[location].offset,
        });
    }
}

// The one format with a layout, in render/vertex_formats.cpp.
template <>
VertexLayoutInfo &Vertex::vertexLayoutInfo<VertexFormat::PBRVertex>();

inline uint32_t Vertex::packMaterialFlags(const VertexFormat::PBRVertex &vertex) {
    // The surface flags fill alphaMode bits 4 to 18 (but 14, the point light) and keep their order thirteen bits higher
    // in the packed word, so they move together.
    constexpr uint32_t surfaceFlags = 0x7BFF0u;
    constexpr uint32_t surfaceShift = 13u;
    static_assert((alphaModeWaterFlag | alphaModeRainFlag | alphaModeHeldFlag | alphaModeGlassFlag |
                   alphaModePlantFlag | alphaModePaneFlag | alphaModeCapUpFlag | alphaModeCapDownFlag |
                   alphaModeBlendFlag | alphaModeUnlitFlag | alphaModeLavaFlag | alphaModeLayerFlag |
                   alphaModeLeavesFlag | alphaModeGlowLightFlag) == surfaceFlags);
    static_assert((alphaModeWaterFlag << surfaceShift) == waterSurfaceBit &&
                  (alphaModeRainFlag << surfaceShift) == rainSurfaceBit &&
                  (alphaModeHeldFlag << surfaceShift) == heldSurfaceBit &&
                  (alphaModeGlassFlag << surfaceShift) == glassSurfaceBit &&
                  (alphaModePlantFlag << surfaceShift) == plantSurfaceBit &&
                  (alphaModePaneFlag << surfaceShift) == paneSurfaceBit &&
                  (alphaModeCapUpFlag << surfaceShift) == paneCapUpBit &&
                  (alphaModeCapDownFlag << surfaceShift) == paneCapDownBit &&
                  (alphaModeBlendFlag << surfaceShift) == blendSurfaceBit &&
                  (alphaModeUnlitFlag << surfaceShift) == unlitSurfaceBit &&
                  (alphaModeLavaFlag << surfaceShift) == lavaSurfaceBit &&
                  (alphaModeLayerFlag << surfaceShift) == layerSurfaceBit &&
                  (alphaModeLeavesFlag << surfaceShift) == leavesSurfaceBit &&
                  (alphaModeGlowLightFlag << surfaceShift) == glowLightSurfaceBit);
    static_assert((alphaModePointLightFlag & surfaceFlags) == 0);
    static_assert((alphaModeEmissionTintFlag & (surfaceFlags | alphaModeFrostFlag | alphaModePointLightFlag | 0x100000u | 15u)) == 0);
    static_assert((emissionOverrideBit & (0x3Fu | frostSurfaceBit | (0xFu << alphaModeShift) |
                                         (0xFu << coordinateShift) | (surfaceFlags << surfaceShift))) == 0);

    uint32_t packed = 0;
    packed |= vertex.useColorLayer != 0 ? useColorLayerBit : 0u;
    packed |= vertex.useTexture != 0 ? useTextureBit : 0u;
    packed |= vertex.useOverlay != 0 ? useOverlayBit : 0u;
    packed |= vertex.useGlint != 0 ? useGlintBit : 0u;
    packed |= vertex.useNorm != 0 ? useNormBit : 0u;
    packed |= vertex.useLight != 0 ? useLightBit : 0u;
    packed |= (vertex.alphaMode & 0xFu) << alphaModeShift;
    packed |= (vertex.coordinate & 0xFu) << coordinateShift;
    packed |= (vertex.alphaMode & alphaModeEmissionTintFlag) != 0u ? emissionOverrideBit : 0u;
    packed |= (vertex.alphaMode & surfaceFlags) << surfaceShift;
    packed |= (vertex.alphaMode & alphaModeFrostFlag) != 0u ? frostSurfaceBit : 0u;
    return packed;
}

inline VertexFormat::PositionVertex Vertex::makePositionVertex(const VertexFormat::PBRVertex &vertex) {
    return {
        .pos = vertex.pos,
        .pad0 = 0,
    };
}

// Raw vertices carry RGB9_E5 in the spare coordinate and alpha-mode bits until material packing.
inline uint32_t packedEmissionTint(const VertexFormat::PBRVertex &vertex) {
    return (vertex.coordinate >> 4u) | (((vertex.alphaMode >> 24u) & 15u) << 28u);
}

inline glm::vec3 emissionTint(const VertexFormat::PBRVertex &vertex) {
    return (vertex.alphaMode & Vertex::alphaModeEmissionTintFlag) != 0u ?
        glm::unpackF3x9_E1x5(packedEmissionTint(vertex)) : glm::vec3(1.0f);
}

inline VertexFormat::PackedMaterialVertex Vertex::makeMaterialVertex(const VertexFormat::PBRVertex &vertex) {
    uint32_t light = packUint16Pair(vertex.lightUV.x, vertex.lightUV.y);
    uint32_t overlay = packInt16Pair(vertex.overlayUV.x, vertex.overlayUV.y);
    if ((vertex.alphaMode & alphaModeEmissionTintFlag) != 0u) {
        // Minecraft light and overlay UVs fit in a byte. Their spare bytes retain the full RGB9_E5 word.
        uint32_t rgb = packedEmissionTint(vertex);
        light = (light & 0x00FF00FFu) | ((rgb & 255u) << 8u) | (((rgb >> 8u) & 255u) << 24u);
        overlay = (overlay & 0x00FF00FFu) | (((rgb >> 16u) & 255u) << 8u) | ((rgb >> 24u) << 24u);
    }
    return {
        .normal = packNormal(vertex.norm),
        .textures = (vertex.textureID & 0xFFFFu) | ((vertex.glintTexture & 0xFFFFu) << 16),
        .color = glm::packUnorm4x8(glm::clamp(vertex.colorLayer, glm::vec4(0.0f), glm::vec4(1.0f))),
        .u = vertex.textureUV.x,
        .v = vertex.textureUV.y,
        .overlay = overlay,
        .glintUV = glm::packHalf2x16(vertex.glintUV),
        .albedoEmission = vertex.albedoEmission,
        .light = light,
        .packedData = packMaterialFlags(vertex),
    };
}

// Octahedral: the normal is projected onto the octahedron |x| + |y| + |z| = 1 and the lower half folded over the
// upper, leaving two snorm16 values. A zero (or NaN) normal packs to 0x80008000, which the shaders read as none.
inline uint32_t Vertex::packNormal(glm::vec3 normal) {
    const float length = std::abs(normal.x) + std::abs(normal.y) + std::abs(normal.z);
    if (!(length > 1e-8f)) return 0x80008000u;
    glm::vec2 folded = glm::vec2(normal.x, normal.y) / length;
    if (normal.z < 0.0f) {
        const glm::vec2 signs(folded.x >= 0.0f ? 1.0f : -1.0f, folded.y >= 0.0f ? 1.0f : -1.0f);
        folded = (1.0f - glm::abs(glm::vec2(folded.y, folded.x))) * signs;
    }
    return glm::packSnorm2x16(glm::clamp(folded, glm::vec2(-1.0f), glm::vec2(1.0f)));
}

inline uint32_t Vertex::packInt16Pair(int32_t x, int32_t y) {
    const auto low = static_cast<uint16_t>(std::clamp(x, -32768, 32767));
    const auto high = static_cast<uint16_t>(std::clamp(y, -32768, 32767));
    return static_cast<uint32_t>(low) | (static_cast<uint32_t>(high) << 16);
}

inline uint32_t Vertex::packUint16Pair(int32_t x, int32_t y) {
    return static_cast<uint32_t>(std::clamp(x, 0, 65535)) | (static_cast<uint32_t>(std::clamp(y, 0, 65535)) << 16);
}

} // namespace gpu
