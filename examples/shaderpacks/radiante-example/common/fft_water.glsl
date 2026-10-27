#ifndef VPT_FFT_WATER_GLSL
#define VPT_FFT_WATER_GLSL

// Animated water surface. Despite the "FFT" name this is a small analytic sum of eight directional sine waves in
// two octaves, warped by a slow flow field so the pattern never visibly tiles. Everything is evaluated in closed
// form (height, slope, horizontal displacement, curvature), so any point can be sampled without a texture.

struct FftWaterSample {
    float height;
    vec2 slope;        // d(height)/d(x, z)
    vec2 displacement; // horizontal offset of the surface point (choppy waves)
    vec2 curvature;    // second derivative along x and z, used for foam/sparkle
    vec3 normal;
};

// 2D rotation by `angle` radians (column-major, as GLSL's mat2 constructor fills it).
mat2 fftWaterRotation(float angle) {
    float sine = sin(angle);
    float cosine = cos(angle);
    return mat2(cosine, -sine, sine, cosine);
}

// Tangent frame of the water plane that does not depend on the triangle's UVs: the tangent is world +x (or +z
// when the plane faces along x) projected onto the plane, with the normal flipped to point up first, so the same
// world position gets the same coordinates on every water face.
void fftWaterStableBasis(vec3 geometricNormal, out vec3 tangent, out vec3 bitangent) {
    vec3 up = geometricNormal.y >= 0.0 ? geometricNormal : -geometricNormal;
    vec3 candidate = vec3(1.0, 0.0, 0.0) - up * up.x;
    if (dot(candidate, candidate) < 1e-10) {
        candidate = vec3(0.0, 0.0, 1.0) - up * up.z;
    }
    tangent = normalize(candidate);
    bitangent = normalize(cross(tangent, up));
}

// Wave-space coordinates of a world position. The surface derivatives are accepted for interface compatibility but
// not needed: the stable basis alone fixes the coordinates.
vec2 fftWaterSurfaceCoord(vec3 absWorldPos, vec3 dPduWorld, vec3 dPdvWorld, vec3 geometricNormal) {
    vec3 tangent;
    vec3 bitangent;
    fftWaterStableBasis(geometricNormal, tangent, bitangent);
    return vec2(dot(absWorldPos, tangent), dot(absWorldPos, bitangent));
}

// Adds one sine wave and its analytic derivatives to the running sums.
void fftWaterAddWave(vec2 p,
                     float t,
                     vec2 dir,
                     float freq,
                     float amp,
                     float speed,
                     inout float height,
                     inout vec2 slope,
                     inout vec2 displacement,
                     inout vec2 curvature) {
    float phase = dot(p, dir) * freq + t * speed;
    float wave = sin(phase);
    float waveDerivative = cos(phase);
    height += amp * wave;
    slope += dir * (amp * freq * waveDerivative);
    displacement += dir * (amp * 0.42 * wave);
    curvature += (dir * dir) * (-amp * freq * freq * wave);
}

// Wave table: direction (unnormalised), spatial frequency, amplitude, phase speed (sign = travel direction).
// The first four form the broad swell, the last four the finer chop on the rotated second octave.
const int FFT_WATER_SWELL_WAVES = 4;
const int FFT_WATER_WAVE_COUNT = 8;
const vec2 FFT_WATER_WAVE_DIRECTION[FFT_WATER_WAVE_COUNT] = vec2[](
    vec2(1.00, 0.18), vec2(-0.61, 0.79), vec2(0.19, 0.98), vec2(-0.93, -0.36),
    vec2(0.71, -0.70), vec2(-0.24, -0.97), vec2(0.89, 0.46), vec2(-0.78, 0.62));
const float FFT_WATER_WAVE_FREQUENCY[FFT_WATER_WAVE_COUNT] = float[](0.27, 0.41, 0.61, 0.92, 1.31, 1.78, 2.34, 2.92);
const float FFT_WATER_WAVE_AMPLITUDE[FFT_WATER_WAVE_COUNT] =
    float[](0.120, 0.090, 0.060, 0.035, 0.020, 0.014, 0.010, 0.007);
const float FFT_WATER_WAVE_SPEED[FFT_WATER_WAVE_COUNT] = float[](0.72, -1.03, 1.41, -1.92, 2.67, -3.21, 4.10, -4.63);

// Samples the surface at a wave-space position. gameTime is in days (one unit = 24000 ticks).
FftWaterSample sampleFftWater(vec2 worldXZ, float gameTime) {
    float t = gameTime * 24000.0 * 0.05;

    // Two layers of slow, large-scale flow that bend the wave fronts.
    vec2 slowFlow = vec2(sin(dot(worldXZ, vec2(0.023, -0.019)) + t * 0.61),
                         cos(dot(worldXZ, vec2(-0.017, 0.021)) - t * 0.54));
    vec2 quickFlow = vec2(cos(dot(worldXZ, vec2(0.041, 0.037)) - t * 0.88 + 1.9),
                          sin(dot(worldXZ, vec2(-0.036, 0.044)) + t * 0.79 - 0.7));
    vec2 flow = slowFlow * 0.55 + quickFlow * 0.25;

    // The chop octave runs on a scaled and rotated copy of the plane so its crests never line up with the swell.
    vec2 swellCoord = worldXZ * 1.14 + flow * 0.75;
    vec2 chopCoord = fftWaterRotation(0.73) * (worldXZ * 1.40) + fftWaterRotation(1.11) * flow * 0.58;

    float height = 0.0;
    vec2 slope = vec2(0.0);
    vec2 displacement = flow * 0.10;
    vec2 curvature = vec2(0.0);
    for (int wave = 0; wave < FFT_WATER_WAVE_COUNT; ++wave) {
        vec2 coord = wave < FFT_WATER_SWELL_WAVES ? swellCoord : chopCoord;
        fftWaterAddWave(coord, t, normalize(FFT_WATER_WAVE_DIRECTION[wave]), FFT_WATER_WAVE_FREQUENCY[wave],
                        FFT_WATER_WAVE_AMPLITUDE[wave], FFT_WATER_WAVE_SPEED[wave], height, slope, displacement,
                        curvature);
    }

    // Artistic gains on the derived quantities.
    FftWaterSample result;
    result.height = height;
    result.slope = slope * 1.34;
    result.displacement = displacement * 0.78;
    result.curvature = curvature * 18.0;
    result.normal = normalize(vec3(-result.slope.x, 1.0, -result.slope.y));
    return result;
}

// Caustic brightness factor (1 = no change) on the bottom of water `waterDepth` deep under the surface point.
// Light is refracted through the surface at the point and at two neighbours; where the refracted rays converge on
// the bottom the light is concentrated (area shrinks), where they spread it is dimmed. The ratio of the projected
// to the original area is the Jacobian of that mapping, softened so it cannot blow up, and faded with depth and a
// low sun.
float sampleFftWaterCaustic(vec2 worldXZ, float gameTime, vec3 toLightDir, float waterDepth) {
    if (toLightDir.y <= 0.01) { return 1.0; }

    const float eta = 1.0 / 1.333; // air to water
    const float spacing = 0.04;
    vec3 incoming = normalize(-toLightDir);

    FftWaterSample here = sampleFftWater(worldXZ, gameTime);
    FftWaterSample alongX = sampleFftWater(worldXZ + vec2(spacing, 0.0), gameTime);
    FftWaterSample alongZ = sampleFftWater(worldXZ + vec2(0.0, spacing), gameTime);

    vec3 rayHere = refract(incoming, here.normal, eta);
    vec3 rayX = refract(incoming, alongX.normal, eta);
    vec3 rayZ = refract(incoming, alongZ.normal, eta);

    // Total internal reflection, or a refracted ray too flat to reach the bottom: leave the light alone.
    bool degenerate = dot(rayHere, rayHere) <= 1e-6 || dot(rayX, rayX) <= 1e-6 || dot(rayZ, rayZ) <= 1e-6;
    if (degenerate) { return 1.0; }
    if (rayHere.y >= -0.1 || rayX.y >= -1e-3 || rayZ.y >= -1e-3) { return 1.0; }

    // Where each ray lands on the bottom, relative to the centre sample's surface position.
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

#endif
