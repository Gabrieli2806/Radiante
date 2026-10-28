#ifndef RADIANTE_HOOK_CLOUDS_GLSL
#define RADIANTE_HOOK_CLOUDS_GLSL

// Hook point (shader packs that extend radiante-pt replace this file). Part of the pack interface: the signature
// stays as it is within radiante-pt@1.
//
// A pack's own cloud layer, called in the overworld miss shader after rain has greyed the sky and after the sun and
// moon discs and their halos are added, so the clouds cover them and stay in rain. Rays that bounce off the world and
// reach the sky go through here too, as through hookSky. The sky behind becomes sky * transmittance + radiance; the
// stars drawn later where the camera looks are dimmed by the transmittance as well.
//   direction world-space ray direction, unit length (y up)
// Returns the light the clouds scatter towards the ray (rgb) and how much of the sky behind them shows through (a).
vec4 hookCloudLayer(vec3 direction) {
    return vec4(0.0, 0.0, 0.0, 1.0);
}

// Only in this default file: radiante-pt then skips the call, so its shaders are the same as before the hook
// existed. A pack's file must not define it.
#define RADIANTE_HOOK_CLOUDS_DEFAULT 1

#endif
