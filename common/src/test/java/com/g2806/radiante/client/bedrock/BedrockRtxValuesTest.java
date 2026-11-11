package com.g2806.radiante.client.bedrock;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;

import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashMap;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import org.junit.jupiter.api.Test;

/**
 * The table of Bedrock RTX's values ({@link BedrockRtxValues}) says what the shipped defaults say: a default changed
 * in radiante-pt's configs.json or in the tone mapping module without the table, or the other way round, fails here.
 */
class BedrockRtxValuesTest {

    private static final String TONE_MODULE = "render_pipeline.module.tone_mapping.name";

    @Test
    void tableMatchesShippedDefaults() throws IOException {
        Map<String, String> pack = new HashMap<>();
        JsonObject configs = JsonParser.parseString(Files.readString(Path.of(
            "../native/src/shader/world/ray_tracing/internal/radiante-pt/configs.json"))).getAsJsonObject();
        collectDefaults(configs, pack);
        Map<String, String> tone = new HashMap<>();
        Matcher m = Pattern.compile("- name: \"([^\"]+)\"\\s*\\n\\s*type: \"[^\"]*\"\\s*\\n\\s*value: \"([^\"]*)\"")
            .matcher(Files.readString(Path.of("src/main/resources/modules/tone_mapping.yaml")));
        while (m.find()) {
            tone.put(m.group(1), m.group(2));
        }
        for (CompatParameter parameter : BedrockRtxValues.PARAMETERS) {
            Map<String, String> defaults = parameter.module() == null ? pack : tone;
            if (parameter.module() != null) {
                assertEquals(TONE_MODULE, parameter.module(), parameter.attribute());
            }
            String declared = defaults.get(parameter.attribute());
            assertNotNull(declared, parameter.attribute() + " has no default");
            assertEquals(number(declared), number(parameter.value()), parameter.attribute());
        }
    }

    private static Object number(String text) {
        try {
            return Double.parseDouble(text);
        } catch (NumberFormatException notANumber) {
            return text;
        }
    }

    private static void collectDefaults(JsonElement element, Map<String, String> out) {
        if (element.isJsonObject()) {
            JsonObject object = element.getAsJsonObject();
            if (object.has("name") && object.has("default_value")) {
                out.put(object.get("name").getAsString(), object.get("default_value").getAsString());
            }
            for (Map.Entry<String, JsonElement> entry : object.entrySet()) {
                collectDefaults(entry.getValue(), out);
            }
        } else if (element.isJsonArray()) {
            element.getAsJsonArray().forEach(child -> collectDefaults(child, out));
        }
    }
}
