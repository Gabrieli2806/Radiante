package com.g2806.radiante.client.option;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

class SettingsFileTest {

    @Test
    void writesAndReplacesWithoutLeavingTheTemporaryFile(@TempDir Path folder) throws IOException {
        Path file = folder.resolve("radiante/pipeline.yaml");
        SettingsFile.write(file, "first");
        SettingsFile.write(file, "second");
        assertEquals("second", Files.readString(file));
        assertFalse(Files.exists(file.resolveSibling("pipeline.yaml.tmp")));
    }

    @Test
    void keepsADamagedFileBesideItself(@TempDir Path folder) throws IOException {
        Path file = folder.resolve("pipeline.yaml");
        Files.writeString(file, "mode: [broken");
        Path kept = SettingsFile.keepDamaged(file);
        assertEquals(folder.resolve("pipeline.yaml.bak"), kept);
        assertEquals("mode: [broken", Files.readString(kept));
        // The file itself stays for the caller to replace.
        assertEquals("mode: [broken", Files.readString(file));
    }

    @Test
    void aMissingFileHasNothingToKeep(@TempDir Path folder) {
        assertNull(SettingsFile.keepDamaged(folder.resolve("missing.yaml")));
    }
}
