package com.g2806.radiante.client.option;

import static org.junit.jupiter.api.Assertions.*;

import java.util.Map;
import org.junit.jupiter.api.Test;

class EmissionOverridesTest {
    @Test
    void defaultAndTenfoldValuesRoundTripWithoutChangingOtherLights() {
        var values = Map.of("minecraft:block/redstone_block", 0, "minecraft:block/sea_lantern", 1000,
            "minecraft:block/glowstone", 100);
        var restored = EmissionOverrides.parse(EmissionOverrides.serialize(values));
        assertEquals(0, EmissionOverrides.percent(restored, "minecraft:block/redstone_block"));
        assertEquals(1000, EmissionOverrides.percent(restored, "minecraft:block/sea_lantern"));
        assertEquals(100, EmissionOverrides.percent(restored, "minecraft:block/glowstone"));
        assertEquals(100, EmissionOverrides.percent(restored, "minecraft:block/torch"));
        assertFalse(restored.containsKey("minecraft:block/glowstone"));
    }

    @Test
    void malformedEntriesDoNotDiscardValidOrUnavailablePackProfiles() {
        var values = EmissionOverrides.parse("""
            {"minecraft:block/redstone_block":25,"custom:block/lamp":700,
             "minecraft:block/torch":"oops","minecraft:block/lantern":12.5,"invalid":1}
            """);
        assertEquals(Map.of("minecraft:block/redstone_block", 25, "custom:block/lamp", 700), values);
        assertThrows(UnsupportedOperationException.class, () -> values.put("minecraft:block/glowstone", 0));
        assertEquals(Map.of(), EmissionOverrides.parse("not json"));
        assertEquals(Map.of(), EmissionOverrides.parse("[]"));
    }

    @Test
    void outOfRangeNumbersClampWithoutIntegerOverflow() {
        var values = EmissionOverrides.parse("""
            {"minecraft:block/redstone_block":-100,"minecraft:block/glowstone":999999999999999999999}
            """);
        assertEquals(0, values.get("minecraft:block/redstone_block"));
        assertEquals(1000, values.get("minecraft:block/glowstone"));
        assertEquals("{}", EmissionOverrides.serialize(Map.of()));
    }
}
