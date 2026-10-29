#ifndef RADIANTE_HOOK_SURFACE_GLSL
#define RADIANTE_HOOK_SURFACE_GLSL

// First Light: the polished look of the showcase worlds, where glazed terracotta, quartz, gold and polished stone read
// as glossy and caught the sky and the lights. Surfaces that are already somewhat smooth (roughness under about 0.5,
// from a PBR or Bedrock material) get smoother in proportion; rough ones (dirt, grass, wool) stay as they are, so the
// world does not turn to plastic. The same for every bounce, so reflections and bounces see the same surfaces the
// camera does. Only the roughness changes, so the albedo guide is untouched.
#ifndef FL_POLISH
#    define FL_POLISH 0.4
#endif

void hookSurface(inout SampledSurface surface, uint surfaceFlags, uint bounce) {
    float polish = clamp(float(FL_POLISH), 0.0, 1.0);
    if (polish <= 0.0) { return; }
    if (isWaterSurface(surfaceFlags) || isLeavesSurface(surfaceFlags) || isPlantSurface(surfaceFlags) ||
        isHeldSurface(surfaceFlags)) {
        return;
    }
    float r = surface.mat.roughness;
    float smoothable = 1.0 - smoothstep(0.35, 0.6, r);
    surface.mat.roughness = r * (1.0 - 0.5 * polish * smoothable);
}

#endif
