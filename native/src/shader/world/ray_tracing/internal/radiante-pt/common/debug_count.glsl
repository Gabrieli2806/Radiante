#ifndef RADIANTE_DEBUG_COUNT_GLSL
#define RADIANTE_DEBUG_COUNT_GLSL

// The heat map debug views (common/debug_view.glsl): `anyhit` counts the any-hit invocations of each pixel's camera
// rays, `bounces` the rays of its path past them. The count is kept per pixel in radiante_debug_count, an atomic image
// so that no ray payload changes, and debug/debug_view.comp turns it into colour and clears it. Everything here
// compiles to nothing unless one of the two views is chosen, so the shaders stay the same. Include it after the ray
// payload's declaration.

#define RADIANTE_DEBUG_COUNT_STORAGE_BINDING 122

#if defined(RADIANTE_DEBUG_VIEW) && (RADIANTE_DEBUG_VIEW == 9 || RADIANTE_DEBUG_VIEW == 10)
layout(set = 5, binding = RADIANTE_DEBUG_COUNT_STORAGE_BINDING, r32ui) uniform uimage2D radianteDebugCountImage;
#endif

// First thing in an any-hit shader of the main ray, so that a hit it goes on to reject still counts: those are the
// costly ones. The SHaRC update's rays are left out: their launch grid is the cache's own, not the picture's.
#if defined(RADIANTE_DEBUG_VIEW) && RADIANTE_DEBUG_VIEW == 9
#    define RADIANTE_DEBUG_COUNT_ANYHIT()                                                                              \
        if (rayBounce(mainRay) == 0u && !rayCacheUpdate(mainRay)) {                                                    \
            imageAtomicAdd(radianteDebugCountImage, ivec2(gl_LaunchIDEXT.xy), 1u);                                     \
        }
#else
#    define RADIANTE_DEBUG_COUNT_ANYHIT()
#endif

// In the ray generation shader, for each ray of the path past the camera's.
#if defined(RADIANTE_DEBUG_VIEW) && RADIANTE_DEBUG_VIEW == 10
#    define RADIANTE_DEBUG_COUNT_BOUNCE() imageAtomicAdd(radianteDebugCountImage, ivec2(gl_LaunchIDEXT.xy), 1u)
#else
#    define RADIANTE_DEBUG_COUNT_BOUNCE()
#endif

#endif
