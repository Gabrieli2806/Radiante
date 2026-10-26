package com.g2806.radiante.neoforge;

import com.g2806.radiante.platform.RadiantePlatform;
import java.io.IOException;
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
    public byte[] modIcon(String modId) throws IOException {
        net.neoforged.neoforgespi.language.IModInfo info = net.neoforged.fml.ModList.get().getModContainerById(modId)
            .map(net.neoforged.fml.ModContainer::getModInfo).orElse(null);
        if (info == null) {
            return null;
        }
        String logo = info.getLogoFile().orElse(null);
        if (logo == null || logo.isBlank()) {
            return null;
        }
        byte[] bytes = info.getOwningFile().getFile().getContents().readFile(logo);
        if (bytes == null) {
            throw new IOException("logo " + logo + " is not in the mod");
        }
        return bytes;
    }

    @Override
    public Path gameDir() {
        return FMLPaths.GAMEDIR.get();
    }
}
