package com.g2806.radiante.client.pipeline;

import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

class ShaderPackValidationTest {

    @Test
    void packThatCompiles() {
        ShaderPackValidation result = ShaderPackValidation.parse("shaderpacks/mine",
            "{\"ok\": true, \"error\": \"\", \"errors\": [], \"shaders\": 45, \"compiled\": 3}");
        assertTrue(result.ok());
        assertEquals(45, result.shaders());
        assertEquals(3, result.compiled());
        assertTrue(result.reportLines().isEmpty());
    }

    @Test
    void loaderErrorAndCompilerLinesAreReported() {
        ShaderPackValidation result = ShaderPackValidation.parse("shaderpacks/mine",
            "{\"ok\": false, \"error\": \"extends: unknown base pack\", \"errors\": "
                + "[\"hooks/sky.glsl: ERROR: hooks/sky.glsl:4: 'x' : undeclared identifier\\nERROR: 1 compilation errors\"],"
                + " \"shaders\": 0, \"compiled\": 0}");
        assertFalse(result.ok());
        assertEquals(2, result.errors().size());
        assertEquals(List.of("extends: unknown base pack", "hooks/sky.glsl: ERROR: hooks/sky.glsl:4: 'x' : undeclared identifier",
            "ERROR: 1 compilation errors"), result.reportLines());
    }

    @Test
    void anErrorMakesTheResultFailEvenIfOkIsSet() {
        assertFalse(ShaderPackValidation.parse("p", "{\"ok\": true, \"error\": \"broken\"}").ok());
        assertFalse(ShaderPackValidation.parse("p", "").ok());
    }
}
