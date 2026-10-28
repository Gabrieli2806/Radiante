package com.g2806.radiante.client.pipeline;

import com.google.gson.JsonParser;
import org.junit.jupiter.api.Test;

import java.util.List;
import java.util.Map;
import java.util.Set;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertTrue;

class ShaderPackSettingsTest {

    private static final String P = ShaderPackSettings.ATTRIBUTE_PREFIX;
    private static final Set<String> BASE = Set.of(P + "cloud_mode", P + "bedrock_original");

    private static ShaderPackSettings parse(String json) {
        return ShaderPackSettings.parse(JsonParser.parseString(json).getAsJsonObject(), BASE);
    }

    private static String attribute(String name, String type, String extra) {
        return "{\"name\": \"" + P + name + "\", \"type\": \"" + type + "\", \"default_value\": \"0\""
            + (extra.isEmpty() ? "" : ", " + extra) + "}";
    }

    @Test
    void packsWithoutTheKeysGetPlainRows() {
        ShaderPackSettings settings = parse("{\"attributes\": [" + attribute("a", "bool", "") + "]}");
        ShaderPackSettings.Attribute a = settings.attributes().get(0);
        assertEquals(P + "a", a.name());
        assertNull(a.group());
        assertFalse(a.advanced());
        assertNull(a.description());
        assertNull(a.impact());
        assertNull(a.step());
        assertNull(a.visibleWhen());
        assertTrue(settings.presets().isEmpty());
        assertTrue(settings.warnings().isEmpty());
        assertEquals(List.of(), settings.groups());
        assertSame(ShaderPackSettings.NONE, ShaderPackSettings.parse(null, BASE));
    }

    @Test
    void onlyThePacksOwnAttributes() {
        ShaderPackSettings settings = parse("{\"attributes\": ["
            + attribute("mine", "bool", "") + ","
            // Radiante PT's attribute with a new default, and a new type: still Radiante PT's.
            + "{\"name\": \"" + P + "cloud_mode\", \"default_value\": \"x\"},"
            + attribute("bedrock_original", "bool", "\"group\": \"g\"") + ","
            // No type: not something the renderer would load as a new attribute.
            + "{\"name\": \"" + P + "untyped\", \"default_value\": \"1\"}]}");
        assertEquals(1, settings.attributes().size());
        assertTrue(settings.isOwn(P + "mine"));
        assertFalse(settings.isOwn(P + "cloud_mode"));
    }

    @Test
    void metadataKeys() {
        ShaderPackSettings settings = parse("{\"attributes\": [" + attribute("x", "float_range:0.0-2.0",
            "\"group\": \"pack.group.sky\", \"order\": 3, \"advanced\": true, \"description\": \"pack.x.desc\","
                + " \"performance_impact\": \"High\", \"step\": 0.25,"
                + " \"visible_when\": {\"attribute\": \"style\", \"equals\": [1, 2]}") + "]}");
        ShaderPackSettings.Attribute x = settings.attributes().get(0);
        assertEquals("pack.group.sky", x.group());
        assertEquals(3.0, x.order());
        assertTrue(x.advanced());
        assertEquals("pack.x.desc", x.description());
        assertEquals(ShaderPackSettings.Impact.HIGH, x.impact());
        assertEquals(0.25, x.step());
        assertEquals(P + "style", x.visibleWhen().attribute());
        assertEquals(List.of("1", "2"), x.visibleWhen().values());
        assertFalse(x.visibleWhen().negated());
        assertTrue(settings.warnings().isEmpty());
    }

    @Test
    void badValuesAreDroppedNotThePack() {
        ShaderPackSettings settings = parse("{\"attributes\": [" + attribute("x", "bool",
            "\"order\": \"first\", \"advanced\": \"yes\", \"performance_impact\": \"huge\", \"step\": 1,"
                + " \"group\": 4, \"visible_when\": {\"equals\": 1}") + "],"
            + " \"presets\": {\"a\": {}}, \"profile_keys\": \"*\"}");
        ShaderPackSettings.Attribute x = settings.attributes().get(0);
        assertEquals(0.0, x.order());
        assertFalse(x.advanced());
        assertNull(x.impact());
        // A step only makes sense on a range.
        assertNull(x.step());
        assertNull(x.group());
        assertNull(x.visibleWhen());
        assertTrue(settings.presets().isEmpty());
        assertFalse(settings.profileKey(P + "cloud_mode"));
        assertEquals(8, settings.warnings().size(), settings.warnings().toString());
    }

    @Test
    void negativeOrZeroStepRefused() {
        assertNull(parse("{\"attributes\": [" + attribute("x", "int_range:0-8", "\"step\": 0") + "]}")
            .attributes().get(0).step());
        assertNull(parse("{\"attributes\": [" + attribute("x", "int_range:0-8", "\"step\": -2") + "]}")
            .attributes().get(0).step());
    }

    @Test
    void orderThenPlaceInFile() {
        ShaderPackSettings settings = parse("{\"attributes\": ["
            + attribute("a", "bool", "\"group\": \"two\", \"order\": 2") + ","
            + attribute("b", "bool", "\"group\": \"one\"") + ","
            + attribute("c", "bool", "\"group\": \"two\"") + ","
            + attribute("d", "bool", "\"order\": -1") + "]}");
        assertEquals(List.of(P + "d", P + "b", P + "c", P + "a"),
            settings.attributes().stream().map(ShaderPackSettings.Attribute::name).toList());
        assertEquals(List.of("one", "two"), settings.groups());
    }

    @Test
    void visibleWhen() {
        ShaderPackSettings settings = parse("{\"attributes\": ["
            + attribute("a", "bool", "\"visible_when\": {\"attribute\": \"" + P + "flag\", \"equals\": true}") + ","
            + attribute("b", "bool", "\"visible_when\": {\"attribute\": \"style\", \"not_equals\": 0}") + ","
            + attribute("c", "bool", "\"visible_when\": {\"attribute\": \"mode\", \"equals\": \"off\"}") + "]}");
        ShaderPackSettings.VisibleWhen a = settings.attributes().get(0).visibleWhen();
        assertTrue(a.test("bool", "render_pipeline.true"));
        assertFalse(a.test("bool", "render_pipeline.false"));
        ShaderPackSettings.VisibleWhen b = settings.attributes().get(1).visibleWhen();
        assertTrue(b.negated());
        assertFalse(b.test("int_range:0-2", "0"));
        assertTrue(b.test("int_range:0-2", "2"));
        assertFalse(b.test("float_range:0.0-2.0", "0.0"));
        // An enum value by the part after the attribute's name.
        ShaderPackSettings.VisibleWhen c = settings.attributes().get(2).visibleWhen();
        String type = "enum:" + P + "mode.on-" + P + "mode.off";
        assertTrue(c.test(type, P + "mode.off"));
        assertFalse(c.test(type, P + "mode.on"));
    }

    @Test
    void presets() {
        ShaderPackSettings settings = parse("{\"attributes\": ["
            + attribute("sky", "float_range:0.0-1.0", "") + "," + attribute("style", "int_range:0-2", "") + "],"
            + " \"presets\": [{\"name\": \"pack.preset.soft\", \"description\": \"pack.preset.soft.desc\","
            + "   \"values\": {\"sky\": 0.5, \"" + P + "style\": 2, \"cloud_mode\": \"off\", \"gone\": 1}},"
            + "  {\"name\": \"empty\", \"values\": {}}, {\"values\": {\"sky\": 1}}]}");
        assertEquals(1, settings.presets().size());
        ShaderPackSettings.Preset soft = settings.presets().get(0);
        assertEquals("pack.preset.soft", soft.name());
        assertEquals("pack.preset.soft.desc", soft.description());
        // Only the pack's own settings; Radiante PT's and unknown names are dropped with a warning.
        assertEquals(Map.of(P + "sky", "0.5", P + "style", "2"), soft.values());
        assertEquals(4, settings.warnings().size(), settings.warnings().toString());
    }

    @Test
    void profileKeys() {
        ShaderPackSettings none = parse("{\"attributes\": []}");
        assertFalse(none.profileKey(P + "bedrock_original"));
        ShaderPackSettings some = parse("{\"profile_keys\": [\"bedrock_original\", \"" + P + "sun_glow\", 3]}");
        assertTrue(some.profileKey(P + "bedrock_original"));
        assertTrue(some.profileKey(P + "sun_glow"));
        assertFalse(some.profileKey(P + "cloud_mode"));
        assertEquals(1, some.warnings().size());
        assertTrue(parse("{\"profile_keys\": [\"*\"]}").profileKey(P + "anything"));
    }

    @Test
    void normalize() {
        assertEquals("render_pipeline.true", ShaderPackSettings.normalize("bool", P + "a", "true"));
        assertEquals("render_pipeline.false", ShaderPackSettings.normalize("bool", P + "a", "render_pipeline.false"));
        assertNull(ShaderPackSettings.normalize("bool", P + "a", "1"));
        assertEquals("2", ShaderPackSettings.normalize("int_range:0-4", P + "a", "2.0"));
        assertNull(ShaderPackSettings.normalize("int_range:0-4", P + "a", "2.5"));
        assertNull(ShaderPackSettings.normalize("int_range:0-4", P + "a", "5"));
        assertEquals("1.0", ShaderPackSettings.normalize("float_range:0.0-2.0", P + "a", "1"));
        assertEquals("0.25", ShaderPackSettings.normalize("float_range:0.0-2.0", P + "a", "0.25"));
        assertNull(ShaderPackSettings.normalize("float_range:0.0-2.0", P + "a", "nope"));
        String type = "enum:" + P + "mode.on-" + P + "mode.off";
        assertEquals(P + "mode.off", ShaderPackSettings.normalize(type, P + "mode", "off"));
        assertEquals(P + "mode.on", ShaderPackSettings.normalize(type, P + "mode", P + "mode.on"));
        assertNull(ShaderPackSettings.normalize(type, P + "mode", "sideways"));
        assertEquals("1,2,3", ShaderPackSettings.normalize("vec3", P + "a", "1,2,3"));
        assertNull(ShaderPackSettings.normalize("vec3", P + "a", "1,2"));
    }

    @Test
    void sameValue() {
        assertTrue(ShaderPackSettings.sameValue("float_range:0.0-1.0", P + "a", "0.5", "0.50"));
        assertTrue(ShaderPackSettings.sameValue("int_range:0-4", P + "a", "2", "2"));
        assertFalse(ShaderPackSettings.sameValue("int_range:0-4", P + "a", "2", "3"));
        assertTrue(ShaderPackSettings.sameValue("bool", P + "a", "true", "render_pipeline.true"));
        assertFalse(ShaderPackSettings.sameValue("bool", P + "a", "true", null));
    }

    /**
     * The example packs: their keys read without a warning, and every name they use (settings, groups, descriptions,
     * presets) is in all three of their lang files.
     */
    @Test
    void examplePacks() throws java.io.IOException {
        java.nio.file.Path repository = java.nio.file.Path.of("").toAbsolutePath().getParent();
        java.nio.file.Path base = repository.resolve(
            "native/src/shader/world/ray_tracing/internal/radiante-pt/configs.json");
        Set<String> baseNames = new java.util.HashSet<>();
        for (var attribute : JsonParser.parseString(java.nio.file.Files.readString(base)).getAsJsonObject()
            .getAsJsonArray("attributes")) {
            baseNames.add(attribute.getAsJsonObject().get("name").getAsString());
        }
        int checked = 0;
        try (var packs = java.nio.file.Files.list(repository.resolve("examples/shaderpacks"))) {
            for (java.nio.file.Path pack : packs.filter(java.nio.file.Files::isDirectory).toList()) {
                var root = JsonParser.parseString(java.nio.file.Files.readString(pack.resolve("configs.json")))
                    .getAsJsonObject();
                ShaderPackSettings settings = ShaderPackSettings.parse(root, baseNames);
                assertTrue(settings.warnings().isEmpty(), pack + ": " + settings.warnings());
                Set<String> keys = new java.util.HashSet<>();
                for (ShaderPackSettings.Attribute attribute : settings.attributes()) {
                    keys.add(attribute.name());
                    if (attribute.group() != null) {
                        keys.add(attribute.group());
                    }
                    if (attribute.description() != null) {
                        keys.add(attribute.description());
                    }
                }
                for (ShaderPackSettings.Preset preset : settings.presets()) {
                    keys.add(preset.name());
                    if (preset.description() != null) {
                        keys.add(preset.description());
                    }
                }
                for (String language : List.of("en_us", "es_es", "zh_cn")) {
                    Set<String> defined = new java.util.HashSet<>();
                    for (String line : java.nio.file.Files.readAllLines(
                        pack.resolve("lang/" + language + ".lang"), java.nio.charset.StandardCharsets.UTF_8)) {
                        int at = line.indexOf('=');
                        if (!line.startsWith("#") && at > 0) {
                            defined.add(line.substring(0, at).trim());
                        }
                    }
                    for (String key : keys) {
                        assertTrue(defined.contains(key), pack.getFileName() + "/" + language + " lacks " + key);
                    }
                }
                checked++;
            }
        }
        assertTrue(checked >= 4);
    }

    @Test
    void shortAndFullNames() {
        assertEquals(P + "sky", ShaderPackSettings.fullName("sky"));
        assertEquals(P + "sky", ShaderPackSettings.fullName(" " + P + "sky "));
        assertEquals("render_pipeline.module.tone_mapping.attribute.contrast",
            ShaderPackSettings.fullName("render_pipeline.module.tone_mapping.attribute.contrast"));
    }
}
