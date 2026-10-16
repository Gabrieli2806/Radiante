#pragma once

namespace framegen {

// What the rest of the renderer and the Java side reach frame generation through: the pipeline images it reads, and
// the Reflex frame and latency markers (VK_NV_low_latency2).
class FrameGeneration {
  public:
    // Pipeline image slots of the depth and motion vectors behind the frame about to be presented, or -1. The mod
    // picks them from the pipeline graph, so the upscaled variants are used when an upscaler runs.
    static void setImageSlots(int depthSlot, int motionVectorSlot);
    static int depthSlot();
    static int motionVectorSlot();

    // Opens a Reflex frame; called once per client frame, before anything else.
    static void beginClientFrame();
    // A Reflex latency marker for the open frame (a VkLatencyMarkerNV value).
    static void marker(int marker);
};

} // namespace framegen
