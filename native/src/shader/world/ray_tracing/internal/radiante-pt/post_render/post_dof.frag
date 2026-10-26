#version 460
#extension GL_GOOGLE_include_directive : require

// Depth of field, as one separable 1D gather (run twice, along x then y, via VPT_POST_DOF_DIRECTION_*). Each pixel
// gets a signed circle of confusion (negative in front of the focal plane, positive behind it) with a dead zone
// around focus; taps along the axis are Gaussian-weighted and then cut back where they would let a sharp
// foreground bleed into the background or a background leak over a blurred foreground.

#include "common/shared.hpp"

#ifndef VPT_POST_ENABLE_DOF
#define VPT_POST_ENABLE_DOF 0
#endif

#ifndef VPT_POST_DOF_AUTO_FOCUS
#define VPT_POST_DOF_AUTO_FOCUS 1
#endif

#ifndef VPT_POST_DOF_AUTO_FOCUS_RADIUS
#define VPT_POST_DOF_AUTO_FOCUS_RADIUS 0.012
#endif

#ifndef VPT_POST_DOF_FOCUS_DISTANCE
#define VPT_POST_DOF_FOCUS_DISTANCE 12.0
#endif

#ifndef VPT_POST_DOF_FOCUS_RANGE
#define VPT_POST_DOF_FOCUS_RANGE 4.0
#endif

#ifndef VPT_POST_DOF_MAX_RADIUS
#define VPT_POST_DOF_MAX_RADIUS 6.0
#endif

#ifndef VPT_POST_DOF_SAMPLE_COUNT
#define VPT_POST_DOF_SAMPLE_COUNT 24
#endif

#ifndef VPT_POST_DOF_FOCUS_BINDING
#define VPT_POST_DOF_FOCUS_BINDING 5
#endif

#ifndef VPT_POST_DOF_SOURCE_BINDING
#define VPT_POST_DOF_SOURCE_BINDING 4
#endif

#ifndef VPT_POST_DOF_DIRECTION_X
#define VPT_POST_DOF_DIRECTION_X 1.0
#endif

#ifndef VPT_POST_DOF_DIRECTION_Y
#define VPT_POST_DOF_DIRECTION_Y 0.0
#endif

// Focus distance, focus range and the largest blur radius are live settings (reads of worldUBO.packParams).
layout(set = 1, binding = 0) uniform WorldUniform {
    WorldUBO worldUBO;
};

layout(set = 3, binding = 1) uniform sampler2D postFirstHitDepth;
layout(set = 4, binding = VPT_POST_DOF_SOURCE_BINDING) uniform sampler2D postDofInput;
layout(set = 4, binding = VPT_POST_DOF_FOCUS_BINDING) uniform sampler2D postDofFocus;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

// Auto-focus probe pattern (same as post_dof_focus.frag): centre, four axis points, four diagonals.
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

// Appends a depth unless it is sky (<= 0).
void addValidDepth(inout float depths[9], inout int count, float depthValue) {
    if (depthValue <= 0.0) { return; }
    depths[count] = depthValue;
    ++count;
}

// Depth of a pixel, steadied over a plus-shaped 5-tap footprint: the median, pulled up to 35% towards the two
// nearest taps where the footprint straddles a depth edge, so foreground silhouettes keep their own depth.
float sampleStableDepth(vec2 uv, vec2 texelSize) {
    vec2 centre = clampUv(uv, texelSize);
    vec2 stepX = vec2(texelSize.x, 0.0);
    vec2 stepY = vec2(0.0, texelSize.y);

    float taps[9];
    int tapCount = 0;
    addValidDepth(taps, tapCount, sampleRawDepth(centre));
    addValidDepth(taps, tapCount, sampleRawDepth(clampUv(centre + stepX, texelSize)));
    addValidDepth(taps, tapCount, sampleRawDepth(clampUv(centre - stepX, texelSize)));
    addValidDepth(taps, tapCount, sampleRawDepth(clampUv(centre + stepY, texelSize)));
    addValidDepth(taps, tapCount, sampleRawDepth(clampUv(centre - stepY, texelSize)));
    if (tapCount == 0) { return sampleRawDepth(centre); }

    sortDepths(taps, tapCount);
    float nearest = taps[0];
    float median = taps[tapCount / 2];
    float farthest = taps[tapCount - 1];
    float nearPair = tapCount > 1 ? 0.5 * (taps[0] + taps[1]) : nearest;
    float spread = (farthest - nearest) / max(median, 1.0);
    return mix(median, nearPair, 0.35 * smoothstep(0.02, 0.16, spread));
}

// Distance range over which the blur ramps from zero to full. It grows with the focus distance (a distant focal
// plane needs a much wider range to look natural) and never drops below the configured range.
float computeFocusRange(float focusDistance) {
    float configured = max(VPT_POST_DOF_FOCUS_RANGE, 1e-3);
    float d = max(focusDistance, 0.0);
    float proportional = d * mix(0.25, 1.2, smoothstep(6.0, 48.0, d));
    float scaledConfigured = configured * mix(1.0, 2.2, smoothstep(4.0, 20.0, d));
    return max(configured, scaledConfigured + proportional);
}

// Band around the focal plane that stays perfectly sharp; capped at 85% of the focus range so blur can still ramp.
float computeFocusDeadZone(float focusDistance, float focusRange) {
    float d = max(focusDistance, 0.0);
    float floorZone = max(1.2, VPT_POST_DOF_FOCUS_RANGE * 0.45);
    float zone = max(floorZone, max(d * 0.10, focusRange * 0.45));
    return min(zone, focusRange * 0.85);
}

// Signed circle of confusion in [-1, 1]: zero inside the dead zone, ramping to +-1 at the edge of the focus range.
float computeSignedCoC(float depthValue, float focusDistance) {
    float range = computeFocusRange(focusDistance);
    float deadZone = computeFocusDeadZone(focusDistance, range);
    float offset = depthValue - focusDistance;
    float amount = max(abs(offset) - deadZone, 0.0) / max(range - deadZone, 1e-3);
    return sign(offset) * clamp(amount, 0.0, 1.0);
}

// Blur radius in pixels.
float computeBlurRadius(float depthValue, float focusDistance) {
    return abs(computeSignedCoC(depthValue, focusDistance)) * VPT_POST_DOF_MAX_RADIUS;
}

float gaussianWeight(float distancePixels, float sigma) {
    float variance = max(sigma * sigma, 1e-3);
    return exp(-0.5 * distancePixels * distancePixels / variance);
}

// Fallback focus when the focus pass has no value yet; identical to post_dof_focus.frag's auto focus. Returns
// false when every probe saw sky.
bool autoFocusDistance(vec2 texelSize, out float focus) {
    focus = 0.0;
    float radius = max(VPT_POST_DOF_AUTO_FOCUS_RADIUS, max(texelSize.x, texelSize.y));
    float centre = sampleRawDepth(vec2(0.5));

    float found[9];
    int foundCount = 0;
    for (int probe = 0; probe < 9; ++probe) {
        addValidDepth(found, foundCount,
                      sampleRawDepth(clampUv(vec2(0.5) + VPT_AUTO_FOCUS_OFFSETS[probe] * radius, texelSize)));
    }
    if (foundCount == 0) { return false; }

    sortDepths(found, foundCount);
    int lowMiddle = (foundCount - 1) / 2;
    int highMiddle = foundCount / 2;
    float median = 0.5 * (found[lowMiddle] + found[highMiddle]);

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
    vec2 resolution = vec2(textureSize(postDofInput, 0));
    vec2 texelSize = 1.0 / resolution;
    vec3 centreColour = texture(postDofInput, fragTexCoord).rgb;

#if VPT_POST_ENABLE_DOF == 0
    outColor = vec4(centreColour, 1.0);
    return;
#else
    float focusDistance = VPT_POST_DOF_FOCUS_DISTANCE;
#if VPT_POST_DOF_AUTO_FOCUS != 0
    // Prefer the smoothed focus from the focus pass; values outside (0, 100000) mean it has none yet.
    float smoothedFocus = texture(postDofFocus, vec2(0.5)).r;
    float fallbackFocus;
    if (smoothedFocus > 0.0 && smoothedFocus < 100000.0) {
        focusDistance = smoothedFocus;
    } else if (autoFocusDistance(texelSize, fallbackFocus)) {
        focusDistance = fallbackFocus;
    }
#endif

    vec2 axis = vec2(VPT_POST_DOF_DIRECTION_X, VPT_POST_DOF_DIRECTION_Y);
    float axisLength = length(axis);
    if (axisLength <= 1e-6) {
        outColor = vec4(centreColour, 1.0);
        return;
    }
    axis /= axisLength;

    float centreDepth = sampleStableDepth(fragTexCoord, texelSize);
    float blurRadius = computeBlurRadius(centreDepth, focusDistance);
    if (blurRadius < 0.25) {
        outColor = vec4(centreColour, 1.0);
        return;
    }

    float centreCoC = computeSignedCoC(centreDepth, focusDistance);
    float blurAmount = clamp(blurRadius / max(VPT_POST_DOF_MAX_RADIUS, 1e-3), 0.0, 1.0);
    float focusRange = computeFocusRange(focusDistance);
    int tapLimit = max(VPT_POST_DOF_SAMPLE_COUNT / 2, 4);
    int tapCount = int(clamp(ceil(blurRadius * 1.75), 1.0, float(tapLimit)));
    float sigma = max(blurRadius * 0.55, 0.85);

    // The centre tap is boosted for slight blur (keeps detail) and damped for strong blur (smoother bokeh).
    float centreWeight = gaussianWeight(0.0, sigma) * mix(1.15, 0.75, blurAmount);
    vec3 sum = centreColour * centreWeight;
    float weightSum = centreWeight;

    for (int tap = 1; tap <= tapCount; ++tap) {
        float distancePixels = float(tap);
        float gaussian = gaussianWeight(distancePixels, sigma);
        vec2 stepUv = axis * (distancePixels / resolution);

        // Negative side first, then positive.
        for (int side = -1; side <= 1; side += 2) {
            vec2 uv = clampUv(fragTexCoord + stepUv * float(side), texelSize);
            vec3 colour = texture(postDofInput, uv).rgb;
            float depth = sampleStableDepth(uv, texelSize);
            float tapRadius = computeBlurRadius(depth, focusDistance);
            float tapCoC = computeSignedCoC(depth, focusDistance);

            float tolerance = max(0.45, min(centreDepth, focusDistance) * 0.028 + focusRange * 0.18);
            float depthGap = depth - centreDepth;
            float behind = max(depthGap, 0.0);

            // A tap behind the centre is mostly rejected (a sharp object must not pick up the background), but
            // strongly blurred pixels may still borrow a little from far behind them.
            float behindBase = 1.0 - smoothstep(tolerance * 0.9, tolerance * 6.0, behind);
            float bleed = smoothstep(tolerance * 0.35, tolerance * 4.5, behind) * smoothstep(0.12, 0.75, blurAmount);
            float behindWeight = mix(behindBase, 1.0, 0.32 * bleed);
            // A tap in front only counts as far as its own blur actually reaches this pixel.
            float frontReach = smoothstep(distancePixels - 1.4, distancePixels + 1.4, tapRadius + 0.45);
            float frontWeight = mix(0.28, 1.0, frontReach);
            float occlusion = depth < centreDepth ? frontWeight : behindWeight;

            float continuity = 1.0 - smoothstep(tolerance * 1.8, tolerance * 7.5, abs(depthGap));
            float cocTolerance = mix(0.75, 2.5, blurAmount);
            float similarCoC = 1.0 - smoothstep(cocTolerance, cocTolerance * 2.8, abs(tapCoC - centreCoC));
            // Taps on the other side of the focal plane are damped.
            float crossesFocus = tapCoC * centreCoC < -0.001 ? 0.3 : 1.0;
            float reach = smoothstep(distancePixels - 1.35, distancePixels + 1.7, max(tapRadius, blurRadius) + 0.55);

            float weight = gaussian * occlusion * mix(0.7, 1.0, continuity) * mix(0.4, 1.0, similarCoC) *
                           crossesFocus * mix(0.45, 1.0, reach);
            sum += colour * weight;
            weightSum += weight;
        }
    }

    outColor = vec4(sum / max(weightSum, 1e-4), 1.0);
#endif
}
