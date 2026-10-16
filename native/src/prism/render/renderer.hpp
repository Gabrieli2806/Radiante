#pragma once

#include "common/singleton.hpp"
#include "prism/all_extern.hpp"
#include "prism/gpu/gpu.hpp"

#include <filesystem>
#include <memory>

class Textures;
class Framework;
struct SharedDeviceHandles;
class Buffers;
class World;

// Settings the game pushes down through the Options JNI calls.
struct Options {
    uint32_t maxFps = 1'000'000;
    uint32_t inactivityFpsLimit = 1'000'000;
    bool vsync = true;
    // Set when a setting needs the world pipeline rebuilt; the next frame (or recreatePending) rebuilds it.
    bool needRecreate = false;

    uint32_t chunkBuildingBatchSize = 2;
    uint32_t chunkBuildingTotalBatches = 4;
    bool collectChunkEmission = false;
    // How far from the camera glowing entities (dropped items, item frames) still light their surroundings, in
    // blocks.
    double entityLightReach = 64.0;
};

// The renderer as the game sees it: the frame framework and the textures, buffers and world it draws. One per
// process, made when Minecraft's device exists (initRenderer) and closed with the game.
class Renderer : public Singleton<Renderer> {
    friend class Singleton<Renderer>;

  public:
    // Where the mod unpacked its native files (shaders, DLSS libraries) and keeps its caches.
    static std::filesystem::path folderPath;
    static Options options;

    ~Renderer();

    std::shared_ptr<Framework> framework();
    std::shared_ptr<Textures> textures();
    std::shared_ptr<Buffers> buffers();
    std::shared_ptr<World> world();

    // Waits for the GPU and releases everything; the accessors return null afterwards.
    void close();

  private:
    explicit Renderer(const SharedDeviceHandles &handles);

    std::shared_ptr<Framework> framework_;
    std::shared_ptr<Textures> textures_;
    std::shared_ptr<Buffers> buffers_;
    std::shared_ptr<World> world_;
};
