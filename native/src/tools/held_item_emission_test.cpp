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
    // A dropped stack may render several model copies, but its owned quads share one calibrated point light.
    auto stack = vertices;
    for (auto &v : stack) v.alphaMode = gpu::Vertex::alphaModeHeldLightFlag;
    for (int copy = 1; copy < 5; ++copy) stack.insert(stack.end(), vertices.begin(), vertices.end());
    for (auto &v : stack) v.alphaMode = gpu::Vertex::alphaModeHeldLightFlag;
    auto marker = vertices[0];
    marker.alphaMode = gpu::Vertex::alphaModePointLightFlag;
    marker.pos = glm::vec3(0.5f);
    marker.colorLayer = glm::vec4(1.0f, 0.5f, 0.1f, 1.0f);
    marker.albedoEmission = 0.25f;
    marker.norm = glm::vec3(0.115f, 0.0f, 0.0f);
    stack.insert(stack.end(), 4, marker);
    lights.clear();
    collectEmissiveQuadLights(emission, stack, 1, 0, occupancy, lights);
    require(lights.size() == 1 && lights[0].kind == 2.0f, "dropped model copies duplicated the point light");
    require(glm::length(lights[0].radiance - glm::vec3(0.25f, 0.125f, 0.025f)) < 1e-6f,
            "dropped point light calibration changed");
    std::cout << "held ownership, ordinary emission and 256 packed flag combinations passed\n";
}
