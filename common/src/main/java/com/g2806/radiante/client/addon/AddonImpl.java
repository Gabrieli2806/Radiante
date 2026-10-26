package com.g2806.radiante.client.addon;

import com.g2806.radiante.api.RadianteAddons;
import com.g2806.radiante.platform.RadiantePlatform;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.Properties;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.function.Consumer;
import java.util.function.Function;
import net.minecraft.network.chat.Component;
import net.minecraft.resources.Identifier;

/**
 * An addon and its settings. One that could not be registered (a bad or repeated id) is not {@code shown}: its
 * settings still answer with their defaults, so the addon's own code goes on working, but nothing is listed or saved.
 */
public final class AddonImpl implements RadianteAddons.Addon {

    private final String id;
    private final Component title;
    private final Properties stored;
    private final boolean shown;
    private final List<SettingImpl<?>> settings = new CopyOnWriteArrayList<>();
    private volatile String version;
    private volatile Identifier icon;

    AddonImpl(String id, Component title, Properties stored, boolean shown) {
        this.id = id;
        this.title = title;
        this.stored = stored != null ? stored : new Properties();
        this.shown = shown;
    }

    @Override
    public String id() {
        return this.id;
    }

    public Component title() {
        return this.title;
    }

    /** The addon's version as its loader reports it, or null when it cannot tell. */
    public String version() {
        if (this.version == null) {
            String found;
            try {
                found = RadiantePlatform.INSTANCE.modVersion(this.id);
            } catch (RuntimeException | LinkageError e) {
                found = null;
            }
            this.version = found == null ? "" : found;
        }
        return this.version.isEmpty() ? null : this.version;
    }

    @Override
    public RadianteAddons.Addon icon(Identifier texture) {
        this.icon = texture;
        return this;
    }

    /** The icon the addon named for itself; null to use the one in its mod's metadata. */
    public Identifier iconTexture() {
        return this.icon;
    }

    /** The settings in the order they were added. */
    public List<SettingImpl<?>> settings() {
        return List.copyOf(this.settings);
    }

    Properties stored() {
        return this.stored;
    }

    @Override
    public RadianteAddons.Setting<Boolean> toggle(String key, Component title, Component tooltip, boolean defaultValue) {
        return add(new SettingImpl<>(this, Kind.TOGGLE, key, title, tooltip, defaultValue, null, null, 0, 0, 1));
    }

    @Override
    public RadianteAddons.Setting<String> cycle(String key, Component title, Component tooltip, List<String> values,
        String defaultValue) {
        return cycle(key, title, tooltip, values, defaultValue, null);
    }

    @Override
    public RadianteAddons.Setting<String> cycle(String key, Component title, Component tooltip, List<String> values,
        String defaultValue, Function<String, Component> label) {
        List<String> clean = new ArrayList<>();
        if (values != null) {
            for (String value : values) {
                if (value != null && !clean.contains(value)) {
                    clean.add(value);
                }
            }
        }
        if (clean.isEmpty()) {
            AddonRegistry.LOGGER.warn("Addon '{}' setting '{}' has no values; it is not shown", this.id, key);
            clean.add(defaultValue == null ? "" : defaultValue);
            return new SettingImpl<>(this, Kind.CYCLE, key, title, tooltip, clean.get(0), clean, label, 0, 0, 1);
        }
        String fallback = defaultValue;
        if (!clean.contains(fallback)) {
            AddonRegistry.LOGGER.warn("Addon '{}' setting '{}': default '{}' is not one of its values; using '{}'",
                this.id, key, defaultValue, clean.get(0));
            fallback = clean.get(0);
        }
        return add(new SettingImpl<>(this, Kind.CYCLE, key, title, tooltip, fallback, List.copyOf(clean), label, 0, 0,
            1));
    }

    @Override
    public RadianteAddons.Setting<Integer> slider(String key, Component title, Component tooltip, int min, int max,
        int step, int defaultValue) {
        int low = Math.min(min, max);
        int high = Math.max(min, max);
        int by = Math.max(1, step);
        if (min > max || step < 1) {
            AddonRegistry.LOGGER.warn("Addon '{}' setting '{}': range {}..{} step {} fixed to {}..{} step {}", this.id,
                key, min, max, step, low, high, by);
        }
        SettingImpl<Integer> setting = new SettingImpl<>(this, Kind.SLIDER, key, title, tooltip, 0, null, null, low,
            high, by);
        setting.defaultValue = setting.sanitize(defaultValue);
        setting.value = setting.defaultValue;
        return add(setting);
    }

    /** Lists the setting with its stored value, or hands it back unlisted when its key cannot be used. */
    private <T> SettingImpl<T> add(SettingImpl<T> setting) {
        String key = setting.key();
        if (key == null || !AddonRegistry.NAME.matcher(key).matches()) {
            AddonRegistry.LOGGER.warn("Addon '{}' setting key '{}' is not valid (lowercase letters, digits, _ - . "
                + "only); it is not shown", this.id, key);
            return setting;
        }
        for (SettingImpl<?> other : this.settings) {
            if (other.key().equals(key)) {
                AddonRegistry.LOGGER.warn("Addon '{}' added setting '{}' twice; the second one is not shown", this.id,
                    key);
                return setting;
            }
        }
        String saved = this.stored.getProperty(key);
        if (saved != null) {
            setting.value = setting.parse(saved);
        }
        if (this.shown) {
            this.settings.add(setting);
        }
        return setting;
    }

    public enum Kind {
        TOGGLE, CYCLE, SLIDER
    }

    /** One setting: what the screen needs to draw it, and its value, which any thread may read. */
    public static final class SettingImpl<T> implements RadianteAddons.Setting<T> {

        private final AddonImpl addon;
        private final Kind kind;
        private final String key;
        private final Component title;
        private final Component tooltip;
        private final List<String> values;
        private final Function<String, Component> label;
        private final int min;
        private final int max;
        private final int step;
        private T defaultValue;
        private volatile T value;
        private final List<Consumer<T>> listeners = new CopyOnWriteArrayList<>();

        SettingImpl(AddonImpl addon, Kind kind, String key, Component title, Component tooltip, T defaultValue,
            List<String> values, Function<String, Component> label, int min, int max, int step) {
            this.addon = addon;
            this.kind = kind;
            this.key = key;
            this.title = title != null ? title : Component.literal(String.valueOf(key));
            this.tooltip = tooltip;
            this.defaultValue = defaultValue;
            this.value = defaultValue;
            this.values = values;
            this.label = label;
            this.min = min;
            this.max = max;
            this.step = step;
        }

        @Override
        public T get() {
            return this.value;
        }

        @Override
        public T defaultValue() {
            return this.defaultValue;
        }

        @Override
        public String key() {
            return this.key;
        }

        @Override
        public RadianteAddons.Setting<T> onChange(Consumer<T> listener) {
            if (listener != null) {
                this.listeners.add(listener);
            }
            return this;
        }

        public AddonImpl addon() {
            return this.addon;
        }

        public Kind kind() {
            return this.kind;
        }

        public Component title() {
            return this.title;
        }

        /** Null for none. */
        public Component tooltip() {
            return this.tooltip;
        }

        /** A cycle's values. */
        public List<String> values() {
            return this.values;
        }

        /** How a cycle's value is shown; the addon's label, or the value as it is written. */
        public Component label(String value) {
            if (this.label != null) {
                try {
                    Component shown = this.label.apply(value);
                    if (shown != null) {
                        return shown;
                    }
                } catch (RuntimeException e) {
                    AddonRegistry.LOGGER.warn("Addon '{}' setting '{}': its label for '{}' failed", this.addon.id(),
                        this.key, value, e);
                }
            }
            return Component.literal(value);
        }

        public int min() {
            return this.min;
        }

        public int max() {
            return this.max;
        }

        public int step() {
            return this.step;
        }

        /** The slider's steps from min to the highest value it reaches. */
        public int stepCount() {
            return (this.max - this.min) / this.step;
        }

        /** A slider value within its range, on a step. */
        @SuppressWarnings("unchecked")
        T sanitize(Object raw) {
            return switch (this.kind) {
                case TOGGLE -> raw instanceof Boolean ? (T) raw : this.defaultValue;
                case CYCLE -> raw instanceof String text && this.values.contains(text) ? (T) text : this.defaultValue;
                case SLIDER -> {
                    if (!(raw instanceof Integer number)) {
                        yield this.defaultValue;
                    }
                    int steps = Math.round((Math.max(this.min, Math.min(this.max, number)) - this.min)
                        / (float) this.step);
                    yield (T) Integer.valueOf(this.min + Math.min(stepCount(), steps) * this.step);
                }
            };
        }

        /** A value read from the file; the default when it is not one this setting takes. */
        T parse(String text) {
            String trimmed = text.trim();
            return switch (this.kind) {
                case TOGGLE -> trimmed.equalsIgnoreCase("true") ? sanitize(Boolean.TRUE)
                    : trimmed.equalsIgnoreCase("false") ? sanitize(Boolean.FALSE) : this.defaultValue;
                case CYCLE -> sanitize(trimmed);
                case SLIDER -> {
                    try {
                        yield sanitize(Integer.parseInt(trimmed));
                    } catch (NumberFormatException e) {
                        yield this.defaultValue;
                    }
                }
            };
        }

        String format(Object value) {
            return String.valueOf(value);
        }

        /** Takes a value the player applied; true when it changed. */
        boolean store(Object raw) {
            T next = sanitize(raw);
            if (Objects.equals(next, this.value)) {
                return false;
            }
            this.value = next;
            return true;
        }

        void notifyListeners() {
            T current = this.value;
            for (Consumer<T> listener : this.listeners) {
                try {
                    listener.accept(current);
                } catch (Exception | LinkageError e) {
                    AddonRegistry.LOGGER.warn("Addon '{}' setting '{}': its change listener failed", this.addon.id(),
                        this.key, e);
                }
            }
        }
    }
}
