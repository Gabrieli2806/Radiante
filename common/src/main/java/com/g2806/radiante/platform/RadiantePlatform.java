package com.g2806.radiante.platform;

import com.g2806.radiante.client.render.EntityCollector;
import java.nio.file.Path;
import java.util.ServiceLoader;

/**
 * The few things Radiante needs from the mod loader it runs on. Everything else is plain Minecraft and lives in the
 * common module; each loader module provides one implementation, registered under META-INF/services.
 */
public interface RadiantePlatform {

    RadiantePlatform INSTANCE = load();

    /** Loader name for logs: "Fabric", "NeoForge" or "Forge". */
    String loaderName();

    /** Radiante's own version, as the loader read it from the mod's metadata; "unknown" when it cannot tell. */
    String modVersion();

    /** Another mod's version, for the heading of an addon in the settings; null when it cannot tell. */
    default String modVersion(String modId) {
        return null;
    }

    /** The game directory; Radiante keeps its native files and settings in a "radiante" folder inside it. */
    Path gameDir();

    /**
     * The collector entity and block submissions are expanded into. A loader that adds submission methods of its
     * own (Fabric's rendering API submits meshes) returns a subclass that handles them.
     */
    default EntityCollector createEntityCollector() {
        return new EntityCollector();
    }

    private static RadiantePlatform load() {
        return ServiceLoader.load(RadiantePlatform.class, RadiantePlatform.class.getClassLoader()).findFirst()
            .orElseThrow(() -> new IllegalStateException("No RadiantePlatform implementation on the classpath"));
    }
}
