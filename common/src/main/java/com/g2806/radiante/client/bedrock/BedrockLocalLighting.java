package com.g2806.radiante.client.bedrock;

import com.google.gson.GsonBuilder;
import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.Map;

/**
 * A Bedrock pack's Vibrant Visuals local lighting ({@code local_lighting/local_lighting.json}): per block, the colour
 * of the light it gives and whether Bedrock lights the world from it as a point light (a torch, a lantern, a candle)
 * or through its surface (a static light: lava, a froglight). The converted pack keeps it under Java block names in
 * {@link #PATH}, which the renderer reads while the pack is enabled (BedrockPointLights).
 *
 * <p>Only the colour and the kind of light are kept. Bedrock's other light settings (its strength comes from the
 * block's light level, as it does here) have no counterpart.
 */
public final class BedrockLocalLighting {

    public static final String PATH = "assets/radiante/bedrock_local_lighting.json";
    static final String SOURCE = "local_lighting/local_lighting.json";

    /** Bedrock block names that differ from Java's; the rest are the same on both sides. */
    private static final Map<String, String> JAVA_NAMES = Map.ofEntries(
        Map.entry("light_block", "light"),
        Map.entry("lit_pumpkin", "jack_o_lantern"),
        Map.entry("magma", "magma_block"),
        Map.entry("flowing_lava", "lava"),
        Map.entry("lit_redstone_lamp", "redstone_lamp"),
        Map.entry("lit_furnace", "furnace"),
        Map.entry("lit_blast_furnace", "blast_furnace"),
        Map.entry("lit_smoker", "smoker"),
        Map.entry("lit_redstone_ore", "redstone_ore"),
        Map.entry("lit_deepslate_redstone_ore", "deepslate_redstone_ore"),
        Map.entry("cave_vines_body_with_berries", "cave_vines_plant"),
        Map.entry("cave_vines_head_with_berries", "cave_vines"));

    private BedrockLocalLighting() {
    }

    /** The light file for the converted pack, or null when the pack has no local lighting. */
    static byte[] convert(BedrockPackFiles files) throws IOException {
        byte[] source = files.read(SOURCE);
        if (source == null) {
            return null;
        }
        JsonObject blocks;
        try {
            blocks = parse(new String(source, StandardCharsets.UTF_8));
        } catch (RuntimeException e) {
            return null;
        }
        if (blocks.isEmpty()) {
            return null;
        }
        JsonObject out = new JsonObject();
        out.add("blocks", blocks);
        return new GsonBuilder().setPrettyPrinting().create().toJson(out).getBytes(StandardCharsets.UTF_8);
    }

    /**
     * The blocks of a local_lighting.json under Java names: {@code {"minecraft:torch": {"color": [r, g, b], "type":
     * "point_light"}}}, colours 0-255 as authored. Blocks of other namespaces (an add-on's own) are kept too; the
     * renderer ignores the ones the game does not have.
     */
    static JsonObject parse(String json) {
        // Bedrock tolerates comments in its JSON; Gson's lenient parser does too.
        JsonObject root = JsonParser.parseString(json).getAsJsonObject();
        JsonObject settings = root.getAsJsonObject("minecraft:local_light_settings");
        JsonObject blocks = new JsonObject();
        if (settings == null) {
            return blocks;
        }
        for (Map.Entry<String, JsonElement> entry : settings.entrySet()) {
            if (!entry.getValue().isJsonObject()) {
                continue;
            }
            JsonObject light = entry.getValue().getAsJsonObject();
            int[] rgb = colour(light.get("light_color"));
            if (rgb == null) {
                continue;
            }
            String type = light.has("light_type") ? light.get("light_type").getAsString() : "static_light";
            String id = javaName(entry.getKey());
            // A Bedrock block and its lit twin (lava and flowing lava) fold into one Java block; the first wins.
            if (blocks.has(id)) {
                continue;
            }
            JsonObject out = new JsonObject();
            JsonArray colour = new JsonArray();
            colour.add(rgb[0]);
            colour.add(rgb[1]);
            colour.add(rgb[2]);
            out.add("color", colour);
            out.addProperty("type", type);
            blocks.add(id, out);
        }
        return blocks;
    }

    private static String javaName(String bedrockId) {
        int colon = bedrockId.indexOf(':');
        String namespace = colon < 0 ? "minecraft" : bedrockId.substring(0, colon);
        String path = colon < 0 ? bedrockId : bedrockId.substring(colon + 1);
        if (namespace.equals("minecraft")) {
            path = JAVA_NAMES.getOrDefault(path, path);
        }
        return namespace + ":" + path;
    }

    /** "#rrggbb" (an alpha after it is ignored) or [r, g, b] in 0-255 or 0-1; null for anything else. */
    private static int[] colour(JsonElement value) {
        if (value == null) {
            return null;
        }
        try {
            if (value.isJsonPrimitive() && value.getAsString().startsWith("#") && value.getAsString().length() >= 7) {
                int hex = Integer.parseInt(value.getAsString().substring(1, 7), 16);
                return new int[] {hex >> 16 & 0xFF, hex >> 8 & 0xFF, hex & 0xFF};
            }
            if (value.isJsonArray() && value.getAsJsonArray().size() >= 3) {
                JsonArray array = value.getAsJsonArray();
                float r = array.get(0).getAsFloat();
                float g = array.get(1).getAsFloat();
                float b = array.get(2).getAsFloat();
                float scale = Math.max(r, Math.max(g, b)) <= 1.0f ? 255.0f : 1.0f;
                return new int[] {clamp(r * scale), clamp(g * scale), clamp(b * scale)};
            }
        } catch (RuntimeException e) {
            return null;
        }
        return null;
    }

    private static int clamp(float value) {
        return Math.clamp(Math.round(value), 0, 255);
    }
}
