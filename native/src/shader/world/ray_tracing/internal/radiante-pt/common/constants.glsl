#ifndef VPT_CONSTANTS_GLSL
#define VPT_CONSTANTS_GLSL

// Reflections rougher than this stop picking up the volumetric clouds (they would only blur away anyway).
// A pack attribute can override the threshold before this file is included.
#ifndef VPT_INDIRECT_VOLUMETRIC_CLOUD_REFLECTION_MAX_ROUGHNESS
#    define VPT_INDIRECT_VOLUMETRIC_CLOUD_REFLECTION_MAX_ROUGHNESS 0.12
#endif

// The primary ray may cross several surfaces before it settles (stacked glass, a translucent cloud entered and
// left again); each crossing costs one step and this caps them.
const int VPT_PRIMARY_TRACE_STEP_LIMIT = 8;
const uint VPT_RAY_FLAGS = gl_RayFlagsNoneEXT;
const uint VPT_TRANSPARENT_SPLIT_MODE_DETERMINISTIC = 0u;
// Lifts a ray leaving the displaced water surface just off it, so it does not hit the same wave again.
const float VPT_FFT_WATER_ORIGIN_BIAS = 0.0006;
// Parallax mapping: grazing views below this cosine are skipped; close surfaces get the larger step budget.
const float VPT_PARALLAX_MIN_VIEW_DOT = 0.001;
const float VPT_PARALLAX_CLOSE_DISTANCE = 48.0;
const int VPT_PARALLAX_PRIMARY_MAX_STEPS = 16;
const int VPT_PARALLAX_SECONDARY_MAX_STEPS = 4;

// Tint of each End portal star layer, deepest first (index 0 also tints the sky layer underneath).
const vec3[] VPT_COLORS = vec3[](
    vec3(0.022087, 0.098399, 0.110818),
    vec3(0.011892, 0.095924, 0.089485),
    vec3(0.027636, 0.101689, 0.100326),
    vec3(0.046564, 0.109883, 0.114838),
    vec3(0.064901, 0.117696, 0.097189),
    vec3(0.063761, 0.086895, 0.123646),
    vec3(0.084817, 0.111994, 0.166380),
    vec3(0.097489, 0.154120, 0.091064),
    vec3(0.106152, 0.131144, 0.195191),
    vec3(0.097721, 0.110188, 0.187229),
    vec3(0.133516, 0.138278, 0.148582),
    vec3(0.070006, 0.243332, 0.235792),
    vec3(0.196766, 0.142899, 0.214696),
    vec3(0.047281, 0.315338, 0.321970),
    vec3(0.204675, 0.390010, 0.302066),
    vec3(0.080955, 0.314821, 0.661491)
);

// Halves the texture coordinates and moves them to the middle of the sheet; applied last to every portal layer.
const mat4 VPT_SCALE_TRANSLATE = mat4(
    0.5, 0.0, 0.0, 0.25,
    0.0, 0.5, 0.0, 0.25,
    0.0, 0.0, 1.0, 0.0,
    0.0, 0.0, 0.0, 1.0
);

// Whether a bounce should see the volumetric clouds: transmission (lobe 2) always does, reflection (lobe 1) only
// off a smooth enough surface, and any other lobe never.
bool vptShouldCaptureIndirectVolumetricCloud(uint lobeType, float roughness) {
    bool isTransmission = lobeType == 2u;
    if (isTransmission) { return true; }
    bool isReflection = lobeType == 1u;
    if (!isReflection) { return false; }
    float roughnessLimit = clamp(VPT_INDIRECT_VOLUMETRIC_CLOUD_REFLECTION_MAX_ROUGHNESS, 0.0, 1.0);
    return roughness <= roughnessLimit;
}

#endif
