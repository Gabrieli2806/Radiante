#ifndef WIND_GLSL
#define WIND_GLSL

// Foliage wind. The pass that moves foliage (world/wind/wind_displace.comp, recorded by render/chunks.cpp) and the
// path tracer's motion vectors both evaluate it, so the two always agree on where a leaf is and where it was.
//
// Java marks every vertex of a swaying block with a wind code (ChunkManager, PBRVertexWriter.wind): 0 for a vertex
// that stays put, else column * WIND_COLUMN_STRIDE + kind * WIND_KIND_STRIDE + weight. The weight, 0 to
// WIND_MAX_WEIGHT, says how far the vertex is from where the plant is held: 0 at the foot of a grass stalk, 1 at its
// tip, 2 at the top of a tall flower. A leaf or water plant vertex with no weight still carries its kind, so that a
// code interpolated across one triangle never reads as another kind. The column, the block's x and z modulo 8
// (x + 8 z), tells which block a plant grows in (windPoint).

const float WIND_KIND_STRIDE = 8.0;
const float WIND_MAX_WEIGHT = 4.0;
const float WIND_COLUMN_STRIDE = 32.0;
// Grass, flowers, ferns, crops, saplings, sugar cane: sway about the foot, the tip furthest.
const float WIND_KIND_GROUND = 0.0;
// Leaves, vines, lily pads: rustle in place.
const float WIND_KIND_LEAVES = 1.0;
// Seagrass and kelp: a slow sway with the current, the same in any weather.
const float WIND_KIND_WATER = 2.0;

// Foliage moves within WIND_FADE_END blocks of the camera, fading out from WIND_FADE_START; beyond that it stands
// still, so only the sections nearby have their structures refit each frame.
const float WIND_FADE_START = 32.0;
const float WIND_FADE_END = 48.0;

// The field repeats every WIND_PERIOD blocks in x and z (every spatial frequency below is a whole number of cycles
// over it), so world positions are wrapped into one period before they become floats, without a seam at the wrap.
const float WIND_PERIOD = 4096.0;
const float WIND_K = 6.28318530718 / WIND_PERIOD;

float windFade(float distanceToCamera) {
    return 1.0 - smoothstep(WIND_FADE_START, WIND_FADE_END, distanceToCamera);
}

// Splits a wind code into its column, kind and weight. Each step is read with a margin: the path tracer's motion
// vectors interpolate the code across a triangle, which can leave it a hair under a step, where a bare floor would
// read the column or kind below.
void windDecode(float code, out float column, out float kind, out float weight) {
    column = floor((code + 4.0) / WIND_COLUMN_STRIDE);
    float rest = code - column * WIND_COLUMN_STRIDE;
    kind = floor((rest + 2.0) / WIND_KIND_STRIDE);
    weight = clamp(rest - kind * WIND_KIND_STRIDE, 0.0, WIND_MAX_WEIGHT);
}

// Where the wind is read for a vertex at p (x and z wrapped, as windOffset takes it). Leaves read it at the vertex
// itself: the corners two leaf blocks share must move together, or the canopy would crack open. A plant reads it at
// the middle of its block, at the vertex's height, so its vertices at one height move alike and every face stays
// flat. Each plane of a plant is two faces back to back, split along opposite diagonals: bent even slightly out of
// its plane, one of them bulges towards the ray and is met first, the back one as often as the front.
vec3 windPoint(vec3 p, float code) {
    float column, kind, weight;
    windDecode(code, column, kind, weight);
    if (kind == WIND_KIND_LEAVES) { return p; }
    // The column's bits repeat every 8 blocks, and a vertex lies within a block and a half of its block's middle,
    // its random offset and the wind's push included: its block is the one with those bits nearest to it.
    vec2 low = vec2(mod(column, 8.0), floor(column / 8.0));
    vec2 block = low + 8.0 * round((p.xz - 0.5 - low) / 8.0);
    return vec3(block.x + 0.5, p.y, block.y + 0.5);
}

// windFade for a vertex at p (relative: the same vertex, relative to the camera), measured from where the wind is
// read, so a plant fades as one piece too.
float windFadeAt(vec3 relative, vec3 p, float code) {
    return windFade(length(relative + windPoint(p, code) - p));
}

// How grass or a leaf sways at one pace of the wind, its clock running pace times as fast as t. at is where the field
// is read (windPoint); the rest as windOffset works them out.
vec3 windSway(float kind, float weight, float phase, vec2 at, float t, float pace, float strength, float weather) {
    // The wind blows along windDir. Gusts are slow swells rolling across the land with it (about a hundred blocks
    // apart, a few blocks a second), so a whole patch leans and settles together.
    const vec2 windDir = vec2(0.8, 0.6);
    float swell = 0.5 + 0.5 * sin(dot(at, WIND_K * vec2(32.0, 24.0)) - 0.35 * pace * t);
    float flurry = 0.5 + 0.5 * sin(dot(at, WIND_K * vec2(-9.0, 21.0)) - 0.21 * pace * t + 1.0);
    float gust = mix(0.25, 1.0, swell * (0.4 + 0.6 * flurry));
    float tf = t * pace;

    if (kind >= WIND_KIND_LEAVES) {
        float amplitude = 0.045 * weight * strength * weather;
        vec3 rustle = vec3(sin(2.7 * tf + phase), 0.6 * sin(3.4 * tf + 1.6 * phase + 1.0),
                           sin(2.3 * tf + 2.2 * phase + 2.0)) * (0.35 + 0.65 * gust);
        rustle.xz += windDir * (0.6 * gust);
        return rustle * amplitude;
    }

    float amplitude = 0.085 * weight * strength * weather;
    float sway = 0.5 * sin(2.2 * tf + phase) + 0.2 * sin(3.7 * tf + 1.9 * phase);
    vec2 lean = windDir * (0.6 * gust + sway * (0.4 + 0.6 * gust)) +
                vec2(-windDir.y, windDir.x) * (0.25 * sin(2.9 * tf + 2.3 * phase));
    lean *= amplitude;
    return vec3(lean.x, -dot(lean, lean) / (2.0 * max(weight, 0.25)), lean.y);
}

// How far a vertex is moved from its rest position, in blocks. p: the rest position with x and z wrapped into
// [0, WIND_PERIOD); t: the wind time in seconds (game time, so it stops with the game); strength: the player's
// slider, 0 for no wind; weather: 1 in clear weather, more in rain and thunder.
vec3 windOffset(vec3 p, float code, float t, float strength, float weather) {
    if (code <= 0.0 || strength <= 0.0) { return vec3(0.0); }
    float column, kind, weight;
    windDecode(code, column, kind, weight);
    if (weight <= 0.0) { return vec3(0.0); }
    // The field is read at windPoint: for a plant, one point for all its vertices at one height.
    vec3 at = windPoint(p, code);

    // A phase that changes from one block to the next, so neighbouring plants do not move in step. It is smooth, not
    // a hash of the block: the corners two leaf blocks share must move together, or the canopy would crack open.
    float phase = dot(at.xz, WIND_K * vec2(709.0, 461.0)) +
                  1.7 * sin(dot(at.xz, WIND_K * vec2(1093.0, -1531.0)) + at.y * 0.71);

    if (kind >= WIND_KIND_WATER) {
        float amplitude = 0.10 * weight * strength;
        vec2 sway = vec2(sin(0.9 * t + phase), sin(0.7 * t + 1.4 * phase + 1.3)) * amplitude;
        // A stalk bends rather than stretches: its tip dips as it leans.
        return vec3(sway.x, -dot(sway, sway) / (2.0 * max(weight, 0.25)), sway.y);
    }

    // Rain and thunder blow harder (weather scales the sway) and quicker. Quicker cannot scale the clock by the
    // weather: each step of the scale as the rain sets in moves the sway on by that step times the whole clock, and
    // thousands of seconds into a game the leaves jumped about on every frame until the weather settled. The sway runs
    // at fixed paces instead, 1 in clear weather, 1.25 in rain and 1.5 in a storm, and weather in between blends the
    // two either side of it.
    float stage = clamp(weather - 1.0, 0.0, 1.0) * 2.0;
    float lower = min(floor(stage), 1.0);
    float blend = stage - lower;
    float pace = 1.0 + 0.25 * lower;
    if (blend <= 0.0) { return windSway(kind, weight, phase, at.xz, t, pace, strength, weather); }
    if (blend >= 1.0) { return windSway(kind, weight, phase, at.xz, t, pace + 0.25, strength, weather); }
    return mix(windSway(kind, weight, phase, at.xz, t, pace, strength, weather),
               windSway(kind, weight, phase, at.xz, t, pace + 0.25, strength, weather), blend);
}

#endif
