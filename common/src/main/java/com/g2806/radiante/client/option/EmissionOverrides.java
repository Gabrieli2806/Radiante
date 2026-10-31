package com.g2806.radiante.client.option;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import java.util.Map;
import java.util.TreeMap;

/** Linear emission multipliers by texture; old percentage settings migrate to neutral RGB. */
public final class EmissionOverrides {
    private EmissionOverrides() { }

    public static Map<String, EmissionColor> parse(String text) {
        Map<String, EmissionColor> values = new TreeMap<>();
        try {
            var root = JsonParser.parseString(text);
            if (!root.isJsonObject()) return Map.of();
            for (var entry : root.getAsJsonObject().entrySet()) {
                if (!entry.getKey().matches("[a-z0-9_.-]+:block/[a-z0-9_./-]+")) continue;
                try {
                    EmissionColor colour;
                    var value = entry.getValue();
                    if (value.isJsonArray() && value.getAsJsonArray().size() == 3) {
                        var rgb = value.getAsJsonArray();
                        for (var channel : rgb) {
                            if (!channel.isJsonPrimitive() || !channel.getAsJsonPrimitive().isNumber()) {
                                throw new IllegalArgumentException("Non-numeric emission channel");
                            }
                        }
                        colour = new EmissionColor(rgb.get(0).getAsFloat(), rgb.get(1).getAsFloat(), rgb.get(2).getAsFloat());
                    } else if (value.isJsonPrimitive() && value.getAsJsonPrimitive().isNumber()) {
                        int percent = value.getAsBigDecimal().max(java.math.BigDecimal.ZERO)
                            .min(java.math.BigDecimal.valueOf(1000)).intValueExact();
                        colour = EmissionColor.WHITE.multiply(percent / 100.0f);
                    } else continue;
                    if (!colour.equals(EmissionColor.WHITE)) values.put(entry.getKey(), colour);
                } catch (RuntimeException ignored) {
                    // An invalid entry must not discard the other saved lights.
                }
            }
        } catch (RuntimeException ignored) {
            return Map.of();
        }
        return Map.copyOf(values);
    }

    public static String serialize(Map<String, EmissionColor> values) {
        JsonObject json = new JsonObject();
        new TreeMap<>(values).forEach((key, colour) -> {
            if (!colour.equals(EmissionColor.WHITE)) {
                JsonArray rgb = new JsonArray();
                rgb.add(colour.red()); rgb.add(colour.green()); rgb.add(colour.blue());
                json.add(key, rgb);
            }
        });
        return json.toString();
    }

    public static EmissionColor color(Map<String, EmissionColor> values, String sprite) {
        return values.getOrDefault(sprite, EmissionColor.WHITE);
    }
}
