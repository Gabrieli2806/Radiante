// Vertex layouts of the formats Minecraft draws with (common/shared.hpp), for graphics pipelines that read them.

#include "prism/gpu/vertex.hpp"

#include <cstddef>

namespace gpu {

template <>
VertexLayoutInfo &Vertex::vertexLayoutInfo<VertexFormat::PositionOnly>() {
    using V = VertexFormat::PositionOnly;
    static VertexLayoutInfo layout = initVertexLayout<V>({
        {VK_FORMAT_R32G32B32_SFLOAT, offsetof(V, position)},
    });
    return layout;
}

template <>
VertexLayoutInfo &Vertex::vertexLayoutInfo<VertexFormat::PositionTexColor>() {
    using V = VertexFormat::PositionTexColor;
    static VertexLayoutInfo layout = initVertexLayout<V>({
        {VK_FORMAT_R32G32B32_SFLOAT, offsetof(V, position)},
        {VK_FORMAT_R32G32_SFLOAT, offsetof(V, uv)},
        {VK_FORMAT_R8G8B8A8_UNORM, offsetof(V, color)},
    });
    return layout;
}

template <>
VertexLayoutInfo &Vertex::vertexLayoutInfo<VertexFormat::PositionColor>() {
    using V = VertexFormat::PositionColor;
    static VertexLayoutInfo layout = initVertexLayout<V>({
        {VK_FORMAT_R32G32B32_SFLOAT, offsetof(V, position)},
        {VK_FORMAT_R8G8B8A8_UNORM, offsetof(V, color)},
    });
    return layout;
}

template <>
VertexLayoutInfo &Vertex::vertexLayoutInfo<VertexFormat::PositionColorNormal>() {
    using V = VertexFormat::PositionColorNormal;
    static VertexLayoutInfo layout = initVertexLayout<V>({
        {VK_FORMAT_R32G32B32_SFLOAT, offsetof(V, position)},
        {VK_FORMAT_R8G8B8A8_UNORM, offsetof(V, color)},
        {VK_FORMAT_R8G8B8A8_SNORM, offsetof(V, normal)},
    });
    return layout;
}

template <>
VertexLayoutInfo &Vertex::vertexLayoutInfo<VertexFormat::PositionTex>() {
    using V = VertexFormat::PositionTex;
    static VertexLayoutInfo layout = initVertexLayout<V>({
        {VK_FORMAT_R32G32B32_SFLOAT, offsetof(V, position)},
        {VK_FORMAT_R32G32_SFLOAT, offsetof(V, uv)},
    });
    return layout;
}

template <>
VertexLayoutInfo &Vertex::vertexLayoutInfo<VertexFormat::PositionColorTexLight>() {
    using V = VertexFormat::PositionColorTexLight;
    static VertexLayoutInfo layout = initVertexLayout<V>({
        {VK_FORMAT_R32G32B32_SFLOAT, offsetof(V, position)},
        {VK_FORMAT_R8G8B8A8_UNORM, offsetof(V, color)},
        {VK_FORMAT_R32G32_SFLOAT, offsetof(V, uv0)},
        {VK_FORMAT_R16G16_SINT, offsetof(V, uv2)},
    });
    return layout;
}

template <>
VertexLayoutInfo &Vertex::vertexLayoutInfo<VertexFormat::PositionColorLight>() {
    using V = VertexFormat::PositionColorLight;
    static VertexLayoutInfo layout = initVertexLayout<V>({
        {VK_FORMAT_R32G32B32_SFLOAT, offsetof(V, position)},
        {VK_FORMAT_R8G8B8A8_UNORM, offsetof(V, color)},
        {VK_FORMAT_R16G16_SINT, offsetof(V, uv2)},
    });
    return layout;
}

template <>
VertexLayoutInfo &Vertex::vertexLayoutInfo<VertexFormat::PositionTexColorLight>() {
    using V = VertexFormat::PositionTexColorLight;
    static VertexLayoutInfo layout = initVertexLayout<V>({
        {VK_FORMAT_R32G32B32_SFLOAT, offsetof(V, position)},
        {VK_FORMAT_R32G32_SFLOAT, offsetof(V, uv0)},
        {VK_FORMAT_R8G8B8A8_UNORM, offsetof(V, color)},
        {VK_FORMAT_R16G16_SINT, offsetof(V, uv2)},
    });
    return layout;
}

template <>
VertexLayoutInfo &Vertex::vertexLayoutInfo<VertexFormat::PositionTexColorNormal>() {
    using V = VertexFormat::PositionTexColorNormal;
    static VertexLayoutInfo layout = initVertexLayout<V>({
        {VK_FORMAT_R32G32B32_SFLOAT, offsetof(V, position)},
        {VK_FORMAT_R32G32_SFLOAT, offsetof(V, uv0)},
        {VK_FORMAT_R8G8B8A8_UNORM, offsetof(V, color)},
        {VK_FORMAT_R8G8B8A8_SNORM, offsetof(V, normal)},
    });
    return layout;
}

template <>
VertexLayoutInfo &Vertex::vertexLayoutInfo<VertexFormat::PositionTexLightColor>() {
    using V = VertexFormat::PositionTexLightColor;
    static VertexLayoutInfo layout = initVertexLayout<V>({
        {VK_FORMAT_R32G32B32_SFLOAT, offsetof(V, position)},
        {VK_FORMAT_R32G32_SFLOAT, offsetof(V, uv0)},
        {VK_FORMAT_R16G16_SINT, offsetof(V, uv2)},
        {VK_FORMAT_R8G8B8A8_UNORM, offsetof(V, color)},
    });
    return layout;
}

template <>
VertexLayoutInfo &Vertex::vertexLayoutInfo<VertexFormat::PositionColorTexLightNormal>() {
    using V = VertexFormat::PositionColorTexLightNormal;
    static VertexLayoutInfo layout = initVertexLayout<V>({
        {VK_FORMAT_R32G32B32_SFLOAT, offsetof(V, position)},
        {VK_FORMAT_R8G8B8A8_UNORM, offsetof(V, color)},
        {VK_FORMAT_R32G32_SFLOAT, offsetof(V, uv0)},
        {VK_FORMAT_R16G16_SINT, offsetof(V, uv2)},
        {VK_FORMAT_R8G8B8A8_SNORM, offsetof(V, normal)},
    });
    return layout;
}

template <>
VertexLayoutInfo &Vertex::vertexLayoutInfo<VertexFormat::PositionColorTexOverlayLightNormal>() {
    using V = VertexFormat::PositionColorTexOverlayLightNormal;
    static VertexLayoutInfo layout = initVertexLayout<V>({
        {VK_FORMAT_R32G32B32_SFLOAT, offsetof(V, position)},
        {VK_FORMAT_R8G8B8A8_UNORM, offsetof(V, color)},
        {VK_FORMAT_R32G32_SFLOAT, offsetof(V, uv0)},
        {VK_FORMAT_R16G16_SINT, offsetof(V, uv1)},
        {VK_FORMAT_R16G16_SINT, offsetof(V, uv2)},
        {VK_FORMAT_R8G8B8A8_SNORM, offsetof(V, normal)},
    });
    return layout;
}

// The world vertex Java sends, for the post-render passes that rasterize it. albedoEmission is not read, and
// postBase is read as four floats, the fourth being the bits of the alphaMode that follows it.
template <>
VertexLayoutInfo &Vertex::vertexLayoutInfo<VertexFormat::PBRVertex>() {
    using V = VertexFormat::PBRVertex;
    static VertexLayoutInfo layout = initVertexLayout<V>({
        {VK_FORMAT_R32G32B32_SFLOAT, offsetof(V, pos)},
        {VK_FORMAT_R32_UINT, offsetof(V, useNorm)},
        {VK_FORMAT_R32G32B32_SFLOAT, offsetof(V, norm)},
        {VK_FORMAT_R32_UINT, offsetof(V, useColorLayer)},
        {VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(V, colorLayer)},
        {VK_FORMAT_R32_UINT, offsetof(V, useTexture)},
        {VK_FORMAT_R32_UINT, offsetof(V, useOverlay)},
        {VK_FORMAT_R32G32_SFLOAT, offsetof(V, textureUV)},
        {VK_FORMAT_R32G32_SINT, offsetof(V, overlayUV)},
        {VK_FORMAT_R32_UINT, offsetof(V, useGlint)},
        {VK_FORMAT_R32_UINT, offsetof(V, textureID)},
        {VK_FORMAT_R32G32_SFLOAT, offsetof(V, glintUV)},
        {VK_FORMAT_R32_UINT, offsetof(V, glintTexture)},
        {VK_FORMAT_R32_UINT, offsetof(V, useLight)},
        {VK_FORMAT_R32G32_SINT, offsetof(V, lightUV)},
        {VK_FORMAT_R32_UINT, offsetof(V, coordinate)},
        {VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(V, postBase)},
    });
    return layout;
}

} // namespace gpu
