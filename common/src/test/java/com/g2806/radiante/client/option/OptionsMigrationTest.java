package com.g2806.radiante.client.option;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.g2806.radiante.client.bedrock.BedrockRtxValues;
import com.g2806.radiante.client.bedrock.CompatParameter;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Base64;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.zip.Deflater;
import org.junit.jupiter.api.Test;

/** Settings from the versions with renderer profiles, and the defaults that took their place. */
class OptionsMigrationTest {

    private static final String TONE_MODULE = "render_pipeline.module.tone_mapping.name";

    @Test
    void enhancedOrMissingProfileChangesNothing() {
        assertTrue(OptionsMigration.settingsToReset(null).isEmpty());
        assertTrue(OptionsMigration.settingsToReset("").isEmpty());
        assertTrue(OptionsMigration.settingsToReset("BEDROCK_RTX_ENHANCED").isEmpty());
        // Profiles removed before this version read as Enhanced, as they did then.
        assertTrue(OptionsMigration.settingsToReset("BEDROCK_RTX").isEmpty());
        assertTrue(OptionsMigration.settingsToReset("RADIANTE").isEmpty());
    }

    @Test
    void realisticResetsEverythingItSet() {
        List<CompatParameter> reset = OptionsMigration.settingsToReset(" realistic ");
        assertEquals(BedrockRtxValues.PARAMETERS.size() + OptionsMigration.REALISTIC_ONLY.size(), reset.size());
        Map<String, String> values = new HashMap<>();
        for (CompatParameter parameter : reset) {
            values.put(parameter.attribute(), parameter.value());
        }
        // Every row the Realistic profile wrote is put back to the default.
        assertEquals("1.5", values.get("render_pipeline.module.ray_tracing.attribute.bounce_light_boost"));
        assertEquals("render_pipeline.module.ray_tracing.attribute.water_surface_mode.bedrock",
            values.get("render_pipeline.module.ray_tracing.attribute.water_surface_mode"));
        assertEquals("1.0", values.get("render_pipeline.module.tone_mapping.attribute.contrast"));
        assertEquals("1.0", values.get("render_pipeline.module.tone_mapping.attribute.saturation"));
        assertEquals("0.01", values.get("render_pipeline.module.tone_mapping.attribute.bloom_intensity"));
        assertEquals("8.0", values.get("render_pipeline.module.tone_mapping.attribute.exposure_up_speed"));
    }

    @Test
    void customResetsTheBedrockValuesOnly() {
        List<CompatParameter> reset = OptionsMigration.settingsToReset("CUSTOM");
        assertEquals(BedrockRtxValues.PARAMETERS, reset);
    }

    /** The table the migration writes says what the shipped defaults say, so a fresh install and a migrated one match. */
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
        List<CompatParameter> all = new ArrayList<>(BedrockRtxValues.PARAMETERS);
        all.addAll(OptionsMigration.REALISTIC_ONLY);
        for (CompatParameter parameter : all) {
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

    /** A share code from a version with profiles still imports; the profile in it is skipped. */
    @Test
    void shareCodeWithProfileKeyImports() {
        String json = "{\"o\":{\"rendererProfile\":\"REALISTIC\",\"biomeAmbiance\":\"true\",\"starAltitude\":\"120\"}}";
        SettingsCode.Parsed parsed = SettingsCode.parse(code(json));
        assertNotNull(parsed);
        assertEquals(2, parsed.count());
        assertEquals(1, parsed.skipped());
    }

    private static String code(String json) {
        Deflater deflater = new Deflater(Deflater.BEST_COMPRESSION, true);
        deflater.setInput(json.getBytes(StandardCharsets.UTF_8));
        deflater.finish();
        ByteArrayOutputStream packed = new ByteArrayOutputStream();
        byte[] buffer = new byte[4096];
        while (!deflater.finished()) {
            packed.write(buffer, 0, deflater.deflate(buffer));
        }
        deflater.end();
        return "RADIANTE1:" + Base64.getUrlEncoder().withoutPadding().encodeToString(packed.toByteArray());
    }
}
