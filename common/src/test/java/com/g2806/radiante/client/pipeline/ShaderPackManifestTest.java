package com.g2806.radiante.client.pipeline;

import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

class ShaderPackManifestTest {

    private static ShaderPackManifest.Problem check(String manifest, String modVersion) {
        return ShaderPackManifest.check(JsonParser.parseString(manifest).getAsJsonObject(), modVersion);
    }

    @Test
    void packsWithoutTheNewFieldsPass() {
        assertNull(check("{\"display_name\": \"Old\"}", "1.1.0"));
        assertNull(ShaderPackManifest.check(null, "1.1.0"));
        assertNull(check("{\"api_version\": 0}", "1.1.0"));
    }

    @Test
    void extendsRadiantePt() {
        assertNull(check("{\"extends\": \"radiante-pt\"}", "1.1.0"));
        assertNull(check("{\"extends\": \"Radiante-PT@1\"}", "1.1.0"));
        assertEquals("screen.radiante.shader_packs.problem.base_version",
            check("{\"extends\": \"radiante-pt@2\"}", "1.1.0").key());
        ShaderPackManifest.Problem unknown = check("{\"extends\": \"other-pack\"}", "1.1.0");
        assertEquals("screen.radiante.shader_packs.problem.base", unknown.key());
        assertEquals("other-pack", unknown.detail());
        assertEquals("screen.radiante.shader_packs.problem.base", check("{\"extends\": \"radiante-pt@x\"}", "1").key());
        assertEquals("screen.radiante.shader_packs.problem.base", check("{\"extends\": 3}", "1").key());
    }

    @Test
    void knownFeaturesPassAndUnknownOnesAreNamed() {
        assertNull(check("{\"requires\": {\"features\": [\"noise\", \"Player_State\", \"named_resources\"]}}",
            "1.1.0"));
        ShaderPackManifest.Problem problem =
            check("{\"requires\": {\"features\": [\"noise\", \"block_ids\"]}}", "1.1.0");
        assertEquals("screen.radiante.shader_packs.problem.feature", problem.key());
        assertEquals("block_ids", problem.detail());
        // Not a list: nothing to check.
        assertNull(check("{\"requires\": {\"features\": \"noise\"}}", "1.1.0"));
    }

    @Test
    void optionalFeaturesNeverRefuse() {
        assertNull(check("{\"requires\": {\"features\": [\"glass_frost?\", \"block_ids?\"]}}", "1.1.0"));
        assertEquals("block_ids",
            check("{\"requires\": {\"features\": [\"omm?\", \"block_ids\"]}}", "1.1.0").detail());
    }

    /** The loader defines RADIANTE_FEATURE_<NAME> from its own list: the two must name the same features. */
    @Test
    void featuresMatchTheNativeList() throws java.io.IOException {
        java.nio.file.Path header = java.nio.file.Path.of("..", "native", "src", "prism", "render", "modules", "world",
            "shader_pack", "shader_pack_features.hpp");
        String text = java.nio.file.Files.readString(header);
        String body = text.substring(text.indexOf("kShaderPackFeatures"));
        body = body.substring(body.indexOf('{') + 1, body.indexOf("};"));
        java.util.Set<String> nativeFeatures = new java.util.HashSet<>();
        java.util.regex.Matcher m = java.util.regex.Pattern.compile("\"([a-z_]+)\"").matcher(body);
        while (m.find()) {
            nativeFeatures.add(m.group(1));
        }
        assertEquals(ShaderPackManifest.FEATURES, nativeFeatures);
    }

    @Test
    void newerApiVersionRefused() {
        assertNull(check("{\"api_version\": 1}", "1.1.0"));
        ShaderPackManifest.Problem problem = check("{\"api_version\": 2}", "1.1.0");
        assertEquals("screen.radiante.shader_packs.problem.api_version", problem.key());
        assertEquals("2", problem.detail());
        assertNull(check("{\"api_version\": \"zero\"}", "1.1.0"));
    }

    @Test
    void requiresRadianteVersion() {
        assertNull(check("{\"requires\": {\"radiante\": \">=1.1\"}}", "1.1.0"));
        assertNull(check("{\"requires\": {\"radiante\": \"1.0\"}}", "1.1.0+26.3"));
        assertNull(check("{\"requires\": {\"radiante\": \"<2\"}}", "1.1.0"));
        assertEquals("screen.radiante.shader_packs.problem.requires",
            check("{\"requires\": {\"radiante\": \">=1.10\"}}", "1.9.0").key());
        assertEquals("screen.radiante.shader_packs.problem.requires",
            check("{\"requires\": {\"radiante\": \"=1.2\"}}", "1.1.0").key());
        assertEquals("screen.radiante.shader_packs.problem.requires",
            check("{\"requires\": {\"radiante\": \"soon\"}}", "1.1.0").key());
        // A feature this Radiante lacks is named before the version is looked at.
        assertEquals("screen.radiante.shader_packs.problem.feature",
            check("{\"requires\": {\"radiante\": \">=1.1\", \"features\": [\"omm\"]}}", "1.1.0").key());
    }

    @Test
    void versionsCompareByNumber() {
        assertTrue(ShaderPackManifest.compareVersions("1.10", "1.9") > 0);
        assertEquals(0, ShaderPackManifest.compareVersions("1.1", "1.1.0"));
        assertTrue(ShaderPackManifest.compareVersions("1.1.0-beta", "1.2") < 0);
    }

    @Test
    void checksInOrder() {
        JsonObject manifest = JsonParser.parseString(
            "{\"extends\": \"nope\", \"api_version\": 3, \"requires\": {\"radiante\": \">=9\"}}").getAsJsonObject();
        assertEquals("screen.radiante.shader_packs.problem.base", ShaderPackManifest.check(manifest, "1.1.0").key());
    }
}
