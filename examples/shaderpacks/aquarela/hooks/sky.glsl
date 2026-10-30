#ifndef RADIANTE_HOOK_SKY_GLSL
#define RADIANTE_HOOK_SKY_GLSL

// Aquarela: the sky as soft wet-in-wet washes. Where the camera looks (its own ray, also through glass or water), two
// blues are dropped into each other, a greener cerulean and a violet ultramarine, in large soft patches, with paler
// blooms where the water pushed the pigment aside, and the band at the horizon left close to the paper. The patches
// are noise on the ray's direction, so they stay in the sky as the camera turns, and they are large and smooth: the
// fine, random marks of the painting (paper, granulation) are the post passes', fixed to the screen.
//
// Only the hue and the brightness of each patch change; their average is Radiante's sky, so the exposure does not
// move. Rays that bounced off the world keep Radiante's sky exactly: they are the light the sky casts, and a sky
// tinted for them tints the whole world.
#ifndef AQUARELA_SKY
#    define AQUARELA_SKY 0.7
#endif

#include "aquarela/aquarela_noise.glsl"

void hookSky(inout vec3 radiance, vec3 direction) {
    float strength = clamp(float(AQUARELA_SKY), 0.0, 1.0);
    if (strength <= 0.0) { return; }
    if (rayBounce(mainRay) != 0u && !rayCameraPath(mainRay)) { return; }
    const vec3 luma = vec3(0.2126, 0.7152, 0.0722);
    float brightness = dot(radiance, luma);
    if (brightness <= 1e-6) { return; }

    // Squashed upwards, so the washes run across the sky in long horizontal strokes, as a brush lays them.
    vec3 p = vec3(direction.x, direction.y * 2.4, direction.z) * 1.8;
    float pigment = aquarelaFbm3(p, 3);
    float bloom = aquarelaFbm3(p * 1.6 + vec3(11.0, 3.0, 7.0), 3);

    vec3 own = radiance / brightness;
    // The two blues as shifts of the sky's own colour, so a dusk sky keeps its oranges.
    vec3 cerulean = vec3(0.90, 1.03, 1.07);
    vec3 ultramarine = vec3(1.08, 0.95, 1.08);
    vec3 hue = own * mix(cerulean, ultramarine, smoothstep(0.35, 0.65, pigment));
    // Near the horizon the paint thins towards the paper: half the colour gone at the horizon itself.
    float horizon = 1.0 - smoothstep(0.0, 0.3, abs(direction.y));
    hue = mix(hue, vec3(dot(hue, luma)), 0.5 * horizon);
    // Paler blooms and darker pools, about a fifth either way, averaging out.
    float light = 1.0 + 0.4 * (bloom - 0.5);
    vec3 washed = hue * (brightness * light / max(dot(hue, luma), 1e-6));
    radiance = mix(radiance, washed, strength);
}

#endif
