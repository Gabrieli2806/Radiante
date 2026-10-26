package com.g2806.radiante.client.addon;

import com.g2806.radiante.api.RadianteAddons;
import com.g2806.radiante.platform.RadiantePlatform;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Properties;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.regex.Pattern;
import net.minecraft.network.chat.Component;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * The addons registered through {@link RadianteAddons}, in the order they came, and the files their values are kept
 * in. Behind the public API: the settings screen lists what is here and hands back what the player applied.
 */
public final class AddonRegistry {

    static final Logger LOGGER = LoggerFactory.getLogger("Radiante/addons");
    /** What an addon id and a setting key may hold: they name a file and a line in it. */
    static final Pattern NAME = Pattern.compile("[a-z0-9_.-]{1,64}");

    private static final List<AddonImpl> ADDONS = new CopyOnWriteArrayList<>();

    private AddonRegistry() {
    }

    public static synchronized RadianteAddons.Addon register(String id, Component title) {
        Component shown = title != null ? title : Component.literal(String.valueOf(id));
        if (id == null || !NAME.matcher(id).matches()) {
            LOGGER.warn("Addon id '{}' is not valid (lowercase letters, digits, _ - . only); its settings are not shown",
                id);
            return new AddonImpl(String.valueOf(id), shown, null, false);
        }
        for (AddonImpl addon : ADDONS) {
            if (addon.id().equals(id)) {
                LOGGER.warn("Addon '{}' registered twice; the second registration is not shown", id);
                return new AddonImpl(id, shown, null, false);
            }
        }
        AddonImpl addon = new AddonImpl(id, shown, load(id), true);
        ADDONS.add(addon);
        LOGGER.info("Addon '{}' registered", id);
        return addon;
    }

    /** The addons shown in the settings, in the order they registered. */
    public static List<AddonImpl> addons() {
        return List.copyOf(ADDONS);
    }

    /**
     * Applies values the player chose: each changed addon's file is written, then its change listeners run. A failure
     * in one addon's file or listener is logged and the others go on.
     */
    public static void apply(Map<AddonImpl.SettingImpl<?>, Object> changes) {
        Map<AddonImpl, List<AddonImpl.SettingImpl<?>>> changed = new LinkedHashMap<>();
        for (Map.Entry<AddonImpl.SettingImpl<?>, Object> entry : changes.entrySet()) {
            AddonImpl.SettingImpl<?> setting = entry.getKey();
            if (setting.store(entry.getValue())) {
                changed.computeIfAbsent(setting.addon(), addon -> new ArrayList<>()).add(setting);
            }
        }
        for (Map.Entry<AddonImpl, List<AddonImpl.SettingImpl<?>>> entry : changed.entrySet()) {
            save(entry.getKey());
            for (AddonImpl.SettingImpl<?> setting : entry.getValue()) {
                setting.notifyListeners();
            }
        }
    }

    private static Path file(String id) {
        return RadiantePlatform.INSTANCE.gameDir().resolve("radiante").resolve("addons").resolve(id + ".properties");
    }

    /** The values stored for an addon; empty when there are none yet or the file cannot be read. */
    private static Properties load(String id) {
        Properties stored = new Properties();
        try {
            Path path = file(id);
            if (Files.isRegularFile(path)) {
                try (InputStream in = Files.newInputStream(path)) {
                    stored.load(in);
                }
            }
        } catch (IOException | RuntimeException | LinkageError e) {
            LOGGER.warn("Could not read the settings of addon '{}'; using its defaults", id, e);
        }
        return stored;
    }

    /**
     * Writes an addon's values, keeping lines for settings it no longer adds (an addon version that drops an option
     * and the next one that brings it back). Written beside the file and moved over it, so a crash leaves one whole.
     */
    private static void save(AddonImpl addon) {
        Properties out = new Properties();
        out.putAll(addon.stored());
        for (AddonImpl.SettingImpl<?> setting : addon.settings()) {
            out.setProperty(setting.key(), setting.format(setting.get()));
        }
        try {
            Path path = file(addon.id());
            Files.createDirectories(path.getParent());
            Path temp = path.resolveSibling(path.getFileName() + ".tmp");
            try (OutputStream stream = Files.newOutputStream(temp)) {
                out.store(stream, "Settings of the Radiante addon " + addon.id() + ", changed in Radiante's settings");
            }
            Files.move(temp, path, StandardCopyOption.REPLACE_EXISTING);
            addon.stored().clear();
            addon.stored().putAll(out);
        } catch (IOException | RuntimeException e) {
            LOGGER.warn("Could not save the settings of addon '{}'", addon.id(), e);
        }
    }
}
