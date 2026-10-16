package com.g2806.radiante.forge;

import com.g2806.radiante.platform.RadiantePlatform;
import java.nio.file.Path;
import net.minecraftforge.fml.loading.FMLPaths;

public final class ForgePlatform implements RadiantePlatform {

    @Override
    public String loaderName() {
        return "Forge";
    }

    @Override
    public String modVersion() {
        // Read from the mod's own metadata, which the build fills in; Forge's mod list API changes between versions.
        try (java.io.InputStream in = ForgePlatform.class.getResourceAsStream("/META-INF/mods.toml")) {
            if (in != null) {
                for (String line : new String(in.readAllBytes(), java.nio.charset.StandardCharsets.UTF_8).split("\n")) {
                    String trimmed = line.trim();
                    if (trimmed.startsWith("version") && trimmed.contains("\"")) {
                        return trimmed.substring(trimmed.indexOf('"') + 1, trimmed.lastIndexOf('"'));
                    }
                }
            }
        } catch (java.io.IOException e) {
            // Falls through.
        }
        return "unknown";
    }

    @Override
    public Path gameDir() {
        return FMLPaths.GAMEDIR.get();
    }
}
