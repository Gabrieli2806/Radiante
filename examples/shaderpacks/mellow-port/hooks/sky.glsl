#ifndef RADIANTE_HOOK_SKY_GLSL
#define RADIANTE_HOOK_SKY_GLSL

// Mellow port: Mellow's sky and clouds, drawn into the sky every ray that leaves the world sees, so they show in the
// sky, in reflections and in the light the sky casts. Ported from Mellow by TheCMK (MIT, LICENSE-MELLOW): get_sky in
// shaders/global/atmosphere/sky.glsl, and get_clouds_flat (Mellow's default, CLOUD_STYLE 1), get_clouds_volumetric
// (CLOUD_STYLE 2) and get_clouds in shaders/global/atmosphere/clouds.glsl, with noise in shaders/lib/noise.glsl.
//
// What changed for Radiante: the sky's hue and gradient are Mellow's, its brightness Radiante's (mellowSkyScale), and
// the clouds are lit with Mellow's own sun and sky colours at that same scale, so they sit in the sky as they do in
// Mellow. The noise images are Mellow's own (textures/, sampled from set 5 in the ray tracing shaders). Mellow
// dithers its cloud march per pixel and lets TAA smooth it; a sky ray here starts in the middle of the first step,
// since the sky the camera sees is not denoised. Radiante PT's own clouds are off by default in this pack.
#ifndef MELLOW_SKY
#    define MELLOW_SKY 1.0
#endif
#ifndef MELLOW_CLOUD_STYLE
#    define MELLOW_CLOUD_STYLE 1
#endif
#ifndef MELLOW_CLOUD_AMOUNT
#    define MELLOW_CLOUD_AMOUNT 22
#endif
#ifndef MELLOW_CLOUD_OPACITY
#    define MELLOW_CLOUD_OPACITY 0.24
#endif
#ifndef MELLOW_CLOUD_SPEED
#    define MELLOW_CLOUD_SPEED 2
#endif

#include "mellow/mellow_common.glsl"

// noisetex (img/cloud_noise+normal.png, 256 texels, linear, repeating) and colortex6 (img/worley_perlin.bin, a
// 64^3 R8 volume), as Mellow's shaders.properties gives them.
layout(set = 5, binding = 48) uniform sampler2D mellowNoiseTex;
layout(set = 5, binding = 49) uniform sampler3D mellowWorleyTex;

const float MELLOW_NOISE_RES = 256.0;

// Mellow's noise(): the red channel at two scales, the finer one carved out of the coarser.
float mellowNoise(vec2 coords) {
    float color = textureLod(mellowNoiseTex, coords / MELLOW_NOISE_RES * 0.75, 0.0).x;
    float detail = textureLod(mellowNoiseTex, coords / MELLOW_NOISE_RES * 5.0, 0.0).x * 0.1;
    return max(color - detail, 0.0);
}

// The clock the clouds drift on, in seconds. worldUBO.gameTime is the day fraction and starts over every 1200 s,
// which made the clouds jump back every twenty minutes; windTime runs six hours before it wraps.
float mellowSeconds() {
    return worldUBO.windTime;
}

// get_clouds_flat. The cloud plane is found from the direction alone (Mellow divides PlayerPos by its own height),
// so a sky ray works as it is.
vec4 mellowCloudsFlat(vec3 dir, vec3 lightDir, vec3 skyGround, vec3 glare, vec3 sunDirect) {
    vec2 cloudPos = dir.xz / (dir.y + length(dir.xz) / 6.0);
    // Mellow moves the plane with the camera, cameraPosition / 512. Both noise scales repeat every 1024 units of
    // cloudPos * 32, so the camera's part is taken modulo 32 in double precision before it becomes a float: far from
    // spawn a float of the raw position loses the fraction and the clouds shimmer.
    vec2 cameraPart = vec2(mod(worldUBO.cameraPos.xz / 512.0, 32.0));
    // Mellow's flat clouds stand still (its speed is commented out: CLOUD_SPEED / 100); here Cloud Speed moves them
    // at that rate, 0 keeping them still. Wrapped on the same 32-unit period.
    float animation = mod(mellowSeconds() * float(MELLOW_CLOUD_SPEED) / 100.0, 32.0);
    cloudPos = (cloudPos + cameraPart + animation) * 32.0;

    float noise = mellowNoise(cloudPos);
    float cloudAmount = float(MELLOW_CLOUD_AMOUNT) / 100.0 + skyUBO.rainGradient / 5.0;
    noise *= smoothstep(0.0, 0.4 - float(MELLOW_CLOUD_OPACITY), noise - 0.6 + cloudAmount);
    if (noise < 0.01) return vec4(0.0, 0.0, 0.0, 1.0);

    // Mellow's light: the noise a little towards the light, as how much cloud the light crosses.
    float absorption = mellowNoise(cloudPos + lightDir.xz * 8.0);
    const float DENSITY = 1.5;
    vec3 cloudColor = (skyGround * 2.0 + glare) * 0.25 / MELLOW_PI;
    float miePhase = max(mellowPhase(dot(dir, lightDir), 0.7) * 1.5, 1.0 / MELLOW_PI);
    cloudColor += sunDirect * miePhase * exp(-absorption * 1.5 * DENSITY);

    float transmittance = exp(-noise * DENSITY);
    return vec4(cloudColor * noise * DENSITY, transmittance);
}

// get_clouds_volumetric: 500 blocks of cloud from 1000 to 1500 above the camera, marched in 4 to 13 steps.
vec4 mellowCloudsVolumetric(vec3 dir, vec3 lightDir, vec3 skyGround, vec3 glare, vec3 sunDirect) {
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
    vec3 pos = stepVec * 0.5 + startPos;

    // The camera and the wind are folded into each texture's repeat before they reach a float: noisetex repeats every
    // 1 / 0.00007 blocks at Mellow's scale, colortex6 every 5120 (64 / 0.0125, a multiple of 64 / 0.05).
    const double BASE_PERIOD = 1.0 / 0.00007;
    const double DETAIL_PERIOD = 5120.0;
    dvec2 camera = worldUBO.cameraPos.xz;
    vec2 wind = vec2(mod(double(mellowSeconds()) * dvec2(12.0, 16.0) * double(MELLOW_CLOUD_SPEED), BASE_PERIOD));
    vec2 baseOffset = vec2(mod(camera, BASE_PERIOD)) - wind;
    vec2 detailOffset = vec2(mod(camera, DETAIL_PERIOD));

    float cloudAmount = (float(MELLOW_CLOUD_AMOUNT) - 12.0) / 10.0 + skyUBO.rainGradient;
    float phase = max(mellowPhase(dot(dir, lightDir), 0.7) * 1.5, 1.0 / MELLOW_PI);

    vec3 totalScattering = vec3(0.0);
    float totalTransmittance = 1.0;
    for (int i = 1; i <= sampleCount; i++) {
        float base = textureLod(mellowNoiseTex, (pos.xz + baseOffset) * 0.00007, 0.0).r;
        float alt = (pos.y - PLANE_BOTTOM) / (PLANE_TOP - PLANE_BOTTOM);
        float heightDensity = mellowLinstep(0.0, 0.75, 1.0 - alt) * mellowLinstep(0.0, 0.2, alt);
        base = pow(base, 13.0 - cloudAmount * 5.0 + (1.0 - heightDensity) * 5.0);
        if (base < 1e-4) {
            pos += stepVec;
            continue;
        }
        vec3 detailPos = pos + vec3(detailOffset.x, 0.0, detailOffset.y);
        float detailTop = textureLod(mellowWorleyTex, detailPos / vec3(64.0) * 0.05, 0.0).r;
        float detailBottom = textureLod(mellowWorleyTex, detailPos / vec3(64.0) * 0.0125, 0.0).r;
        base = max(0.0, base - detailBottom * detailTop * 0.1);

        float fms = CLOUD_SCATTERING * (1.0 - exp(-2.5 * base * CLOUD_EXTINCTION)) / CLOUD_EXTINCTION;
        float ms = MELLOW_ISOTROPIC_PHASE * fms / (1.0 - fms);
        vec3 scattering = (skyGround * 2.0 + glare) * MELLOW_ISOTROPIC_PHASE;
        float lightFactor = mellowLinstep(PLANE_BOTTOM, PLANE_TOP, pos.y) * 0.7 + 0.3;
        scattering += sunDirect * (phase + ms) * 1.5 * lightFactor;

        float transmittance = exp(-base * stepSize * CLOUD_EXTINCTION);
        scattering *= CLOUD_SCATTERING * totalTransmittance * (1.0 - transmittance) / CLOUD_EXTINCTION;
        totalScattering += scattering;
        totalTransmittance *= transmittance;
        if (totalTransmittance < 0.01) break;
        pos += stepVec;
    }
    return vec4(totalScattering, totalTransmittance);
}

// How much brighter Radiante's sky is than Mellow's, from the zenith and the horizon a quarter turn from the sun (away
// from its glow): Mellow's colours times this keep Radiante's sky brightness and exposure through the day and night,
// with Mellow's hue and its gradient from the horizon up.
float mellowSkyScale(MellowTime t, vec3 sunDir) {
    vec3 up = vec3(0.0, 1.0, 0.0);
    vec3 side = cross(up, sunDir);
    side = dot(side.xz, side.xz) > 1e-6 ? normalize(vec3(side.x, 0.05, side.z)) : vec3(1.0, 0.05, 0.0);
    float radiante = mellowLuminance(textureLod(skyFull, up, 0.0).rgb) +
                     mellowLuminance(textureLod(skyFull, side, 0.0).rgb);
    float mellow = mellowLuminance(mellowSky(t, up, sunDir)) + mellowLuminance(mellowSky(t, side, sunDir));
    return radiante / max(mellow, 1e-6);
}

void hookSky(inout vec3 radiance, vec3 direction) {
    vec3 sunDir = normalize(skyUBO.sunDirection);
    MellowTime t = mellowTime(sunDir);
    float scale = mellowSkyScale(t, sunDir);

    vec3 sky = mellowSky(t, direction, sunDir) * scale;
    radiance = mix(radiance, sky, float(MELLOW_SKY));

#if MELLOW_CLOUD_STYLE != 0
    if (direction.y <= 0.0) return;
    // Mellow lights its clouds with the shadow light: the sun by day, the moon (opposite it) by night.
    vec3 lightDir = sunDir.y > 0.0 ? sunDir : -sunDir;
    vec3 skyGround = mellowSkyGround(t) * scale;
    vec3 glare = mellowSunGlare(t, dot(direction, sunDir)) * scale;
    vec3 sunDirect = mellowSunDirect(t, sunDir) * scale;
#    if MELLOW_CLOUD_STYLE == 2
    vec4 clouds = mellowCloudsVolumetric(direction, lightDir, skyGround, glare, sunDirect);
#    else
    vec4 clouds = mellowCloudsFlat(direction, lightDir, skyGround, glare, sunDirect);
#    endif
    // get_clouds: blended in over the lowest fifth of the sky, at CLOUD_OPACITY + 0.5.
    vec3 clouded = radiance * clouds.a + clouds.rgb;
    radiance = mix(radiance, clouded, mellowLinstep(0.0, 0.2, direction.y) * (float(MELLOW_CLOUD_OPACITY) + 0.5));
#endif
}

#endif
