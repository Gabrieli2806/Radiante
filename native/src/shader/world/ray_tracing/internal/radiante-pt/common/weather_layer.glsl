#ifndef VPT_WEATHER_LAYER_GLSL
#define VPT_WEATHER_LAYER_GLSL

// The falling weather as a layer of its own over the picture: rain streaks and snowflakes the camera sees (straight,
// or through glass and water) are not added to the radiance the denoiser and the upscaler take, but written here, and
// the tone mapping module puts them over the finished picture (tone_mapping.frag), after DLSS Ray Reconstruction, NRD,
// FSR or XeSS. They fall on their own, so the history those keep of the picture had them leave trails behind as they
// fell (ghosting) or blended them away while the camera stood still; over the picture they show as they are in each
// frame, and the denoiser has only the still world to settle.
//
// rgb: the light the weather adds, front to back, on the radiance's scale. a: how much of the picture behind it the
// weather hides (0 none, 1 all). Cleared by the ray generation shader at the start of every pixel
// (vptWeatherLayerClear) and written by the hit shaders at their pixel (gl_LaunchIDEXT), one ray of it at a time.
// Render resolution; the tone mapping pass samples it at its own.

// Its storage binding as the loader names it (configs.json, radiante_weather_layer).
#ifndef RADIANTE_IMAGE_RADIANTE_WEATHER_LAYER
#    define RADIANTE_IMAGE_RADIANTE_WEATHER_LAYER 54
#endif
layout(set = 5, binding = RADIANTE_IMAGE_RADIANTE_WEATHER_LAYER, rgba16f) uniform image2D vptWeatherLayerImage;

void vptWeatherLayerClear(ivec2 pixel) {
    imageStore(vptWeatherLayerImage, pixel, vec4(0.0));
}

// Light in front of what the layer holds so far is added under what it already hides: the nearer streak covers the
// farther one.
void vptWeatherLayerAdd(vec3 light, float hides) {
    ivec2 pixel = ivec2(gl_LaunchIDEXT.xy);
    vec4 layer = imageLoad(vptWeatherLayerImage, pixel);
    float behind = 1.0 - clamp(layer.a, 0.0, 1.0);
    layer.rgb += behind * max(light, vec3(0.0));
    layer.a = 1.0 - behind * (1.0 - clamp(hides, 0.0, 1.0));
    imageStore(vptWeatherLayerImage, pixel, layer);
}

#endif
