package com.g2806.radiante.client.compat.controlify;

import com.g2806.radiante.client.RadianteClient;

/**
 * Controlify on the settings screen. Without this the screen already works with a controller: Controlify moves
 * through a screen by asking it for the next focus in a direction and presses Enter for A, both of which the screen
 * answers (SettingsLayout.FocusTarget). This adds what that leaves out: the bumpers change section, X applies, Y
 * undoes, the right stick scrolls (up and down) or takes big steps on a slider (left and right), and the buttons show
 * the controller's pictures for them.
 *
 * <p>Nothing in this class touches Controlify, so it is safe to call whether or not the mod is installed. The
 * classes that do are only loaded once the mod has been found, and a mismatch with the installed version leaves the
 * integration off rather than taking the game down.
 */
public final class ControlifyCompat {

    private static final String API_CLASS = "dev.isxander.controlify.api.ControlifyApi";

    private ControlifyCompat() {
    }

    public static void init() {
        try {
            Class.forName(API_CLASS, false, ControlifyCompat.class.getClassLoader());
        } catch (ClassNotFoundException | LinkageError notInstalled) {
            return;
        }
        try {
            ControlifySettingsScreen.register();
            RadianteClient.LOGGER.info("Controlify found: the settings screen takes its section, apply and undo buttons");
        } catch (RuntimeException | LinkageError mismatch) {
            RadianteClient.LOGGER.warn("Controlify integration off: this Controlify does not match the one Radiante "
                + "was built against", mismatch);
        }
    }
}
