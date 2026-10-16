#pragma once

#include "common/shared.hpp"
#include "common/singleton.hpp"
#include "prism/all_extern.hpp"
#include "prism/gpu/gpu.hpp"

#include <condition_variable>
#include <future>
#include <thread>

class Framework;
class Chunks;
class Entities;

// The world as the renderer traces it: the chunk sections, the entities, and where the camera is.
class World : public SharedObject<World> {
  public:
    // Vertex layouts geometry may come in. The values are shared with the Java side (NativeGeometry.VERTEX_FORMAT_PBR
    // is PBR_TRIANGLE).
    enum VertexFormats {
        POSITION_COLOR_TEXTURE_LIGHT_NORMAL,
        POSITION_COLOR_TEXTURE_OVERLAY_LIGHT_NORMAL,
        POSITION_TEXTURE_COLOR_LIGHT,
        POSITION,
        POSITION_COLOR,
        LINES,
        POSITION_COLOR_LIGHT,
        POSITION_TEXTURE,
        POSITION_TEXTURE_COLOR,
        POSITION_COLOR_TEXTURE_LIGHT,
        POSITION_TEXTURE_LIGHT_COLOR,
        POSITION_TEXTURE_COLOR_NORMAL,
        PBR_TRIANGLE,
        NUM_VERTEX_FORMATS,
    };

    // What a geometry is to the tracer (NativeGeometry.GEOMETRY_TYPE_*).
    enum GeometryTypes {
        SHADOW,
        WORLD_SOLID,
        WORLD_TRANSPARENT,
        WORLD_NO_REFLECT,
        WORLD_CLOUD,
        BOAT_WATER_MASK,
        END_PORTAL,
        END_GATE_WAY,
        NUM_GEOMETRY_TYPES,
    };

    // The space vertices are given in (NativeGeometry.COORDINATE_*): the world, the camera's view, or world axes
    // centred on the camera.
    enum Coordinates {
        WORLD,
        CAMERA,
        CAMERA_SHIFT,
    };

    // Minecraft's draw modes, in the order of its VertexFormat.Mode (NativeGeometry.DRAW_MODE_QUADS is QUADS).
    enum class DrawMode {
        LINES,
        LINE_STRIP,
        DEBUG_LINES,
        DEBUG_LINE_STRIP,
        TRIANGLES,
        TRIANGLE_STRIP,
        TRIANGLE_FAN,
        QUADS,
    };

    explicit World(std::shared_ptr<Framework> framework);

    // Nothing of the world itself changes per frame; the framework resets the chunks and the entities on its own.
    void resetFrame();
    // Whether frames trace the world: the Java side sets it for each level frame it records, close() clears it.
    bool &shouldRender();

    std::shared_ptr<Chunks> chunks() const;
    std::shared_ptr<Entities> entities() const;

    void setCameraPos(glm::dvec3 cameraPos);
    glm::dvec3 getCameraPos() const;

    void close();

  private:
    std::shared_ptr<Chunks> chunks_;
    std::shared_ptr<Entities> entities_;

    glm::dvec3 cameraPos_ = {0, 0, 0};

    bool shouldRenderWorld_ = false;
};
