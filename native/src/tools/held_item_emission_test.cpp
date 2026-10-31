#include "prism/gpu/vertex.hpp"
#include "prism/render/emission.hpp"

#include <iostream>
#include <stdexcept>

void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    Emission emission({});
    EmissionCellUpload cell{0, 0, 1, 1, 1, 1, 1, 1};
    emission.updateTile(1, 1, &cell, 1);
    std::vector<gpu::VertexFormat::PBRVertex> vertices(4);
    for (unsigned i = 0; i < vertices.size(); ++i) {
        const float x = i == 1 || i == 2 ? 1.0f : 0.0f;
        const float y = i >= 2 ? 1.0f : 0.0f;
        vertices[i].pos = {x, y, 0};
        vertices[i].textureUV = {x, y};
        vertices[i].textureID = 1;
        vertices[i].useTexture = 1;
    }
    std::unordered_map<uint32_t, Emission::TextureOccupancy> occupancy;
    std::vector<LightInfo> lights;
    collectEmissiveQuadLights(emission, vertices, 1, 0, occupancy, lights);
    require(!lights.empty(), "ordinary emissive items must still supply light");
    const auto ordinaryCount = lights.size();
    for (auto &vertex : vertices) {
        vertex.alphaMode |= gpu::Vertex::alphaModeHeldLightFlag;
    }
    lights.clear();
    collectEmissiveQuadLights(emission, vertices, 1, 0, occupancy, lights);
    require(lights.empty(), "a dedicated held light must not also supply mesh lights");

    // The ownership flag must survive packing without changing older surface flags or coordinates.
    auto &vertex = vertices[0];
    for (uint32_t coordinate = 0; coordinate < 16; ++coordinate) {
        for (uint32_t mode = 0; mode < 16; ++mode) {
            vertex.coordinate = coordinate;
            vertex.alphaMode = mode | gpu::Vertex::alphaModeGlassFlag | gpu::Vertex::alphaModeFrostFlag |
                               gpu::Vertex::alphaModeGlowLightFlag;
            const uint32_t ordinary = gpu::Vertex::packMaterialFlags(vertex);
            vertex.alphaMode |= gpu::Vertex::alphaModeHeldLightFlag;
            const uint32_t held = gpu::Vertex::packMaterialFlags(vertex);
            require((ordinary ^ held) == gpu::Vertex::heldLightSurfaceBit, "held ownership changed other fields");
        }
    }
    for (auto &v : vertices) v.alphaMode = 0;
    lights.clear();
    collectEmissiveQuadLights(emission, vertices, 1, 0, occupancy, lights);
    require(lights.size() == ordinaryCount, "reused geometry must recover ordinary emission");
    std::cout << "held ownership, ordinary emission and 256 packed flag combinations passed\n";
}
