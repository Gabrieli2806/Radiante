package com.g2806.radiante.client.render;

import com.g2806.radiante.client.option.Options;
import com.g2806.radiante.compat.VersionCompat;
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

import static com.g2806.radiante.client.render.BiomeAmbiance.*;

/**
 * The water of each biome for {@link BiomeAmbiance}: how far light reaches in it and what colour it scatters, from a
 * Bedrock pack where it gives one and from the biome's vanilla water colour otherwise, eased around the camera as the
 * haze is. The values go to the shaders' water medium (water_medium.glsl).
 */
final class BiomeWater {

    private BiomeWater() {
    }

    /**
     * The clear teal water: red is absorbed within a few blocks while green and blue carry on for dozens, and the
     * light the water scatters is nearly all green-blue. The numbers are real clear water's, per block as the
     * shaders take them (the camera's own view of it is thinned a tenth, as the pack's is): red 0.25, green 0.053,
     * blue 0.058. The scattering albedo (0.21 / 1.0 / 0.90 for real clear water) is held to a fifth of that, so the water
     * glows as lit water does and not as milk. {@code water_clear_teal} blends each biome's water towards it.
     */
    static final Vector3fc CLEAR_TEAL_EXTINCTION = new Vector3f(0.255f, 0.053f, 0.058f);
    static final Vector3fc CLEAR_TEAL_ALBEDO = new Vector3f(0.03f, 0.22f, 0.20f);
    static final String CLEAR_TEAL_ATTRIBUTE = "render_pipeline.module.ray_tracing.attribute.water_clear_teal";

    static float clearTealShare() {
        String value = com.g2806.radiante.client.pipeline.PipelineSettings.getShaderPackValue(CLEAR_TEAL_ATTRIBUTE);
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
    static void updateWater(ClientLevel level, Vec3 cameraPos, float blend, boolean styled) {
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
        for (int i = 0; i < SAMPLES; i++) {
            Water water = waterOf(sampled[i], styled);
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
    static float cameraWaterDepth(ClientLevel level, Vec3 cameraPos) {
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
    static final float VANILLA_WATER_ABSORPTION = 0.22f;
    /** Floor of the extinction, so even the palest water fades with depth. */
    static final float VANILLA_WATER_MIN_EXTINCTION = 0.02f;
    /**
     * How much of the vanilla water's extinction scatters back, times its colour squared: water mostly absorbs, and
     * what little it scatters is in its own colour. At 0.45 times the colour it scattered a sixth to a half of what
     * it took, five to ten times a Bedrock pack's water (albedo 0.01 to 0.2, bluest of all), and a lake glowed milky
     * white as if it gave off light.
     */
    static final float VANILLA_WATER_SCATTER = 0.2f;

    /**
     * How much of a Bedrock pack's own water extinction is kept. Bedrock RTX's water reads far clearer than its
     * coefficients taken per block: the length of a sunken hall stays in sight where they alone fog it out in ten.
     */
    static final float PACK_WATER_CLARITY = 0.35f;
    /** A pack's ocean, per block and averaged over the channels: the water PLAIN_VISIBILITY is for. */
    static final float PACK_WATER_REFERENCE = 0.34f;
    /** Over how many blocks light takes on the biome's water colour whole. */
    static final float WATER_COLOUR_DEPTH = 6.0f;
    /**
     * Bedrock RTX's water light is turquoise whatever the biome's own water colour (a deep blue for most oceans); the
     * colour taken is this far towards that. Not further: at 0.7 every biome's water came out the same turquoise,
     * a swamp's as a warm ocean's.
     */
    static final float WATER_TURQUOISE_SHARE = 0.35f;
    static final float[] WATER_TURQUOISE = {0.25f, 0.85f, 0.9f};
    /** Floor of each channel of the water colour, so no colour is absorbed outright. */
    static final float WATER_COLOUR_FLOOR = 0.12f;
    /**
     * The colour the view fades into under water, from the biome's water colour: deepened (a power), which turns
     * vanilla's default water blue into a clear sea blue, a warm ocean's into turquoise and a swamp's into an olive
     * murk, and then brought to one level. Left at the level the power gave it, a dull water colour (a swamp's
     * grey-green, a mod's dark teal) came out next to black, lights off rather than murk; how dark a water is, is
     * for its WaterStyle to say. The level is a cap on the strongest channel and one on the brightness, whichever is
     * reached first: by the channel alone a green water glowed at twice a blue one's brightness (a pack's swamp was
     * a lit green soup), by the brightness alone a pure blue went off the scale.
     */
    static final float WATER_FOG_POWER = 1.6f;
    static final float WATER_FOG_LEVEL = 0.46f;
    static final float WATER_FOG_LUMA = 0.24f;
    static final float WATER_FOG_MIN_CHANNEL = 0.04f;

    /**
     * The water of a biome. A Bedrock pack's water is taken as the pack writes it, as Bedrock RTX does:
     * its absorption and scattering per block, neither thinned nor coloured by the biome's water colour, and lit
     * per colour channel by the shaders (water_medium.glsl). Side by side with Bedrock RTX (Vanilla RTX, plains) the
     * turquoise and the thinning below made a pool teal-green where Bedrock shows it blue, and the sea under water
     * a dark teal where Bedrock's is a bright blue.
     */
    static Water waterOf(Holder<Biome> biome, boolean styled) {
        String id = biome.unwrapKey().map(key -> key.identifier().toString()).orElse(null);
        float[] pack = id == null ? null : PACK_WATER_BY_BIOME.get(id);
        // A Bedrock pack's own water colour for the biome where it gives one, as Bedrock tints its water.
        Integer packColour = id == null ? null : PACK_WATER_COLOUR_BY_BIOME.get(id);
        int colour = packColour != null ? packColour : biome.value().getWaterColor();
        boolean asPacked = pack != null;
        WaterStyle style = styled && !asPacked ? waterStyleOf(biome) : PLAIN_WATER;

        float[] medium = pack == null ? vanillaWater(colour) : (asPacked ? pack : coloured(pack, colour));
        float visibility = style.visibility();
        if (pack != null && styled) {
            // The pack's own say in how far its water lets one see, met half way with the kind of water it is.
            float mean = Math.max((pack[0] + pack[1] + pack[2]) / 3.0f, 1.0e-3f);
            float packVisibility =
                Math.clamp(PLAIN_VISIBILITY * (float) Math.sqrt(PACK_WATER_REFERENCE / mean), 12.0f, 40.0f);
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
    static float[] coloured(float[] pack, int rgb) {
        float grey = (pack[0] + pack[1] + pack[2]) / 3.0f * PACK_WATER_CLARITY;
        return new float[] {grey + colourExtinction(rgb >> 16 & 0xFF, 0), grey + colourExtinction(rgb >> 8 & 0xFF, 1),
            grey + colourExtinction(rgb & 0xFF, 2), pack[3], pack[4], pack[5]};
    }

    static float colourExtinction(int channel, int index) {
        float colour = channel / 255.0f + (WATER_TURQUOISE[index] - channel / 255.0f) * WATER_TURQUOISE_SHARE;
        return (float) -Math.log(Math.max(colour, WATER_COLOUR_FLOOR)) / WATER_COLOUR_DEPTH;
    }

    /** Water that looks like the vanilla tint: it keeps its colour and absorbs the rest. */
    static float[] vanillaWater(int rgb) {
        float r = (rgb >> 16 & 0xFF) / 255.0f;
        float g = (rgb >> 8 & 0xFF) / 255.0f;
        float b = (rgb & 0xFF) / 255.0f;
        return new float[] {VANILLA_WATER_MIN_EXTINCTION + (1.0f - r) * VANILLA_WATER_ABSORPTION,
            VANILLA_WATER_MIN_EXTINCTION + (1.0f - g) * VANILLA_WATER_ABSORPTION,
            VANILLA_WATER_MIN_EXTINCTION + (1.0f - b) * VANILLA_WATER_ABSORPTION,
            r * r * VANILLA_WATER_SCATTER, g * g * VANILLA_WATER_SCATTER, b * b * VANILLA_WATER_SCATTER};
    }

    static WaterStyle waterStyleOf(Holder<Biome> biome) {
        WaterStyle known = biome.unwrapKey().map(WATER_BY_BIOME::get).orElse(null);
        if (known != null) {
            return known;
        }
        return DERIVED_WATER.computeIfAbsent(biome, BiomeWater::deriveWaterStyle);
    }

    /** The kind of water of a biome Radiante has no entry for, from its tags and what it says of its water fog. */
    static WaterStyle deriveWaterStyle(Holder<Biome> biome) {
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
        float reach = Math.clamp(end / VANILLA_WATER_FOG_END, 0.3f, 1.3f);
        if (Math.abs(reach - 1.0f) > 0.01f && style == PLAIN_WATER) {
            // Stronger than vanilla's own step: its 85 % stands for the swamp's murk.
            float factor = (float) Math.pow(reach, 4.0);
            style = new WaterStyle(Math.clamp(factor, 0.5f, 1.4f), Math.clamp(PLAIN_VISIBILITY * factor, 8.0f, 36.0f),
                Math.clamp(0.6f + 0.4f * factor, 0.7f, 1.1f));
        }
        return style;
    }
}
