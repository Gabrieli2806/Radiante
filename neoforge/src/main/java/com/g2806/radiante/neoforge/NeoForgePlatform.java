package com.g2806.radiante.neoforge;

import com.g2806.radiante.platform.RadiantePlatform;
import java.nio.file.Path;
import net.neoforged.fml.loading.FMLPaths;

public final class NeoForgePlatform implements RadiantePlatform {

    @Override
    public String loaderName() {
        return "NeoForge";
    }

    @Override
    public String modVersion() {
        try {
            return net.neoforged.fml.ModList.get().getModContainerById("radiante")
                .map(mod -> mod.getModInfo().getVersion().toString()).orElse("unknown");
        } catch (RuntimeException e) {
            return "unknown";
        }
    }

    @Override
    public String modVersion(String modId) {
        try {
            return net.neoforged.fml.ModList.get().getModContainerById(modId)
                .map(mod -> mod.getModInfo().getVersion().toString()).orElse(null);
        } catch (RuntimeException e) {
            return null;
        }
    }

    @Override
    public Path gameDir() {
        return FMLPaths.GAMEDIR.get();
    }
}
