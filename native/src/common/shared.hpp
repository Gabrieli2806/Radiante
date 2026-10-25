// Data that crosses between C++ and the shaders: vertex formats and the uniform blocks. Both languages compile this
// one file (the T_ types come from mapping.hpp), so a member added here appears on both sides at once.
//
// Layout rules for anyone editing it:
//   - Members are only ever appended or put in place of padding. Shaders read these by offset, Java writes the start
//     of WorldUBO byte by byte, and render/buffers.cpp asserts the offsets that Java depends on.
//   - C++ lays members out by natural alignment, the uniform blocks by std140. The two agree only when every vec3 is
//     followed by a 4-byte scalar and every vec4/mat4 starts on 16 bytes, which is what the pad members are for.
#ifndef SHARED_HPP
#define SHARED_HPP

#include "common/mapping.hpp"

// Largest finite half float: the "nothing was hit" distance, safe to store in 16-bit targets.
#define INF_DISTANCE 65504.0
#define PI 3.14159265358979323
#define INV_PI 0.31830988618379067
#define TWO_PI 6.28318530717958648
#define INV_TWO_PI 0.15915494309189533
#define INV_4_PI 0.07957747154594766

// Slots in WorldUBO.packParams: shader pack settings the shaders read live instead of having them compiled in.
#define PRISM_PACK_PARAM_COUNT 64

#ifdef __cplusplus
namespace gpu {
namespace VertexFormat {
#endif

// ---------------------------------------------------------------------------------------------------------------------
// Vertex formats. The Position* ones follow Minecraft's vertex formats of the same names: "uv0" is the texture
// coordinate, "uv1" the overlay, "uv2" the lightmap, and a packed normal uses only its first three bytes.
// ---------------------------------------------------------------------------------------------------------------------

struct PositionOnly {
    T_VEC3 position;
};

struct PositionTexColor {
    T_VEC3 position;
    T_VEC2 uv;
    T_UINT color;
};

struct PositionColor {
    T_VEC3 position;
    T_UINT color;
};

struct PositionColorNormal {
    T_VEC3 position;
    T_UINT color;
    T_UINT normal;
};

struct PositionTex {
    T_VEC3 position;
    T_VEC2 uv;
};

struct PositionColorTexLight {
    T_VEC3 position;
    T_UINT color;
    T_VEC2 uv0;
    T_UINT uv2;
};

struct PositionColorLight {
    T_VEC3 position;
    T_UINT color;
    T_UINT uv2;
};

struct PositionTexColorLight {
    T_VEC3 position;
    T_VEC2 uv0;
    T_UINT color;
    T_UINT uv2;
};

struct PositionTexColorNormal {
    T_VEC3 position;
    T_VEC2 uv0;
    T_UINT color;
    T_UINT normal;
};

struct PositionTexLightColor {
    T_VEC3 position;
    T_VEC2 uv0;
    T_UINT uv2;
    T_UINT color;
};

struct PositionColorTexLightNormal {
    T_VEC3 position;
    T_UINT color;
    T_VEC2 uv0;
    T_UINT uv2;
    T_UINT normal;
};

struct PositionColorTexOverlayLightNormal {
    T_VEC3 position;
    T_UINT color;
    T_VEC2 uv0;
    T_UINT uv1;
    T_UINT uv2;
    T_UINT normal;
};

// Everything one vertex of a world or entity mesh can carry before it is split into position and material. The use*
// flags say which of the optional inputs below them are meaningful, since a single format serves every mesh.
struct PBRVertex {
    T_VEC3 pos;
    T_UINT useNorm;

    T_VEC3 norm;
    T_UINT useColorLayer;

    T_VEC4 colorLayer;

    T_UINT useTexture;
    T_UINT useOverlay;
    T_VEC2 textureUV;

    T_IVEC2 overlayUV;
    T_UINT useGlint;
    T_UINT textureID;

    T_VEC2 glintUV;
    T_UINT glintTexture;
    T_UINT useLight;

    T_IVEC2 lightUV;
    T_UINT coordinate;
    T_FLOAT albedoEmission;

    // Section geometry carries its wind code in x (util/wind.glsl): 0 for a vertex that stays put. y and z are unused
    // there; the star pass reads all three.
    T_VEC3 postBase;
    T_UINT alphaMode;
};

// The position half of a PBRVertex (Vertex::buildPositionVertices); pad0 keeps the stride at 16 bytes.
struct PositionVertex {
    T_VEC3 pos;
    T_UINT pad0;
};

// The shading half of a vertex, as the shaders work with it. Not what the buffers store: see PackedMaterialVertex.
struct MaterialVertex {
    T_VEC3 norm;
    T_UINT textureID;

    T_VEC4 colorLayer;

    T_VEC2 textureUV;
    T_IVEC2 overlayUV;

    T_VEC2 glintUV;
    T_UINT glintTexture;
    T_FLOAT albedoEmission;

    T_IVEC2 lightUV;
    T_UINT packedData;
    T_UINT pad0;
};

// MaterialVertex as the material buffers hold it, in 40 bytes instead of 80. At far render distances those buffers
// were most of the video memory in use (5.4 GB of 9 at 32 chunks). loadTriangleMaterial in the shaders unpacks it into
// a MaterialVertex, so nothing after that point sees the difference.
struct PackedMaterialVertex {
    T_UINT normal;          // octahedral, 2 x snorm16; 0x80008000 means no normal
    T_UINT textures;        // textureID in the low 16 bits, glintTexture in the high 16
    T_UINT color;           // colorLayer as 4 x unorm8
    T_FLOAT u;              // textureUV kept as full floats: weather and some entity UVs run far past 1
    T_FLOAT v;
    T_UINT overlay;         // overlayUV as 2 x int16
    T_UINT glintUV;         // 2 x half float
    T_FLOAT albedoEmission; // unchanged
    T_UINT light;           // lightUV as 2 x uint16
    T_UINT packedData;      // unchanged
};

#ifdef __cplusplus
} // namespace VertexFormat

static_assert(sizeof(VertexFormat::PackedMaterialVertex) == 40);
static_assert(sizeof(VertexFormat::MaterialVertex) == 80);
static_assert(offsetof(VertexFormat::MaterialVertex, lightUV) == 64);
static_assert(offsetof(VertexFormat::MaterialVertex, packedData) == 72);

namespace Data {
#endif

// ---------------------------------------------------------------------------------------------------------------------
// Uniform and storage blocks.
// ---------------------------------------------------------------------------------------------------------------------

// Per-frame state of the world pass. Java writes everything before packParams (736 bytes, BufferProxy.java's
// WORLD_UBO_SIZE) each frame; the native side fills the fields marked as set natively and packParams before upload.
// Offsets in the comments are bytes, identical in C++ and std140.
struct WorldUBO {
    // ---- Camera (0..399)
    T_MAT4 cameraViewMat;
    // The view with Java's camera shake applied; primary rays start from this one.
    T_MAT4 cameraEffectedViewMat;
    T_MAT4 cameraProjMat;
    T_MAT4 cameraViewMatInv;
    T_MAT4 cameraEffectedViewMatInv;
    T_MAT4 cameraProjMatInv;
    T_VEC2 cameraJitter;
    T_FLOAT gameTime;
    T_UINT seed;

    // ---- Textures and fog (400..511)
    T_MAT4 textureMat;

    T_UINT overlayTextureID;
    // Bit 0: first person. Bit 1: the first person hands are drawn this frame (the hand rays are traced only then).
    T_UINT isFirstPerson;
    T_FLOAT fogStart;
    T_FLOAT fogEnd;

    T_VEC4 fogColor;

    T_UINT fogType;
    T_UINT skyType;
    // Non-zero while some entity glows, so the outline rays only run when there is something to find.
    T_UINT hasGlowOutline;
    // Non-zero when block lights are sampled directly: the option is on and Block Emission fills the light list.
    T_UINT blockLightSampling;

    // ---- World position (512..575)
    T_DVEC4 cameraPos;              // w unused
    T_IVEC4 chunkGridInfo;          // xyz: grid size in sections, w: lowest section y
    T_IVEC4 chunkStorageSectionPos; // xyz: BuiltChunkStorage's section origin

    // ---- Textures and lighting (576..655)
    T_UINT endSkyTextureID;
    T_UINT endPortalTextureID;
    T_UINT lightMapTextureID;
    // tan(hudFov / 2) / tan(fov / 2). Minecraft draws the first person hand with a fixed 70 degree field of view
    // (Camera.calculateHudFov) whatever the setting, so the hand keeps its size while the world widens or narrows;
    // primary rays that look for the hand are scaled by this to match.
    T_FLOAT handFovScale;

    // Light from what the main hand holds (torch, lantern, glowstone...). Pos: xyz relative to the camera, w its reach
    // in blocks. Color: rgb its radiance, w non-zero while there is one.
    T_VEC4 heldLightPos;
    T_VEC4 heldLightColor;

    // Non-zero: where a parallax-carved face meets an outer edge of its block the carved corner is see-through; zero
    // shows the border's colour instead.
    T_UINT parallaxTransparentEdges;
    // Player brightness sliders, 1 meaning the shader pack's own values: sun and day sky, moon and night sky, and
    // light-giving blocks (their glow and the light they cast).
    T_FLOAT sunBrightness;
    T_FLOAT moonBrightness;
    T_FLOAT emissionBrightness;
    // Non-zero lights each texel as a flat tile of its own, the blocky look of pixelated lighting; zero is smooth.
    T_UINT pixelLighting;
    // Ray reach for terrain in blocks when it has to go past the usual distance (Distant Horizons terrain); else 0.
    T_FLOAT traceDistance;
    // Blocks the rain fell since the last frame. The drops scroll down a texture on sheets that never move, so their
    // motion vectors come from this; without it the upscalers smear the drops into streaks.
    T_FLOAT rainFallPerFrame;
    // Brightness of what the player holds, its glow and the light it casts, kept apart from emissionBrightness so the
    // two can be set separately.
    T_FLOAT heldLightBrightness;

    // ---- Mostly set natively (656..703)
    // Frames rendered so far; picks which half of a ping-pong history (the ReSTIR reservoirs) is read this frame.
    T_UINT frameCounter;
    // Lights inside emissive entities (dropped items, item frames...), sampled with the block lights: their count and
    // the device address of their buffer, split in two 32-bit halves.
    T_UINT entityLightCount;
    T_UINT entityLightAddressLo;
    T_UINT entityLightAddressHi;
    T_UINT entityLightPad;
    // Distant Horizons coverage (Chunks::updateLodCoverage) as a device address, 0 without far terrain: far terrain is
    // hidden per ray wherever the near terrain is built.
    T_UINT lodCoverageAddressLo;
    T_UINT lodCoverageAddressHi;
    T_UINT lodCoveragePad0;
    T_UINT lodCoveragePad1;
    // Foliage wind (util/wind.glsl), written by Java: the wind time in seconds of game time, the player's strength (0
    // with wind off) and the weather's (1 in clear weather, more in rain and thunder). The native pass that moves
    // foliage reads them too.
    T_FLOAT windTime;
    T_FLOAT windStrength;
    // std140 starts the next vec4 at 704; C++ would put it at 700 without this one.
    T_FLOAT windWeather;

    // ---- Off hand (704..735)
    // The off hand's light, laid out like heldLightPos / heldLightColor, so a torch in each hand lights both sides.
    T_VEC4 offHandLightPos;
    T_VEC4 offHandLightColor;

    // ---- Shader pack settings (736..1759), never written by Java
    // The pack's settings marked "runtime" in its configs.json, one slot each in the order the pack lists them: a
    // number in x (an int, a bool as 0 or 1, a choice as its index), a vec3 in xyz. Buffers::setPackParams copies
    // them in every frame, so changing one needs no shader rebuild.
    T_VEC4 packParams[PRISM_PACK_PARAM_COUNT];
};

struct SkyUBO {
    T_VEC3 baseColor;
    T_UINT skyType;

    T_VEC4 horizonColor;

    T_VEC3 sunDirection;
    T_UINT isSunRisingOrSetting;

    T_UINT isSkyDark;
    T_UINT hasBlindnessOrDarkness;
    T_UINT cameraSubmersionType;
    T_UINT moonPhase;
    T_FLOAT rainGradient;

    T_UINT sunTextureID;
    T_UINT moonTextureID;
    // Night vision strength 0..1, fading out over its last ten seconds like vanilla's. Vanilla applies it through the
    // lightmap, which a path tracer never reads, so the shaders get it here.
    T_FLOAT nightVision;

    // Each body's rectangle (u0, v0, u1, v1) in the celestials atlas. Minecraft 26.3 stitches the sun and all eight
    // moon phases into one atlas built at runtime, so there is no whole texture or fixed grid to sample; Java picks
    // the phase and sends its rectangle.
    T_VEC4 sunUvRect;
    T_VEC4 moonUvRect;

    // Biome haze blended around the camera on the Java side. rgb: the scattering share of the extinction (the haze's
    // tint), a: extinction per block. All zero with the option off, outside the overworld sky, or when the camera is
    // out of sky light.
    T_VEC4 biomeFog;

    // xyz: the axis the sun and moon turn around, which vanilla keeps along one edge of their square sprites.
    // w: 1 to orient the sprites to it (the "Vanilla Sun/Moon Orientation" option), 0 for a free basis.
    T_VEC4 celestialAxis;

    // rgb: each channel's extinction relative to biomeFog.a. A Bedrock RTX fog that absorbs more blue than red turns
    // distant things orange; Radiante's own grey haze uses (1, 1, 1).
    T_VEC4 biomeFogChroma;
    // x: world height below which the haze is at full density, y: height where it has thinned to nothing. Radiante's
    // own haze never thins, so both sit far above the world.
    T_VEC4 biomeFogHeights;

    // Water as a medium for the biomes at the camera, from a converted Bedrock pack when it has one, otherwise derived
    // from the biome's vanilla water colour. Extinction: rgb per block, w 1 plus the camera's depth under water.
    // Albedo: rgb the share of the extinction that scatters, a how much sky reaches the surface of the water the camera
    // is in (0..1).
    T_VEC4 waterExtinction;
    T_VEC4 waterAlbedo;

    // x: how far Blindness or Darkness has closed in, 0..1, faded as vanilla does (in at the start, back out at the
    //    end rather than vanishing).
    // y: Darkness's pulse, 0..1, which vanilla subtracts from the lightmap to make the world blink dark.
    // z: the Volumetric Fog Strength option, 1 at 100 %.
    // w: star clarity from the camera's height and biome air, a factor on their brightness. 0 means unset and reads as
    //    1; negative means another mod draws the stars and none are drawn here.
    T_VEC4 fogControls;

    // Rain on the ground (RainExposure). x: wetness 0..1, rising with rain and drying slowly after. y: id of the
    // 512 x 512 texture of rain-blocking column heights around the camera, 0 while nothing is wet.
    T_VEC4 rainWetness;

    // The view from under water for the biomes at the camera (BiomeAmbiance). rgb: the colour it fades into, lit by
    // the sky in the shaders. a: the distance in blocks over which it fades to about a third; 0 with the Biome
    // Ambiance option off, in which case the shaders use one water for every biome.
    T_VEC4 waterFog;

    // The moon's own path (the Moon Path settings): xyz the direction towards it, which keeps the hour opposite the
    // sun's but may lean and turn apart from its path; exactly -sunDirection while it follows the sun. w unused.
    T_VEC4 moonDirection;
    // xyz: the axis the moon's path turns around, the moon's counterpart of celestialAxis. w unused.
    T_VEC4 moonAxis;
    // The Sun/Moon Rotation settings, each sprite turned around its own centre: xy (cos, sin) of the sun's angle,
    // zw the moon's; (1, 0) is vanilla's.
    T_VEC4 spriteRotation;
};

// Per-texture lookup from an atlas texture to its PBR companions. specular, normal and rawMer are texture ids,
// negative when there is none.
struct TextureMapEntry {
    T_INT specular;
    T_INT normal;
    T_INT flag;
    T_INT rawMer; // the original MER atlas when kept; its alpha marks which texels it covers
};

struct TextureMapping {
    TextureMapEntry entries[4096];
};

#ifdef __cplusplus
} // namespace Data
} // namespace gpu
#endif

#endif
