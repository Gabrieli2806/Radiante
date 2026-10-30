#pragma once

#include <atomic>
#include <cstdint>

// What the F3 screen shows of the scene (Pipeline.sceneStats). Written by the render code, read from the game's thread.
namespace SceneStats {
inline std::atomic<uint32_t> tlasInstances{0};
inline std::atomic<uint32_t> entityBlasThisFrame{0};
inline std::atomic<uint32_t> entityBlasLastFrame{0};
} // namespace SceneStats
