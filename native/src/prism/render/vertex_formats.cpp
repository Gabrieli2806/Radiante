// The vertex layout of PBRVertex (common/shared.hpp), for the graphics pipelines that read it.

#include "prism/gpu/vertex.hpp"

#include <cstddef>

namespace gpu {

// The world vertex Java sends, as the star pass rasterizes it. albedoEmission is not read, and
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
