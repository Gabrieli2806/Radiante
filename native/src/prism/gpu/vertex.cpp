#include "prism/gpu/vertex.hpp"

#include <cstddef>

namespace gpu {

std::vector<VertexFormat::PositionVertex>
Vertex::buildPositionVertices(const std::vector<VertexFormat::PBRVertex> &vertices) {
    std::vector<VertexFormat::PositionVertex> packed;
    packed.reserve(vertices.size());
    for (const auto &vertex : vertices) packed.push_back(makePositionVertex(vertex));
    return packed;
}

std::vector<VertexFormat::PackedMaterialVertex>
Vertex::buildMaterialVertices(const std::vector<VertexFormat::PBRVertex> &vertices) {
    std::vector<VertexFormat::PackedMaterialVertex> packed;
    packed.reserve(vertices.size());
    for (const auto &vertex : vertices) packed.push_back(makeMaterialVertex(vertex));
    return packed;
}

template <>
VertexLayoutInfo &Vertex::vertexLayoutInfo<VertexFormat::Triangle>() {
    using V = VertexFormat::Triangle;
    static VertexLayoutInfo layout = initVertexLayout<V>({
        {VK_FORMAT_R32G32B32_SFLOAT, offsetof(V, pos)},
        {VK_FORMAT_R32G32B32_SFLOAT, offsetof(V, color)},
    });
    return layout;
}

template <>
VertexLayoutInfo &Vertex::vertexLayoutInfo<VertexFormat::TexturedTriangle>() {
    using V = VertexFormat::TexturedTriangle;
    static VertexLayoutInfo layout = initVertexLayout<V>({
        {VK_FORMAT_R32G32B32_SFLOAT, offsetof(V, pos)},
        {VK_FORMAT_R32G32_SFLOAT, offsetof(V, uv)},
    });
    return layout;
}

template <>
VertexLayoutInfo &Vertex::vertexLayoutInfo<VertexFormat::ArrayTexturedTriangle>() {
    using V = VertexFormat::ArrayTexturedTriangle;
    static VertexLayoutInfo layout = initVertexLayout<V>({
        {VK_FORMAT_R32G32B32_SFLOAT, offsetof(V, pos)},
        {VK_FORMAT_R32G32_SFLOAT, offsetof(V, uv)},
        {VK_FORMAT_R32_SFLOAT, offsetof(V, textureLayer)},
    });
    return layout;
}

} // namespace gpu
