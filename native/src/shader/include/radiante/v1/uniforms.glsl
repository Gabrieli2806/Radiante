#ifndef RADIANTE_V1_UNIFORMS_GLSL
#define RADIANTE_V1_UNIFORMS_GLSL

// What the engine tells the shaders every frame, as two uniform blocks: RadianteWorld (the camera, the fog, the hands'
// lights, the wind, the pack's live settings) and RadianteSky (the sun and the moon, the weather, the biome, the
// water). The engine fills them (Buffers, BufferProxy on the Java side) with the layout of WorldUBO and SkyUBO in
// common/shared.hpp; these structs give the same bytes their own names, and the native build checks the two agree
// member by member (native/src/tools/check_v1_layout.py). Part of radiante/v1: members are only ever added at the end.
//
// A pack declares the blocks itself, std140, where the engine binds them. In the ray tracing passes, set 2: binding 0
// this frame's world, 1 the last frame's (for motion vectors), 2 the sky. In a pack's post passes, set 1: binding 0
// the world, 1 the sky. For example:
//
//     layout(set = 2, binding = 0) uniform PackWorld { RadianteWorld world; };
//
// Positions are in blocks and directions are unit vectors unless a comment says otherwise. A texture id indexes the
// pack's array of all textures (bindless).

struct RadianteWorld {
    // ---- The camera (bytes 0 to 399)
    // The camera's view (world to view space) and its projection, each with its inverse. The shaken view adds the
    // game's view bobbing and hurt shake; rays from the camera start from it.
    mat4 view;
    mat4 shakenView;
    mat4 projection;
    mat4 viewInverse;
    mat4 shakenViewInverse;
    mat4 projectionInverse;
    // This frame's sub-pixel offset of the picture for the upscaler, in pixels (each axis -0.5 to 0.5); 0 when off.
    vec2 jitter;
    // Game time in ticks, modulo 24000, as a fraction from 0 to 1 (partial ticks included): a 20 minute clock.
    float gameCycle;
    // A random number, new every frame.
    uint seed;

    // ---- Textures and fog (bytes 400 to 511)
    // The matrix the game slides the enchantment glint's texture with.
    mat4 glintTextureMatrix;
    // Texture id of the overlay texture (an entity's red flash when hurt, white before it explodes).
    uint overlayTexture;
    // Bit 0: the camera is in first person. Bit 1: the first person hands are drawn this frame.
    uint firstPerson;
    // Where the game's fog starts and ends, in blocks from the camera: in water, lava or powder snow, or with
    // blindness or darkness, that short fog; else the render distance's, pushed out to Distant Horizons' reach.
    float fogStart;
    float fogEnd;
    vec4 fogColor;
    // The fog's shape: always 0, a sphere around the camera.
    uint fogShape;
    // The dimension's sky: 0 none (the Nether), 1 the overworld's, 2 the End's.
    uint skyType;
    // Not 0 while some entity glows, so the outline rays run only then.
    uint glowOutline;
    // Not 0 when block lights are sampled one by one: the option is on and Block Emission fills the light list.
    uint blockLightSampling;

    // ---- Where the camera is (bytes 512 to 575)
    // The camera's position in the world, in blocks, in double precision; w is 0.
    dvec4 cameraPosition;
    // The grid of chunk sections kept built around the camera: its size in sections along x, y and z, and in w the
    // lowest section's y (in sections: a block's y over 16).
    ivec4 sectionGrid;
    // Where that grid starts, in sections; w is 0.
    ivec4 sectionGridOrigin;

    // ---- Textures and light (bytes 576 to 655)
    // Texture ids: the End's sky, the End portal's, and the game's light map (the colours of block and sky light).
    uint endSkyTexture;
    uint endPortalTexture;
    uint lightMapTexture;
    // The game draws the first person hand at a 70 degree field of view whatever the setting: tan(35 degrees) over
    // tan(half the field of view), which rays that look for the hand are scaled by.
    float handFovScale;
    // The light of what the main hand holds (a torch, a lantern...): xyz where it is, relative to the camera, and w
    // how far it reaches in blocks; rgb its light, and w not 0 while there is one.
    vec4 mainHandLightPosition;
    vec4 mainHandLightColor;
    // Not 0: where a parallax-carved face meets its block's outer edge, the carved corner is see-through.
    uint parallaxTransparentEdges;
    // The player's brightness sliders, 1 meaning the pack's own values: the sun and the day's sky, the moon and the
    // night's, and the blocks that give light (their glow and the light they cast).
    float sunBrightness;
    float moonBrightness;
    float emissionBrightness;
    // Always 0: Radiante writes nothing here now.
    uint pixelLighting;
    // How far rays reach for terrain past the usual distance (Distant Horizons' terrain), in blocks; 0 without it.
    float farTerrainReach;
    // How many blocks the rain fell since the last frame (what the drops' motion vectors come from).
    float rainFallPerFrame;
    // The player's slider for what the hands hold (its glow and its light), apart from emissionBrightness.
    float heldLightBrightness;

    // ---- Lights, far terrain and wind (bytes 656 to 703)
    // Frames drawn so far.
    uint frameCounter;
    // The lights inside glowing entities (dropped items, item frames...), lit like block lights: how many, and the
    // device address of their list in two 32 bit halves.
    uint entityLightCount;
    uint entityLightsLow;
    uint entityLightsHigh;
    uint entityLightsPad;
    // The map of where Distant Horizons' terrain stays hidden because the near terrain is built there: its device
    // address in two halves, 0 without far terrain.
    uint farTerrainCoverLow;
    uint farTerrainCoverHigh;
    uint farTerrainCoverPad0;
    uint farTerrainCoverPad1;
    // The wind's clock in seconds of game time (it stops with the game and starts over every six hours of play), the
    // player's wind strength (0 with wind off) and the weather's (1 when clear, more in rain and thunder).
    float windTime;
    float windStrength;
    float windWeather;

    // ---- The off hand (bytes 704 to 735)
    // Its light, as mainHandLightPosition and mainHandLightColor: a torch in each hand lights both sides.
    vec4 offHandLightPosition;
    vec4 offHandLightColor;

    // ---- The pack's settings (bytes 736 to 4831)
    // The settings the pack's configs.json marks "runtime", one slot each in the order it lists them: a number in x
    // (an int, a bool as 0 or 1, a choice as its index), a vec3 in xyz. Copied in every frame: changing one needs no
    // rebuild.
    vec4 packParams[256];
};

struct RadianteSky {
    // ---- The vanilla sky (bytes 0 to 111)
    vec3 skyColor;
    // As RadianteWorld.skyType: 0 none (the Nether), 1 the overworld's, 2 the End's.
    uint skyType;
    // The sunrise and sunset glow on the horizon: rgb its colour, a how strong (0 outside dawn and dusk).
    vec4 sunsetColor;
    // Towards the sun. In the End, towards its flash, scaled by the flash's strength (0 between flashes).
    vec3 sunDirection;
    // Not 0 while the sun is up and the horizon glows.
    uint sunRisingOrSetting;
    // Not 0 when the game darkens the sky below the horizon (the camera is under the world's horizon height).
    uint darkLowerSky;
    // Not 0 under Blindness or Darkness.
    uint blindOrDark;
    // What the camera is in: 0 lava, 1 water, 2 powder snow, 3 none of them.
    uint submersion;
    // The game's moon phase: 0 full, 4 new.
    uint moonPhase;
    // How hard it rains, 0 to 1.
    float rainLevel;
    // Texture ids of the sun and the moon: both the celestials atlas, the bodies at sunUvRect and moonUvRect.
    uint sunTexture;
    uint moonTexture;
    // Night Vision's strength, 0 to 1, fading out over its last ten seconds as vanilla's does.
    float nightVision;
    // Each body's rectangle in that atlas (u0, v0, u1, v1); the moon's for this phase.
    vec4 sunUvRect;
    vec4 moonUvRect;

    // ---- Radiante's ambiance (bytes 112 to 303)
    // The biome haze around the camera: rgb the share of its extinction that scatters (its tint), a its extinction
    // per block. All 0 with Biome Ambiance off, outside the overworld's sky, or out of sky light.
    vec4 biomeHaze;
    // xyz the axis the sun and the moon turn around (along one edge of their square sprites); w 1 to line the
    // sprites up with it (Vanilla Sun/Moon Orientation), 0 for a free turn.
    vec4 celestialAxis;
    // rgb each channel's extinction against biomeHaze.a: a haze that takes more blue than red turns the distance
    // orange.
    vec4 biomeHazeChroma;
    // x the height below which the haze is full, y where it is gone; z how open the camera is to the sky (0 to 1,
    // eased); w 1 on a frame where the time was set and the sun jumped.
    vec4 biomeHazeHeights;
    // The water around the camera as a medium: extinction rgb per block, w 1 plus the camera's depth under water;
    // albedo rgb the share that scatters, a how much sky reaches the water's surface (0 to 1).
    vec4 waterExtinction;
    vec4 waterAlbedo;
    // x how far Blindness or Darkness has closed in (0 to 1), y Darkness's pulse (0 to 1), z the Volumetric Fog
    // Strength option (1 at 100 %), w a factor on the stars' brightness (0 reads as 1; below 0, another mod draws
    // them).
    vec4 fogControls;
    // x how wet the ground is (0 to 1, drying slowly after rain); y the texture id of the 512 x 512 heights where
    // something stops the rain, around the camera, 0 while nothing is wet.
    vec4 groundWetness;
    // Looking out under water: rgb the colour it fades into (lit by the sky in the shader), a the distance in blocks
    // over which it fades to about a third, 0 with Biome Ambiance off.
    vec4 underwaterFog;
    // xyz towards the moon, w unused: opposite the sun unless the Moon Path settings lean or turn it.
    vec4 moonDirection;
    // xyz the axis the moon's path turns around, w unused.
    vec4 moonAxis;
    // Each sprite turned around its own centre (Sun/Moon Rotation): xy cos and sin of the sun's angle, zw the moon's.
    vec4 spriteRotation;

    // ---- Weather, biome and water (bytes 304 to 351)
    // The biome at the camera, eased over a couple of seconds: x its temperature, y its downfall, z how snowy (1 where
    // snow falls at the camera's height), w how dry (1 where nothing falls).
    vec4 biomeClimate;
    // How hard it thunders, 0 to 1.
    float thunderLevel;
    // A hash (FNV-1a) of the biome's id at the camera ("minecraft:snowy_plains") and of the one before it, 0 without
    // a world, and how far the change between them has come (0 to 1, eased as biomeClimate).
    uint biomeId;
    uint previousBiomeId;
    float biomeBlend;
    // The air's wetness: the rain smoothed, rising with a half-life of a minute and falling with one of 20 seconds.
    float airWetness;
    // Seconds since the rain eased off after wetting the air: 0 while it rains, 1e6 before any rain.
    float rainStoppedFor;
    // The water the camera is in, eased, both 0 out of water: its depth at the eye (floor to surface, at most 64
    // blocks) and how far it runs across at eye level (1 for a 1 x 1 pool, at most 49).
    float waterBodyDepth;
    float waterBodyWidth;
};

#endif
