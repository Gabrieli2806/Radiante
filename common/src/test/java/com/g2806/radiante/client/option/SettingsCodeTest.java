package com.g2806.radiante.client.option;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;

import com.google.gson.JsonObject;
import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Base64;
import java.util.zip.Deflater;
import org.junit.jupiter.api.Test;

/** Share codes: what one may carry, and what is skipped rather than refused. */
class SettingsCodeTest {

    /** A code from another version still imports; a setting this version does not know is skipped. */
    @Test
    void codeWithAnUnknownSettingImports() {
        String json = "{\"o\":{\"settingOfAnotherVersion\":\"X\",\"biomeAmbiance\":\"true\",\"starAltitude\":\"120\"}}";
        SettingsCode.Parsed parsed = SettingsCode.parse(code(json));
        assertNotNull(parsed);
        assertEquals(2, parsed.count());
        assertEquals(1, parsed.skipped());
    }

    /** The sun path switch of older versions is still taken from their codes. */
    @Test
    void codeWithTheOldSunPathSwitchImports() {
        SettingsCode.Parsed parsed = SettingsCode.parse(code("{\"o\":{\"vanillaSunPath\":\"false\"}}"));
        assertNotNull(parsed);
        assertEquals(1, parsed.count());
        assertEquals(0, parsed.skipped());
    }

    @Test
    void codeAcceptsEmissionOverridesAndRejectsNonObjects() {
        JsonObject root = new JsonObject();
        JsonObject options = new JsonObject();
        options.addProperty("blockEmissionOverrides", "{\"minecraft:block/redstone_block\":25}");
        root.add("o", options);
        SettingsCode.Parsed parsed = SettingsCode.parse(code(root.toString()));
        assertNotNull(parsed);
        assertEquals(1, parsed.count());
        assertEquals(0, parsed.skipped());
        parsed = SettingsCode.parse(code("{\"o\":{\"blockEmissionOverrides\":\"[]\",\"biomeAmbiance\":\"true\"}}"));
        assertNotNull(parsed);
        assertEquals(1, parsed.count());
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
