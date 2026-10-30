#ifndef RADIANTE_HOOK_CELESTIAL_GLSL
#define RADIANTE_HOOK_CELESTIAL_GLSL

// Aether: a round sun with a soft corona, and a round moon with its phase, its seas and a faint halo.
//
// Round Sun and Moon (compiled in) replaces vanilla's square sprites: the sun a disc about three degrees across
// (Sun and Moon Size scales both), darker and warmer towards its rim as a real sun is (limb darkening); the moon a
// sphere lit from the side its phase says (skyUBO.moonPhase), the unlit part faintly earthlit, with darker seas from
// the noise image. Both are drawn at the brightness radiante-pt gives its own discs (sunLight, moonLight), for every
// ray, as radiante-pt draws them. Off, vanilla's sprites stay and only the glow is added.
//
// The glow (Sun and Moon Glow): a tight corona round the disc and a wider, fainter aureole, for the camera only
// (aetherSeen), like radiante-pt's own halo: a bright patch of sky that bounce rays could find would light the world
// in speckles. Rain hides the discs and their glow, as radiante-pt hides its own.
#include "aether/aether_sky.glsl"
#include "aether/aether_noise.glsl"

// Angular radii at size 1, in radians.
const float AETHER_SUN_RADIUS = 0.05;
const float AETHER_MOON_RADIUS = 0.042;

void hookSunDisc(inout vec3 radiance, vec3 direction, vec3 sunDirection, vec3 sunLight) {
    float c = clamp(dot(direction, sunDirection), -1.0, 1.0);
    // Past about 25 degrees there is neither disc nor glow.
    if (c < 0.9) { return; }
    float angle = acos(c);
    float radius = AETHER_SUN_RADIUS * clamp(float(AETHER_SUN_SIZE), 0.25, 3.0);
#if AETHER_ROUND_DISCS
    float x = angle / radius;
    float disc = 1.0 - smoothstep(0.86, 1.0, x);
    radiance = vec3(0.0);
    if (disc > 0.0) {
        float mu = sqrt(max(1.0 - x * x, 0.0));
        vec3 tint = mix(vec3(1.00, 0.80, 0.62), vec3(1.00, 0.98, 0.94), mu);
        radiance = sunLight * (tint / aetherLuma(tint)) * (disc * (0.45 + 0.55 * mu));
    }
#endif
    float corona = clamp(float(AETHER_CORONA), 0.0, 2.0);
    if (corona > 0.0 && aetherSeen()) {
        float inner = exp(-pow(angle / (radius * 2.2), 2.0));
        float outer = exp(-pow(angle / 0.16, 2.0));
        radiance += sunLight * ((0.10 * inner + 0.012 * outer) * corona);
    }
}

void hookMoonDisc(inout vec3 radiance, vec3 direction, vec3 moonDirection, vec3 moonLight) {
    float c = clamp(dot(direction, moonDirection), -1.0, 1.0);
    if (c < 0.95) { return; }
    float angle = acos(c);
    float radius = AETHER_MOON_RADIUS * clamp(float(AETHER_SUN_SIZE), 0.25, 3.0);
#if AETHER_ROUND_DISCS
    radiance = vec3(0.0);
    if (angle < radius) {
        // The disc's own frame: right along the moon's path across the sky, so the lit side is towards where the
        // sun is along it.
        vec3 axis = skyUBO.moonAxis.xyz;
        if (dot(axis, axis) < 1e-6) { axis = skyUBO.celestialAxis.xyz; }
        vec3 right = cross(axis, moonDirection);
        right = dot(right, right) > 1e-6 ? normalize(right) : vec3(1.0, 0.0, 0.0);
        vec3 up = cross(moonDirection, right);
        vec2 q = vec2(dot(direction, right), dot(direction, up)) / sin(radius);
        float r2 = dot(q, q);
        float disc = 1.0 - smoothstep(0.86, 1.0, sqrt(r2));
        // The sphere's normal, facing the viewer, and the sunlight on it: from behind the viewer at full moon (phase
        // 0), from the side at the quarters, from behind the moon at new moon (4).
        vec3 normal = q.x * right + q.y * up - sqrt(max(1.0 - r2, 0.0)) * moonDirection;
        float phase = float(radianteMoonPhase()) * (AETHER_PI / 4.0);
        vec3 sunlight = -moonDirection * cos(phase) + right * sin(phase);
        float lit = smoothstep(-0.06, 0.10, dot(normal, sunlight));
        float seas = 0.72 + 0.28 * smoothstep(0.25, 0.75, aetherNoiseAt(q * 0.25 + vec2(0.31, 0.57)).r);
        vec3 tint = vec3(0.92, 0.96, 1.06);
        radiance = moonLight * (tint / aetherLuma(tint)) * (disc * seas * mix(0.025, 1.0, lit));
    }
#endif
    float halo = clamp(float(AETHER_CORONA), 0.0, 2.0);
    if (halo > 0.0 && aetherSeen()) {
        float inner = exp(-pow(angle / (radius * 2.5), 2.0));
        float outer = exp(-pow(angle / 0.10, 2.0));
        radiance += moonLight * ((0.06 * inner + 0.02 * outer) * halo * radianteMoonLit());
    }
}

#endif
