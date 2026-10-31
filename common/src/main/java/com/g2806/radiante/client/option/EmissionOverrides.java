package com.g2806.radiante.client.option;

import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import java.math.BigDecimal;
import java.util.Map;
import java.util.TreeMap;

/** Percentage multipliers of authored or built-in emission, keyed by block texture identifier. */
public final class EmissionOverrides {
    public static final int DEFAULT = 100;
    public static final int MAX = 1000;

    private EmissionOverrides() {
    }

    public static Map<String, Integer> parse(String text) {
        Map<String, Integer> values = new TreeMap<>();
        try {
            var root = JsonParser.parseString(text);
            if (!root.isJsonObject()) return Map.of();
            for (var entry : root.getAsJsonObject().entrySet()) {
                if (!entry.getKey().matches("[a-z0-9_.-]+:block/[a-z0-9_./-]+")) continue;
                try {
                    if (!entry.getValue().isJsonPrimitive() || !entry.getValue().getAsJsonPrimitive().isNumber()) continue;
                    BigDecimal number = entry.getValue().getAsBigDecimal();
                    int value = number.max(BigDecimal.ZERO).min(BigDecimal.valueOf(MAX)).intValueExact();
                    if (value != DEFAULT) values.put(entry.getKey(), value);
                } catch (RuntimeException ignored) {
                    // An invalid entry must not discard the other saved lights.
                }
            }
        } catch (RuntimeException ignored) {
            return Map.of();
        }
        return Map.copyOf(values);
    }

    public static String serialize(Map<String, Integer> values) {
        JsonObject json = new JsonObject();
        new TreeMap<>(values).forEach((key, value) -> {
            int percent = Math.max(0, Math.min(MAX, value));
            if (percent != DEFAULT) json.addProperty(key, percent);
        });
        return json.toString();
    }

    public static int percent(Map<String, Integer> values, String sprite) {
        return values.getOrDefault(sprite, DEFAULT);
    }
}
