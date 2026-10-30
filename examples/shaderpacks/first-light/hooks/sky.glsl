#ifndef RADIANTE_HOOK_SKY_GLSL
#define RADIANTE_HOOK_SKY_GLSL

// First Light: the deep, saturated blue sky of the RTX showcases. Only for the sky the camera sees (its own ray,
// through glass and water, and the reflections smooth surfaces show it, RAY_VIEW_BOUNCE_MARK): there the colour is
// pushed further from grey about its own luminance and the upper sky darkened a little towards a deeper blue, so the
// exposure stays Radiante's. Rain fades it back to Radiante's sky.
//
// The sky that lights the world (rays that bounced off it) is left as Radiante draws it: it decides how bright and
// what colour the shade is, and a bluer one would flood the world with blue.
#ifndef FL_SKY_BLUE
#    define FL_SKY_BLUE 0.5
#endif

#include "first_light/fl_common.glsl"

bool flSkySeen() {
    return rayBounce(mainRay) == 0u || rayCameraPath(mainRay) || mainRay.pad0 == RAY_VIEW_BOUNCE_MARK;
}

void hookSky(inout vec3 radiance, vec3 direction) {
    float amount = clamp(float(FL_SKY_BLUE), 0.0, 1.0) * (1.0 - radianteRain());
    if (amount <= 0.0 || !flSkySeen()) { return; }
    vec3 deep = flSaturate(radiance, 1.0 + 0.6 * amount);
    // Deeper overhead, the horizon kept bright: the gradient the showcase skies had.
    deep *= 1.0 - 0.25 * amount * smoothstep(0.15, 0.9, direction.y);
    radiance = deep;
}

#endif
