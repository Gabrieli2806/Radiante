#ifndef AETHER_SETTINGS_GLSL
#define AETHER_SETTINGS_GLSL

// Aether: the pack's settings (configs.json), with their defaults for a compile without them. Most are live
// ("runtime": true): their defines read worldUBO.packParams while the shader runs, so the hooks test them with if,
// never #if. The ones that decide which code exists are compiled in: AETHER_ROUND_DISCS and AETHER_CLOUDS.

// Sky
#ifndef AETHER_SKY
#    define AETHER_SKY 0.8
#endif
#ifndef AETHER_SKY_LIGHT
#    define AETHER_SKY_LIGHT 0.2
#endif
#ifndef AETHER_ROUND_DISCS
#    define AETHER_ROUND_DISCS 1
#endif
#ifndef AETHER_SUN_SIZE
#    define AETHER_SUN_SIZE 1.0
#endif
#ifndef AETHER_CORONA
#    define AETHER_CORONA 0.5
#endif
#ifndef AETHER_END_SKY
#    define AETHER_END_SKY 1.0
#endif

// Night
#ifndef AETHER_STARS
#    define AETHER_STARS 1.0
#endif
#ifndef AETHER_MILKY_WAY
#    define AETHER_MILKY_WAY 0.6
#endif
#ifndef AETHER_NIGHT_GLOW
#    define AETHER_NIGHT_GLOW 1.0
#endif

// Clouds
#ifndef AETHER_CLOUDS
#    define AETHER_CLOUDS 1
#endif
#ifndef AETHER_CLOUD_COVER
#    define AETHER_CLOUD_COVER 0.45
#endif
#ifndef AETHER_CIRRUS
#    define AETHER_CIRRUS 0.5
#endif
#ifndef AETHER_CLOUD_SPEED
#    define AETHER_CLOUD_SPEED 1.0
#endif
#ifndef AETHER_CLOUD_SHADOWS
#    define AETHER_CLOUD_SHADOWS 0.6
#endif

// Atmosphere
#ifndef AETHER_HAZE
#    define AETHER_HAZE 1.0
#endif
#ifndef AETHER_VALLEY_MIST
#    define AETHER_VALLEY_MIST 0.5
#endif
#ifndef AETHER_NETHER_HAZE
#    define AETHER_NETHER_HAZE 1.2
#endif

// Light and surfaces
#ifndef AETHER_SUN_WARMTH
#    define AETHER_SUN_WARMTH 0.5
#endif
#ifndef AETHER_LEAF_LIGHT
#    define AETHER_LEAF_LIGHT 1.0
#endif
#ifndef AETHER_TORCH_WARMTH
#    define AETHER_TORCH_WARMTH 0.6
#endif
#ifndef AETHER_WETNESS
#    define AETHER_WETNESS 0.8
#endif
#ifndef AETHER_PUDDLES
#    define AETHER_PUDDLES 0.6
#endif

// Water
#ifndef AETHER_WATER_TINT
#    define AETHER_WATER_TINT 0.7
#endif
#ifndef AETHER_WATER_CLARITY
#    define AETHER_WATER_CLARITY 0.5
#endif
#ifndef AETHER_WATER_CALM
#    define AETHER_WATER_CALM 0.5
#endif
#ifndef AETHER_UNDERWATER_WARP
#    define AETHER_UNDERWATER_WARP 0.4
#endif

// Post
#ifndef AETHER_GRADE
#    define AETHER_GRADE 1.0
#endif
#ifndef AETHER_BLOOM
#    define AETHER_BLOOM 0.25
#endif
#ifndef AETHER_STREAK
#    define AETHER_STREAK 0.3
#endif
#ifndef AETHER_VIGNETTE
#    define AETHER_VIGNETTE 0.35
#endif
#ifndef AETHER_ABERRATION
#    define AETHER_ABERRATION 0.25
#endif

// The pack's debug view (configs.json "debug_views", /radiante debug view aether_wetness): 1 while chosen.
#ifndef AETHER_DEBUG_WETNESS
#    define AETHER_DEBUG_WETNESS 0
#endif

#endif
