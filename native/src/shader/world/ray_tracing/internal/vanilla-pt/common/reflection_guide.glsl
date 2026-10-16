#ifndef VPT_REFLECTION_GUIDE_GLSL
#define VPT_REFLECTION_GUIDE_GLSL

// Directional specular reflectance, not the normal-incidence F0. This is a
// reconstruction guide only; it must never scale the traced radiance.
// EnvBRDFApprox2 fit: NVIDIA Streamline ProgrammingGuideDLSS_RR, section 4.2.1
// (Ray Tracing Gems, chapter 32). Written as scalar products to avoid HLSL/GLSL
// matrix-constructor transposition differences. alpha is linear roughness squared.
vec3 reflectionAlbedoGuide(vec3 f0, float roughness, float noV) {
    float x = clamp(abs(noV), 0.0, 1.0);
    float x2 = x * x;
    float x3 = x2 * x;
    float a = clamp(roughness * roughness, 0.0, 1.0);
    float a3 = a * a * a;
    float bias = ((0.99044 - 1.28514 * x) + a * (1.29678 - 0.755907 * x)) /
        ((1.0 + 2.92338 * x + 59.4188 * x3) +
         a * (20.3225 - 27.0302 * x + 222.592 * x3) +
         a3 * (121.563 + 626.13 * x + 316.627 * x3));
    float scale = ((0.0365463 + 3.32707 * x) + a * (9.0632 - 9.04756 * x)) /
        ((1.0 + 3.59685 * x2 - 1.36772 * x3) +
         a * (9.04401 - 16.3174 * x2 + 9.22949 * x3) +
         a3 * (5.56589 + 19.7886 * x2 - 20.2123 * x3));
    bias *= clamp(f0.g * 50.0, 0.0, 1.0);
    return clamp(f0 * max(scale, 0.0) + max(bias, 0.0), vec3(0.0), vec3(1.0));
}

#endif
