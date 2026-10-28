#ifndef RADIANTE_HOOK_SKY_GLSL
#define RADIANTE_HOOK_SKY_GLSL

// Mellow port: Mellow's volumetric clouds, drawn into the sky every ray that leaves the world sees, so they show in
// the sky, in reflections and in the light the sky casts. Ported from Mellow by TheCMK (MIT, LICENSE-MELLOW):
// get_clouds_volumetric in shaders/global/atmosphere/clouds.glsl and xlf_phase in shaders/lib/util.glsl.
//
// What changed for Radiante: Mellow reads its noise from noisetex and colortex6, images Iris gives it; here the same
// shapes come from procedural value noise at the same scales. Mellow lights clouds with its own sun and sky colours;
// here the sun is Radiante's (VPT_SUN_RADIANCE through a simple airmass falloff) and the ambient is the clear sky
// overhead. Radiante PT's own clouds are off by default in this pack (configs.json).
#ifndef MELLOW_CLOUDS
#    define MELLOW_CLOUDS 1
#endif
#ifndef MELLOW_CLOUD_AMOUNT
#    define MELLOW_CLOUD_AMOUNT 22
#endif
#ifndef MELLOW_CLOUD_SPEED
#    define MELLOW_CLOUD_SPEED 2
#endif

#define MELLOW_PI 3.14159265
#define MELLOW_ISOTROPIC_PHASE (0.25 / MELLOW_PI)

float mellowPhase(float angle, const float g) {
    float g2 = g * g;
    const float k = 3.0 / 2.0;
    float denom = (1.0 + g2 - 2.0 * g * angle);
    float result = k * ((1.0 - g2) / (2.0 + g2)) * ((1.0 + angle * angle) / denom) + g * angle;
    return 1.0 / (4.0 * MELLOW_PI) * result;
}

float mellowHash(vec3 p) {
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

// Value noise, 0 to 1, as a stand-in for Mellow's noise images (a linearly filtered random image).
float mellowNoise(vec3 x) {
    vec3 i = floor(x);
    vec3 f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(mellowHash(i), mellowHash(i + vec3(1, 0, 0)), f.x),
                   mix(mellowHash(i + vec3(0, 1, 0)), mellowHash(i + vec3(1, 1, 0)), f.x), f.y),
               mix(mix(mellowHash(i + vec3(0, 0, 1)), mellowHash(i + vec3(1, 0, 1)), f.x),
                   mix(mellowHash(i + vec3(0, 1, 1)), mellowHash(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}

float mellowLinstep(float a, float b, float x) {
    return clamp((x - a) / (b - a), 0.0, 1.0);
}

vec4 mellowClouds(vec3 dir, vec3 ambient) {
    const float PLANE_TOP = 1500.0;
    const float PLANE_BOTTOM = 1000.0;
    const float CLOUD_EXTINCTION = 0.5;
    const float CLOUD_SCATTERING = CLOUD_EXTINCTION;
    const int SAMPLE_COUNT = 12;
    int sampleCount = int(((1.0 - dir.y) * 0.75 + 0.25) * float(SAMPLE_COUNT) + 1.0);

    vec3 startPos = PLANE_BOTTOM / dir.y * dir;
    vec3 endPos = PLANE_TOP / dir.y * dir;
    vec3 stepVec = (endPos - startPos) / float(sampleCount);
    float stepSize = length(stepVec);
    vec3 camera = vec3(worldUBO.cameraPos.xyz);
    // Mellow starts at a dithered point in the first step; the middle of it here (the denoiser smooths the rest).
    vec3 pos = stepVec * 0.5 + startPos + vec3(camera.x, 0.0, camera.z);

    // Mellow's wind is per second (frameTimeCounter); gameTime counts days, so a day is 1200 seconds.
    vec3 wind = worldUBO.gameTime * 1200.0 * vec3(12.0, 0.0, 16.0) * float(MELLOW_CLOUD_SPEED);
    float cloudAmount = (float(MELLOW_CLOUD_AMOUNT) - 12.0) / 10.0 + skyUBO.rainGradient;

    vec3 sunDir = normalize(skyUBO.sunDirection);
    bool day = sunDir.y > -0.05;
    vec3 lightDir = day ? sunDir : -sunDir;
    // Radiante's sun, reddened and dimmed through more air when low; the moon a small, cool part of it.
    float airmass = 1.0 / max(lightDir.y + 0.05, 0.05);
    vec3 sun = VPT_SUN_RADIANCE * exp(-vec3(0.02, 0.05, 0.12) * airmass) * smoothstep(-0.05, 0.05, lightDir.y);
    if (!day) { sun *= vec3(0.02, 0.025, 0.035); }
    float phase = max(mellowPhase(dot(dir, lightDir), 0.7) * 1.5, 1.0 / MELLOW_PI);

    vec3 totalScattering = vec3(0.0);
    float totalTransmittance = 1.0;
    for (int i = 1; i <= sampleCount; i++) {
        // Mellow: noisetex (256 texels) at 0.00007 per block, so 0.0179 noise cells per block.
        float base = mellowNoise(vec3((pos.xz - wind.xz) * 0.0179, 0.0));
        float alt = clamp((pos.y - PLANE_BOTTOM) / (PLANE_TOP - PLANE_BOTTOM), 0.0, 1.0);
        float heightDensity = mellowLinstep(0.0, 0.75, 1.0 - alt) * mellowLinstep(0.0, 0.2, alt);
        base = pow(base, 13.0 - cloudAmount * 5.0 + (1.0 - heightDensity) * 5.0);
        if (base < 1e-4) {
            pos += stepVec;
            continue;
        }
        // Mellow: colortex6 (a 64-texel 3D noise) at 0.05 and 0.0125 of its size per block.
        float detailTop = mellowNoise(pos * 0.05);
        float detailBottom = mellowNoise(pos * 0.0125);
        base = max(0.0, base - detailBottom * detailTop * 0.1);

        float fms = CLOUD_SCATTERING * (1.0 - exp(-2.5 * base * CLOUD_EXTINCTION)) / CLOUD_EXTINCTION;
        float ms = MELLOW_ISOTROPIC_PHASE * fms / (1.0 - fms);
        vec3 scattering = ambient * 2.0 * MELLOW_ISOTROPIC_PHASE;
        float lightFactor = mellowLinstep(PLANE_BOTTOM, PLANE_TOP, pos.y) * 0.7 + 0.3;
        scattering += sun * (phase + ms) * 1.5 * lightFactor;

        float transmittance = exp(-base * stepSize * CLOUD_EXTINCTION);
        scattering *= CLOUD_SCATTERING * totalTransmittance * (1.0 - transmittance) / CLOUD_EXTINCTION;
        totalScattering += scattering;
        totalTransmittance *= transmittance;
        if (totalTransmittance < 0.01) { break; }
        pos += stepVec;
    }
    return vec4(totalScattering, totalTransmittance);
}

void hookSky(inout vec3 radiance, vec3 direction) {
#if MELLOW_CLOUDS != 0
    if (direction.y <= 0.02) { return; }
    // The ambient: the clear sky overhead, which lights a cloud from above and around (Mellow's SKY_GROUND), as
    // radiance over the whole sphere (times 4 pi) to match Mellow's isotropic phase.
    vec3 ambient = texture(skyFull, vec3(0.0, 1.0, 0.0)).rgb * 4.0 * MELLOW_PI;
    vec4 clouds = mellowClouds(direction, ambient);
    // Faded into the horizon, where the cloud planes run off to infinity.
    float fade = smoothstep(0.02, 0.12, direction.y);
    radiance = mix(radiance, radiance * clouds.a + clouds.rgb, fade);
#endif
}

#endif
