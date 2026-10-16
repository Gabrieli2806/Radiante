#pragma once

#include "prism/all_extern.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <iosfwd>
#include <memory>

class Framework;
class NgxContext;

namespace gpu {
class Image;
} // namespace gpu

namespace framegen {

// The camera of the rendered frame the generated ones are made from.
struct CameraFrame {
    glm::mat4 viewToClip{1.0f};
    glm::mat4 clipToView{1.0f};
    glm::mat4 clipToPrevClip{1.0f};
    glm::mat4 prevClipToClip{1.0f};
    // The camera's sub-pixel jitter; zero once depth and motion vectors were upscaled, which removes it.
    glm::vec2 jitter{0.0f};
    glm::vec3 position{0.0f};
    glm::vec3 right{1.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    float verticalFov = 1.0f; // radians
    float aspect = 1.0f;      // width over height
};

// What one evaluation generates from. The images are in SHADER_READ_ONLY_OPTIMAL.
struct GenerationInputs {
    gpu::Image *finalColor = nullptr;    // world plus UI: what the player sees
    gpu::Image *hudless = nullptr;       // the world alone, in the final image's size and format
    gpu::Image *depth = nullptr;         // device depth between kCameraNear and kCameraFar
    gpu::Image *motionVectors = nullptr; // in pixels
    uint32_t displayWidth = 0;
    uint32_t displayHeight = 0;
    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;
    CameraFrame camera;
    float frameTimeMs = 16.67f;
    // The history does not carry on from the last evaluation.
    bool reset = false;
};

// The planes the depth handed to the backends is rebuilt with from the traced hit distance
// (linear_to_device_depth.comp).
inline constexpr float kCameraNear = 0.05f;
inline constexpr float kCameraFar = 16384.0f;

// A frame generation backend: DLSS (NGX) or FSR (FidelityFX).
class Generator {
  public:
    virtual ~Generator() = default;

    // Records generated frame `index` (counted from 1) of `count` into `output`, which is in GENERAL. False when
    // nothing usable was recorded.
    virtual bool generate(VkCommandBuffer cmd,
                          const GenerationInputs &inputs,
                          gpu::Image &output,
                          uint32_t count,
                          uint32_t index) = 0;
    // Frees what the backend made; the next generate makes it again.
    virtual void release() = 0;
};

// Both wait for the framework's queue before freeing what frames in flight may still use.
std::unique_ptr<Generator> makeDlssGenerator(std::shared_ptr<NgxContext> ngx, std::weak_ptr<Framework> framework);
// Null in builds without FidelityFX, where FSR frame generation is never offered.
std::unique_ptr<Generator> makeFsrGenerator(std::weak_ptr<Framework> framework);

// radiante::out() after frame generation's "[Frame Generation] " prefix.
std::ostream &out();

} // namespace framegen
