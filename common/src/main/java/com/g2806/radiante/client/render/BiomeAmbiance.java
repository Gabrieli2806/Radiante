package com.g2806.radiante.client.render;

import com.g2806.radiante.client.option.Options;
import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import java.io.Reader;
import java.util.HashMap;
import java.util.IdentityHashMap;
import java.util.Map;
import java.util.Optional;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.ClientLevel;
import net.minecraft.client.renderer.texture.TextureAtlas;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Holder;
import net.minecraft.core.registries.Registries;
import net.minecraft.data.AtlasIds;
import net.minecraft.resources.Identifier;
import net.minecraft.resources.ResourceKey;
import net.minecraft.server.packs.resources.Resource;
import net.minecraft.tags.BiomeTags;
import net.minecraft.tags.TagKey;
import net.minecraft.util.Mth;
import net.minecraft.world.attribute.EnvironmentAttributes;
import net.minecraft.world.level.LightLayer;
import net.minecraft.world.level.biome.Biome;
import net.minecraft.world.level.biome.Biomes;
import net.minecraft.world.phys.Vec3;
import org.joml.Vector3f;
import org.joml.Vector3fc;
import org.joml.Vector4f;

/**
 * The air and the water of each biome: a faint haze with the biome's own tone (warm dust over a desert, a green murk
 * over a swamp, a cold bright mist over snow, red, teal and ash in the Nether, violet in the End), and water that is
 * clear and bright by a beach and dark and close in the deep ocean. Kept subtle on purpose: it sets a mood at a
 * distance and never closes the view in.
 *
 * <p>The biomes around the camera are averaged and the result eased in over a couple of seconds, so walking, flying
 * or sailing across a border fades from one to the other instead of switching. The haze also thins out with the sky
 * light at the camera: it is weather, and it has no business filling a cave.
 *
 * <p>Three sources, in this order:
 * <ol>
 * <li>A Bedrock RTX pack converted by {@link com.g2806.radiante.client.bedrock.BedrockPackConverter} brings its own
 * fog and water per biome. Their hue and how they compare from biome to biome are kept; how strong they are is
 * brought into the same subtle range as the rest (see {@link #styled}). Without one, Radiante's
 * built-in Bedrock RTX ones are taken ({@link BedrockDefaults}), the same way.
 * <li>Radiante's own values for the vanilla biomes, written with those packs as the reference.
 * <li>Any other biome - a mod's or a data pack's - is worked out from what it says about itself: its tags (the
 * loaders' shared {@code c:} tags too), its climate, and its own fog and water colours. Nothing has to be set up per
 * mod.
 * </ol>
 */
public final class BiomeAmbiance {

    /**
     * A biome's air. In the overworld rgb is the tint of the light the haze scatters and the haze is lit by the sky;
     * in the other dimensions, which have no sky to light it, rgb is the haze's colour itself (ownColour) or a factor
     * on the biome's vanilla fog colour. density is its extinction per block, chroma how each channel's extinction
     * departs from it, the heights where it is full and where it has thinned out to nothing, and rainScale how much
     * rain thickens it.
     */
    private record Haze(float r, float g, float b, float density, float chromaR, float chromaG, float chromaB,
        float fullBelow, float zeroAt, float rainScale, boolean ownColour) {

        Haze withDensity(float newDensity) {
            return new Haze(r, g, b, newDensity, chromaR, chromaG, chromaB, fullBelow, zeroAt, rainScale, ownColour);
        }

        Haze withTint(float newR, float newG, float newB) {
            return new Haze(newR, newG, newB, density, chromaR, chromaG, chromaB, fullBelow, zeroAt, rainScale,
                ownColour);
        }
    }

    /**
     * A biome's water as a medium - extinction per block per channel and the share of it that scatters - and what is
     * seen from inside it: how far (the distance the view fades to about a third over) and the colour it fades into.
     */
    private record Water(float extinctionR, float extinctionG, float extinctionB, float albedoR, float albedoG,
        float albedoB, float visibility, float fogR, float fogG, float fogB) {
    }

    /**
     * How a biome's water departs from plain water: clarity divides its extinction, visibility is how far one sees in
     * it in blocks, brightness scales its own light (the colour the view fades into and what it scatters on the way).
     */
    private record WaterStyle(float clarity, float visibility, float brightness) {
    }

    // ---- the air of the overworld

    /**
     * The haze settles like Bedrock RTX's fog: full up to a little above sea level and gone by the height of the
     * mountain tops, so valleys and plains are hazy and the view from a peak is clear.
     */
    private static final float OVERWORLD_FULL_BELOW = 70.0f;
    private static final float OVERWORLD_ZERO_AT = 260.0f;
    /** Far above any world: haze that never thins out with height (the Nether, the End). */
    private static final float NO_THINNING = 100_000.0f;
    /** Rain thickens the haze by up to this factor: a mist between the trees, not a wall. */
    private static final float RAIN_SCALE = 1.8f;

    private static Haze air(float r, float g, float b, float density) {
        return air(r, g, b, density, 1.0f, 1.0f, 1.0f);
    }

    /** With extinction per channel relative to density: a dusty haze that takes more blue than red turns warm. */
    private static Haze air(float r, float g, float b, float density, float chromaR, float chromaG, float chromaB) {
        return new Haze(r, g, b, density, chromaR, chromaG, chromaB, OVERWORLD_FULL_BELOW, OVERWORLD_ZERO_AT,
            RAIN_SCALE, false);
    }

    /** A low mist: full to a little above the water, gone not far over the tree tops. */
    private static Haze mist(float r, float g, float b, float density, float zeroAt) {
        return new Haze(r, g, b, density, 1.0f, 1.0f, 1.0f, 64.0f, zeroAt, RAIN_SCALE, false);
    }

    /** Haze of a dimension without a sky, in its own colour. */
    private static Haze glow(float r, float g, float b, float density) {
        return new Haze(r, g, b, density, 1.0f, 1.0f, 1.0f, NO_THINNING, NO_THINNING + 1.0f, 1.0f, true);
    }

    /** Haze of a dimension without a sky, in the biome's vanilla fog colour times rgb. */
    private static Haze fogColoured(float r, float g, float b, float density) {
        return new Haze(r, g, b, density, 1.0f, 1.0f, 1.0f, NO_THINNING, NO_THINNING + 1.0f, 1.0f, false);
    }

    // Tint and extinction per block. A plain's air is nearly clear (about 8 % of the view gone at ten chunks); the
    // thickest, a pale garden's, takes about a third at that distance. Bedrock RTX packs go much further (Vanilla
    // RTX's taiga takes half in fifty blocks and darkens the day with it); the tones follow theirs, the strength does
    // not. Set by looking at each biome with the option on and off: at half as much again, a forest at noon looked
    // washed rather than far.
    private static final Haze NONE = new Haze(1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, OVERWORLD_FULL_BELOW,
        OVERWORLD_ZERO_AT, 1.0f, false);
    private static final Haze PLAINS = air(0.96f, 0.98f, 1.00f, 0.00055f);
    private static final Haze FOREST = air(0.90f, 0.96f, 0.93f, 0.0010f);
    private static final Haze DAPPLED_FOREST = air(1.00f, 0.90f, 0.78f, 0.0011f, 0.90f, 1.00f, 1.15f);
    private static final Haze DARK_FOREST = air(0.66f, 0.76f, 0.70f, 0.0016f);
    private static final Haze PALE_GARDEN = air(0.84f, 0.83f, 0.86f, 0.0026f);
    private static final Haze TAIGA = air(0.74f, 0.84f, 0.88f, 0.0014f);
    // Ice crystals scatter blue most: a bright, cold mist that leaves the distance a little warm.
    private static final Haze SNOWY = air(0.95f, 0.98f, 1.06f, 0.0013f, 0.75f, 0.80f, 1.45f);
    private static final Haze ICE_SPIKES = air(0.95f, 0.98f, 1.06f, 0.0016f, 0.75f, 0.80f, 1.45f);
    private static final Haze FROZEN_OCEAN = air(0.94f, 0.98f, 1.06f, 0.0010f, 0.80f, 0.85f, 1.35f);
    private static final Haze PEAKS = air(0.95f, 0.98f, 1.04f, 0.0004f);
    private static final Haze WINDSWEPT = air(0.94f, 0.97f, 1.02f, 0.0006f);
    // Dust takes blue about four times as fast as red, so the distance turns warm.
    private static final Haze DESERT = air(1.00f, 0.76f, 0.54f, 0.0014f, 0.40f, 1.00f, 1.60f);
    private static final Haze BADLANDS = air(1.00f, 0.68f, 0.48f, 0.0014f, 0.42f, 1.00f, 1.58f);
    private static final Haze SAVANNA = air(1.00f, 0.86f, 0.64f, 0.0011f, 0.55f, 1.00f, 1.45f);
    private static final Haze SWAMP = mist(0.60f, 0.70f, 0.56f, 0.0021f, 120.0f);
    private static final Haze MANGROVE = mist(0.58f, 0.76f, 0.64f, 0.0019f, 130.0f);
    private static final Haze JUNGLE = air(0.70f, 0.82f, 0.70f, 0.0015f);
    private static final Haze SPARSE_JUNGLE = air(0.80f, 0.88f, 0.80f, 0.0011f);
    private static final Haze CHERRY = air(1.00f, 0.88f, 0.95f, 0.0011f);
    private static final Haze MUSHROOM = air(0.84f, 0.76f, 0.94f, 0.0015f);
    private static final Haze BEACH = air(0.98f, 0.96f, 0.92f, 0.0008f, 0.80f, 1.00f, 1.20f);
    private static final Haze OCEAN = air(0.90f, 0.95f, 1.02f, 0.0007f);
    private static final Haze WARM_OCEAN = air(0.93f, 0.98f, 1.00f, 0.0009f);
    private static final Haze COLD_OCEAN = air(0.90f, 0.95f, 1.04f, 0.0008f);
    private static final Haze RIVER = air(0.92f, 0.96f, 0.98f, 0.0009f);
    // For biomes known only by a tag or their climate.
    private static final Haze WASTELAND = air(0.86f, 0.82f, 0.76f, 0.0015f, 0.85f, 1.00f, 1.20f);
    private static final Haze MAGICAL = air(0.88f, 0.84f, 1.00f, 0.0014f);
    private static final Haze LUSH = air(0.80f, 0.90f, 0.80f, 0.0015f);

    // The Nether's haze is added as light, and a cavern lit by lava alone is metered up to the brightest exposure (64
    // times): at vanilla's fog colours (0.2 red) it came out as a flat pink sky, so its colours are a thirtieth of them.
    // ---- the Nether and the End: the haze's own colour, with nothing to light it. Thin: away from the lava a path
    // traced cavern is dark, and a haze that glows by itself is soon all there is to see in it (at the thickness
    // vanilla's fog has, a crimson forest was red silhouettes in a red bath).

    private static final Haze NETHER_WASTES = glow(0.0053f, 0.0011f, 0.0009f, 0.0030f);
    private static final Haze CRIMSON_FOREST = glow(0.0057f, 0.0007f, 0.0009f, 0.0034f);
    private static final Haze WARPED_FOREST = glow(0.0007f, 0.0032f, 0.0035f, 0.0034f);
    private static final Haze SOUL_SAND_VALLEY = glow(0.0017f, 0.0050f, 0.0055f, 0.0045f);
    private static final Haze BASALT_DELTAS = glow(0.0067f, 0.0060f, 0.0072f, 0.0060f);
    private static final Haze NETHER_OTHER = fogColoured(0.033f, 0.033f, 0.033f, 0.0032f);
    private static final Haze END_CENTER = fogColoured(1.00f, 1.00f, 1.00f, 0.0012f);
    private static final Haze END_OUTER = fogColoured(1.00f, 1.00f, 1.00f, 0.0018f);
    /** Vanilla's End fog is close to black, which as haze would only darken; this is the least it is lifted to. */
    private static final float END_FLOOR_R = 0.10f;
    private static final float END_FLOOR_G = 0.07f;
    private static final float END_FLOOR_B = 0.15f;

    private static final Map<ResourceKey<Biome>, Haze> BY_BIOME = new HashMap<>();

    static {
        put(PLAINS, Biomes.PLAINS, Biomes.SUNFLOWER_PLAINS, Biomes.MEADOW);
        put(FOREST, Biomes.FOREST, Biomes.FLOWER_FOREST, Biomes.BIRCH_FOREST, Biomes.OLD_GROWTH_BIRCH_FOREST);
        put(DARK_FOREST, Biomes.DARK_FOREST);
        put(PALE_GARDEN, Biomes.PALE_GARDEN);
        put(TAIGA, Biomes.TAIGA, Biomes.OLD_GROWTH_PINE_TAIGA, Biomes.OLD_GROWTH_SPRUCE_TAIGA);
        put(SNOWY, Biomes.SNOWY_PLAINS, Biomes.SNOWY_TAIGA, Biomes.SNOWY_SLOPES, Biomes.GROVE, Biomes.SNOWY_BEACH,
            Biomes.FROZEN_RIVER);
        put(ICE_SPIKES, Biomes.ICE_SPIKES);
        put(FROZEN_OCEAN, Biomes.FROZEN_OCEAN, Biomes.DEEP_FROZEN_OCEAN);
        put(PEAKS, Biomes.JAGGED_PEAKS, Biomes.FROZEN_PEAKS, Biomes.STONY_PEAKS);
        put(WINDSWEPT, Biomes.WINDSWEPT_HILLS, Biomes.WINDSWEPT_GRAVELLY_HILLS, Biomes.WINDSWEPT_FOREST,
            Biomes.STONY_SHORE);
        put(DESERT, Biomes.DESERT);
        put(BADLANDS, Biomes.BADLANDS, Biomes.ERODED_BADLANDS, Biomes.WOODED_BADLANDS);
        put(SAVANNA, Biomes.SAVANNA, Biomes.SAVANNA_PLATEAU, Biomes.WINDSWEPT_SAVANNA);
        put(SWAMP, Biomes.SWAMP);
        put(MANGROVE, Biomes.MANGROVE_SWAMP);
        put(JUNGLE, Biomes.JUNGLE, Biomes.BAMBOO_JUNGLE);
        put(SPARSE_JUNGLE, Biomes.SPARSE_JUNGLE);
        put(CHERRY, Biomes.CHERRY_GROVE);
        put(MUSHROOM, Biomes.MUSHROOM_FIELDS);
        put(BEACH, Biomes.BEACH);
        put(OCEAN, Biomes.OCEAN, Biomes.DEEP_OCEAN);
        put(WARM_OCEAN, Biomes.WARM_OCEAN, Biomes.LUKEWARM_OCEAN, Biomes.DEEP_LUKEWARM_OCEAN);
        put(COLD_OCEAN, Biomes.COLD_OCEAN, Biomes.DEEP_COLD_OCEAN);
        put(RIVER, Biomes.RIVER);
        // Cave biomes: the sky light check already keeps the haze out, this keeps their colour out of the average.
        put(NONE, Biomes.LUSH_CAVES, Biomes.DRIPSTONE_CAVES, Biomes.DEEP_DARK, Biomes.SULFUR_CAVES, Biomes.THE_VOID);
        put(NETHER_WASTES, Biomes.NETHER_WASTES);
        put(CRIMSON_FOREST, Biomes.CRIMSON_FOREST);
        put(WARPED_FOREST, Biomes.WARPED_FOREST);
        put(SOUL_SAND_VALLEY, Biomes.SOUL_SAND_VALLEY);
        put(BASALT_DELTAS, Biomes.BASALT_DELTAS);
        put(END_CENTER, Biomes.THE_END);
        put(END_OUTER, Biomes.END_HIGHLANDS, Biomes.END_MIDLANDS, Biomes.SMALL_END_ISLANDS, Biomes.END_BARRENS);
    }

    @SafeVarargs
    private static void put(Haze haze, ResourceKey<Biome>... biomes) {
        for (ResourceKey<Biome> biome : biomes) {
            BY_BIOME.put(biome, haze);
        }
    }

    // ---- the water

    /** How far one sees in plain water, in blocks; what the other visibilities are measured against. */
    private static final float PLAIN_VISIBILITY = 22.0f;
    private static final WaterStyle PLAIN_WATER = new WaterStyle(1.0f, PLAIN_VISIBILITY, 1.0f);
    // By a beach and over a reef the water is clear and bright and the bottom shows far off; the deep ocean is
    // dark and closes in; a swamp's is murk.
    private static final WaterStyle SHALLOW_WATER = new WaterStyle(1.40f, 34.0f, 1.15f);
    private static final WaterStyle OCEAN_WATER = new WaterStyle(1.00f, 24.0f, 1.00f);
    private static final WaterStyle COLD_WATER = new WaterStyle(1.10f, 26.0f, 0.92f);
    private static final WaterStyle DEEP_WATER = new WaterStyle(0.80f, 17.0f, 0.72f);
    private static final WaterStyle DEEP_COLD_WATER = new WaterStyle(0.80f, 16.0f, 0.62f);
    private static final WaterStyle RIVER_WATER = new WaterStyle(1.10f, 22.0f, 1.00f);
    private static final WaterStyle SWAMP_WATER = new WaterStyle(0.65f, 9.0f, 0.90f);
    private static final WaterStyle MANGROVE_WATER = new WaterStyle(0.70f, 11.0f, 0.85f);
    private static final WaterStyle LUSH_WATER = new WaterStyle(1.20f, 26.0f, 1.05f);

    private static final Map<ResourceKey<Biome>, WaterStyle> WATER_BY_BIOME = new HashMap<>();

    static {
        putWater(SHALLOW_WATER, Biomes.WARM_OCEAN, Biomes.LUKEWARM_OCEAN, Biomes.BEACH);
        putWater(OCEAN_WATER, Biomes.OCEAN, Biomes.STONY_SHORE, Biomes.MUSHROOM_FIELDS);
        putWater(COLD_WATER, Biomes.COLD_OCEAN, Biomes.FROZEN_OCEAN, Biomes.SNOWY_BEACH, Biomes.FROZEN_RIVER);
        putWater(DEEP_WATER, Biomes.DEEP_OCEAN, Biomes.DEEP_LUKEWARM_OCEAN);
        putWater(DEEP_COLD_WATER, Biomes.DEEP_COLD_OCEAN, Biomes.DEEP_FROZEN_OCEAN);
        putWater(RIVER_WATER, Biomes.RIVER);
        putWater(SWAMP_WATER, Biomes.SWAMP);
        putWater(MANGROVE_WATER, Biomes.MANGROVE_SWAMP);
        putWater(LUSH_WATER, Biomes.LUSH_CAVES);
    }

    @SafeVarargs
    private static void putWater(WaterStyle style, ResourceKey<Biome>... biomes) {
        for (ResourceKey<Biome> biome : biomes) {
            WATER_BY_BIOME.put(biome, style);
        }
    }

    // ---- biomes Radiante has no entry for

    // The loaders' shared tags (Fabric and NeoForge both fill c:), which mods put their biomes in.
    private static final TagKey<Biome> C_SWAMP = conventional("is_swamp");
    private static final TagKey<Biome> C_DESERT = conventional("is_desert");
    private static final TagKey<Biome> C_SNOWY = conventional("is_snowy");
    private static final TagKey<Biome> C_ICY = conventional("is_icy");
    private static final TagKey<Biome> C_COLD = conventional("is_cold");
    private static final TagKey<Biome> C_HOT = conventional("is_hot");
    private static final TagKey<Biome> C_DRY = conventional("is_dry");
    private static final TagKey<Biome> C_WET = conventional("is_wet");
    private static final TagKey<Biome> C_MUSHROOM = conventional("is_mushroom");
    private static final TagKey<Biome> C_CAVE = conventional("is_cave");
    private static final TagKey<Biome> C_UNDERGROUND = conventional("is_underground");
    private static final TagKey<Biome> C_VOID = conventional("is_void");
    private static final TagKey<Biome> C_DENSE = conventional("is_dense_vegetation");
    private static final TagKey<Biome> C_SPOOKY = conventional("is_spooky");
    private static final TagKey<Biome> C_DEAD = conventional("is_dead");
    private static final TagKey<Biome> C_WASTELAND = conventional("is_wasteland");
    private static final TagKey<Biome> C_MAGICAL = conventional("is_magical");
    private static final TagKey<Biome> C_LUSH = conventional("is_lush");
    private static final TagKey<Biome> C_FLORAL = conventional("is_floral");
    private static final TagKey<Biome> C_PEAK = conventional("is_mountain/peak");
    private static final TagKey<Biome> C_WINDSWEPT = conventional("is_windswept");
    private static final TagKey<Biome> C_SHALLOW_OCEAN = conventional("is_shallow_ocean");

    private static TagKey<Biome> conventional(String path) {
        return TagKey.create(Registries.BIOME, Identifier.fromNamespaceAndPath("c", path));
    }

    /** The overworld's own fog and water fog colours, which a biome that sets neither is drawn with. */
    private static final Vector3fc OVERWORLD_FOG = new Vector3f(0xC0 / 255.0f, 0xD8 / 255.0f, 1.0f);
    private static final float VANILLA_WATER_FOG_END = 96.0f;

    /** How much of a derived biome's tint is its own fog colour's hue, softened by a power, and how dark it gets. */
    private static final float DERIVED_FOG_SHARE = 0.65f;
    private static final float DERIVED_FOG_POWER = 0.75f;
    private static final float DERIVED_MIN_BRIGHTNESS = 0.7f;

    private static final Map<Holder<Biome>, Haze> DERIVED = new IdentityHashMap<>();
    private static final Map<Holder<Biome>, WaterStyle> DERIVED_WATER = new IdentityHashMap<>();
    private static Object derivedFor;

    // ---- blending

    /**
     * The biomes are sampled at this many points around the camera, out to this many blocks, the nearer ones
     * weighing more: a border is felt from 20 blocks off and left behind 20 blocks past it. The points lie on a
     * spiral, not a grid, so no two of them cross a straight border together and no step is more than a
     * twentieth of the way.
     */
    private static final int SAMPLES = 24;
    private static final float SAMPLE_REACH = 20.0f;
    private static final float[] SAMPLE_X = new float[SAMPLES];
    private static final float[] SAMPLE_Z = new float[SAMPLES];
    private static final float[] SAMPLE_WEIGHT = new float[SAMPLES];

    static {
        float total = 0.0f;
        for (int i = 0; i < SAMPLES; i++) {
            // A sunflower's seeds: even cover of the disc, each point a golden angle on from the last.
            float radius = (float) Math.sqrt((i + 0.5f) / SAMPLES);
            double angle = i * 2.399963229728653;
            SAMPLE_X[i] = radius * SAMPLE_REACH * (float) Math.cos(angle);
            SAMPLE_Z[i] = radius * SAMPLE_REACH * (float) Math.sin(angle);
            SAMPLE_WEIGHT[i] = 1.0f - 0.6f * radius;
            total += SAMPLE_WEIGHT[i];
        }
        for (int i = 0; i < SAMPLES; i++) {
            SAMPLE_WEIGHT[i] /= total;
        }
    }

    /** The biomes at the sample points this frame, shared by the air and the water. */
    @SuppressWarnings("unchecked")
    private static final Holder<Biome>[] sampled = new Holder[SAMPLES];
    /** Seconds for the blend to cover most of the way to a new biome, and for the sky light to catch up. */
    private static final float BLEND_SECONDS = 2.5f;
    private static final int OVERWORLD_SKY = 1;
    private static final int END_SKY = 2;

    // ---- a converted Bedrock pack's own values

    /** Written into converted Bedrock packs; the topmost pack that has one decides the fog. */
    private static final Identifier PACK_FOG = Identifier.fromNamespaceAndPath("radiante", "bedrock_fog.json");
    /** A converted Bedrock pack's fog by biome id, empty without one. */
    private static final Map<String, Haze> PACK_BY_BIOME = new HashMap<>();
    private static final Map<String, float[]> PACK_WATER_BY_BIOME = new HashMap<>();
    private static final Map<String, Integer> PACK_WATER_COLOUR_BY_BIOME = new HashMap<>();
    private static Object packFogLoadedFor;

    // ---- what the shaders are handed

    private static final Vector4f waterExtinction = new Vector4f(0.0f);
    private static final Vector4f waterAlbedo = new Vector4f(0.0f);
    private static final Vector4f waterFog = new Vector4f(0.0f);
    private static boolean hasWater;
    /** The sky light over the water the camera is in, 0 to 1, eased; and what it is this frame. */
    private static float waterSky = 1.0f;
    private static float waterSkyNow = 1.0f;
    private static boolean wasInWater;

    private static final Vector4f current = new Vector4f(0.0f);
    private static final Vector4f currentChroma = new Vector4f(1.0f, 1.0f, 1.0f, 0.0f);
    private static final Vector4f currentHeights = new Vector4f(NO_THINNING, NO_THINNING + 1.0f, 0.0f, 0.0f);
    private static float currentExposure;
    private static boolean hasCurrent;
    private static int currentSkyType = -1;
    private static long lastNanos;
    private static long lastLogNanos;

    private BiomeAmbiance() {
    }

    /** How each channel's extinction compares with the density handed over by {@link #update}. */
    public static Vector4f chroma() {
        return new Vector4f(currentChroma);
    }

    /** Water extinction per block around the camera in xyz, w 1 once there is a world to take it from. */
    public static Vector4f waterExtinction() {
        return new Vector4f(waterExtinction);
    }

    /**
     * The share of the water's extinction that scatters in xyz; in w how much of the sky reaches the surface of the
     * water the camera is in, 0 to 1 (1 out of water).
     */
    public static Vector4f waterAlbedo() {
        return new Vector4f(waterAlbedo);
    }

    /**
     * Seen from under water: the colour the view fades into in xyz and the distance in blocks it fades over in w
     * (0 while the option is off, for the shaders' own).
     */
    public static Vector4f waterFog() {
        return new Vector4f(waterFog);
    }

    /** Where the stars start getting clearer with height, and where they are at their clearest. */
    private static final float STARS_LOW = 70.0f;
    private static final float STARS_HIGH = 190.0f;
    /** The haze the stars are seen through as tuned, and how far a clear or a thick air moves them. */
    private static final float STARS_HAZE_REFERENCE = 0.0010f;
    private static final float STARS_HAZE_SHARE = 0.3f;

    /**
     * How clear the stars are from where the camera is, as a factor on their brightness (1 as before): up to
     * 1 + Options.starAltitude from a mountain top, where less air lies over the eye, and a little more or less
     * by the biome's air - sharp over a desert, a peak or the open sea, dimmed in a swamp's or a jungle's haze.
     * The biome's part follows the eased haze, so it fades across a border with it.
     */
    public static float starClarity(float eyeY) {
        if (ModBackdrop.modStars()) {
            // A mod draws the night sky's stars itself; negative tells the star pass to draw none of its own.
            return -1.0f;
        }
        float t = Mth.clamp((eyeY - STARS_LOW) / (STARS_HIGH - STARS_LOW), 0.0f, 1.0f);
        float clarity = 1.0f + Options.starAltitude / 100.0f * t * t * (3.0f - 2.0f * t);
        if (hasCurrent && currentSkyType == OVERWORLD_SKY && Options.biomeAmbianceStrength > 0) {
            float density = current.w / (Options.biomeAmbianceStrength / 100.0f);
            clarity *= Mth.clamp(1.0f + STARS_HAZE_SHARE * (1.0f - density / STARS_HAZE_REFERENCE),
                1.0f - STARS_HAZE_SHARE, 1.0f + STARS_HAZE_SHARE);
        }
        return clarity;
    }

    /**
     * The haze is at full density below x and gone from y up, in world heights. z is how open the camera is to the sky
     * (its sky light, 0 to 1, eased), which the air's own haze (aerial perspective) follows whether or not Biome
     * Ambiance is on: it is lit by the sky, and has no business filling a cave.
     */
    public static Vector4f heights() {
        return new Vector4f(currentHeights.x, currentHeights.y, skyOpen, 0.0f);
    }

    /** The camera's sky light, 0 to 1, eased like the haze; 0 outside the overworld. */
    private static float skyOpen;
    private static boolean hasSkyOpen;

    private static void updateSkyOpen(ClientLevel level, Vec3 cameraPos, float blend, int skyType) {
        float target = 0.0f;
        if (level != null && skyType == OVERWORLD_SKY) {
            target = level.getBrightness(LightLayer.SKY, BlockPos.containing(cameraPos)) / 15.0f;
        }
        skyOpen = hasSkyOpen ? skyOpen + (target - skyOpen) * blend : target;
        hasSkyOpen = level != null;
    }

    /** Reads a converted Bedrock pack's fog, once per resource reload (each one rebuilds the block atlas). */
    public static void reloadPackFogIfNeeded(Minecraft minecraft) {
        TextureAtlas atlas = minecraft.getAtlasManager().getAtlasOrThrow(AtlasIds.BLOCKS);
        Object key = TextureTracker.gpuTextureOrNull(atlas);
        if (key == null || key == packFogLoadedFor) {
            return;
        }
        packFogLoadedFor = key;
        PACK_BY_BIOME.clear();
        PACK_WATER_BY_BIOME.clear();
        PACK_WATER_COLOUR_BY_BIOME.clear();
        hasCurrent = false;
        hasWater = false;
        Optional<Resource> resource = minecraft.getResourceManager().getResource(PACK_FOG);
        if (resource.isEmpty()) {
            // No Bedrock pack brings a fog: the built-in one is taken, the same file a converted pack would bring
            // (BedrockDefaults), read the same way.
            JsonObject builtIn = BedrockDefaults.fogBiomes();
            if (builtIn != null) {
                readPackFog(builtIn);
                RadianteRenderer.LOGGER.info("Using the built-in fog and water for {} biomes (no Bedrock pack)",
                    PACK_BY_BIOME.size());
            }
            return;
        }
        try (Reader reader = resource.get().openAsReader()) {
            readPackFog(JsonParser.parseReader(reader).getAsJsonObject().getAsJsonObject("biomes"));
            RadianteRenderer.LOGGER.info("Using the Bedrock fog of {} for {} biomes", resource.get().sourcePackId(),
                PACK_BY_BIOME.size());
        } catch (Exception e) {
            PACK_BY_BIOME.clear();
            PACK_WATER_BY_BIOME.clear();
            PACK_WATER_COLOUR_BY_BIOME.clear();
            RadianteRenderer.LOGGER.warn("Could not read the Bedrock fog of {}", resource.get().sourcePackId(), e);
        }
    }

    /** Fills the pack maps from a bedrock_fog.json's biomes (BedrockFog's format). */
    private static void readPackFog(JsonObject biomes) {
        for (Map.Entry<String, JsonElement> entry : biomes.entrySet()) {
            JsonObject biome = entry.getValue().getAsJsonObject();
            if (biome.has("albedo")) {
                Identifier id = Identifier.tryParse(entry.getKey());
                Haze own = id == null ? null : BY_BIOME.get(ResourceKey.create(Registries.BIOME, id));
                PACK_BY_BIOME.put(entry.getKey(), packHaze(biome, own));
            }
            if (biome.has("water_color")) {
                PACK_WATER_COLOUR_BY_BIOME.put(entry.getKey(), biome.get("water_color").getAsInt());
            }
            if (biome.has("water")) {
                JsonObject water = biome.getAsJsonObject("water");
                float[] extinction = vec3(water.getAsJsonArray("extinction"));
                float[] albedo = vec3(water.getAsJsonArray("albedo"));
                PACK_WATER_BY_BIOME.put(entry.getKey(), new float[] {extinction[0], extinction[1], extinction[2],
                    albedo[0], albedo[1], albedo[2]});
            }
        }
    }

    private static Haze packHaze(JsonObject fog, Haze own) {
        float[] albedo = vec3(fog.getAsJsonArray("albedo"));
        float[] extinction = vec3(fog.getAsJsonArray("extinction"));
        boolean thins = fog.has("full_below") && fog.has("zero_at");
        float fullBelow = thins ? fog.get("full_below").getAsFloat() : OVERWORLD_FULL_BELOW;
        float zeroAt = thins ? fog.get("zero_at").getAsFloat() : OVERWORLD_ZERO_AT;
        float rainScale = fog.has("rain_scale") ? fog.get("rain_scale").getAsFloat() : RAIN_SCALE;
        return styled(albedo, extinction, fullBelow, zeroAt, rainScale, own);
    }

    /** The extinction a pack's densest fogs come down to, and the extinction at which a fog is two thirds there. */
    private static final float STYLED_MAX_DENSITY = 0.0026f;
    private static final float STYLED_KNEE = 0.011f;
    /** How much of a pack fog's own hue and of its per-channel extinction is kept. */
    private static final float STYLED_SATURATION = 0.55f;
    private static final float STYLED_CHROMA = 0.7f;
    /** The darkest a fog's tint gets: a pack's absorbing murk (an albedo of 0.05) dims the haze, not the day. */
    private static final float STYLED_MIN_BRIGHTNESS = 0.62f;
    /** The least height a pack's fog thins out over; some stop within two blocks, a flat lid over the swamp. */
    private static final float STYLED_MIN_FADE = 56.0f;

    /**
     * A Bedrock pack's fog brought into Radiante's subtle range. Its hue, which channels it takes most and how it
     * compares with the pack's other biomes are kept; the extinction is compressed (a plain's 0.0012 a block comes to
     * about a quarter of that, a taiga's 0.014 to 0.0019), and the tint is lifted, so a fog that only absorbs
     * reads as a dim haze instead of turning noon into dusk.
     *
     * <p>A fog that scatters (a desert's, a snow field's, a plain's) has a colour of its own, and keeps it. One that
     * mostly absorbs has next to none - what is left of a taiga's albedo of 0.06, 0.05, 0.07 came out violet when
     * lifted - so the darker the albedo, the more of the tint is Radiante's own for that biome ({@code own}, null
     * for a biome it has none for).
     */
    private static Haze styled(float[] albedo, float[] extinction, float fullBelow, float zeroAt, float rainScale,
        Haze own) {
        float mean = (extinction[0] + extinction[1] + extinction[2]) / 3.0f;
        if (mean <= 0.0f) {
            return NONE;
        }
        float density = STYLED_MAX_DENSITY * (1.0f - (float) Math.exp(-mean / STYLED_KNEE));
        float peak = Math.max(albedo[0], Math.max(albedo[1], albedo[2]));
        float luma = 0.2126f * albedo[0] + 0.7152f * albedo[1] + 0.0722f * albedo[2];
        float brightness = Math.min(1.0f,
            STYLED_MIN_BRIGHTNESS + (1.0f - STYLED_MIN_BRIGHTNESS) * (float) Math.sqrt(Math.max(luma, 0.0f)));
        // How much the albedo has to say about the colour: all of it from a luma of about a half up.
        float packShare = Mth.clamp((float) Math.sqrt(Math.max(luma, 0.0f)) * 1.4f, 0.0f, 1.0f);
        float[] ownTint = own == null || own.density() <= 0.0f ? null : new float[] {own.r(), own.g(), own.b()};
        float[] tint = new float[3];
        float[] chroma = new float[3];
        for (int i = 0; i < 3; i++) {
            float hue = peak > 0.0f ? albedo[i] / peak : 1.0f;
            if (ownTint == null) {
                tint[i] = brightness * Mth.lerp(STYLED_SATURATION * packShare, 1.0f, hue);
            } else {
                tint[i] = Mth.lerp(packShare, ownTint[i], brightness * Mth.lerp(STYLED_SATURATION, 1.0f, hue));
            }
            chroma[i] = Mth.lerp(STYLED_CHROMA, 1.0f, extinction[i] / mean);
        }
        float top = Math.max(zeroAt, fullBelow + STYLED_MIN_FADE);
        return new Haze(tint[0], tint[1], tint[2], density, chroma[0], chroma[1], chroma[2], fullBelow, top,
            Mth.clamp(rainScale, 1.0f, 3.0f), false);
    }

    private static float[] vec3(JsonArray array) {
        return new float[] {array.get(0).getAsFloat(), array.get(1).getAsFloat(), array.get(2).getAsFloat()};
    }

    /** Looks the biomes up at the sample points, around where the camera is to the fraction of a block. */
    private static void sample(ClientLevel level, Vec3 cameraPos) {
        BlockPos.MutableBlockPos pos = new BlockPos.MutableBlockPos();
        int y = Mth.floor(cameraPos.y);
        for (int i = 0; i < SAMPLES; i++) {
            sampled[i] = level.getBiome(pos.set(Mth.floor(cameraPos.x + SAMPLE_X[i]), y,
                Mth.floor(cameraPos.z + SAMPLE_Z[i])));
        }
    }

    private static float blend(long now) {
        float seconds = Math.min((now - lastNanos) / 1.0e9f, 1.0f);
        return 1.0f - (float) Math.exp(-seconds * 3.0f / BLEND_SECONDS);
    }

    /**
     * The value handed to the shaders: extinction per block in w, zero when the option is off. In the overworld xyz
     * is a tint for sky-lit haze; in the Nether and the End it is the haze colour itself.
     */
    public static Vector4f update(Minecraft minecraft, Vec3 cameraPos, int skyType, float rain,
        org.joml.Vector4fc vanillaFogColor) {
        ClientLevel level = minecraft.level;
        if (level != derivedFor) {
            // Tags and biomes belong to the world (a server's data packs, a mod's registry).
            DERIVED.clear();
            DERIVED_WATER.clear();
            derivedFor = level;
            hasCurrent = false;
            hasWater = false;
        }
        long now = System.nanoTime();
        float blend = blend(now);
        boolean enabled = Options.biomeAmbiance && Options.biomeAmbianceStrength > 0;
        if (level != null) {
            sample(level, cameraPos);
        }
        updateWater(level, cameraPos, blend, enabled);
        updateSkyOpen(level, cameraPos, blend, skyType);
        if (!enabled || level == null) {
            hasCurrent = false;
            lastNanos = now;
            return new Vector4f(1.0f, 1.0f, 1.0f, 0.0f);
        }
        // The colour means something different in each kind of dimension, so never blend across a change.
        if (skyType != currentSkyType) {
            hasCurrent = false;
            currentSkyType = skyType;
        }
        boolean overworld = skyType == OVERWORLD_SKY;
        float fogR = vanillaFogColor.x();
        float fogG = vanillaFogColor.y();
        float fogB = vanillaFogColor.z();
        if (skyType == END_SKY) {
            fogR = Math.max(fogR, END_FLOOR_R);
            fogG = Math.max(fogG, END_FLOOR_G);
            fogB = Math.max(fogB, END_FLOOR_B);
        }

        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float density = 0.0f;
        // (JOML's empty Vector4f starts with w at 1.)
        Vector4f chroma = new Vector4f(0.0f);
        Vector4f heights = new Vector4f(0.0f);
        float rainScale = 0.0f;
        BlockPos.MutableBlockPos pos = new BlockPos.MutableBlockPos();
        int cx = Mth.floor(cameraPos.x);
        int cy = Mth.floor(cameraPos.y);
        int cz = Mth.floor(cameraPos.z);
        for (int i = 0; i < SAMPLES; i++) {
            Haze haze = hazeOf(sampled[i], overworld);
            float weight = SAMPLE_WEIGHT[i];
            // A colour has no say where there is no haze to show it in.
            float colourWeight = weight * Math.max(haze.density(), 1.0e-5f);
            boolean fogColoured = !overworld && !haze.ownColour();
            r += haze.r() * (fogColoured ? fogR : 1.0f) * colourWeight;
            g += haze.g() * (fogColoured ? fogG : 1.0f) * colourWeight;
            b += haze.b() * (fogColoured ? fogB : 1.0f) * colourWeight;
            chroma.add(haze.chromaR() * colourWeight, haze.chromaG() * colourWeight, haze.chromaB() * colourWeight,
                colourWeight);
            density += haze.density() * weight;
            heights.add(haze.fullBelow() * weight, haze.zeroAt() * weight, 0.0f, 0.0f);
            rainScale += haze.rainScale() * weight;
        }
        float colourTotal = Math.max(chroma.w, 1.0e-9f);
        r /= colourTotal;
        g /= colourTotal;
        b /= colourTotal;
        chroma.div(colourTotal).w = 0.0f;
        float exposure = 1.0f;
        if (overworld) {
            density *= Mth.lerp(Mth.clamp(rain, 0.0f, 1.0f), 1.0f, rainScale);
            exposure = level.getBrightness(LightLayer.SKY, pos.set(cx, cy, cz)) / 15.0f;
        }
        density *= Options.biomeAmbianceStrength / 100.0f;

        if (!hasCurrent) {
            current.set(r, g, b, density);
            currentChroma.set(chroma);
            currentHeights.set(heights);
            currentExposure = exposure;
            hasCurrent = true;
        } else {
            current.lerp(new Vector4f(r, g, b, density), blend);
            currentChroma.lerp(chroma, blend);
            currentHeights.lerp(heights, blend);
            currentExposure += (exposure - currentExposure) * blend;
        }
        lastNanos = now;

        if (Options.debugLogging && now - lastLogNanos > 5_000_000_000L) {
            lastLogNanos = now;
            RadianteRenderer.LOGGER.info("biome ambiance: biome={} tint=({}, {}, {}) density={} chroma={} heights={} "
                    + "sky exposure={} water fog={} water extinction={}",
                level.getBiome(pos.set(cx, cy, cz)).unwrapKey().map(key -> key.identifier().toString()).orElse("?"),
                current.x, current.y, current.z, current.w, currentChroma, currentHeights, currentExposure, waterFog,
                waterExtinction);
        }

        return new Vector4f(current.x, current.y, current.z, current.w * currentExposure);
    }

    /**
     * The clear teal water: red is absorbed within a few blocks while green and blue carry on for dozens, and the
     * light the water scatters is nearly all green-blue. The numbers are real clear water's, per block as the
     * shaders take them (the camera's own view of it is thinned a tenth, as the pack's is): red 0.25, green 0.053,
     * blue 0.058. The scattering albedo (0.21 / 1.0 / 0.90 for real clear water) is held to a fifth of that, so the water
     * glows as lit water does and not as milk. {@code water_clear_teal} blends each biome's water towards it.
     */
    private static final Vector3fc CLEAR_TEAL_EXTINCTION = new Vector3f(0.255f, 0.053f, 0.058f);
    private static final Vector3fc CLEAR_TEAL_ALBEDO = new Vector3f(0.03f, 0.22f, 0.20f);
    private static final String CLEAR_TEAL_ATTRIBUTE = "render_pipeline.module.ray_tracing.attribute.water_clear_teal";

    private static float clearTealShare() {
        String value = com.g2806.radiante.client.pipeline.Pipeline.getShaderPackValue(CLEAR_TEAL_ATTRIBUTE);
        if (value == null) {
            return 0.0f;
        }
        try {
            return Math.max(0.0f, Math.min(1.0f, Float.parseFloat(value)));
        } catch (NumberFormatException e) {
            return 0.0f;
        }
    }

    /**
     * The water around the camera, averaged and eased like the haze: a Bedrock pack's where it has one, and otherwise
     * one worked out from the biome's vanilla water colour, so every biome's water keeps its own tone as it gets
     * deeper, and each kind of water its own reach and darkness.
     */
    private static void updateWater(ClientLevel level, Vec3 cameraPos, float blend, boolean styled) {
        if (level == null) {
            waterExtinction.set(0.0f);
            waterAlbedo.set(0.0f);
            waterFog.set(0.0f);
            hasWater = false;
            wasInWater = false;
            return;
        }
        Vector3f extinction = new Vector3f();
        Vector3f albedo = new Vector3f();
        Vector4f fog = new Vector4f(0.0f);
        boolean bedrock = com.g2806.radiante.client.pipeline.Pipeline.isShaderPackToggleOn(
            com.g2806.radiante.client.pipeline.Pipeline.BEDROCK_ORIGINAL_ATTRIBUTE);
        for (int i = 0; i < SAMPLES; i++) {
            Water water = waterOf(sampled[i], styled, bedrock);
            float weight = SAMPLE_WEIGHT[i];
            extinction.add(water.extinctionR() * weight, water.extinctionG() * weight, water.extinctionB() * weight);
            albedo.add(water.albedoR() * weight, water.albedoG() * weight, water.albedoB() * weight);
            fog.add(water.fogR() * weight, water.fogG() * weight, water.fogB() * weight,
                water.visibility() * weight);
        }
        float clearTeal = clearTealShare();
        if (clearTeal > 0.0f) {
            extinction.lerp(CLEAR_TEAL_EXTINCTION, clearTeal);
            albedo.lerp(CLEAR_TEAL_ALBEDO, clearTeal);
        }
        if (!hasWater) {
            waterExtinction.set(extinction, 1.0f);
            waterAlbedo.set(albedo, 0.0f);
            waterFog.set(fog);
            hasWater = true;
        } else {
            waterExtinction.lerp(new Vector4f(extinction, 1.0f), blend);
            waterAlbedo.lerp(new Vector4f(albedo, 0.0f), blend);
            waterFog.lerp(fog, blend);
        }
        // w: 1 plus how deep the camera is under the water, so the shaders dim the light filling the water around it
        // the deeper it goes (water_medium.glsl).
        float depth = cameraWaterDepth(level, cameraPos);
        waterExtinction.w = 1.0f + depth;
        // The sky over that water, for how brightly it is lit from above. Taken as it is on going in, eased after:
        // swimming in under a tree or a roof darkens the water over a moment, as the eye would have it.
        boolean inWater = depth > 0.0f;
        waterSky = inWater && wasInWater ? waterSky + (waterSkyNow - waterSky) * blend : waterSkyNow;
        wasInWater = inWater;
        waterAlbedo.w = waterSky;
        if (!styled) {
            waterFog.set(0.0f);
        }
    }

    /**
     * Blocks of water above the camera, from the eye up to the surface; 0 out of water. Capped at 64. Leaves the sky
     * light just over that surface in waterSkyNow (1 out of water).
     */
    private static float cameraWaterDepth(ClientLevel level, Vec3 cameraPos) {
        BlockPos.MutableBlockPos pos = new BlockPos.MutableBlockPos(Mth.floor(cameraPos.x), Mth.floor(cameraPos.y),
            Mth.floor(cameraPos.z));
        waterSkyNow = 1.0f;
        if (!level.getFluidState(pos).is(net.minecraft.tags.FluidTags.WATER)) {
            return 0.0f;
        }
        int top = pos.getY();
        while (top - pos.getY() < 64) {
            BlockPos above = new BlockPos(pos.getX(), top + 1, pos.getZ());
            if (!level.getFluidState(above).is(net.minecraft.tags.FluidTags.WATER)) {
                break;
            }
            top++;
        }
        // Vanilla's sky light in the first block over the water: 15 under the open sky, a little less under leaves,
        // falling off under a roof with the distance to its edge, none in a cave. Smooth from block to block, which
        // one ray straight up from the eye is not (dark under every lily pad).
        waterSkyNow = level.getBrightness(LightLayer.SKY, new BlockPos(pos.getX(), top + 1, pos.getZ())) / 15.0f;
        // The surface of a full water block is at its top, a little lower for a flowing one; the top block's own
        // height is good enough here.
        return (float) Math.max(0.0, top + 1.0 - cameraPos.y);
    }

    /** Per block, how much of the colours the biome's water is not gets absorbed. */
    private static final float VANILLA_WATER_ABSORPTION = 0.22f;
    /** Floor of the extinction, so even the palest water fades with depth. */
    private static final float VANILLA_WATER_MIN_EXTINCTION = 0.02f;
    /**
     * How much of the vanilla water's extinction scatters back, times its colour squared: water mostly absorbs, and
     * what little it scatters is in its own colour. At 0.45 times the colour it scattered a sixth to a half of what
     * it took, five to ten times a Bedrock pack's water (albedo 0.01 to 0.2, bluest of all), and a lake glowed milky
     * white as if it gave off light.
     */
    private static final float VANILLA_WATER_SCATTER = 0.2f;

    /**
     * How much of a Bedrock pack's own water extinction is kept. Bedrock RTX's water reads far clearer than its
     * coefficients taken per block: the length of a sunken hall stays in sight where they alone fog it out in ten.
     */
    private static final float PACK_WATER_CLARITY = 0.35f;
    /** A pack's ocean, per block and averaged over the channels: the water PLAIN_VISIBILITY is for. */
    private static final float PACK_WATER_REFERENCE = 0.34f;
    /** Over how many blocks light takes on the biome's water colour whole. */
    private static final float WATER_COLOUR_DEPTH = 6.0f;
    /**
     * Bedrock RTX's water light is turquoise whatever the biome's own water colour (a deep blue for most oceans); the
     * colour taken is this far towards that. Not further: at 0.7 every biome's water came out the same turquoise,
     * a swamp's as a warm ocean's.
     */
    private static final float WATER_TURQUOISE_SHARE = 0.35f;
    private static final float[] WATER_TURQUOISE = {0.25f, 0.85f, 0.9f};
    /** Floor of each channel of the water colour, so no colour is absorbed outright. */
    private static final float WATER_COLOUR_FLOOR = 0.12f;
    /**
     * The colour the view fades into under water, from the biome's water colour: deepened (a power), which turns
     * vanilla's default water blue into a clear sea blue, a warm ocean's into turquoise and a swamp's into an olive
     * murk, and then brought to one level. Left at the level the power gave it, a dull water colour (a swamp's
     * grey-green, a mod's dark teal) came out next to black, lights off rather than murk; how dark a water is, is
     * for its WaterStyle to say. The level is a cap on the strongest channel and one on the brightness, whichever is
     * reached first: by the channel alone a green water glowed at twice a blue one's brightness (a pack's swamp was
     * a lit green soup), by the brightness alone a pure blue went off the scale.
     */
    private static final float WATER_FOG_POWER = 1.6f;
    private static final float WATER_FOG_LEVEL = 0.46f;
    private static final float WATER_FOG_LUMA = 0.24f;
    private static final float WATER_FOG_MIN_CHANNEL = 0.04f;

    /**
     * The water of a biome. bedrock: Bedrock RTX's own mode is on, which takes a pack's water as the pack writes it:
     * its absorption and scattering per block, neither thinned nor coloured by the biome's water colour, and lit
     * per colour channel by the shaders (water_medium.glsl). Side by side with Bedrock RTX (Vanilla RTX, plains) the
     * turquoise and the thinning below made a pool teal-green where Bedrock shows it blue, and the sea under water
     * a dark teal where Bedrock's is a bright blue.
     */
    private static Water waterOf(Holder<Biome> biome, boolean styled, boolean bedrock) {
        String id = biome.unwrapKey().map(key -> key.identifier().toString()).orElse(null);
        float[] pack = id == null ? null : PACK_WATER_BY_BIOME.get(id);
        // A Bedrock pack's own water colour for the biome where it gives one, as Bedrock tints its water.
        Integer packColour = id == null ? null : PACK_WATER_COLOUR_BY_BIOME.get(id);
        int colour = packColour != null ? packColour : biome.value().getWaterColor();
        boolean asPacked = bedrock && pack != null;
        WaterStyle style = styled && !asPacked ? waterStyleOf(biome) : PLAIN_WATER;

        float[] medium = pack == null ? vanillaWater(colour) : (asPacked ? pack : coloured(pack, colour));
        float visibility = style.visibility();
        if (pack != null && styled) {
            // The pack's own say in how far its water lets one see, met half way with the kind of water it is.
            float mean = Math.max((pack[0] + pack[1] + pack[2]) / 3.0f, 1.0e-3f);
            float packVisibility =
                Mth.clamp(PLAIN_VISIBILITY * (float) Math.sqrt(PACK_WATER_REFERENCE / mean), 12.0f, 40.0f);
            visibility = (float) Math.sqrt(packVisibility * visibility);
        }
        float[] fog = new float[3];
        float peak = 0.0f;
        for (int i = 0; i < 3; i++) {
            float channel = Math.max((colour >> (16 - 8 * i) & 0xFF) / 255.0f, WATER_FOG_MIN_CHANNEL);
            fog[i] = (float) Math.pow(channel, WATER_FOG_POWER);
            peak = Math.max(peak, fog[i]);
        }
        float luma = 0.2126f * fog[0] + 0.7152f * fog[1] + 0.0722f * fog[2];
        float level = Math.min(WATER_FOG_LEVEL / peak, WATER_FOG_LUMA / luma) * style.brightness();
        for (int i = 0; i < 3; i++) {
            fog[i] *= level;
        }
        // The light the water scatters along the view follows the same brightness: thicker water alone scatters
        // more, and a deep ocean made murkier glowed brighter for it.
        return new Water(medium[0] / style.clarity(), medium[1] / style.clarity(), medium[2] / style.clarity(),
            medium[3] * style.brightness(), medium[4] * style.brightness(), medium[5] * style.brightness(),
            visibility, fog[0], fog[1], fog[2]);
    }

    /**
     * A pack's water, coloured by the biome's water colour as Bedrock RTX colours it: a pack's coefficients are
     * nearly grey for the ocean, yet its water is turquoise. Light takes on that colour over WATER_COLOUR_DEPTH blocks,
     * on top of the pack's own, thinned, extinction.
     */
    private static float[] coloured(float[] pack, int rgb) {
        float grey = (pack[0] + pack[1] + pack[2]) / 3.0f * PACK_WATER_CLARITY;
        return new float[] {grey + colourExtinction(rgb >> 16 & 0xFF, 0), grey + colourExtinction(rgb >> 8 & 0xFF, 1),
            grey + colourExtinction(rgb & 0xFF, 2), pack[3], pack[4], pack[5]};
    }

    private static float colourExtinction(int channel, int index) {
        float colour = channel / 255.0f + (WATER_TURQUOISE[index] - channel / 255.0f) * WATER_TURQUOISE_SHARE;
        return (float) -Math.log(Math.max(colour, WATER_COLOUR_FLOOR)) / WATER_COLOUR_DEPTH;
    }

    /** Water that looks like the vanilla tint: it keeps its colour and absorbs the rest. */
    private static float[] vanillaWater(int rgb) {
        float r = (rgb >> 16 & 0xFF) / 255.0f;
        float g = (rgb >> 8 & 0xFF) / 255.0f;
        float b = (rgb & 0xFF) / 255.0f;
        return new float[] {VANILLA_WATER_MIN_EXTINCTION + (1.0f - r) * VANILLA_WATER_ABSORPTION,
            VANILLA_WATER_MIN_EXTINCTION + (1.0f - g) * VANILLA_WATER_ABSORPTION,
            VANILLA_WATER_MIN_EXTINCTION + (1.0f - b) * VANILLA_WATER_ABSORPTION,
            r * r * VANILLA_WATER_SCATTER, g * g * VANILLA_WATER_SCATTER, b * b * VANILLA_WATER_SCATTER};
    }

    private static WaterStyle waterStyleOf(Holder<Biome> biome) {
        WaterStyle known = biome.unwrapKey().map(WATER_BY_BIOME::get).orElse(null);
        if (known != null) {
            return known;
        }
        return DERIVED_WATER.computeIfAbsent(biome, BiomeAmbiance::deriveWaterStyle);
    }

    /** The kind of water of a biome Radiante has no entry for, from its tags and what it says of its water fog. */
    private static WaterStyle deriveWaterStyle(Holder<Biome> biome) {
        WaterStyle style = PLAIN_WATER;
        boolean cold = biome.is(C_SNOWY) || biome.is(C_ICY) || biome.value().getBaseTemperature() < 0.15f;
        if (biome.is(C_SWAMP)) {
            style = SWAMP_WATER;
        } else if (biome.is(BiomeTags.IS_DEEP_OCEAN)) {
            style = cold ? DEEP_COLD_WATER : DEEP_WATER;
        } else if (biome.is(BiomeTags.IS_BEACH) || biome.is(C_SHALLOW_OCEAN)) {
            style = cold ? COLD_WATER : SHALLOW_WATER;
        } else if (biome.is(BiomeTags.IS_OCEAN)) {
            style = cold ? COLD_WATER : OCEAN_WATER;
        } else if (biome.is(BiomeTags.IS_RIVER)) {
            style = cold ? COLD_WATER : RIVER_WATER;
        }
        // Vanilla's own knob for murky water: a swamp pulls its water fog in to 85 % of the usual distance.
        float end = biome.value().getAttributes().applyModifier(EnvironmentAttributes.WATER_FOG_END_DISTANCE,
            VANILLA_WATER_FOG_END);
        float reach = Mth.clamp(end / VANILLA_WATER_FOG_END, 0.3f, 1.3f);
        if (Math.abs(reach - 1.0f) > 0.01f && style == PLAIN_WATER) {
            // Stronger than vanilla's own step: its 85 % stands for the swamp's murk.
            float factor = (float) Math.pow(reach, 4.0);
            style = new WaterStyle(Mth.clamp(factor, 0.5f, 1.4f), Mth.clamp(PLAIN_VISIBILITY * factor, 8.0f, 36.0f),
                Mth.clamp(0.6f + 0.4f * factor, 0.7f, 1.1f));
        }
        return style;
    }

    private static Haze hazeOf(Holder<Biome> biome, boolean overworld) {
        if (overworld && !PACK_BY_BIOME.isEmpty()) {
            Haze pack = biome.unwrapKey().map(key -> PACK_BY_BIOME.get(key.identifier().toString())).orElse(null);
            if (pack != null) {
                return pack;
            }
        }
        Haze known = biome.unwrapKey().map(BY_BIOME::get).orElse(null);
        if (known != null) {
            return known;
        }
        return DERIVED.computeIfAbsent(biome, BiomeAmbiance::derive);
    }

    /**
     * The air of a biome Radiante has no entry for - a mod's, a data pack's: the kind of place its tags say it is,
     * or else its climate, tinted by its own fog colour where it sets one. A biome that makes its fog darker than the
     * overworld's gets a thicker haze for it, as that is what it is asking for.
     */
    private static Haze derive(Holder<Biome> biome) {
        if (biome.is(BiomeTags.IS_NETHER)) {
            return NETHER_OTHER;
        }
        if (biome.is(BiomeTags.IS_END)) {
            return END_OUTER;
        }
        if (biome.is(C_CAVE) || biome.is(C_UNDERGROUND) || biome.is(C_VOID)) {
            return NONE;
        }
        Biome value = biome.value();
        float temperature = value.getBaseTemperature();
        Haze base;
        if (biome.is(C_SWAMP)) {
            base = SWAMP;
        } else if (biome.is(BiomeTags.IS_BADLANDS)) {
            base = BADLANDS;
        } else if (biome.is(C_DESERT)) {
            base = DESERT;
        } else if (biome.is(BiomeTags.IS_SAVANNA)) {
            base = SAVANNA;
        } else if (biome.is(BiomeTags.IS_JUNGLE)) {
            base = JUNGLE;
        } else if (biome.is(C_MUSHROOM)) {
            base = MUSHROOM;
        } else if (biome.is(BiomeTags.IS_OCEAN)) {
            base = biome.is(C_ICY) || biome.is(C_SNOWY) ? FROZEN_OCEAN : biome.is(C_COLD) ? COLD_OCEAN : OCEAN;
        } else if (biome.is(BiomeTags.IS_BEACH)) {
            base = biome.is(C_SNOWY) ? SNOWY : BEACH;
        } else if (biome.is(BiomeTags.IS_RIVER)) {
            base = biome.is(C_SNOWY) ? SNOWY : RIVER;
        } else if (biome.is(C_SNOWY) || biome.is(C_ICY)) {
            base = SNOWY;
        } else if (biome.is(C_PEAK)) {
            base = PEAKS;
        } else if (biome.is(BiomeTags.IS_TAIGA)) {
            base = TAIGA;
        } else if (biome.is(C_SPOOKY) || biome.is(C_DENSE) && biome.is(BiomeTags.IS_FOREST)) {
            base = DARK_FOREST;
        } else if (biome.is(C_DEAD) || biome.is(C_WASTELAND)) {
            base = WASTELAND;
        } else if (biome.is(C_MAGICAL)) {
            base = MAGICAL;
        } else if (biome.is(BiomeTags.IS_FOREST)) {
            base = FOREST;
        } else if (biome.is(C_LUSH) || biome.is(C_WET) && temperature >= 0.9f) {
            base = LUSH;
        } else if (biome.is(BiomeTags.IS_MOUNTAIN) || biome.is(BiomeTags.IS_HILL) || biome.is(C_WINDSWEPT)) {
            base = WINDSWEPT;
        } else if (biome.is(C_FLORAL)) {
            base = PLAINS;
        } else if (temperature >= 1.5f && !value.hasPrecipitation() || biome.is(C_HOT) && biome.is(C_DRY)) {
            base = DESERT;
        } else if (temperature < 0.15f) {
            base = SNOWY;
        } else {
            base = PLAINS;
        }

        // The biome's own fog colour, against the overworld's: its hue goes into the tint, and a darker fog thickens
        // the haze. A biome that leaves the fog alone comes out as its base.
        int fogRgb = value.getAttributes().applyModifier(EnvironmentAttributes.FOG_COLOR, 0xC0D8FF);
        Vector3fc fog = new Vector3f(net.minecraft.util.ARGB.redFloat(fogRgb), net.minecraft.util.ARGB.greenFloat(fogRgb),
            net.minecraft.util.ARGB.blueFloat(fogRgb));
        float rr = fog.x() / OVERWORLD_FOG.x();
        float rg = fog.y() / OVERWORLD_FOG.y();
        float rb = fog.z() / OVERWORLD_FOG.z();
        float peak = Math.max(rr, Math.max(rg, rb));
        if (peak <= 1.0e-3f || Math.abs(rr - 1.0f) + Math.abs(rg - 1.0f) + Math.abs(rb - 1.0f) < 0.02f) {
            return base;
        }
        float luma = Mth.clamp(0.2126f * rr + 0.7152f * rg + 0.0722f * rb, 0.0f, 1.0f);
        // The fog colour has most of the say in the tint and the kind of place the rest. Not all of it, and softened:
        // the haze is lit by the sky, which such a biome has usually coloured to match already.
        float level = Math.max(base.r(), Math.max(base.g(), base.b()))
            * (DERIVED_MIN_BRIGHTNESS + (1.0f - DERIVED_MIN_BRIGHTNESS) * (float) Math.sqrt(luma));
        Haze tinted = base.withTint(
            Mth.lerp(DERIVED_FOG_SHARE, base.r(), level * (float) Math.pow(rr / peak, DERIVED_FOG_POWER)),
            Mth.lerp(DERIVED_FOG_SHARE, base.g(), level * (float) Math.pow(rg / peak, DERIVED_FOG_POWER)),
            Mth.lerp(DERIVED_FOG_SHARE, base.b(), level * (float) Math.pow(rb / peak, DERIVED_FOG_POWER)));
        float thicker = 1.0f + 1.5f * (1.0f - luma);
        return tinted.withDensity(Math.min(Math.max(base.density(), PLAINS.density() * 2.0f) * thicker,
            STYLED_MAX_DENSITY));
    }
}
