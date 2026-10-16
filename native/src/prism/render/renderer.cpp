#include "prism/render/renderer.hpp"

#include "prism/render/buffers.hpp"
#include "prism/render/hdr_output.hpp"
#include "prism/render/render_framework.hpp"
#include "prism/render/textures.hpp"
#include "prism/render/world.hpp"

std::filesystem::path Renderer::folderPath{};
Options Renderer::options{};

Renderer::Renderer(const SharedDeviceHandles &handles)
    : framework_(Framework::create(handles)),
      textures_(Textures::create(framework_)),
      buffers_(Buffers::create(framework_)),
      world_(World::create(framework_)) {}

Renderer::~Renderer() = default;

std::shared_ptr<Framework> Renderer::framework() {
    return framework_;
}

std::shared_ptr<Textures> Renderer::textures() {
    return textures_;
}

std::shared_ptr<Buffers> Renderer::buffers() {
    return buffers_;
}

std::shared_ptr<World> Renderer::world() {
    return world_;
}

void Renderer::close() {
    // Nothing may still be running on the GPU when the HDR output and the world let go of what they hold.
    if (framework_ != nullptr) framework_->waitDeviceIdle();
    HdrOutput::instance().release();

    if (world_ != nullptr) world_->close();
    if (framework_ != nullptr) framework_->close();

    textures_ = nullptr;
    buffers_ = nullptr;
    world_ = nullptr;
    framework_ = nullptr;
}
