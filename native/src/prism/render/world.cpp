#include "prism/render/world.hpp"

#include "prism/render/chunks.hpp"
#include "prism/render/entities.hpp"

World::World(std::shared_ptr<Framework> framework)
    : chunks_(Chunks::create(framework)), entities_(Entities::create(std::move(framework))) {}

void World::resetFrame() {}

bool &World::shouldRender() {
    return shouldRenderWorld_;
}

std::shared_ptr<Chunks> World::chunks() const {
    return chunks_;
}

std::shared_ptr<Entities> World::entities() const {
    return entities_;
}

void World::setCameraPos(glm::dvec3 cameraPos) {
    cameraPos_ = cameraPos;
}

glm::dvec3 World::getCameraPos() const {
    return cameraPos_;
}

void World::close() {
    shouldRenderWorld_ = false;
    chunks_->close();
    entities_->close();
}
