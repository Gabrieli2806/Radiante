package com.g2806.radiante.client.compat.controlify;

import com.g2806.radiante.client.RadianteClient;
import com.g2806.radiante.client.gui.RadianteOptionsScreen;
import com.g2806.radiante.client.gui.SettingsControls;
import dev.isxander.controlify.InputMode;
import dev.isxander.controlify.api.ControlifyApi;
import dev.isxander.controlify.api.bind.InputBinding;
import dev.isxander.controlify.api.bind.InputBindingSupplier;
import dev.isxander.controlify.bindings.ControlifyBindings;
import dev.isxander.controlify.controller.ControllerEntity;
import dev.isxander.controlify.screenop.ScreenProcessor;
import dev.isxander.controlify.screenop.ScreenProcessorProvider;

/**
 * Controlify's handler for the settings screen; only loaded with Controlify installed (see {@link ControlifyCompat}).
 * The D-pad and left stick, A and B are left to Controlify's own handling, which the screen answers like any other.
 */
final class ControlifySettingsScreen extends ScreenProcessor<RadianteOptionsScreen> {

    /** Pixels the list scrolls per controller update with the right stick all the way over. */
    private static final double SCROLL_SPEED = 14.0;
    /** A slider's step with the right stick held left or right. */
    private static final int BIG_STEP = 10;

    /** Updates until a held right stick steps again: a pause after the first step, then a quicker repeat. */
    private int stepWait;
    /** Set once anything here failed against an unexpected Controlify: the rest is left to Controlify. */
    private static boolean broken;

    private ControlifySettingsScreen(RadianteOptionsScreen screen) {
        super(screen);
    }

    static void register() {
        ScreenProcessorProvider.registerProvider(RadianteOptionsScreen.class, ControlifySettingsScreen::new);
        SettingsControls.glyphs(hint -> {
            if (broken || !ControlifyApi.get().currentInputMode().isController()) {
                return null;
            }
            InputBindingSupplier binding = switch (hint) {
                case BACK -> ControlifyBindings.GUI_BACK;
                case APPLY -> ControlifyBindings.GUI_ABSTRACT_ACTION_1;
                case UNDO -> ControlifyBindings.GUI_ABSTRACT_ACTION_2;
                case PREVIOUS_SECTION -> ControlifyBindings.GUI_PREV_TAB;
                case NEXT_SECTION -> ControlifyBindings.GUI_NEXT_TAB;
            };
            return binding.inputGlyph();
        });
    }

    @Override
    protected void handleButtons(ControllerEntity controller) {
        if (!broken) {
            try {
                // Apply and Undo open a new screen: nothing more for this one once they have.
                if (ControlifyBindings.GUI_ABSTRACT_ACTION_1.on(controller).justPressed()) {
                    SettingsControls.apply(this.screen);
                    return;
                }
                if (ControlifyBindings.GUI_ABSTRACT_ACTION_2.on(controller).justPressed()) {
                    SettingsControls.undo(this.screen);
                    return;
                }
                rightStick(controller);
            } catch (RuntimeException | LinkageError mismatch) {
                fail(mismatch);
            }
        }
        super.handleButtons(controller);
    }

    /** Up and down scroll the list; left and right take big steps on a slider, repeating while held. */
    private void rightStick(ControllerEntity controller) {
        double vertical = ControlifyBindings.GUI_SECONDARY_NAVI_DOWN.on(controller).analogueNow()
            - ControlifyBindings.GUI_SECONDARY_NAVI_UP.on(controller).analogueNow();
        if (Math.abs(vertical) > 0.15) {
            SettingsControls.scroll(this.screen, vertical * SCROLL_SPEED);
        }
        InputBinding left = ControlifyBindings.GUI_SECONDARY_NAVI_LEFT.on(controller);
        InputBinding right = ControlifyBindings.GUI_SECONDARY_NAVI_RIGHT.on(controller);
        InputBinding held = right.digitalNow() ? right : left.digitalNow() ? left : null;
        if (held == null) {
            this.stepWait = 0;
            return;
        }
        if (!held.digitalPrev()) {
            this.stepWait = 8;
        } else if (--this.stepWait > 0) {
            return;
        } else {
            this.stepWait = 2;
        }
        SettingsControls.step(this.screen, held == right, BIG_STEP);
    }

    @Override
    protected void handleTabNavigation(ControllerEntity controller) {
        // The bumpers: Controlify only knows vanilla's tab bars, and the sections here are drawn by the screen.
        if (!broken) {
            try {
                if (ControlifyBindings.GUI_NEXT_TAB.on(controller).justPressed()) {
                    SettingsControls.section(this.screen, 1);
                    return;
                }
                if (ControlifyBindings.GUI_PREV_TAB.on(controller).justPressed()) {
                    SettingsControls.section(this.screen, -1);
                    return;
                }
            } catch (RuntimeException | LinkageError mismatch) {
                fail(mismatch);
            }
        }
        super.handleTabNavigation(controller);
    }

    @Override
    public void onInputModeChanged(InputMode mode) {
        super.onInputModeChanged(mode);
        // The buttons' controller pictures come and go with the controller's use.
        SettingsControls.refreshHints(this.screen);
    }

    private static void fail(Throwable mismatch) {
        broken = true;
        RadianteClient.LOGGER.warn("Controlify integration off for the settings screen", mismatch);
    }
}
