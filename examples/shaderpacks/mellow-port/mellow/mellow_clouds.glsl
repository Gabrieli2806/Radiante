#ifndef MELLOW_CLOUDS_GLSL
#define MELLOW_CLOUDS_GLSL

// Mellow port: Mellow's clouds, shared by the cloud layer (hooks/clouds.glsl) and the cloud shadow
// (hooks/cloud_shadow.glsl). Ported from Mellow by TheCMK (MIT, LICENSE-MELLOW): get_clouds_flat (Mellow's default,
// CLOUD_STYLE 1), get_clouds_volumetric (CLOUD_STYLE 2) and get_clouds in shaders/global/atmosphere/clouds.glsl, with
// noise in shaders/lib/noise.glsl.
//
// What changed for Radiante: the clouds are lit with Mellow's own sun and sky colours scaled to Radiante's sky as it
// is after the rain (mellowSkyScale), so they sit in the sky as they do in Mellow, rain included: Mellow thickens its
// clouds in rain and greys and darkens their light, where Radiante's own sky turns grey. The noise images are Mellow's
// own (textures/, sampled from set 5 in the ray tracing shaders). Mellow dithers its cloud march per pixel and lets
// TAA smooth it; a sky ray here starts in the middle of the first step, since the sky the camera sees is not denoised.
// Radiante PT's own clouds are off by default in this pack.
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

// Where a direction meets Mellow's flat cloud plane, in the noise's coordinates. The plane is found from the
// direction alone (Mellow divides PlayerPos by its own height), so a sky ray works as it is.
vec2 mellowFlatCloudPos(vec3 dir) {
    vec2 cloudPos = dir.xz / (dir.y + length(dir.xz) / 6.0);
    // Mellow moves the plane with the camera, cameraPosition / 512. Both noise scales repeat every 1024 units of
    // cloudPos * 32, so the camera's part is taken modulo 32 in double precision before it becomes a float: far from
    // spawn a float of the raw position loses the fraction and the clouds shimmer.
    vec2 cameraPart = vec2(mod(worldUBO.cameraPos.xz / 512.0, 32.0));
    // Mellow's flat clouds stand still (its speed is commented out: CLOUD_SPEED / 100); here Cloud Speed moves them
    // at that rate, 0 keeping them still. Wrapped on the same 32-unit period.
    float animation = mod(mellowSeconds() * float(MELLOW_CLOUD_SPEED) / 100.0, 32.0);
    return (cloudPos + cameraPart + animation) * 32.0;
}

// The flat clouds' density at a point of the plane: Mellow's noise, cut by the cloud amount, which rises with rain.
float mellowFlatCloudDensity(vec2 cloudPos) {
    float noise = mellowNoise(cloudPos);
    float cloudAmount = float(MELLOW_CLOUD_AMOUNT) / 100.0 + skyUBO.rainGradient / 5.0;
    return noise * smoothstep(0.0, 0.4 - float(MELLOW_CLOUD_OPACITY), noise - 0.6 + cloudAmount);
}

const float MELLOW_FLAT_CLOUD_DENSITY = 1.5;

// get_clouds_flat.
vec4 mellowCloudsFlat(vec3 dir, vec3 lightDir, vec3 skyGround, vec3 glare, vec3 sunDirect) {
    vec2 cloudPos = mellowFlatCloudPos(dir);
    float noise = mellowFlatCloudDensity(cloudPos);
    if (noise < 0.01) return vec4(0.0, 0.0, 0.0, 1.0);

    // Mellow's light: the noise a little towards the light, as how much cloud the light crosses.
    float absorption = mellowNoise(cloudPos + lightDir.xz * 8.0);
    vec3 cloudColor = (skyGround * 2.0 + glare) * 0.25 / MELLOW_PI;
    float miePhase = max(mellowPhase(dot(dir, lightDir), 0.7) * 1.5, 1.0 / MELLOW_PI);
    cloudColor += sunDirect * miePhase * exp(-absorption * 1.5 * MELLOW_FLAT_CLOUD_DENSITY);

    float transmittance = exp(-noise * MELLOW_FLAT_CLOUD_DENSITY);
    return vec4(cloudColor * noise * MELLOW_FLAT_CLOUD_DENSITY, transmittance);
}

// get_clouds_volumetric: 500 blocks of cloud from 1000 to 1500 above the camera.
const float MELLOW_VOLUMETRIC_BOTTOM = 1000.0;
const float MELLOW_VOLUMETRIC_TOP = 1500.0;
const float MELLOW_VOLUMETRIC_EXTINCTION = 0.5;

// The camera and the wind are folded into each texture's repeat before they reach a float: noisetex repeats every
// 1 / 0.00007 blocks at Mellow's scale, colortex6 every 5120 (64 / 0.0125, a multiple of 64 / 0.05).
void mellowVolumetricOffsets(out vec2 baseOffset, out vec2 detailOffset) {
    const double BASE_PERIOD = 1.0 / 0.00007;
    const double DETAIL_PERIOD = 5120.0;
    dvec2 camera = worldUBO.cameraPos.xz;
    vec2 wind = vec2(mod(double(mellowSeconds()) * dvec2(12.0, 16.0) * double(MELLOW_CLOUD_SPEED), BASE_PERIOD));
    baseOffset = vec2(mod(camera, BASE_PERIOD)) - wind;
    detailOffset = vec2(mod(camera, DETAIL_PERIOD));
}

// The volumetric clouds' density at a camera-relative point of their layer.
float mellowVolumetricDensity(vec3 pos, vec2 baseOffset, vec2 detailOffset) {
    float cloudAmount = (float(MELLOW_CLOUD_AMOUNT) - 12.0) / 10.0 + skyUBO.rainGradient;
    float base = textureLod(mellowNoiseTex, (pos.xz + baseOffset) * 0.00007, 0.0).r;
    float alt = (pos.y - MELLOW_VOLUMETRIC_BOTTOM) / (MELLOW_VOLUMETRIC_TOP - MELLOW_VOLUMETRIC_BOTTOM);
    float heightDensity = mellowLinstep(0.0, 0.75, 1.0 - alt) * mellowLinstep(0.0, 0.2, alt);
    base = pow(base, 13.0 - cloudAmount * 5.0 + (1.0 - heightDensity) * 5.0);
    if (base < 1e-4) return 0.0;
    vec3 detailPos = pos + vec3(detailOffset.x, 0.0, detailOffset.y);
    float detailTop = textureLod(mellowWorleyTex, detailPos / vec3(64.0) * 0.05, 0.0).r;
    float detailBottom = textureLod(mellowWorleyTex, detailPos / vec3(64.0) * 0.0125, 0.0).r;
    return max(0.0, base - detailBottom * detailTop * 0.1);
}

// Marched in 4 to 13 steps.
vec4 mellowCloudsVolumetric(vec3 dir, vec3 lightDir, vec3 skyGround, vec3 glare, vec3 sunDirect) {
    const float CLOUD_SCATTERING = MELLOW_VOLUMETRIC_EXTINCTION;
    const int SAMPLE_COUNT = 12;
    int sampleCount = int(((1.0 - dir.y) * 0.75 + 0.25) * float(SAMPLE_COUNT) + 1.0);

    vec3 startPos = MELLOW_VOLUMETRIC_BOTTOM / dir.y * dir;
    vec3 endPos = MELLOW_VOLUMETRIC_TOP / dir.y * dir;
    vec3 stepVec = (endPos - startPos) / float(sampleCount);
    float stepSize = length(stepVec);
    vec3 pos = stepVec * 0.5 + startPos;

    vec2 baseOffset;
    vec2 detailOffset;
    mellowVolumetricOffsets(baseOffset, detailOffset);
    float phase = max(mellowPhase(dot(dir, lightDir), 0.7) * 1.5, 1.0 / MELLOW_PI);

    vec3 totalScattering = vec3(0.0);
    float totalTransmittance = 1.0;
    for (int i = 1; i <= sampleCount; i++) {
        float base = mellowVolumetricDensity(pos, baseOffset, detailOffset);
        if (base <= 0.0) {
            pos += stepVec;
            continue;
        }
        float fms = CLOUD_SCATTERING * (1.0 - exp(-2.5 * base * MELLOW_VOLUMETRIC_EXTINCTION)) /
                    MELLOW_VOLUMETRIC_EXTINCTION;
        float ms = MELLOW_ISOTROPIC_PHASE * fms / (1.0 - fms);
        vec3 scattering = (skyGround * 2.0 + glare) * MELLOW_ISOTROPIC_PHASE;
        float lightFactor = mellowLinstep(MELLOW_VOLUMETRIC_BOTTOM, MELLOW_VOLUMETRIC_TOP, pos.y) * 0.7 + 0.3;
        scattering += sunDirect * (phase + ms) * 1.5 * lightFactor;

        float transmittance = exp(-base * stepSize * MELLOW_VOLUMETRIC_EXTINCTION);
        scattering *= CLOUD_SCATTERING * totalTransmittance * (1.0 - transmittance) / MELLOW_VOLUMETRIC_EXTINCTION;
        totalScattering += scattering;
        totalTransmittance *= transmittance;
        if (totalTransmittance < 0.01) break;
        pos += stepVec;
    }
    return vec4(totalScattering, totalTransmittance);
}

// get_clouds: the clouds blended in over the lowest fifth of the sky, at CLOUD_OPACITY + 0.5. Mellow mixes the sky
// towards the clouded sky by that weight; as light and transmittance that is the clouds' light times the weight, and
// a transmittance of 1 - weight * (1 - clouds' transmittance).
float mellowCloudWeight(vec3 dir) {
    return mellowLinstep(0.0, 0.2, dir.y) * (float(MELLOW_CLOUD_OPACITY) + 0.5);
}

vec4 mellowCloudLayer(vec3 direction) {
#if MELLOW_CLOUD_STYLE == 0
    return vec4(0.0, 0.0, 0.0, 1.0);
#else
    if (direction.y <= 0.0) return vec4(0.0, 0.0, 0.0, 1.0);
    vec3 sunDir = normalize(skyUBO.sunDirection);
    MellowTime t = mellowTime(sunDir);
    float scale = mellowSkyScale(t, sunDir, true);
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
    float weight = mellowCloudWeight(direction);
    return vec4(clouds.rgb * weight, 1.0 - weight * (1.0 - clouds.a));
#endif
}

// The clouds' shadow on a camera-relative point, towards the light. Mellow has no cloud shadows (its shadow map
// stops at the world); this is the port's own, from the same density the sky shows, so the shadows move with the
// clouds above. The flat clouds stand as a plane 512 blocks above the camera, the height at which Mellow's camera
// offset (cameraPosition / 512) moves them as far as the view does; the light ray is followed to that plane and
// the cloud found in the direction of that point from the camera. The volumetric layer is sampled at three points
// along the light ray through it.
float mellowCloudShadow(vec3 worldPos, vec3 lightDir) {
#if MELLOW_CLOUD_STYLE == 0
    return 1.0;
#else
    if (lightDir.y <= 0.02) return 1.0;
#    if MELLOW_CLOUD_STYLE == 2
    vec2 baseOffset;
    vec2 detailOffset;
    mellowVolumetricOffsets(baseOffset, detailOffset);
    float enter = (MELLOW_VOLUMETRIC_BOTTOM - worldPos.y) / lightDir.y;
    float leave = (MELLOW_VOLUMETRIC_TOP - worldPos.y) / lightDir.y;
    if (leave <= 0.0) return 1.0;
    enter = max(enter, 0.0);
    float stepSize = (leave - enter) / 3.0;
    float opticalDepth = 0.0;
    for (int i = 0; i < 3; i++) {
        vec3 pos = worldPos + lightDir * (enter + stepSize * (float(i) + 0.5));
        opticalDepth += mellowVolumetricDensity(pos, baseOffset, detailOffset) * stepSize;
    }
    float transmittance = exp(-opticalDepth * MELLOW_VOLUMETRIC_EXTINCTION);
    vec3 seen = normalize(worldPos + lightDir * (enter + leave) * 0.5);
#    else
    const float PLANE = 512.0;
    float distanceToPlane = (PLANE - worldPos.y) / lightDir.y;
    if (distanceToPlane <= 0.0) return 1.0;
    vec3 seen = normalize(worldPos + lightDir * distanceToPlane);
    float noise = mellowFlatCloudDensity(mellowFlatCloudPos(seen));
    float transmittance = noise < 0.01 ? 1.0 : exp(-noise * MELLOW_FLAT_CLOUD_DENSITY);
#    endif
    // As much of the light as of the sky behind them the clouds let through where they are seen from the camera.
    return 1.0 - mellowCloudWeight(seen) * (1.0 - transmittance);
#endif
}

#endif
