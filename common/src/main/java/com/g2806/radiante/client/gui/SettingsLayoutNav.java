package com.g2806.radiante.client.gui;

import com.g2806.radiante.client.gui.SettingsLayout.FlatButton;
import com.g2806.radiante.client.gui.SettingsLayout.Line;
import java.util.List;
import net.minecraft.client.OptionInstance;
import net.minecraft.client.gui.navigation.ScreenDirection;
import net.minecraft.client.input.KeyEvent;
import net.minecraft.network.chat.Component;

import static com.g2806.radiante.client.gui.SettingsLayout.*;

/**
 * Keyboard, controller and screen-reader focus navigation for {@link SettingsLayout}.
 */
final class SettingsLayoutNav {

    private SettingsLayoutNav() {
    }

    /**
     * Keyboard: Up and Down move through the options and the folded groups' headings, on past the last one to the
     * buttons along the bottom; Left and Right step a slider by one (ten with Shift) or a list of values to its
     * neighbour, fold a group shut or open, or move between the buttons; Enter or Space flips a switch, steps a list,
     * folds a group or presses a button; Tab moves to the next section (Shift: the previous). Without a row picked
     * yet, the keys work on the one under the mouse. True when used; Up on the first row is not, so the screen's own
     * navigation moves on to the search field.
     */
    static boolean keyPressed(SettingsLayout layout, KeyEvent event) {
        if (event.isCycleFocus()) {
            section(layout, event.hasShiftDown() ? -1 : 1);
            return true;
        }
        if (event.isUp()) {
            return navigate(layout, ScreenDirection.UP, 1);
        }
        if (event.isDown()) {
            return navigate(layout, ScreenDirection.DOWN, 1);
        }
        if (event.isLeft() || event.isRight()) {
            return navigate(layout, event.isLeft() ? ScreenDirection.LEFT : ScreenDirection.RIGHT,
                event.hasShiftDown() ? 10 : 1);
        }
        if (event.isSelection()) {
            return activateFocused(layout);
        }
        return false;
    }

    /** The next section (or with -1 the previous one), as Tab does. */
    static void section(SettingsLayout layout, int direction) {
        // Through the sections shown: Addons only is one when an addon is installed.
        int at = 0;
        for (int i = 0; i < layout.sections.size(); i++) {
            if (layout.sections.get(i).category() == selected) {
                at = i;
            }
        }
        selected = layout.sections.get(Math.floorMod(at + direction, layout.sections.size())).category();
        layout.scroll = 0.0;
        layout.onScroll.accept(layout.scroll);
        if (layout.search != null) {
            layout.search.setValue("");
        }
        layout.layoutLines();
        focusedButton = -1;
        // With the keys or a controller in use, the outline goes to the new section's first row.
        List<Line> rows = layout.navRows();
        if (keyboardFocus && !rows.isEmpty()) {
            layout.focusRow(rows.get(0));
        }
        layout.layoutButtons();
        clickSound();
    }

    /**
     * One step in a direction, as the arrow keys and a controller's D-pad or stick make it: Up and Down move the
     * focus, Left and Right change the focused value by {@code amount} steps (or move along the buttons). True when
     * something happened; false at the top of the list, where the focus may leave it.
     */
    @SuppressWarnings("unchecked")
    static boolean navigate(SettingsLayout layout, ScreenDirection direction, int amount) {
        List<Line> rows = layout.navRows();
        if (focusedButton >= 0 && keyboardFocus) {
            List<FlatButton> footer = layout.footer();
            switch (direction) {
                case UP -> {
                    if (rows.isEmpty()) {
                        return false;
                    }
                    int index = layout.focusedRow(rows);
                    layout.focusRow(rows.get(index < 0 ? rows.size() - 1 : index));
                }
                case LEFT, RIGHT -> {
                    int next = Math.max(0, Math.min(footer.size() - 1,
                        focusedButton + (direction == ScreenDirection.LEFT ? -1 : 1)));
                    focusedButton = next;
                }
                default -> {
                }
            }
            return true;
        }
        int index = layout.focusedRow(rows);
        if (direction == ScreenDirection.UP || direction == ScreenDirection.DOWN) {
            if (rows.isEmpty()) {
                if (direction == ScreenDirection.DOWN && !layout.footer().isEmpty()) {
                    focusedButton = 0;
                    keysInUse();
                    return true;
                }
                return false;
            }
            if (index < 0 || !keyboardFocus) {
                // The first step only shows where the keys are.
                layout.focusRow(rows.get(Math.max(0, index)));
                return true;
            }
            if (direction == ScreenDirection.UP) {
                if (index == 0) {
                    return false;
                }
                layout.focusRow(rows.get(index - 1));
                return true;
            }
            if (index == rows.size() - 1) {
                // On past the last row to the buttons along the bottom.
                if (!layout.footer().isEmpty()) {
                    focusedButton = 0;
                }
                return true;
            }
            layout.focusRow(rows.get(index + 1));
            return true;
        }
        if (index < 0) {
            return false;
        }
        Line line = rows.get(index);
        focusedKey = navKey(line);
        keysInUse();
        int sign = direction == ScreenDirection.LEFT ? -1 : 1;
        if (line.heading()) {
            // An addon's heading: Right opens it, Left folds it (not while a search shows it open anyway).
            if (layout.searchEmpty() && blockOpen(line.section(), line.block()) != (sign > 0)) {
                layout.toggleBlock(line);
            }
            return true;
        }
        if (line.option() == null) {
            // A group's heading: Right opens it, Left folds it.
            boolean open = openGroups.contains(line.group().key());
            if (open != (sign > 0)) {
                layout.toggleGroup(line.group());
            }
            return true;
        }
        OptionInstance<?> option = line.option();
        layout.described = option;
        if (layout.blocked(option)) {
            return true;
        }
        Object value = option.get();
        if (value instanceof com.g2806.radiante.client.option.EmissionColor) {
            BlockEmissionSettings.open(option);
        } else if (value instanceof Boolean on) {
            ((OptionInstance<Boolean>) option).set(!on);
            clickSound();
        } else if (option.values() instanceof OptionInstance.IntRange range) {
            int next = Math.max(range.minInclusive(), Math.min(range.maxInclusive(), (Integer) value + sign * amount));
            if (next != (Integer) value) {
                ((OptionInstance<Integer>) option).set(next);
            }
        } else if (option.values() instanceof OptionInstance.Enum<?> values) {
            List<Object> list = (List<Object>) values.valueListSupplier().getDefaultList();
            int at = list.indexOf(value);
            ((OptionInstance<Object>) option).set(list.get(Math.floorMod(at + sign, list.size())));
            clickSound();
        }
        layout.layoutButtons();
        return true;
    }

    /** Enter, Space or a controller's A: flips the focused switch, steps its list, folds its group or presses it. */
    @SuppressWarnings("unchecked")
    static boolean activateFocused(SettingsLayout layout) {
        FlatButton button = layout.focusedFooterButton();
        if (button != null) {
            clickSound();
            button.shownAction().run();
            return true;
        }
        List<Line> rows = layout.navRows();
        int index = layout.focusedRow(rows);
        if (index < 0) {
            return false;
        }
        Line line = rows.get(index);
        focusedKey = navKey(line);
        keysInUse();
        if (line.heading()) {
            layout.toggleBlock(line);
            return true;
        }
        if (line.option() == null) {
            layout.toggleGroup(line.group());
            return true;
        }
        OptionInstance<?> option = line.option();
        layout.described = option;
        if (layout.blocked(option) || option.values() instanceof OptionInstance.IntRange) {
            return true;
        }
        Object value = option.get();
        if (value instanceof com.g2806.radiante.client.option.EmissionColor) {
            BlockEmissionSettings.open(option);
        } else if (value instanceof Boolean on) {
            ((OptionInstance<Boolean>) option).set(!on);
        } else if (option.values() instanceof OptionInstance.Enum<?> values) {
            List<Object> list = (List<Object>) values.valueListSupplier().getDefaultList();
            ((OptionInstance<Object>) option).set(list.get(Math.floorMod(list.indexOf(value) + 1, list.size())));
        }
        clickSound();
        layout.layoutButtons();
        return true;
    }

    /**
     * Scrolls the list by so many pixels (a controller's right stick), taking the focus along to a row still in
     * view so it never sits off screen.
     */
    static void scrollKeepingFocus(SettingsLayout layout, double pixels) {
        double before = layout.scroll;
        layout.scroll = layout.clampScroll(layout.scroll + pixels);
        layout.onScroll.accept(layout.scroll);
        if (layout.scroll == before || !keyboardFocus || focusedButton >= 0) {
            return;
        }
        keysLead = true;
        List<Line> rows = layout.navRows();
        int index = layout.focusedRow(rows);
        if (index < 0) {
            return;
        }
        int view = layout.bottom - layout.top;
        Line line = rows.get(index);
        if (line.y() >= layout.scroll + HEADER && line.y() + ROW <= layout.scroll + view) {
            return;
        }
        // The first row wholly in view going down, the last one going up.
        Line pick = null;
        for (Line candidate : rows) {
            boolean visible = candidate.y() >= layout.scroll && candidate.y() + ROW <= layout.scroll + view;
            if (visible && (pick == null || pixels < 0)) {
                pick = candidate;
            }
        }
        if (pick != null) {
            focusedKey = navKey(pick);
            if (pick.option() != null) {
                layout.described = pick.option();
            }
            layout.layoutButtons();
        }
    }

    /** The screen's focus came to the list (a controller, or the arrow keys from the search field). */
    static void focusGained(SettingsLayout layout) {
        keysInUse();
        List<Line> rows = layout.navRows();
        if (focusedButton < 0 && layout.focusedRow(rows) < 0 && !rows.isEmpty()) {
            layout.focusRow(rows.get(0));
        }
    }

    /** The screen's focus left the list, for the search field. */
    static void focusLost() {
        keyboardFocus = false;
        keysLead = false;
    }

    /** What a screen reader says for the focused row: its name and value. */
    static Component focusedNarration(SettingsLayout layout) {
        FlatButton button = layout.focusedFooterButton();
        if (button != null) {
            return button.shownLabel();
        }
        List<Line> rows = layout.navRows();
        int index = layout.focusedRow(rows);
        if (index < 0) {
            return null;
        }
        Line line = rows.get(index);
        if (line.heading()) {
            return line.block().heading();
        }
        if (line.option() == null) {
            return line.group().name();
        }
        return caption(line.option()).copy().append(": ").append(valueText(line.option()));
    }
}
