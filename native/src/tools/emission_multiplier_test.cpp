#include "prism/gpu/vertex.hpp"
#include "prism/render/emission.hpp"
#include <iostream>
#include <stdexcept>

void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }

void tint(gpu::VertexFormat::PBRVertex &vertex, glm::vec3 colour) {
    uint32_t word = glm::packF3x9_E1x5(colour);
    vertex.coordinate = (vertex.coordinate & 15u) | (word << 4u);
    vertex.alphaMode = (vertex.alphaMode & 0xFFFFFFu) | gpu::Vertex::alphaModeEmissionTintFlag | ((word >> 28u) << 24u);
}

int main() {
    Emission emission({});
    EmissionCellUpload cell{0, 0, 1, 1, 1, 1, 1, 1};
    emission.updateTile(1, 1, &cell, 1);
    std::vector<gpu::VertexFormat::PBRVertex> vertices(4);
    for (unsigned i = 0; i < 4; ++i) {
        float x = i == 1 || i == 2 ? 1 : 0, y = i >= 2 ? 1 : 0;
        vertices[i].pos = {x,y,0}; vertices[i].textureUV = {x,y};
        vertices[i].textureID = 1; vertices[i].useTexture = 1;
        vertices[i].lightUV = {240,176}; vertices[i].overlayUV = {10,3};
    }
    require(glm::packF3x9_E1x5(glm::vec3(1)) == 0x84020100u, "RGB9_E5 standard changed");
    for (uint32_t coordinate = 0; coordinate < 16; ++coordinate) {
        for (float gain : {0.0f, 0.0001f, 0.1f, 1.0f, 10.0f, 256.0f, 10000.0f, 65408.0f}) {
            auto v = vertices[0]; v.coordinate = coordinate;
            tint(v, glm::vec3(gain * 0.9f, gain * 0.95f, gain));
            auto p = gpu::Vertex::makeMaterialVertex(v);
            uint32_t word = ((p.light >> 8u) & 255u) | ((p.light >> 24u) << 8u) |
                            (((p.overlay >> 8u) & 255u) << 16u) | ((p.overlay >> 24u) << 24u);
            require(word == gpu::packedEmissionTint(v), "HDR colour lost in packing");
            require(((p.packedData >> 12u) & 15u) == coordinate, "coordinate overwritten");
            require((p.light & 255u) == 240 && ((p.light >> 16u) & 255u) == 176, "light UV changed");
            require((p.overlay & 255u) == 10 && ((p.overlay >> 16u) & 255u) == 3, "overlay UV changed");
            require(glm::length(glm::unpackF3x9_E1x5(word) - gpu::emissionTint(v)) == 0, "CPU/GPU colour mismatch");
        }
    }
    auto neutral = gpu::Vertex::makeMaterialVertex(vertices[0]);
    require((neutral.packedData & gpu::Vertex::emissionOverrideBit) == 0, "default flags changed");
    std::unordered_map<uint32_t, Emission::TextureOccupancy> occupancy;
    for (int kind = 0; kind < 3; ++kind) {
        std::vector<LightInfo> baseline;
        for (auto colour : {glm::vec3(1), glm::vec3(0,2,10), glm::vec3(0)}) {
            for (auto &v : vertices) {
                v.alphaMode = kind == 1 ? gpu::Vertex::alphaModePointLightFlag : kind == 2 ? gpu::Vertex::alphaModeGlowLightFlag : 0;
                v.colorLayer = glm::vec4(1); v.albedoEmission = 1; v.postBase.y = 1;
                v.postBase.z = std::bit_cast<float>(0xFFFFFFu); tint(v, colour);
            }
            std::vector<LightInfo> lights;
            collectEmissiveQuadLights(emission, vertices, 1, 0, occupancy, lights);
            if (colour == glm::vec3(0)) require(lights.empty(), "black still emits");
            else if (colour == glm::vec3(1)) { require(!lights.empty(), "default light missing"); baseline = lights; }
            else {
                require(lights.size() == baseline.size(), "tint changes light geometry");
                for (size_t i = 0; i < lights.size(); ++i)
                    require(glm::length(lights[i].radiance - baseline[i].radiance * colour) < 1e-5f, "HDR tint clipped or applied twice");
            }
        }
    }
    std::cout << "HDR RGB9_E5 packing, coordinates, light/overlay UVs and surface/point/vertex radiance passed\n";
}
