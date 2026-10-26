package com.g2806.radiante.fabric;

import com.g2806.radiante.client.render.EntityCollector;
import com.g2806.radiante.platform.RadiantePlatform;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import net.fabricmc.loader.api.FabricLoader;
import net.fabricmc.loader.api.ModContainer;

public final class FabricPlatform implements RadiantePlatform {

    @Override
    public String loaderName() {
        return "Fabric";
    }

    @Override
    public String modVersion() {
        return FabricLoader.getInstance().getModContainer("radiante")
            .map(mod -> mod.getMetadata().getVersion().getFriendlyString()).orElse("unknown");
    }

    @Override
    public String modVersion(String modId) {
        return FabricLoader.getInstance().getModContainer(modId)
            .map(mod -> mod.getMetadata().getVersion().getFriendlyString()).orElse(null);
    }

    @Override
    public byte[] modIcon(String modId) throws IOException {
        ModContainer mod = FabricLoader.getInstance().getModContainer(modId).orElse(null);
        if (mod == null) {
            return null;
        }
        // The size asked for picks among several icons; a single icon is returned whatever its size.
        String icon = mod.getMetadata().getIconPath(32).orElse(null);
        if (icon == null) {
            return null;
        }
        Path path = mod.findPath(icon).orElseThrow(() -> new IOException("icon " + icon + " is not in the mod"));
        return Files.readAllBytes(path);
    }

    @Override
    public Path gameDir() {
        return FabricLoader.getInstance().getGameDir();
    }

    @Override
    public EntityCollector createEntityCollector() {
        return new FabricEntityCollector();
    }
}
