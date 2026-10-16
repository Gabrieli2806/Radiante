#include "prism/render/framegen/frame_generation.hpp"

#include "prism/render/framegen/reflex.hpp"

#include <atomic>

namespace framegen {
namespace {

// Set when the Java side builds a pipeline, which may be on another thread than the one recording frames.
std::atomic<int> g_depthSlot{-1};
std::atomic<int> g_motionVectorSlot{-1};

} // namespace

void FrameGeneration::setImageSlots(int depthSlot, int motionVectorSlot) {
    g_depthSlot.store(depthSlot);
    g_motionVectorSlot.store(motionVectorSlot);
}

int FrameGeneration::depthSlot() {
    return g_depthSlot.load();
}

int FrameGeneration::motionVectorSlot() {
    return g_motionVectorSlot.load();
}

void FrameGeneration::beginClientFrame() {
    Reflex::beginFrame();
}

void FrameGeneration::marker(int marker) {
    Reflex::marker(marker);
}

} // namespace framegen
