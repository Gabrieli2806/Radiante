#ifndef VPT_BEDROCK_SOLAR_BRDF_GLSL
#define VPT_BEDROCK_SOLAR_BRDF_GLSL

// Bedrock RTX's observed sun highlight.
// Direct sunlight uses this regularized distribution/visibility, not the
// correlated Smith GGX used to sample a reflected ray. Keep both contracts.
float bedrockSolarDistribution(float roughness, float noH) {
    float r = max(roughness, 0.01);
    float a = r * r;
    float a2 = a * a;
    float d = (noH * a2 - noH) * noH + 1.0;
    return a2 / (d * d + 1e-10) * 0.318309873;
}

float bedrockSolarVisibility(float roughness, float noV, float noL) {
    float r = max(roughness, 0.01);
    float k = r * r * 0.5;
    float v = noV * (1.0 - k) + k;
    float l = noL * (1.0 - k) + k;
    return 0.25 / (v * l + 1e-10);
}

vec3 bedrockSolarBrdf(LabPBRMat mat, vec3 V, vec3 N, vec3 L) {
    float noV = clamp(dot(N, V), 0.0, 1.0);
    float noL = clamp(dot(N, L), 0.0, 1.0);
    vec3 halfVector = V + L;
    if (noV <= 0.0 || noL <= 0.0 || dot(halfVector, halfVector) <= 1e-10) { return vec3(0.0); }
    vec3 H = normalize(halfVector);
    float noH = clamp(dot(N, H), 0.0, 1.0);
    float voH = clamp(dot(V, H), 0.0, 1.0);
    vec3 f0 = clamp(mat.f0, 0.0, 1.0);
    float f90 = clamp(50.0 * dot(f0, vec3(0.3, 0.59, 0.11)), 0.0, 1.0);
    vec3 fresnel = f0 + (vec3(f90) - f0) * pow(1.0 - voH, 5.0);
    float r = max(mat.roughness, 0.01);
    float fd90 = 2.0 * r * voH * voH - 0.5;
    float diffuse = (1.0 + fd90 * pow(1.0 - noV, 5.0)) *
                    (1.0 + fd90 * pow(1.0 - noL, 5.0)) * 0.318309873;
    // Material diffuse weight remains Radiante's; AO/cache inputs are not the
    // same buffers as Bedrock's final pass. Do not claim full shader parity.
    vec3 diffuseColour = mat.albedo * (1.0 - mat.metallic) * max(1.0 - mat.emission, 0.0);
    vec3 specular = fresnel * bedrockSolarDistribution(r, noH) * bedrockSolarVisibility(r, noV, noL);
    return (diffuseColour * (vec3(1.0) - fresnel) * diffuse + specular) * noL;
}

#endif
