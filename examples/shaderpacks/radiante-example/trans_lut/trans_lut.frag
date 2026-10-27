#version 460
#extension GL_GOOGLE_include_directive : require

// Bakes the atmosphere transmittance lookup table. x maps the cosine of the view angle from -1 to 1, y the
// altitude from the ground radius (RG) to the top of the atmosphere (RT). Each texel holds the transmittance from
// that point to the edge of the atmosphere, from Rayleigh plus Mie extinction integrated over 128 midpoint steps.

#include "common/shared.hpp"

layout(location = 0) in vec2 texCoord;

layout(location = 0) out vec4 outColor;

// Ray against a sphere centred at the origin (direction must be unit length). Returns false on a miss; otherwise
// the entry and exit distances, either of which may be negative.
bool intersectSphere(vec3 rayOrigin, vec3 rayDir, float radius, out float tNear, out float tFar) {
    float halfB = dot(rayOrigin, rayDir);
    float discriminant = halfB * halfB - (dot(rayOrigin, rayOrigin) - radius * radius);
    if (discriminant < 0.0) return false;
    float root = sqrt(discriminant);
    tNear = -halfB - root;
    tFar = -halfB + root;
    return true;
}

// Exponential density profile; below the ground it stays at the sea-level value.
float densityExp(float height, float scaleHeight) {
    return exp(-max(height, 0.0) / scaleHeight);
}

void main() {
    float cosView = texCoord.x * 2.0 - 1.0;
    float radius = mix(VPT_ATMOSPHERE_RG, VPT_ATMOSPHERE_RT, texCoord.y);

    // Work in the plane of the ray: start on the y axis, lean towards +x by the view angle.
    vec3 start = vec3(0.0, radius, 0.0);
    vec3 direction = normalize(vec3(sqrt(max(1.0 - cosView * cosView, 0.0)), cosView, 0.0));

    float enter, exit;
    if (!intersectSphere(start, direction, VPT_ATMOSPHERE_RT, enter, exit)) {
        outColor = vec4(1.0);
        return;
    }
    enter = max(enter, 0.0);

    const int STEP_COUNT = 128;
    float stepLength = (exit - enter) / float(STEP_COUNT);
    vec3 depth = vec3(0.0);
    for (int stepIndex = 0; stepIndex < STEP_COUNT; stepIndex++) {
        vec3 point = start + direction * (enter + (float(stepIndex) + 0.5) * stepLength);
        float altitude = length(point) - VPT_ATMOSPHERE_RG;
        vec3 extinction = VPT_ATMOSPHERE_BETA_R * densityExp(altitude, VPT_ATMOSPHERE_HR) +
                          VPT_ATMOSPHERE_BETA_M * densityExp(altitude, VPT_ATMOSPHERE_HM);
        depth += extinction * stepLength;
    }

    outColor = vec4(exp(-depth), 1.0);
}
