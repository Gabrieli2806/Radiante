package com.g2806.radiante.client.render;

import com.google.gson.JsonElement;
import com.mojang.serialization.JsonOps;
import java.nio.charset.StandardCharsets;
import java.util.IdentityHashMap;
import java.util.Map;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.ClientLevel;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Holder;
import net.minecraft.util.Mth;
import net.minecraft.world.level.biome.Biome;
import net.minecraft.world.phys.Vec3;
import org.joml.Vector4f;

/**
 * The biome at the camera and the thunder, for shader packs (radiante-pt's {@code common/pack_world.glsl}). Radiante
 * PT reads the downfall and the thunder for how hazy the air is (aerial perspective). The climate is eased over a couple of seconds like the biome haze, so an effect a pack
 * ties to it fades across a border instead of switching; the biome's id is handed over as a hash with the one before
 * it and how far the change has come, so a pack can crossfade an effect it gives one biome alone.
 */
public final class BiomeClimate {

    /** Seconds for the blend to cover most of the way to a new biome, as BiomeAmbiance's. */
    private static final float BLEND_SECONDS = 2.5f;
    /** A biome's downfall when it cannot be read. */
    private static final float DEFAULT_DOWNFALL = 0.5f;

    private static final Map<Holder<Biome>, Float> DOWNFALL = new IdentityHashMap<>();
    private static Object downfallFor;

    /** x temperature, y downfall, z snowy 0..1, w dry 0..1, all eased. */
    private static final Vector4f climate = new Vector4f(0.8f, 0.4f, 0.0f, 0.0f);
    private static int biomeId;
    private static int previousBiomeId;
    private static float idBlend = 1.0f;
    private static boolean hasClimate;
    private static long lastNanos;

    private BiomeClimate() {
    }

    /**
     * Eases the climate towards the biome at the camera; call once per frame before reading the values. Without a
     * level everything stays where it was.
     */
    public static void update(Minecraft minecraft, Vec3 cameraPos) {
        ClientLevel level = minecraft.level;
        long now = System.nanoTime();
        float seconds = Math.min((now - lastNanos) / 1.0e9f, 1.0f);
        lastNanos = now;
        if (level == null) {
            hasClimate = false;
            return;
        }
        if (level != downfallFor) {
            DOWNFALL.clear();
            downfallFor = level;
            hasClimate = false;
        }
        BlockPos pos = BlockPos.containing(cameraPos);
        Holder<Biome> holder = level.getBiome(pos);
        Biome biome = holder.value();
        // As Iris's biome_precipitation: what falls at the camera's own height, so the top of a mountain in a cold
        // biome is snowy while its foot may be rainy.
        Biome.Precipitation precipitation = biome.getPrecipitationAt(pos, level.getSeaLevel());
        float temperature = biome.getBaseTemperature();
        float downfall = downfall(holder);
        float snowy = precipitation == Biome.Precipitation.SNOW ? 1.0f : 0.0f;
        float dry = precipitation == Biome.Precipitation.NONE ? 1.0f : 0.0f;
        int id = hash(holder.unwrapKey().map(key -> key.identifier().toString()).orElse(""));

        if (!hasClimate) {
            climate.set(temperature, downfall, snowy, dry);
            biomeId = id;
            previousBiomeId = id;
            idBlend = 1.0f;
            hasClimate = true;
            return;
        }
        float blend = 1.0f - (float) Math.exp(-seconds * 3.0f / BLEND_SECONDS);
        climate.set(Mth.lerp(blend, climate.x, temperature), Mth.lerp(blend, climate.y, downfall),
            Mth.lerp(blend, climate.z, snowy), Mth.lerp(blend, climate.w, dry));
        if (id != biomeId) {
            // A change back before the last one finished starts from where that one had got to.
            if (id == previousBiomeId) {
                idBlend = 1.0f - idBlend;
            } else {
                idBlend = 0.0f;
            }
            previousBiomeId = biomeId;
            biomeId = id;
        }
        idBlend = idBlend + (1.0f - idBlend) * blend;
        if (idBlend > 0.999f) {
            idBlend = 1.0f;
        }
    }

    /** x temperature, y downfall, z snowy weight, w dry weight, eased. */
    public static Vector4f climate() {
        return new Vector4f(climate);
    }

    public static int biomeId() {
        return biomeId;
    }

    public static int previousBiomeId() {
        return previousBiomeId;
    }

    /** 0 just after the biome changed, 1 once it has fully faded in. */
    public static float idBlend() {
        return idBlend;
    }

    /** The thunder at the camera, 0..1, as Iris's thunderStrength. */
    public static float thunder(Minecraft minecraft, float partialTicks) {
        return minecraft.level == null ? 0.0f : minecraft.level.getThunderLevel(partialTicks);
    }

    /**
     * FNV-1a over the UTF-8 bytes of a biome's id ("minecraft:snowy_plains"). radiante-pt's pack_world.glsl lists
     * the vanilla biomes' values, and a pack can work one out for a modded biome the same way.
     */
    public static int hash(String id) {
        int hash = 0x811C9DC5;
        for (byte b : id.getBytes(StandardCharsets.UTF_8)) {
            hash ^= b & 0xFF;
            hash *= 0x01000193;
        }
        return hash;
    }

    /**
     * The biome's downfall. Biome keeps its climate settings to itself and their class is not public, so it is read
     * from the biome's network encoding, once per biome.
     */
    private static float downfall(Holder<Biome> holder) {
        return DOWNFALL.computeIfAbsent(holder, h -> {
            try {
                JsonElement json = Biome.NETWORK_CODEC.encodeStart(JsonOps.INSTANCE, h.value())
                    .resultOrPartial(error -> { }).orElse(null);
                if (json != null && json.isJsonObject() && json.getAsJsonObject().has("downfall")) {
                    return json.getAsJsonObject().get("downfall").getAsFloat();
                }
            } catch (RuntimeException e) {
                // A modded biome whose effects do not encode without registries; the default will do.
            }
            return DEFAULT_DOWNFALL;
        });
    }
}
