#ifndef VPT_FFT_WATER_SHAPED_GLSL
#define VPT_FFT_WATER_SHAPED_GLSL

// The Realistic water's waves (common/fft_water.glsl) as a shader pack shapes them with hookWaterWaves
// (hooks/water_waves.glsl). Only included when a pack replaces that hook, after it: radiante-pt's own shaders then
// stay exactly as they were.

// shape is hookWaterWaves' strength, speed and size, (1, 1, 1) for radiante-pt's own waves. The size stretches the pattern without flattening it, as a longer wave of
// the same steepness is also a taller one; the strength scales the steepness and the height.
FftWaterSample sampleFftWaterShaped(vec2 worldXZ, float gameTime, vec3 shape) {
    float size = max(shape.z, 1e-3);
    FftWaterSample result = sampleFftWater(worldXZ / size, gameTime * shape.y);
    result.height *= shape.x * size;
    result.slope *= shape.x;
    result.displacement *= shape.x * size;
    result.curvature *= shape.x / size;
    result.normal = normalize(vec3(-result.slope.x, 1.0, -result.slope.y));
    return result;
}

// sampleFftWaterCaustic on the waves a pack shaped (sampleFftWaterShaped); the same steps, kept apart so radiante-pt's
// own shaders stay as they were without a pack.
float sampleFftWaterCausticShaped(vec2 worldXZ, float gameTime, vec3 toLightDir, float waterDepth, vec3 shape) {
    if (toLightDir.y <= 0.01) { return 1.0; }

    const float eta = 1.0 / 1.333;
    const float spacing = 0.04;
    vec3 incoming = normalize(-toLightDir);

    FftWaterSample here = sampleFftWaterShaped(worldXZ, gameTime, shape);
    FftWaterSample alongX = sampleFftWaterShaped(worldXZ + vec2(spacing, 0.0), gameTime, shape);
    FftWaterSample alongZ = sampleFftWaterShaped(worldXZ + vec2(0.0, spacing), gameTime, shape);

    vec3 rayHere = refract(incoming, here.normal, eta);
    vec3 rayX = refract(incoming, alongX.normal, eta);
    vec3 rayZ = refract(incoming, alongZ.normal, eta);

    bool degenerate = dot(rayHere, rayHere) <= 1e-6 || dot(rayX, rayX) <= 1e-6 || dot(rayZ, rayZ) <= 1e-6;
    if (degenerate) { return 1.0; }
    if (rayHere.y >= -0.1 || rayX.y >= -1e-3 || rayZ.y >= -1e-3) { return 1.0; }

    vec2 landHere = here.displacement + rayHere.xz * (waterDepth / -rayHere.y);
    vec2 landX = vec2(spacing, 0.0) + alongX.displacement + rayX.xz * (waterDepth / -rayX.y);
    vec2 landZ = vec2(0.0, spacing) + alongZ.displacement + rayZ.xz * (waterDepth / -rayZ.y);

    vec2 edgeX = landX - landHere;
    vec2 edgeZ = landZ - landHere;
    float areaRatio = abs(edgeX.x * edgeZ.y - edgeX.y * edgeZ.x) / (spacing * spacing);

    float softening = 0.03 + waterDepth * 0.05;
    float concentration = (1.0 / (areaRatio + softening)) * (1.0 + softening);
    float depthFade = 1.0 / (1.0 + waterDepth * waterDepth * 0.045);
    float caustic = 1.0 + (concentration - 1.0) * depthFade * 1.18;
    float sunHeight = clamp(toLightDir.y * 1.5, 0.0, 1.0);
    return clamp(mix(1.0, caustic, sunHeight), 0.1, 5.8);
}

// hookWaterWaves' strength, speed and size as the shape the functions above take, each kept in range.
vec3 fftWaterWaveShape() {
    float strength = 1.0;
    float speed = 1.0;
    float size = 1.0;
    hookWaterWaves(strength, speed, size);
    return vec3(max(strength, 0.0), speed, max(size, 1e-3));
}

#endif
