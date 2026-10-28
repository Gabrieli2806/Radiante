package com.g2806.radiante.client.render;

import com.g2806.radiante.client.pipeline.Pipeline;
import com.g2806.radiante.client.profile.RendererProfile;
import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.Reader;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.Map;
import net.minecraft.resources.Identifier;

/**
 * The Bedrock RTX (Enhanced) profile's built-in fog, water and light block glow, used when no Bedrock pack is enabled.
 * They are data, never textures:
 *
 * <ul>
 * <li>{@code bedrock_defaults/fog.json}: the per-biome fog and water, in the format of the {@code bedrock_fog.json} that
 * Radiante's Bedrock converter (BedrockFog) writes into a converted pack, read by {@link BiomeAmbiance} when no enabled
 * pack brings one.
 * <li>{@code bedrock_defaults/emission.json}: per light block sprite, how much light a MER map gives it (MER green
 * squared over its texels, what {@link EmissionTiles#uploadFromMer} casts) and the metalness and roughness of its
 * glowing texels. {@link PbrAtlases} lays a MER map with that much glow over the texels Radiante already finds glowing
 * in vanilla's texture, so a block shows and casts that light; where its glow sits within the texture still follows
 * vanilla's texture.
 * </ul>
 *
 * Only the Bedrock RTX (Enhanced) profile uses them: Realistic and Custom keep Radiante's own values, and a Bedrock pack
 * that covers a biome or a sprite always wins.
 */
public final class BedrockDefaults {

    private static final String FOG = "/assets/radiante/bedrock_defaults/fog.json";
    private static final String EMISSION = "/assets/radiante/bedrock_defaults/emission.json";

    /** A sprite's built-in light: glow is the mean of MER green squared over all its texels. */
    record Glow(float glow, float metalness, float roughness) {
    }

    private static final Glow DARK = new Glow(0.0f, 0.0f, 1.0f);

    private static JsonObject fog;
    private static Map<Identifier, Glow> glowBySprite;

    private BedrockDefaults() {
    }

    /** Whether the profile that takes these values is the one in use. */
    public static boolean active() {
        return RendererProfile.current() == RendererProfile.BEDROCK_RTX_ENHANCED;
    }

    /** Whether the light blocks take their glow from the table: the profile, and Bedrock's glow being cast. */
    static boolean emissionActive() {
        return active() && Pipeline.castsBedrockGlow();
    }

    /** The built-in fog file's biomes, in the format of a converted pack's bedrock_fog.json; null if unreadable. */
    static synchronized JsonObject fogBiomes() {
        if (fog == null) {
            JsonObject root = read(FOG);
            fog = root != null && root.has("biomes") ? root.getAsJsonObject("biomes") : new JsonObject();
        }
        return fog.isEmpty() ? null : fog;
    }

    /** The built-in light for a block sprite, or null for a sprite without one. */
    static synchronized Glow glow(Identifier sprite) {
        if (glowBySprite == null) {
            Map<Identifier, Glow> table = new HashMap<>();
            JsonObject root = read(EMISSION);
            if (root != null) {
                for (Map.Entry<String, JsonElement> entry : root.getAsJsonObject("sprites").entrySet()) {
                    JsonArray values = entry.getValue().getAsJsonArray();
                    Identifier id = Identifier.tryParse(entry.getKey());
                    if (id != null) {
                        table.put(id, new Glow(values.get(0).getAsFloat(), values.get(1).getAsFloat(),
                            values.get(2).getAsFloat()));
                    }
                }
                for (JsonElement name : root.getAsJsonArray("dark")) {
                    Identifier id = Identifier.tryParse(name.getAsString());
                    if (id != null) {
                        table.put(id, DARK);
                    }
                }
            }
            glowBySprite = table;
        }
        return glowBySprite.get(sprite);
    }

    private static JsonObject read(String path) {
        try (InputStream stream = BedrockDefaults.class.getResourceAsStream(path)) {
            if (stream == null) {
                RadianteRenderer.LOGGER.warn("Missing built-in {}", path);
                return null;
            }
            try (Reader reader = new InputStreamReader(stream, StandardCharsets.UTF_8)) {
                return JsonParser.parseReader(reader).getAsJsonObject();
            }
        } catch (Exception e) {
            RadianteRenderer.LOGGER.warn("Could not read built-in {}", path, e);
            return null;
        }
    }
}
