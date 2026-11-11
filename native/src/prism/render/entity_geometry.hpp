#pragma once

// Reading an entities task as Java hands it over and turning its geometries into indexed triangles in the PBR
// layout (entity_geometry.cpp). For entities.cpp, which builds the structures from them; not for the rest of the
// renderer.

#include "prism/render/entities.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace entity_geometry {

using PBRVertex = gpu::VertexFormat::PBRVertex;

// One entity of a task, with where its geometries start in the geometry arrays.
struct EntityInput {
    int hashCode;
    double x, y, z;
    int rayTracingFlag;
    int prebuiltBLAS;
    size_t firstGeometry;
    int geometryCount;
};

EntityInput readEntity(const EntitiesBuildTask &task, int entity, size_t firstGeometry);
size_t vertexCount(const EntitiesBuildTask &task, size_t geometry);
std::string_view groupName(const EntitiesBuildTask &task, size_t geometry);
std::string_view contentName(const EntitiesBuildTask &task, size_t geometry);

// The identity of an entity's content, for finding it among the structures already built: by the names Java gives
// it (KEYED_BLAS), or by everything that shapes its triangles.
uint64_t keyedContentKey(const EntitiesBuildTask &task, const EntityInput &entity);
uint64_t cacheableContentKey(const EntitiesBuildTask &task, const EntityInput &entity);

// What every vertex a geometry keeps is told about where it is.
struct Placement {
    World::Coordinates coordinate;
    bool normalOffset;
    float lineWidth;
};

Placement placementOf(const EntitiesBuildTask &task, const EntityInput &entity);
void place(PBRVertex &vertex, const Placement &placement);

// Working memory for turning one geometry into triangles, reused from geometry to geometry and frame to frame.
struct TriangleScratch {
    std::vector<PBRVertex> vertices;
    std::vector<PBRVertex> assembled;
    std::vector<uint32_t> indices;
};

// Turns one geometry of the task into indexed triangles in the PBR layout, left in scratch.vertices and
// scratch.indices. False when it has none; such geometries are left out of the entity.
bool triangulate(const EntitiesBuildTask &task, size_t geometry, const Placement &placement, TriangleScratch &scratch);

} // namespace entity_geometry
