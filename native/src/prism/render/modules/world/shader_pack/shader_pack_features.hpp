#pragma once

#include <string_view>

// The parts of the pack interface that came after the first release with shader packs, by the name a pack's manifest
// gives them in "requires": {"features": [...]} (docs/dev/shader-packs.md, section 3). Every pack compile gets
// RADIANTE_FEATURE_<NAME> defined to 1 for each (ShaderPackLoader::addResourceDefinitions), so a pack that can do
// without one tests it with #ifdef and still compiles on a Radiante without it, where the define is missing. Such a
// pack does not list it, or lists it with a "?" ("glass_frost?"), which Radiante after 1.2.0 skips; 1.2.0 itself
// refuses any name it does not know, "?" or not.
//
// Only ever added to. The same names are in ShaderPackManifest.FEATURES (Java, which refuses a pack that needs one it
// does not know) and read from this file by compile_pack.py and ShaderPackManifestTest, which checks the two lists
// agree.
inline constexpr std::string_view kShaderPackFeatures[] = {
    "noise",            // radiante/v1/noise.glsl
    "clock",            // radianteTime(), radianteFrame() in common/pack_world.glsl
    "player_state",     // radianteHealth() and the player's other values in common/pack_world.glsl
    "named_resources",  // textures without a binding, RADIANTE_TEXTURE_<NAME>
    "shadow_softness",  // hookShadowSoftness in hooks/ray_origins.glsl
    "colour_settings",  // a vec3 attribute shown as three sliders
    "glass_frost",      // hooks/glass_frost.glsl, radianteIsGlassFrost
    "footprints",       // hooks/footprints.glsl
    "falling_weather",  // hooks/rain_streak.glsl, radianteIsSnowfall
    "changes_every_frame",  // hookChangesEveryFrame() for hookSurface
};
