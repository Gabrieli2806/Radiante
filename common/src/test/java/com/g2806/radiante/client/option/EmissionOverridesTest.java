package com.g2806.radiante.client.option;

import static org.junit.jupiter.api.Assertions.*;
import java.util.Map;
import org.junit.jupiter.api.Test;

class EmissionOverridesTest {
    @Test void oldPercentageSettingsMigrateWithoutChangingBrightness() {
        var values = EmissionOverrides.parse("{\"minecraft:block/torch\":1000,\"minecraft:block/glowstone\":100,\"minecraft:block/redstone_block\":0}");
        assertEquals(new EmissionColor(10, 10, 10), values.get("minecraft:block/torch"));
        assertEquals(EmissionColor.BLACK, values.get("minecraft:block/redstone_block"));
        assertFalse(values.containsKey("minecraft:block/glowstone"));
        assertEquals(values, EmissionOverrides.parse(EmissionOverrides.serialize(values)));
    }
    @Test void hdrColoursAndUnavailablePackProfilesRoundTrip() {
        var values = Map.of("custom:block/lamp", new EmissionColor(0, 2.5f, 64), "minecraft:block/glowstone", EmissionColor.WHITE);
        var parsed = EmissionOverrides.parse(EmissionOverrides.serialize(values));
        assertEquals(values.get("custom:block/lamp"), parsed.get("custom:block/lamp"));
        assertEquals(EmissionColor.WHITE, EmissionOverrides.color(parsed, "minecraft:block/sea_lantern"));
        assertThrows(UnsupportedOperationException.class, () -> parsed.clear());
    }
    @Test void malformedChannelsDoNotDiscardOtherLights() {
        var parsed = EmissionOverrides.parse("""
            {"minecraft:block/torch":[0.9,0.95,1],"custom:block/huge":[-1,999999,2],
             "minecraft:block/lantern":[1,"bad",3],"bad":20,"custom:block/fraction":12.5}
            """);
        assertEquals(2, parsed.size());
        assertEquals(new EmissionColor(0, EmissionColor.MAX, 2), parsed.get("custom:block/huge"));
        assertEquals(Map.of(), EmissionOverrides.parse("[]"));
        assertEquals(Map.of(), EmissionOverrides.parse("not json"));
        assertThrows(IllegalArgumentException.class, () -> new EmissionColor(Float.NaN, 0, 1));
    }
    @Test void rgb9e5MatchesStandardWordsAndRetainsHdrColour() {
        assertEquals(0x84020100L, EmissionColor.WHITE.packed());
        assertEquals(0xFFFFFFFFL, new EmissionColor(65408,65408,65408).packed());
        assertEquals(EmissionColor.BLACK, EmissionColor.unpack(0));
        for (float gain : new float[] {0.0001f, 0.1f, 1, 10, 256, 10000}) {
            var colour = new EmissionColor(0.9f * gain, 0.95f * gain, gain);
            var restored = EmissionColor.unpack(colour.packed());
            float tolerance = Math.max(6e-8f, gain / 255);
            assertEquals(colour.red(), restored.red(), tolerance);
            assertEquals(colour.green(), restored.green(), tolerance);
            assertEquals(colour.blue(), restored.blue(), tolerance);
        }
    }
}
