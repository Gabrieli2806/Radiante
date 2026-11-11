package com.g2806.radiante.client.gui;

import static org.junit.jupiter.api.Assertions.assertTrue;

import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.stream.Stream;
import org.junit.jupiter.api.Test;
import org.yaml.snakeyaml.Yaml;

/**
 * A {@link Tunable} states its setting's default and range a second time, in slider steps: the first time is the
 * attribute itself, in radiante-pt's {@code configs.json} or a module's YAML. Reset to Defaults and the slider's
 * marked default come from the Tunable, what a new player gets from the attribute, so the two have to say the same,
 * and a slider must not reach past what the attribute takes.
 */
class TunableDefaultsTest {

    private static final Path PACK = Path.of(
        "../native/src/shader/world/ray_tracing/internal/radiante-pt/configs.json");
    private static final Path MODULES = Path.of("src/main/resources/modules");

    private record Attribute(String type, String value) {
    }

    @Test
    void everyTunableAgreesWithItsAttribute() throws IOException {
        Map<String, Attribute> attributes = attributes();
        List<String> problems = new ArrayList<>();
        for (Tunable tunable : Tunable.values()) {
            for (String name : tunable.twin == null ? List.of(tunable.attribute)
                : List.of(tunable.attribute, tunable.twin)) {
                Attribute attribute = attributes.get(name);
                if (attribute == null) {
                    problems.add(tunable + ": no attribute " + name);
                    continue;
                }
                double stored;
                try {
                    stored = Double.parseDouble(attribute.value().trim());
                } catch (NumberFormatException e) {
                    problems.add(tunable + ": " + name + " has the default " + attribute.value() + ", not a number");
                    continue;
                }
                double shown = tunable.defaultSteps * tunable.unit;
                // Half a slider step: the nearest the slider can show the attribute's default.
                if (Math.abs(stored - shown) > tunable.unit / 2 + 1e-9) {
                    problems.add(String.format(Locale.ROOT, "%s: the slider's default is %s (%d steps) but %s says %s",
                        tunable, shown, tunable.defaultSteps, name, attribute.value()));
                }
                double[] range = range(attribute.type());
                if (range != null && (tunable.min * tunable.unit < range[0] - tunable.unit / 2 - 1e-9
                    || tunable.max * tunable.unit > range[1] + tunable.unit / 2 + 1e-9)) {
                    problems.add(String.format(Locale.ROOT, "%s: the slider runs %s to %s but %s takes %s", tunable,
                        tunable.min * tunable.unit, tunable.max * tunable.unit, name, attribute.type()));
                }
            }
        }
        assertTrue(problems.isEmpty(), "sliders that disagree with their attributes:\n  "
            + String.join("\n  ", problems));
    }

    /** The lowest and highest value of a {@code float_range:a-b} or {@code int_range:a-b} type, or null. */
    private static double[] range(String type) {
        if (type == null || !type.contains("_range:")) {
            return null;
        }
        String bounds = type.substring(type.indexOf(':') + 1);
        // The separator is the first '-' that is not the sign of the lower bound.
        int separator = bounds.indexOf('-', 1);
        try {
            return new double[] {Double.parseDouble(bounds.substring(0, separator)),
                Double.parseDouble(bounds.substring(separator + 1))};
        } catch (RuntimeException e) {
            return null;
        }
    }

    private static Map<String, Attribute> attributes() throws IOException {
        Map<String, Attribute> attributes = new HashMap<>();
        JsonObject pack = JsonParser.parseString(Files.readString(PACK)).getAsJsonObject();
        for (JsonElement entry : pack.getAsJsonArray("attributes")) {
            JsonObject attribute = entry.getAsJsonObject();
            attributes.put(attribute.get("name").getAsString(), new Attribute(
                attribute.has("type") ? attribute.get("type").getAsString() : null,
                attribute.has("default_value") ? attribute.get("default_value").getAsString() : ""));
        }
        try (Stream<Path> files = Files.list(MODULES)) {
            for (Path file : files.filter(path -> path.toString().endsWith(".yaml")).toList()) {
                Map<String, Object> module = new Yaml().load(Files.readString(file));
                if (module.get("attributeConfigs") instanceof List<?> configs) {
                    for (Object config : configs) {
                        Map<?, ?> attribute = (Map<?, ?>) config;
                        attributes.put(String.valueOf(attribute.get("name")), new Attribute(
                            attribute.get("type") == null ? null : String.valueOf(attribute.get("type")),
                            String.valueOf(attribute.get("value"))));
                    }
                }
            }
        }
        return attributes;
    }
}
