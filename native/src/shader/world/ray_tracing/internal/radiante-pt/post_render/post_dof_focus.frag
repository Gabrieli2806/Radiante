#version 460
#extension GL_GOOGLE_include_directive : require

// Depth-of-field focus pass: renders a single value, the focus distance for this frame. With auto focus it looks
// at the depth around the screen centre; the result is then eased towards from last frame's focus so the focal
// plane glides instead of snapping (faster when the jump is large).

#include "common/shared.hpp"

#ifndef VPT_POST_DOF_AUTO_FOCUS
#define VPT_POST_DOF_AUTO_FOCUS 1
#endif

#ifndef VPT_POST_DOF_AUTO_FOCUS_RADIUS
#define VPT_POST_DOF_AUTO_FOCUS_RADIUS 0.012
#endif

#ifndef VPT_POST_DOF_FOCUS_DISTANCE
#define VPT_POST_DOF_FOCUS_DISTANCE 12.0
#endif

#ifndef VPT_POST_DOF_FOCUS_SPEED
#define VPT_POST_DOF_FOCUS_SPEED 0.2
#endif

#ifndef VPT_POST_DOF_FOCUS_HISTORY_READY
#define VPT_POST_DOF_FOCUS_HISTORY_READY false
#endif

#ifndef VPT_POST_DOF_PREVIOUS_FOCUS_BINDING
#define VPT_POST_DOF_PREVIOUS_FOCUS_BINDING 5
#endif

// The manual focus distance is a live setting (a read of worldUBO.packParams), hence the world uniform.
layout(set = 1, binding = 0) uniform WorldUniform {
    WorldUBO worldUBO;
};

layout(set = 3, binding = 1) uniform sampler2D postFirstHitDepth;
layout(set = 4, binding = VPT_POST_DOF_PREVIOUS_FOCUS_BINDING) uniform sampler2D postPreviousDofFocus;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out float outFocusDistance;

// Auto-focus probe pattern around the screen centre: the centre, four axis points and four diagonals on a unit
// circle (scaled by the probe radius).
const vec2 VPT_AUTO_FOCUS_OFFSETS[9] = vec2[](
    vec2(0.0, 0.0),
    vec2(0.0, -1.0),
    vec2(1.0, 0.0),
    vec2(0.0, 1.0),
    vec2(-1.0, 0.0),
    vec2(0.7071, -0.7071),
    vec2(0.7071, 0.7071),
    vec2(-0.7071, 0.7071),
    vec2(-0.7071, -0.7071)
);

// Keeps a UV at least half a texel inside the image.
vec2 clampUv(vec2 uv, vec2 texelSize) {
    vec2 margin = texelSize * 0.5;
    return clamp(uv, margin, vec2(1.0) - margin);
}

// Distance to the first hit: the stored value is view-space z, negative in front of the camera. Zero means sky.
float sampleRawDepth(vec2 uv) {
    return abs(texture(postFirstHitDepth, uv).r);
}

// Ascending insertion sort of the first `count` entries (count is at most 9).
void sortDepths(inout float depths[9], int count) {
    for (int next = 1; next < count; ++next) {
        float key = depths[next];
        int slot = next;
        while (slot > 0 && depths[slot - 1] > key) {
            depths[slot] = depths[slot - 1];
            --slot;
        }
        depths[slot] = key;
    }
}

// Picks a focus distance from the probe pattern. Near the camera the centre probe wins (focus on what the
// crosshair is on); far away it hands over to a robust average of the middle probes, so distant scenery with gaps
// does not make the focus flicker. Returns false when every probe saw sky.
bool autoFocusDistance(vec2 texelSize, out float focus) {
    focus = 0.0;
    float radius = max(VPT_POST_DOF_AUTO_FOCUS_RADIUS, max(texelSize.x, texelSize.y));
    float centre = sampleRawDepth(vec2(0.5));

    float found[9];
    int foundCount = 0;
    for (int probe = 0; probe < 9; ++probe) {
        float depth = sampleRawDepth(clampUv(vec2(0.5) + VPT_AUTO_FOCUS_OFFSETS[probe] * radius, texelSize));
        // Skip sky (zero) probes; written as "not <= 0" so a NaN depth is kept, as it always has been.
        if (!(depth <= 0.0)) {
            found[foundCount] = depth;
            ++foundCount;
        }
    }
    if (foundCount == 0) { return false; }

    sortDepths(found, foundCount);
    int lowMiddle = (foundCount - 1) / 2;
    int highMiddle = foundCount / 2;
    float median = 0.5 * (found[lowMiddle] + found[highMiddle]);

    // With enough probes, widen the median by one neighbour on each side and average that band.
    float band = median;
    if (foundCount >= 5) {
        int first = max(lowMiddle - 1, 0);
        int last = min(highMiddle + 1, foundCount - 1);
        float total = 0.0;
        int taken = 0;
        for (int index = first; index <= last; ++index) {
            total += found[index];
            ++taken;
        }
        band = total / max(taken, 1);
    }

    if (centre <= 0.0) { centre = median; }
    focus = mix(centre, band, smoothstep(16.0, 96.0, max(centre, band)));
    return true;
}

void main() {
    vec2 texelSize = 1.0 / vec2(textureSize(postFirstHitDepth, 0));
    float target = VPT_POST_DOF_FOCUS_DISTANCE;
#if VPT_POST_DOF_AUTO_FOCUS != 0
    float autoFocus;
    if (autoFocusDistance(texelSize, autoFocus)) { target = autoFocus; }
#endif

    // No usable history (first frame, reset, or a sentinel value): take the target as is.
    float previous = texture(postPreviousDofFocus, vec2(0.5)).r;
    bool historyValid = VPT_POST_DOF_FOCUS_HISTORY_READY && !(previous <= 0.0 || previous >= 100000.0);
    if (!historyValid) {
        outFocusDistance = target;
        return;
    }

    // Ease towards the target; jumps that are large relative to the distance speed the easing up.
    float jump = abs(target - previous);
    float urgency = smoothstep(max(0.5, target * 0.04), max(4.0, target * 0.35), jump);
    float rate = clamp(VPT_POST_DOF_FOCUS_SPEED * mix(0.45, 2.5, urgency), 0.0, 1.0);
    outFocusDistance = mix(previous, target, rate);
}
