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
        float x = i == 1 || i == 2 ? 1.0f : 0.0f;
        float y = i >= 2 ? 1.0f : 0.0f;
        vertices[i].pos = {x, y, 0};
        vertices[i].textureUV = {x, y};
        vertices[i].textureID = 1;
        vertices[i].useTexture = 1;
        vertices[i].lightUV = {240, 176};
    }
    for (unsigned coordinate = 0; coordinate < 16; ++coordinate) {
        for (unsigned percent = 0; percent <= 1000; ++percent) {
            auto vertex = vertices[0];
            vertex.coordinate = coordinate | ((percent + 1) << 4);
            auto packed = gpu::Vertex::makeMaterialVertex(vertex);
            bool overridden = (packed.packedData & gpu::Vertex::emissionOverrideBit) != 0;
            require(overridden == (percent != 100), "default material encoding changed");
            require(((packed.packedData >> 12) & 15) == coordinate, "coordinate was overwritten");
            unsigned decoded = overridden ? ((packed.light >> 8) & 255) | ((packed.light >> 24) << 8) : 100;
            require(decoded == percent, "emission percentage lost precision");
            require((packed.light & 255) == 240 && ((packed.light >> 16) & 255) == 176, "light UVs changed");
        }
    }
    std::unordered_map<uint32_t, Emission::TextureOccupancy> occupancy;
    for (unsigned kind = 0; kind < 3; ++kind) {
        std::vector<LightInfo> baseline;
        for (unsigned percent : {100u, 1000u, 0u}) {
            for (auto &vertex : vertices) {
                vertex.coordinate = (percent + 1) << 4;
                vertex.alphaMode = kind == 1 ? gpu::Vertex::alphaModePointLightFlag :
                                   kind == 2 ? gpu::Vertex::alphaModeGlowLightFlag : 0;
                vertex.colorLayer = glm::vec4(1);
                vertex.albedoEmission = 1;
                vertex.postBase.y = 1;
                vertex.postBase.z = std::bit_cast<float>(0xFFFFFFu);
            }
            std::vector<LightInfo> lights;
            collectEmissiveQuadLights(emission, vertices, 1, 0, occupancy, lights);
            if (percent == 0) require(lights.empty(), "zero percent still emits light");
            else if (percent == 100) {
                require(!lights.empty(), "default light disappeared");
                baseline = lights;
            } else {
                require(lights.size() == baseline.size(), "scaling changed light geometry");
                for (size_t i = 0; i < lights.size(); ++i) {
                    require(glm::length(lights[i].radiance - baseline[i].radiance * 10.0f) < 1e-5f,
                            "tenfold emission was clipped or applied twice");
                }
            }
        }
    }
    std::cout << "16016 packed percentages and 0/100/1000% surface, point and vertex lights passed\n";
}
