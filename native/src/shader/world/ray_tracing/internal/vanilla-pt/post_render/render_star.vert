#version 460
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : require

// Star dome vertex stage. The star mesh is built around +x; it is turned so that +x points at the sun, which makes
// the stars wheel across the sky with the time of day. Stars fade in as the sun drops below the horizon.

#include "common/shared.hpp"

layout(set = 1, binding = 0) uniform WorldUniform {
    WorldUBO worldUBO;
};

layout(set = 1, binding = 1) uniform SkyUniform {
    SkyUBO skyUBO;
};

layout(location = 0) in vec3 inPos;
layout(location = 4) in vec4 inColorLayer;

layout(location = 0) out vec3 outPos;
layout(location = 1) out vec4 outColorLayer;
layout(location = 2) out vec2 outScreenUv;

// Rotation taking +x onto the given unit direction (Rodrigues' formula). Nearly aligned directions keep the
// identity; nearly opposite ones use a half turn about y, where the cross product is too small to give an axis.
mat3 rotateXOnto(vec3 target) {
    const vec3 meshAxis = vec3(1.0, 0.0, 0.0);
    float cosAngle = clamp(dot(meshAxis, target), -1.0, 1.0);
    if (cosAngle < -0.9999) { return mat3(-1, 0, 0, 0, 1, 0, 0, 0, -1); }
    // Written as "not <=" so that a NaN direction also keeps the identity.
    if (!(cosAngle <= 0.9999)) { return mat3(1.0); }

    vec3 perpendicular = cross(meshAxis, target);
    vec3 axis = normalize(perpendicular);
    float sinAngle = length(perpendicular);
    // Cross-product matrix of the axis (column-major).
    mat3 k = mat3(0.0, axis.z, -axis.y,
                  -axis.z, 0.0, axis.x,
                  axis.y, -axis.x, 0.0);
    return mat3(1.0) + k * sinAngle + (k * k) * (1.0 - cosAngle);
}

void main() {
    vec3 position = rotateXOnto(normalize(skyUBO.sunDirection)) * inPos;
    outPos = position;

    vec4 clip = worldUBO.cameraProjMat * worldUBO.cameraEffectedViewMat * vec4(position, 1.0);
    gl_Position = clip;
    // Screen UV for the cloud-transmittance lookup in the fragment stage.
    outScreenUv = clip.xy / max(clip.w, 1e-4) * 0.5 + 0.5;

    // Fully visible once the sun is 0.2 below the horizon (in direction y).
    float night = smoothstep(0.0, -0.2, skyUBO.sunDirection.y);
    outColorLayer = vec4(inColorLayer.rgb * night, night);
}
