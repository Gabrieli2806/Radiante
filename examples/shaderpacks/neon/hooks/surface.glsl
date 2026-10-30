#ifndef RADIANTE_HOOK_SURFACE_GLSL
#define RADIANTE_HOOK_SURFACE_GLSL

// Neon Dusk: wet ground everywhere, rain or not, so the neon lights and the sky shine back from the streets. Faces
// that look up get darker and glossy, the way radiante-pt's own rain wets the ground (common/rain_wetness.glsl: the
// rough, non-metal ones darken the most as water fills their pores), and in puddles nearly mirror smooth. The
// puddles are a fixed pattern on the world (two octaves of value noise on the absolute x and z, a few blocks across),
// the same in every frame and for every ray, so reflections and bounces see the same wet ground the camera does and
// nothing in it is noise for the denoiser.
//
// Not wetted: water, lava, glass, leaves and plants, what the player holds, glowing and see-through surfaces and
// metals. Lawns get only a little of it: wet grass as glossy as wet stone looked like plastic.
//
// albedoValue (the denoiser's albedo guide) and mat.albedo change by the same ratio, as hooks/surface.glsl asks.
#ifndef NEON_WET
#    define NEON_WET 0.8
#endif
#ifndef NEON_PUDDLES
#    define NEON_PUDDLES 0.5
#endif

float neonHash(ivec2 cell) {
    uint h = uint(cell.x) * 0x8DA6B343u ^ uint(cell.y) * 0xD8163841u;
    h ^= h >> 13u;
    h *= 0x85EBCA6Bu;
    h ^= h >> 16u;
    return float(h & 0xFFFFu) / 65535.0;
}

// Value noise at an absolute position given as whole cells and a fraction, so it stays exact far from the origin.
float neonValueNoise(ivec2 cell, vec2 f) {
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = neonHash(cell);
    float b = neonHash(cell + ivec2(1, 0));
    float c = neonHash(cell + ivec2(0, 1));
    float d = neonHash(cell + ivec2(1, 1));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float neonNoiseAt(dvec2 position, double scale) {
    dvec2 p = position * scale;
    dvec2 whole = floor(p);
    return neonValueNoise(ivec2(whole), vec2(p - whole));
}

// How much of a puddle this point of the ground is, 0 to 1: the noise over a threshold that NEON_PUDDLES lowers.
float neonPuddle(vec3 worldPos, float amount) {
    if (amount <= 0.0) { return 0.0; }
    dvec2 position = dvec2(worldUBO.cameraPos.xz) + dvec2(worldPos.xz);
    float n = 0.65 * neonNoiseAt(position, 0.23LF) + 0.35 * neonNoiseAt(position + dvec2(31.7LF, 17.3LF), 0.61LF);
    float threshold = mix(0.78, 0.38, clamp(amount, 0.0, 1.0));
    return smoothstep(threshold, threshold + 0.06, n);
}

void hookSurface(inout SampledSurface surface, uint surfaceFlags, uint bounce) {
    float wet = clamp(float(NEON_WET), 0.0, 1.0);
    if (wet <= 0.0) { return; }
    if (isWaterSurface(surfaceFlags) || isLavaSurface(surfaceFlags) || isGlassSurface(surfaceFlags) ||
        isPaneSurface(surfaceFlags) || isLeavesSurface(surfaceFlags) || isPlantSurface(surfaceFlags) ||
        isHeldSurface(surfaceFlags)) {
        return;
    }
    if (surface.mat.transmission > 0.0 || surface.mat.emission > 0.0 || surface.mat.metallic > 0.5) { return; }
    // geometricNormal faces the ray: up for a floor seen from above, down for a ceiling seen from below.
    float facing = smoothstep(0.55, 0.9, surface.geometricNormal.y);
    if (facing <= 0.0) { return; }

    vec3 albedo = clamp(surface.albedoValue.rgb, 0.0, 1.0);
    float greenness = clamp((albedo.g - max(albedo.r, albedo.b)) * 6.0, 0.0, 1.0);
    float amount = wet * facing * (1.0 - 0.75 * greenness);
    float puddle = neonPuddle(surface.worldPos, float(NEON_PUDDLES)) * facing * (1.0 - 0.75 * greenness);

    // Darker where the water soaks in (rough, non-metal), darker still under a film of water.
    float absorbency = clamp(surface.mat.roughness * 2.0, 0.0, 1.0);
    float ratio = 1.0 - amount * (0.4 * absorbency + 0.15 * puddle);
    surface.albedoValue.rgb *= ratio;
    surface.mat.albedo *= ratio;

    float damp = mix(surface.mat.roughness, min(surface.mat.roughness, 0.12), amount);
    surface.mat.roughness = mix(damp, min(damp, 0.025), puddle * wet);
    surface.mat.f0 = mix(surface.mat.f0, max(surface.mat.f0, vec3(0.02)), amount);
}

#endif
