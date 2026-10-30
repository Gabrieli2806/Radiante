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
    void newerApiVersionRefused() {
        ShaderPackManifest.Problem problem = check("{\"api_version\": 1}", "1.1.0");
        assertEquals("screen.radiante.shader_packs.problem.api_version", problem.key());
        assertEquals("1", problem.detail());
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
        // Features are not checked yet.
        assertNull(check("{\"requires\": {\"features\": [\"omm?\"]}}", "1.1.0"));
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
