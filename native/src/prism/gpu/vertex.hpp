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
    //   bits 8-11   the alpha mode (the low four bits of PBRVertex::alphaMode)
    //   bits 12-15  the coordinate system
    //   bits 17-26  the surface flags Java sets in alphaMode above the mode (below)
    static constexpr uint32_t useColorLayerBit = 1u << 0u;
    static constexpr uint32_t useTextureBit = 1u << 1u;
    static constexpr uint32_t useOverlayBit = 1u << 2u;
    static constexpr uint32_t useGlintBit = 1u << 3u;
    static constexpr uint32_t useNormBit = 1u << 4u;
    static constexpr uint32_t useLightBit = 1u << 5u;
    static constexpr uint32_t alphaModeShift = 8u;
    static constexpr uint32_t coordinateShift = 12u;

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
    static std::vector<VertexFormat::PositionVertex>
    buildPositionVertices(const std::vector<VertexFormat::PBRVertex> &vertices);
    static std::vector<VertexFormat::PackedMaterialVertex>
    buildMaterialVertices(const std::vector<VertexFormat::PBRVertex> &vertices);

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

// The formats of common/shared.hpp that have a layout: the plain ones in gpu/vertex.cpp, Minecraft's in
// render/vertex_formats.cpp.
template <>
VertexLayoutInfo &Vertex::vertexLayoutInfo<VertexFormat::Triangle>();
template <>
VertexLayoutInfo &Vertex::vertexLayoutInfo<VertexFormat::TexturedTriangle>();
template <>
VertexLayoutInfo &Vertex::vertexLayoutInfo<VertexFormat::ArrayTexturedTriangle>();
template <>
VertexLayoutInfo &Vertex::vertexLayoutInfo<VertexFormat::PositionOnly>();
template <>
VertexLayoutInfo &Vertex::vertexLayoutInfo<VertexFormat::PositionTexColor>();
template <>
VertexLayoutInfo &Vertex::vertexLayoutInfo<VertexFormat::PositionColor>();
template <>
VertexLayoutInfo &Vertex::vertexLayoutInfo<VertexFormat::PositionColorNormal>();
template <>
VertexLayoutInfo &Vertex::vertexLayoutInfo<VertexFormat::PositionTex>();
template <>
VertexLayoutInfo &Vertex::vertexLayoutInfo<VertexFormat::PositionColorTexLight>();
template <>
VertexLayoutInfo &Vertex::vertexLayoutInfo<VertexFormat::PositionColorLight>();
template <>
VertexLayoutInfo &Vertex::vertexLayoutInfo<VertexFormat::PositionTexColorLight>();
template <>
VertexLayoutInfo &Vertex::vertexLayoutInfo<VertexFormat::PositionTexColorNormal>();
template <>
VertexLayoutInfo &Vertex::vertexLayoutInfo<VertexFormat::PositionTexLightColor>();
template <>
VertexLayoutInfo &Vertex::vertexLayoutInfo<VertexFormat::PositionColorTexLightNormal>();
template <>
VertexLayoutInfo &Vertex::vertexLayoutInfo<VertexFormat::PositionColorTexOverlayLightNormal>();
template <>
VertexLayoutInfo &Vertex::vertexLayoutInfo<VertexFormat::PBRVertex>();

inline uint32_t Vertex::packMaterialFlags(const VertexFormat::PBRVertex &vertex) {
    // The surface flags fill alphaMode bits 4 to 13 and keep their order thirteen bits higher in the packed word, so
    // they move together.
    constexpr uint32_t surfaceFlags = 0xBFF0u;
    constexpr uint32_t surfaceShift = 13u;
    static_assert((alphaModeWaterFlag | alphaModeRainFlag | alphaModeHeldFlag | alphaModeGlassFlag |
                   alphaModePlantFlag | alphaModePaneFlag | alphaModeCapUpFlag | alphaModeCapDownFlag |
                   alphaModeBlendFlag | alphaModeUnlitFlag | alphaModeLavaFlag) == surfaceFlags);
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
                  (alphaModeLavaFlag << surfaceShift) == lavaSurfaceBit);
    static_assert((alphaModePointLightFlag & surfaceFlags) == 0);

    uint32_t packed = 0;
    packed |= vertex.useColorLayer != 0 ? useColorLayerBit : 0u;
    packed |= vertex.useTexture != 0 ? useTextureBit : 0u;
    packed |= vertex.useOverlay != 0 ? useOverlayBit : 0u;
    packed |= vertex.useGlint != 0 ? useGlintBit : 0u;
    packed |= vertex.useNorm != 0 ? useNormBit : 0u;
    packed |= vertex.useLight != 0 ? useLightBit : 0u;
    packed |= (vertex.alphaMode & 0xFu) << alphaModeShift;
    packed |= (vertex.coordinate & 0xFu) << coordinateShift;
    packed |= (vertex.alphaMode & surfaceFlags) << surfaceShift;
    return packed;
}

inline VertexFormat::PositionVertex Vertex::makePositionVertex(const VertexFormat::PBRVertex &vertex) {
    return {
        .pos = vertex.pos,
        .pad0 = 0,
    };
}

inline VertexFormat::PackedMaterialVertex Vertex::makeMaterialVertex(const VertexFormat::PBRVertex &vertex) {
    return {
        .normal = packNormal(vertex.norm),
        .textures = (vertex.textureID & 0xFFFFu) | ((vertex.glintTexture & 0xFFFFu) << 16),
        .color = glm::packUnorm4x8(glm::clamp(vertex.colorLayer, glm::vec4(0.0f), glm::vec4(1.0f))),
        .u = vertex.textureUV.x,
        .v = vertex.textureUV.y,
        .overlay = packInt16Pair(vertex.overlayUV.x, vertex.overlayUV.y),
        .glintUV = glm::packHalf2x16(vertex.glintUV),
        .albedoEmission = vertex.albedoEmission,
        .light = packUint16Pair(vertex.lightUV.x, vertex.lightUV.y),
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
