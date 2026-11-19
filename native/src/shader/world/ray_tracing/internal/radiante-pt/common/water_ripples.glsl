#ifndef VPT_WATER_RIPPLES_GLSL
#define VPT_WATER_RIPPLES_GLSL

// Interactive radial wave packets on water surfaces and rain puddles, driven by entities entering or moving through
// water and stepping on wet ground (WaterRipples.java -> skyUBO.waterRipple*).
//
// Each active impulse represents a dispersive circular capillary-gravity wave train expanding from skyUBO.waterRipplePos[i].xyz
// at radial group speed skyUBO.waterRipplePos[i].w. Consecutive impulses emitted along a moving swimmer's or boat's path
// superpose by Huygens' principle into a natural V-shaped wake.

// Minecraft's look for the weather's marks: puddles, the rings on them and on water, the wakes, the wetness of every
// surface, the snow and the footprints in it go on in pixels of a sixteenth of a block, the size of a block
// texture's, with hard edges, as a texture painted for the game would show them. Always in Radiante PT; a pack that
// extends it may define VPT_RAIN_PIXELS 0 for smooth, round shapes (Aether's Pixel Weather).
#ifndef VPT_RAIN_PIXELS
#    define VPT_RAIN_PIXELS 1
#endif

#ifndef VPT_WATER_RIPPLES
#    define VPT_WATER_RIPPLES 1.0
#endif

const float INTERACTIVE_RIPPLE_SLOPE = 0.30;
const float INTERACTIVE_PUDDLE_RIPPLE_SCALE = 0.65;

/**
 * Analytic radial slope (dh/dx, dh/dz) at camera-relative samplePos for surfaceKind (0.0 = water, 1.0 = rain puddle).
 * footprint is the ray cone width at the hit, attenuating wavelengths finer than a pixel so distant rings never alias.
 */
vec2 vptInteractiveRippleSlope(vec3 samplePos, float surfaceKind, float footprint) {
    float strength = clamp(VPT_WATER_RIPPLES, 0.0, 2.0);
    int count = clamp(int(skyUBO.waterRippleControl.x + 0.5), 0, PRISM_WATER_RIPPLE_MAX);
    if (strength <= 0.0 || count <= 0) {
        return vec2(0.0);
    }

    // With Pixel Rain the rings are drawn as the rain's are: looked up at the centre of the sixteenth of a block the
    // hit falls in, and their slope stepped (below), so a wake spreads in pixels instead of as smooth real water.
    if (VPT_RAIN_PIXELS != 0) {
        vec2 cameraFraction = vec2(mod(worldUBO.cameraPos.xz, dvec2(1.0)));
        vec2 world = samplePos.xz + cameraFraction;
        samplePos.xz = (floor(world * 16.0) + 0.5) / 16.0 - cameraFraction;
    }

    bool isPuddle = surfaceKind > 0.5;
    // Wider rings with Pixel Rain: a crest and a trough of three or four pixels each.
    float waveNumber = VPT_RAIN_PIXELS != 0 ? (isPuddle ? 11.0 : 8.0) : (isPuddle ? 16.5 : 12.0);
    float coneFade = 1.0 / (1.0 + waveNumber * waveNumber * max(footprint, 0.0) * max(footprint, 0.0));
    if (coneFade < 0.02) {
        return vec2(0.0);
    }

    vec2 totalSlope = vec2(0.0);
    for (int i = 0; i < count; i++) {
        vec4 data = skyUBO.waterRippleData[i];
        float kind = data.w;
        if (abs(kind - surfaceKind) > 0.25) {
            continue;
        }
        float age = data.x;
        float amplitude = data.y;
        float maxAge = max(data.z, 0.1);
        if (age < 0.0 || age >= maxAge || amplitude <= 0.0) {
            continue;
        }

        vec4 posSpeed = skyUBO.waterRipplePos[i];
        if (abs(samplePos.y - posSpeed.y) > 0.85) {
            continue;
        }

        vec2 offset = samplePos.xz - posSpeed.xz;
        float rSq = dot(offset, offset);
        float front = posSpeed.w * age;
        float lifeFrac = age / maxAge;
        float tailWidth = mix(0.45, isPuddle ? 1.15 : 1.85, lifeFrac);
        float minR = max(0.0, front - 2.6 * tailWidth);
        float maxR = front + 0.65;
        if (rSq < minR * minR || rSq > maxR * maxR) {
            continue;
        }

        float r = sqrt(rSq);
        float u = r - front;
        float invWidth = u > 0.0 ? 3.6 : (1.0 / max(tailWidth, 0.15));
        float scaledU = u * invWidth;
        float env = exp(-scaledU * scaledU);

        // Phase velocity runs slightly faster than group velocity, drifting crests forward through the packet.
        float phase = u * waveNumber - age * 2.8;
        float dWaveDr = (waveNumber * cos(phase) - 2.0 * scaledU * invWidth * sin(phase)) * env;

        // Cylindrical 1/sqrt(r) energy spreading, smooth origin taper, and smooth lifetime fade-in/out.
        float originTaper = smoothstep(0.0, 0.18, r);
        float spreadAtten = inversesqrt(1.0 + 0.55 * r);
        float timeFade = smoothstep(0.0, 0.04, lifeFrac) * (1.0 - lifeFrac) * (1.0 - lifeFrac);

        vec2 dir = offset / max(r, 1e-4);
        totalSlope += dir * (dWaveDr * amplitude * originTaper * spreadAtten * timeFade);
    }

    // Softly saturate overlapping wake trains so a crowd of entities never folds the surface normal upside down.
    float mag = length(totalSlope);
    if (mag > 1e-5) {
        float scale = INTERACTIVE_RIPPLE_SLOPE * strength * coneFade *
                      (isPuddle ? INTERACTIVE_PUDDLE_RIPPLE_SCALE : 1.0);
        float saturated = 1.35 * tanh(mag * scale / 1.35);
        // In steps with Pixel Rain: each pixel of a ring leans one of a few set amounts, as a texture painted for the
        // game would shade it, not by a smooth gradient.
        if (VPT_RAIN_PIXELS != 0) { saturated = floor(saturated / 0.1 + 0.5) * 0.1; }
        totalSlope *= saturated / mag;
    }
    return totalSlope;
}

vec3 vptApplyInteractiveRippleNormal(vec3 shadingNormal, vec3 baseGeoNormal, vec2 slope) {
    if (dot(slope, slope) <= 0.0) {
        return shadingNormal;
    }
    float signY = baseGeoNormal.y < 0.0 ? -1.0 : 1.0;
    return normalize(shadingNormal + vec3(-slope.x, 0.0, -slope.y) * signY);
}

#endif // VPT_WATER_RIPPLES_GLSL
