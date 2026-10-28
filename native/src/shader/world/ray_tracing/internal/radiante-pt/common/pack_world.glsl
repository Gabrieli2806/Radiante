#ifndef RADIANTE_PACK_WORLD_GLSL
#define RADIANTE_PACK_WORLD_GLSL

// The weather and the biome at the camera, for shader packs. Part of the pack interface (radiante-pt@1): functions
// are only added. radiante-pt itself never includes this file, so it renders the same without it.
//
// Include it from a hook file: #include "common/pack_world.glsl". It reads skyUBO, which every hook point can see
// (the miss shader's sky, stars and clouds, the closest hit's surface, direct light and cloud shadow, the fog and the
// water); post_render/post_hook_grade.frag declares it too, so hooks/post_grade.glsl can use it as well.
//
// The biome values are those at the camera, not at the point shaded, and they are eased over about two and a half
// seconds, so an effect tied to them fades across a border instead of switching.

// ---- Weather

// Rain, 0..1 (Iris's rainStrength).
float radianteRain() { return skyUBO.rainGradient; }
// Thunder, 0..1 (Iris's thunderStrength): the game fades it in and out, and it is never above the rain.
float radianteThunder() { return skyUBO.thunderGradient; }

// ---- Climate of the biome at the camera, eased

// The biome's base temperature (Iris's temperature): about -0.7 (frozen peaks) to 2 (desert, badlands, Nether).
float radianteBiomeTemperature() { return skyUBO.biomeClimate.x; }
// The biome's downfall (Iris's rainfall), 0..1.
float radianteBiomeDownfall() { return skyUBO.biomeClimate.y; }
// How snowy, 0..1: 1 where snow falls at the camera's height (rain turns to snow there), 0 elsewhere.
float radianteBiomeSnowy() { return skyUBO.biomeClimate.z; }
// How dry, 0..1: 1 where nothing falls (desert, savanna, badlands, the Nether and the End).
float radianteBiomeDry() { return skyUBO.biomeClimate.w; }
// How rainy, 0..1: where rain falls rather than snow or nothing. snowy + dry + rainy = 1.
float radianteBiomeRainy() { return clamp(1.0 - skyUBO.biomeClimate.z - skyUBO.biomeClimate.w, 0.0, 1.0); }
// Iris's biome_precipitation, eased: 0 nothing falls, 1 rain, 2 snow. Mellow's
// smooth(biome_precipitation, ...) is this.
float radianteBiomePrecipitation() { return 1.0 + skyUBO.biomeClimate.z - skyUBO.biomeClimate.w; }

// ---- Which biome

// The biome at the camera as the FNV-1a hash of its id ("minecraft:snowy_plains"), 0 without a world. Compare it
// with the RADIANTE_BIOME_* constants below, or with radianteBiomeHash for any other biome.
uint radianteBiomeId() { return skyUBO.biomeId; }

// How much the camera is in the biome with this hash, 0..1, fading over a couple of seconds as the biome changes.
float radianteBiomeIs(uint biome) {
    if (biome == skyUBO.biomeId) { return skyUBO.biomeIdBlend; }
    if (biome == skyUBO.previousBiomeId) { return 1.0 - skyUBO.biomeIdBlend; }
    return 0.0;
}

// Adds one byte to an FNV-1a hash; with it a pack can work out the hash of a modded biome's id in GLSL, one
// character code at a time, starting from RADIANTE_BIOME_HASH_START (or once by hand: offset basis 0x811C9DC5, prime
// 0x01000193, over the id's UTF-8 bytes; Python: h = 0x811C9DC5; for b in id.encode(): h = ((h ^ b) * 0x01000193)
// & 0xFFFFFFFF). Constant arguments fold to a constant.
const uint RADIANTE_BIOME_HASH_START = 0x811C9DC5u;
uint radianteBiomeHash(uint hash, uint byteValue) { return (hash ^ byteValue) * 0x01000193u; }

// The vanilla biomes (Minecraft 26.3).
const uint RADIANTE_BIOME_THE_VOID = 0x59218D88u; // minecraft:the_void
const uint RADIANTE_BIOME_PLAINS = 0xA514DE73u; // minecraft:plains
const uint RADIANTE_BIOME_SUNFLOWER_PLAINS = 0xA42FB605u; // minecraft:sunflower_plains
const uint RADIANTE_BIOME_SNOWY_PLAINS = 0xF187B5B4u; // minecraft:snowy_plains
const uint RADIANTE_BIOME_ICE_SPIKES = 0x25D936F9u; // minecraft:ice_spikes
const uint RADIANTE_BIOME_DESERT = 0xE6295CC7u; // minecraft:desert
const uint RADIANTE_BIOME_SWAMP = 0xDFE5F0A6u; // minecraft:swamp
const uint RADIANTE_BIOME_MANGROVE_SWAMP = 0x30F2ADCEu; // minecraft:mangrove_swamp
const uint RADIANTE_BIOME_FOREST = 0x1C7EDD2Fu; // minecraft:forest
const uint RADIANTE_BIOME_FLOWER_FOREST = 0x0BCB2687u; // minecraft:flower_forest
const uint RADIANTE_BIOME_BIRCH_FOREST = 0xDC4A05A4u; // minecraft:birch_forest
const uint RADIANTE_BIOME_DAPPLED_FOREST = 0x9E094482u; // minecraft:dappled_forest
const uint RADIANTE_BIOME_DARK_FOREST = 0xB81C099Au; // minecraft:dark_forest
const uint RADIANTE_BIOME_PALE_GARDEN = 0xF0F2D9F6u; // minecraft:pale_garden
const uint RADIANTE_BIOME_OLD_GROWTH_BIRCH_FOREST = 0xE43463C2u; // minecraft:old_growth_birch_forest
const uint RADIANTE_BIOME_OLD_GROWTH_PINE_TAIGA = 0xB35266C7u; // minecraft:old_growth_pine_taiga
const uint RADIANTE_BIOME_OLD_GROWTH_SPRUCE_TAIGA = 0xAB4ED3CBu; // minecraft:old_growth_spruce_taiga
const uint RADIANTE_BIOME_TAIGA = 0x07730B98u; // minecraft:taiga
const uint RADIANTE_BIOME_SNOWY_TAIGA = 0x7508EB85u; // minecraft:snowy_taiga
const uint RADIANTE_BIOME_SAVANNA = 0x7F90E692u; // minecraft:savanna
const uint RADIANTE_BIOME_SAVANNA_PLATEAU = 0x2822396Fu; // minecraft:savanna_plateau
const uint RADIANTE_BIOME_WINDSWEPT_HILLS = 0x7FB06846u; // minecraft:windswept_hills
const uint RADIANTE_BIOME_WINDSWEPT_GRAVELLY_HILLS = 0x8AB5D72Du; // minecraft:windswept_gravelly_hills
const uint RADIANTE_BIOME_WINDSWEPT_FOREST = 0xE3776017u; // minecraft:windswept_forest
const uint RADIANTE_BIOME_WINDSWEPT_SAVANNA = 0x3E036A3Au; // minecraft:windswept_savanna
const uint RADIANTE_BIOME_JUNGLE = 0xD41D9CC3u; // minecraft:jungle
const uint RADIANTE_BIOME_SPARSE_JUNGLE = 0x508EFEF2u; // minecraft:sparse_jungle
const uint RADIANTE_BIOME_BAMBOO_JUNGLE = 0x1A9017AAu; // minecraft:bamboo_jungle
const uint RADIANTE_BIOME_BADLANDS = 0x3A8B17A1u; // minecraft:badlands
const uint RADIANTE_BIOME_ERODED_BADLANDS = 0x6976B093u; // minecraft:eroded_badlands
const uint RADIANTE_BIOME_WOODED_BADLANDS = 0x3EA0A8ACu; // minecraft:wooded_badlands
const uint RADIANTE_BIOME_MEADOW = 0x53F5946Fu; // minecraft:meadow
const uint RADIANTE_BIOME_CHERRY_GROVE = 0x527C78F7u; // minecraft:cherry_grove
const uint RADIANTE_BIOME_GROVE = 0x4AC1B419u; // minecraft:grove
const uint RADIANTE_BIOME_SNOWY_SLOPES = 0x9431C369u; // minecraft:snowy_slopes
const uint RADIANTE_BIOME_FROZEN_PEAKS = 0xF446B2AFu; // minecraft:frozen_peaks
const uint RADIANTE_BIOME_JAGGED_PEAKS = 0x6D174EAFu; // minecraft:jagged_peaks
const uint RADIANTE_BIOME_STONY_PEAKS = 0x797A4550u; // minecraft:stony_peaks
const uint RADIANTE_BIOME_RIVER = 0x4D4AFB98u; // minecraft:river
const uint RADIANTE_BIOME_FROZEN_RIVER = 0x5BD57735u; // minecraft:frozen_river
const uint RADIANTE_BIOME_BEACH = 0xBD42C9FDu; // minecraft:beach
const uint RADIANTE_BIOME_SNOWY_BEACH = 0x4EFC4C50u; // minecraft:snowy_beach
const uint RADIANTE_BIOME_STONY_SHORE = 0xCD0B4AE5u; // minecraft:stony_shore
const uint RADIANTE_BIOME_WARM_OCEAN = 0x27A6F3B2u; // minecraft:warm_ocean
const uint RADIANTE_BIOME_LUKEWARM_OCEAN = 0x01D4364Du; // minecraft:lukewarm_ocean
const uint RADIANTE_BIOME_DEEP_LUKEWARM_OCEAN = 0x4AC7FC0Au; // minecraft:deep_lukewarm_ocean
const uint RADIANTE_BIOME_OCEAN = 0xEADC8BF4u; // minecraft:ocean
const uint RADIANTE_BIOME_DEEP_OCEAN = 0x44DA7D05u; // minecraft:deep_ocean
const uint RADIANTE_BIOME_COLD_OCEAN = 0xF6BF8519u; // minecraft:cold_ocean
const uint RADIANTE_BIOME_DEEP_COLD_OCEAN = 0xB54D601Au; // minecraft:deep_cold_ocean
const uint RADIANTE_BIOME_FROZEN_OCEAN = 0xA2013C2Du; // minecraft:frozen_ocean
const uint RADIANTE_BIOME_DEEP_FROZEN_OCEAN = 0x9AA45456u; // minecraft:deep_frozen_ocean
const uint RADIANTE_BIOME_MUSHROOM_FIELDS = 0x57B14A3Cu; // minecraft:mushroom_fields
const uint RADIANTE_BIOME_DRIPSTONE_CAVES = 0xE6497FF3u; // minecraft:dripstone_caves
const uint RADIANTE_BIOME_LUSH_CAVES = 0xE0FC675Bu; // minecraft:lush_caves
const uint RADIANTE_BIOME_DEEP_DARK = 0x0F7BDAA1u; // minecraft:deep_dark
const uint RADIANTE_BIOME_SULFUR_CAVES = 0x8BF2A672u; // minecraft:sulfur_caves
const uint RADIANTE_BIOME_NETHER_WASTES = 0x730110EAu; // minecraft:nether_wastes
const uint RADIANTE_BIOME_WARPED_FOREST = 0xB5517217u; // minecraft:warped_forest
const uint RADIANTE_BIOME_CRIMSON_FOREST = 0xF58DCDD5u; // minecraft:crimson_forest
const uint RADIANTE_BIOME_SOUL_SAND_VALLEY = 0xE1F8CB8Eu; // minecraft:soul_sand_valley
const uint RADIANTE_BIOME_BASALT_DELTAS = 0xC853055Fu; // minecraft:basalt_deltas
const uint RADIANTE_BIOME_THE_END = 0x2FE29CB3u; // minecraft:the_end
const uint RADIANTE_BIOME_END_HIGHLANDS = 0xD9B91CAAu; // minecraft:end_highlands
const uint RADIANTE_BIOME_END_MIDLANDS = 0xD282C7ECu; // minecraft:end_midlands
const uint RADIANTE_BIOME_SMALL_END_ISLANDS = 0xEE79DDD6u; // minecraft:small_end_islands
const uint RADIANTE_BIOME_END_BARRENS = 0x7BBEDB47u; // minecraft:end_barrens

#endif
