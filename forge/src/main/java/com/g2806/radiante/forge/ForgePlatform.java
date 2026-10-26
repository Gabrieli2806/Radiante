package com.g2806.radiante.forge;

import com.g2806.radiante.platform.RadiantePlatform;
import java.io.IOException;
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
    public String modVersion(String modId) {
        // A LinkageError too: Forge's mod list API changes between versions, and a missing version only drops a label.
        try {
            return net.minecraftforge.fml.ModList.getModContainerById(modId)
                .map(mod -> mod.getModInfo().getVersion().toString()).orElse(null);
        } catch (RuntimeException | LinkageError e) {
            return null;
        }
    }

    @Override
    public byte[] modIcon(String modId) throws IOException {
        // As for the version, a LinkageError means no icon: Forge's mod list API changes between versions.
        java.nio.file.Path path;
        try {
            net.minecraftforge.forgespi.language.IModInfo info = net.minecraftforge.fml.ModList.getModContainerById(modId)
                .map(net.minecraftforge.fml.ModContainer::getModInfo).orElse(null);
            String logo = info == null ? null : info.getLogoFile().orElse(null);
            if (logo == null || logo.isBlank()) {
                return null;
            }
            path = info.getOwningFile().getFile().findResource(logo);
        } catch (LinkageError e) {
            return null;
        }
        return java.nio.file.Files.readAllBytes(path);
    }

    @Override
    public Path gameDir() {
        return FMLPaths.GAMEDIR.get();
    }
}
