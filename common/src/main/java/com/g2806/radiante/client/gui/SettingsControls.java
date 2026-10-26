package com.g2806.radiante.client.gui;

import net.minecraft.client.gui.navigation.ScreenDirection;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.network.chat.Component;

/**
 * What a controller integration may do on the settings screen, beyond what every controller mod already gets through
 * Minecraft's own focus navigation and key presses (see {@link SettingsLayout.FocusTarget}): change section, apply,
 * undo, scroll, take bigger steps, and show its buttons' pictures beside the buttons they stand for. Calls on any
 * other screen do nothing.
 */
public final class SettingsControls {

    /** The buttons a controller has a button of its own for. */
    public enum Hint {
        BACK, APPLY, UNDO, PREVIOUS_SECTION, NEXT_SECTION
    }

    /** The picture of a controller's button for a hint, or null while no controller is in use. */
    @FunctionalInterface
    public interface Glyphs {
        Component glyph(Hint hint);
    }

    private static Glyphs glyphs = hint -> null;

    private SettingsControls() {
    }

    /** Set once by a controller integration. */
    public static void glyphs(Glyphs provider) {
        glyphs = provider;
    }

    static Component glyph(Hint hint) {
        try {
            return glyphs.glyph(hint);
        } catch (RuntimeException | LinkageError broken) {
            // A controller mod of another version than the one built against: no pictures rather than no screen.
            glyphs = h -> null;
            return null;
        }
    }

    static Component withGlyph(Hint hint, Component label) {
        Component glyph = glyph(hint);
        // Side by side, not one inside the other: the picture's own font would carry over to the label.
        return glyph == null ? label : Component.empty().append(glyph).append(" ").append(label);
    }

    private static SettingsLayout layout(Screen screen) {
        return screen instanceof RadianteOptionsScreen settings ? settings.controllerLayout() : null;
    }

    public static boolean isSettingsScreen(Screen screen) {
        return layout(screen) != null;
    }

    /** The next section, or with -1 the previous one. */
    public static void section(Screen screen, int direction) {
        SettingsLayout layout = layout(screen);
        if (layout != null) {
            layout.section(direction);
        }
    }

    /** Apply, as its button: what was changed takes effect and the screen stays open. */
    public static void apply(Screen screen) {
        SettingsLayout layout = layout(screen);
        if (layout != null) {
            layout.press(SettingsLayout.Press.APPLY);
        }
    }

    /** Undo, as its button: back to what is applied. */
    public static void undo(Screen screen) {
        SettingsLayout layout = layout(screen);
        if (layout != null) {
            layout.press(SettingsLayout.Press.UNDO);
        }
    }

    /** Scrolls the list by so many pixels; the focus moves along to a row still in view. */
    public static void scroll(Screen screen, double pixels) {
        SettingsLayout layout = layout(screen);
        if (layout != null) {
            layout.scrollKeepingFocus(pixels);
        }
    }

    /** Changes the focused value by so many steps to the left or right (a slider's big steps). */
    public static void step(Screen screen, boolean right, int amount) {
        SettingsLayout layout = layout(screen);
        if (layout != null) {
            layout.navigate(right ? ScreenDirection.RIGHT : ScreenDirection.LEFT, amount);
        }
    }

    /** The controller came into use or went out of it: the buttons' pictures follow. */
    public static void refreshHints(Screen screen) {
        SettingsLayout layout = layout(screen);
        if (layout != null) {
            layout.refreshHints();
        }
    }
}
