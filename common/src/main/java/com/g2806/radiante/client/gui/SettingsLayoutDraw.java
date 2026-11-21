package com.g2806.radiante.client.gui;

import com.g2806.radiante.client.gui.SettingsLayout.FlatButton;
import com.g2806.radiante.client.gui.SettingsLayout.Line;
import com.g2806.radiante.client.gui.SettingsLayout.Section;
import com.g2806.radiante.compat.VersionCompat;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import net.minecraft.ChatFormatting;
import net.minecraft.client.Minecraft;
import net.minecraft.client.OptionInstance;
import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.client.gui.components.Tooltip;
import net.minecraft.network.chat.Component;
import net.minecraft.resources.Identifier;
import net.minecraft.util.FormattedCharSequence;

import static com.g2806.radiante.client.gui.SettingsLayout.*;

/**
 * Drawing for {@link SettingsLayout}: the sidebar, scrolling option list, rows, side description panel and flat
 * footer buttons.
 */
final class SettingsLayoutDraw {

    private SettingsLayoutDraw() {
    }

    static void extract(SettingsLayout layout, GuiGraphicsExtractor g, int mouseX, int mouseY) {
        // The mouse moving hands the description back to the row under it.
        if (mouseX != layout.lastMouseX || mouseY != layout.lastMouseY) {
            if (layout.lastMouseX != Integer.MIN_VALUE) {
                keysLead = false;
            }
            layout.lastMouseX = mouseX;
            layout.lastMouseY = mouseY;
        }
        // Sidebar: the mod, then the sections.
        g.fill(layout.sidebarX, layout.top, layout.sidebarX + layout.sidebarW, layout.bottom, PANEL);
        int headerY = layout.top + 6;
        Identifier icon = layout.texture("icon");
        int nameX = layout.sidebarX + PAD;
        if (icon != null) {
            VersionCompat.blit(g, icon, nameX, headerY, 0.0f, 0.0f, 24, 24, 24, 24);
            nameX += 30;
        }
        g.text(layout.font, Component.literal("Radiante").withStyle(ChatFormatting.BOLD), nameX, headerY + 3, TEXT);
        g.text(layout.font, Component.translatable("options.radiante.subtitle"), nameX, headerY + 14, TEXT_DIM);
        int entryY = layout.top + 38;
        RadianteOptionsScreen.Category inView = layout.sectionInView();
        for (Section section : layout.sections) {
            boolean selectedSection = section.category() == inView;
            boolean hover = inside(mouseX, mouseY, layout.sidebarX, entryY, layout.sidebarW, 18);
            if (selectedSection) {
                g.fill(layout.sidebarX, entryY, layout.sidebarX + layout.sidebarW, entryY + 18, PANEL_DARK);
                g.fill(layout.sidebarX + layout.sidebarW - 3, entryY, layout.sidebarX + layout.sidebarW, entryY + 18,
                    ACCENT);
            } else if (hover) {
                g.fill(layout.sidebarX, entryY, layout.sidebarX + layout.sidebarW, entryY + 18, ROW_HOVER);
            }
            int textX = layout.sidebarX + PAD;
            Identifier sectionIcon = section.icon() != null ? null
                : layout.texture(section.category().name().toLowerCase(Locale.ROOT));
            if (section.icon() != null) {
                drawIcon(g, section.icon(), textX, entryY + 1);
                textX += 20;
            } else if (sectionIcon != null) {
                VersionCompat.blit(g, sectionIcon, textX, entryY + 1, 0.0f, 0.0f, 16, 16, 16, 16);
                textX += 20;
            }
            // A pack's name can be longer than the sidebar is wide.
            Component name = section.name();
            int room = layout.sidebarX + layout.sidebarW - 6 - textX;
            if (layout.font.width(name) > room) {
                name = Component.literal(layout.font.plainSubstrByWidth(name.getString(),
                    Math.max(0, room - layout.font.width("…"))) + "…");
            }
            g.text(layout.font, name, textX, entryY + 5, selectedSection ? TEXT : ACCENT);
            entryY += 20;
        }
        // With a controller in use: the bumpers that move between the sections, under them.
        Component previous = SettingsControls.glyph(SettingsControls.Hint.PREVIOUS_SECTION);
        Component next = SettingsControls.glyph(SettingsControls.Hint.NEXT_SECTION);
        if (previous != null && next != null) {
            Component pair = Component.empty().append(previous).append(" ").append(next);
            Component hint = Component.empty().append(pair).append(" ")
                .append(Component.translatable("options.radiante.controller.sections"));
            g.text(layout.font, layout.font.width(hint) <= layout.sidebarW - PAD * 2 ? hint : pair,
                layout.sidebarX + PAD, entryY + 6, TEXT_DIM);
        }

        // The list.
        g.enableScissor(layout.listX, layout.top, layout.listX + layout.listW, layout.bottom);
        OptionInstance<?> hovered = null;
        int base = layout.top - (int) layout.scroll;
        for (int i = 0; i < layout.lines.size(); i++) {
            Line line = layout.lines.get(i);
            int y = base + line.y();
            if (line.sectionHeading()) {
                if (y + HEADER > layout.top && y < layout.bottom) {
                    g.fill(layout.listX, Math.max(y, layout.top), layout.listX + layout.listW,
                        Math.min(y + HEADER, layout.bottom), PANEL_DARK);
                }
                g.text(layout.font, Component.literal("◆ ").append(line.section().name()), layout.listX + PAD, y + 7,
                    ACCENT);
                continue;
            }
        }
        for (int i = 0; i < layout.lines.size(); i++) {
            Line line = layout.lines.get(i);
            if (line.sectionHeading()) {
                continue;
            }
            int y = base + line.y();
            if (y + ROW < layout.top || y > layout.bottom) {
                continue;
            }
            // The group's dark box, a slice per row (the first one reaching up to the header): one tall box for the
            // whole group went missing once its header scrolled out of view.
            boolean firstOfSection = i > 0 && layout.lines.get(i - 1).sectionHeading();
            int sliceTop = Math.max(firstOfSection ? y - 2 : y, layout.top);
            int sliceBottom = Math.min(y + ROW, layout.bottom);
            if (sliceBottom > sliceTop) {
                g.fill(layout.listX + 4, sliceTop, layout.listX + layout.listW - 4, sliceBottom, GROUP);
            }
            boolean hover = inside(mouseX, mouseY, layout.listX + 4, y, layout.listW - 8, ROW)
                && mouseY >= layout.top && mouseY < layout.bottom;
            if (line.heading()) {
                // A block's heading (an addon's name): an arrow for folded or open as on the advanced groups, the
                // addon's icon fitted into a square left of the name, the name brighter than a note, how many
                // options it holds on the right, and a rule under it.
                boolean open = blockOpen(line.section(), line.block()) || !layout.searchEmpty();
                if (hover) {
                    g.fill(layout.listX + 4, y, layout.listX + layout.listW - 4, y + ROW, ROW_HOVER);
                }
                if (keyboardFocus && focusedButton < 0 && navKey(line).equals(focusedKey)) {
                    g.outline(layout.listX + 4, y, layout.listW - 8, ROW, ACCENT);
                }
                int textX = layout.listX + PAD + 4;
                Component arrow = Component.literal(open ? "▾ " : "▸ ");
                g.text(layout.font, arrow, textX, y + 6, ACCENT);
                textX += layout.font.width(arrow);
                AddonIcons.Icon addonIcon = line.block().icon();
                if (addonIcon != null) {
                    drawIcon(g, addonIcon, textX, y + 1);
                    textX += ICON + 4;
                }
                g.text(layout.font, line.block().heading(), textX, y + 6, hover ? ACCENT : TEXT);
                Component count = Component.literal(String.valueOf(line.block().options().size()
                    + line.block().advanced().size()));
                g.text(layout.font, count, layout.listX + layout.listW - PAD - 4 - layout.font.width(count), y + 6,
                    TEXT_DIM);
                g.fill(layout.listX + PAD + 4, y + ROW - 2, layout.listX + layout.listW - PAD - 4, y + ROW - 1,
                    0x60FFFFFF);
                continue;
            }
            if (line.note() != null) {
                g.text(layout.font, line.note(), layout.listX + PAD + 4, y + 6, TEXT_DIM);
                continue;
            }
            if (line.option() == null) {
                // A group's heading: an arrow for folded or open, and how many options it holds.
                boolean open = openGroups.contains(line.group().key()) || !layout.searchEmpty();
                if (hover) {
                    g.fill(layout.listX + 4, y, layout.listX + layout.listW - 4, y + ROW, ROW_HOVER);
                }
                if (keyboardFocus && focusedButton < 0 && navKey(line).equals(focusedKey)) {
                    g.outline(layout.listX + 4, y, layout.listW - 8, ROW, ACCENT);
                }
                g.text(layout.font, Component.literal(open ? "▾ " : "▸ ").append(line.group().name()),
                    layout.listX + PAD + 4, y + 6, hover ? TEXT : ACCENT);
                Component count = Component.literal(String.valueOf(line.group().options().size()));
                g.text(layout.font, count, layout.listX + layout.listW - PAD - 4 - layout.font.width(count), y + 6,
                    TEXT_DIM);
                continue;
            }
            if (hover) {
                g.fill(layout.listX + 4, y, layout.listX + layout.listW - 4, y + ROW, ROW_HOVER);
                hovered = line.option();
            }
            drawRow(layout, g, line.option(), y, hover, line.advanced());
        }
        g.disableScissor();
        if (layout.contentHeight > layout.bottom - layout.top) {
            int view = layout.bottom - layout.top;
            int bar = Math.max(16, view * view / layout.contentHeight);
            int barY = layout.top + (int) ((view - bar) * layout.scroll / (layout.contentHeight - view));
            g.fill(layout.listX + layout.listW - 2, barY, layout.listX + layout.listW, barY + bar, ACCENT);
        }
        if (hovered != null && !keysLead) {
            layout.described = hovered;
        }

        drawDescription(layout, g, mouseX, mouseY, hovered);

        FlatButton keyed = layout.focusedFooterButton();
        for (FlatButton button : layout.buttons) {
            boolean hover = button.enabled() && (inside(mouseX, mouseY, button.x(), button.y(), button.w(), 16)
                || button == keyed);
            g.fill(button.x(), button.y(), button.x() + button.w(), button.y() + 16, hover ? PANEL_DARK : PANEL);
            if (button == keyed) {
                g.outline(button.x(), button.y(), button.w(), 16, ACCENT);
                if (keysLead && button.shownTooltip() != null) {
                    // Above the button, clear of it (vanilla's tooltip starts 12 above the point it is given).
                    List<FormattedCharSequence> tip = layout.font.split(button.shownTooltip(), 200);
                    g.setTooltipForNextFrame(tip, button.x(), Math.max(12, button.y() - tip.size() * 10 - 6));
                }
            }
            if (hover) {
                g.fill(button.x(), button.y() + 15, button.x() + button.w(), button.y() + 16, ACCENT);
            }
            if (button.shownLabel() == SettingsLayout.COMPARE_ICON) {
                drawCompareIcon(g, button.x() + button.w() / 2, button.y() + 8, hover ? ACCENT : TEXT);
            } else {
                g.centeredText(layout.font, button.shownLabel(), button.x() + button.w() / 2, button.y() + 4,
                    button.enabled() ? (hover ? ACCENT : TEXT) : 0xFF606060);
            }
            if (button.shownTooltip() != null && inside(mouseX, mouseY, button.x(), button.y(), button.w(), 16)) {
                g.setTooltipForNextFrame(layout.font.split(button.shownTooltip(), 200), mouseX, mouseY);
            }
        }
    }

    /** Before / After's icon: a frame, its left half dim and its right half lit, split down the middle. */
    private static void drawCompareIcon(GuiGraphicsExtractor g, int cx, int cy, int color) {
        g.fill(cx - 6, cy - 4, cx, cy + 4, 0xFF3A403E);
        g.fill(cx, cy - 4, cx + 6, cy + 4, color);
        g.outline(cx - 7, cy - 5, 14, 10, color);
        g.fill(cx - 1, cy - 6, cx + 1, cy + 6, 0xFFFFFFFF);
    }

    /** The mark of a row that compiles shaders: its colour, a cool blue against the amber of a restart. */
    private static final int COMPILE_ICON = 0xFF7FB8FF;

    /**
     * The mark of a row that compiles shaders when changed: an hourglass of whole pixels, eight tall, its sand run
     * half into the lower bulb. Top left at (x, y).
     */
    private static void drawCompileIcon(GuiGraphicsExtractor g, int x, int y, int color) {
        int sand = (color & 0x00FFFFFF) | 0x90000000;
        g.fill(x, y, x + 7, y + 1, color);
        g.fill(x + 1, y + 1, x + 6, y + 2, color);
        g.fill(x + 2, y + 2, x + 5, y + 3, sand);
        g.fill(x + 3, y + 3, x + 4, y + 5, color);
        g.fill(x + 2, y + 5, x + 5, y + 6, sand);
        g.fill(x + 1, y + 6, x + 6, y + 7, sand);
        g.fill(x, y + 7, x + 7, y + 8, color);
    }

    /** An icon fitted into a square of {@link SettingsLayout#ICON} at its own proportions. */
    private static void drawIcon(GuiGraphicsExtractor g, AddonIcons.Icon icon, int x, int y) {
        int iw = icon.width();
        int ih = icon.height();
        int w = iw >= ih ? ICON : Math.max(1, ICON * iw / ih);
        int h = ih >= iw ? ICON : Math.max(1, ICON * ih / iw);
        VersionCompat.blit(g, icon.texture(), x + (ICON - w) / 2, y + (ICON - h) / 2, 0.0f, 0.0f, w, h,
            iw, ih, iw, ih);
    }

    private static void drawRow(SettingsLayout layout, GuiGraphicsExtractor g, OptionInstance<?> option, int y,
        boolean hover, boolean advanced) {
        int right = layout.listX + layout.listW - PAD - 4;
        if (keyboardFocus && focusedButton < 0 && focusedKey != null && focusedKey.equals(focusKey(option))) {
            // The row the arrow keys work on.
            g.outline(layout.listX + 4, y, layout.listW - 8, ROW, ACCENT);
        }
        // Grey all over while another setting keeps it from doing anything; its description says which.
        boolean blocked = layout.blocked(option);
        // Advanced options sit a step in and a shade dimmer than the everyday ones above them.
        int captionX = layout.listX + PAD + (advanced ? 14 : 4);
        // A light block's row shows its texture before its name (BlockEmissionSettings).
        java.util.List<String> sprites = BlockEmissionSettings.ROW_SPRITES.get(option);
        if (sprites != null && !sprites.isEmpty() && BlockEmissionSettings.iconsShown()) {
            BlockEmissionSettings.drawIcon(g, BlockEmissionSettings.ROW_ITEMS.get(option), sprites, captionX, y + 2, 16);
            captionX += 20;
        }
        g.text(layout.font, caption(option), captionX, y + 6, blocked ? TEXT_BLOCKED : advanced ? TEXT_DIM : TEXT);
        int tagX = captionX + layout.font.width(caption(option)) + 6;
        if (ShaderCompileRows.compiles(focusKey(option))) {
            // Changing it compiles the shaders that use it: the description says so.
            drawCompileIcon(g, tagX, y + 6, blocked ? TEXT_BLOCKED : COMPILE_ICON);
            tagX += 10;
        }
        if (layout.restart.needsRestart(option)) {
            // Marked on the row itself, amber once the choice differs from what the running game uses.
            boolean pending = layout.restart.pending(option);
            Component tag = Component.translatable(pending ? "options.radiante.restart.tag_pending"
                : "options.radiante.restart.tag");
            g.text(layout.font, tag, tagX + 2, y + 6, pending ? 0xFFF0B040 : 0xFF808080);
        }
        Object value = option.get();
        if (value instanceof com.g2806.radiante.client.option.EmissionColor colour
            && !colour.equals(com.g2806.radiante.client.option.EmissionColor.WHITE)) {
            // A swatch only once the light is tinted: untouched, the light keeps the pack's own colour, and a white
            // swatch read as "this block casts white light".
            int boxX = right - layout.font.width(valueText(option)) - 18;
            g.fill(boxX, y + 4, boxX + 12, y + 16, colour.preview());
            g.outline(boxX, y + 4, 12, 12, TEXT_DIM);
        }
        if (isCheckbox(option) && value instanceof Boolean on) {
            int box = 10;
            int bx = right - box;
            int by = y + 5;
            g.outline(bx, by, box, box, blocked ? TEXT_BLOCKED : hover ? ACCENT : 0xFFD0D0D0);
            if (on) {
                g.fill(bx + 2, by + 2, bx + box - 2, by + box - 2, blocked ? TEXT_BLOCKED : ACCENT);
            }
            return;
        }
        Component text = valueText(option);
        int textWidth = layout.font.width(text);
        if (option.values() instanceof OptionInstance.IntRange range) {
            // A thin track under the value, filled to where it stands.
            int trackW = Math.min(90, layout.listW / 3);
            int tx = right - trackW;
            int ty = y + ROW - 4;
            int span = Math.max(1, range.maxInclusive() - range.minInclusive());
            int filled = (int) ((long) trackW * ((Integer) value - range.minInclusive()) / span);
            g.fill(tx, ty, tx + trackW, ty + 2, blocked ? 0x30FFFFFF : 0x60FFFFFF);
            g.fill(tx, ty, tx + filled, ty + 2, blocked ? TEXT_BLOCKED : ACCENT);
            g.fill(tx + filled - 1, ty - 2, tx + filled + 1, ty + 4, blocked ? TEXT_BLOCKED : TEXT);
        }
        g.text(layout.font, text, right - textWidth, y + 6, blocked ? TEXT_BLOCKED : hover ? ACCENT : TEXT);
    }

    private static void drawDescription(SettingsLayout layout, GuiGraphicsExtractor g, int mouseX, int mouseY,
        OptionInstance<?> hovered) {
        OptionInstance<?> option = layout.panelW > 0 ? layout.described : hovered;
        int tipX = mouseX;
        int tipY = mouseY;
        int keyedRowY = -1;
        if (layout.panelW <= 0 && keysLead && focusedButton < 0) {
            // No panel beside the list (a narrow window or a large GUI scale): the description of the row the keys
            // or a controller are on goes under that row, since there may be no mouse pointer to follow.
            option = null;
            List<Line> rows = layout.navRows();
            int index = layout.focusedRow(rows);
            if (index >= 0 && rows.get(index).option() != null) {
                Line line = rows.get(index);
                int y = layout.top - (int) layout.scroll + line.y();
                if (y >= layout.top && y + ROW <= layout.bottom) {
                    option = line.option();
                    tipX = layout.listX + layout.listW / 3;
                    tipY = y + ROW + 14;
                    keyedRowY = y;
                }
            }
        }
        if (option == null) {
            return;
        }
        List<FormattedCharSequence> lines = new ArrayList<>();
        Tooltip tooltip = tooltipOf(option);
        int wrap = layout.panelW > 0 ? layout.panelW - PAD * 2 : 200;
        if (tooltip != null) {
            lines.addAll(tooltip.toCharSequence(Minecraft.getInstance()));
        }
        String optionKey = keyOf(caption(option));
        OptionImpact declared = layout.availability.impact(option);
        OptionImpact impact = declared != null ? declared : OptionImpact.of(optionKey);
        if (layout.restart.needsRestart(option)) {
            lines.addAll(layout.font.split(Component.translatable("options.radiante.restart.hint")
                .withColor(NOTICE), wrap));
        }
        if (ShaderCompileRows.compiles(optionKey)) {
            lines.addAll(layout.font.split(Component.translatable("options.radiante.compiles.hint")
                .withColor(COMPILE_ICON), wrap));
        }
        Component blockedBy = layout.availability.blocked(optionKey);
        if (blockedBy != null) {
            lines.addAll(layout.font.split(blockedBy.copy().withColor(NOTICE), wrap));
        }
        Component notice = layout.availability.notice(optionKey);
        if (notice != null) {
            lines.addAll(layout.font.split(notice.copy().withColor(0xFF9FC8FF), wrap));
        }
        List<Component> affects = layout.availability.affects(optionKey);
        if (!affects.isEmpty()) {
            net.minecraft.network.chat.MutableComponent names = Component.empty();
            for (int i = 0; i < affects.size(); i++) {
                names.append(i == 0 ? "" : ", ").append(affects.get(i));
            }
            lines.addAll(layout.font.split(Component.translatable("options.radiante.affects", names)
                .withColor(ACCENT), wrap));
        }
        if (layout.panelW <= 0) {
            List<FormattedCharSequence> all = new ArrayList<>(lines);
            if (impact != null) {
                all.addAll(layout.font.split(impactLine(impact), wrap));
            }
            if (!all.isEmpty()) {
                // Under the focused row, or above it when it does not fit below (vanilla's tooltip starts 12 above
                // the point it is given and is pushed up off the bottom edge, over the row).
                int tall = all.size() * 10 + 8;
                if (keyedRowY >= 0 && keyedRowY + ROW + 2 + tall > layout.height) {
                    tipY = Math.max(12, keyedRowY - tall + 10);
                }
                g.setTooltipForNextFrame(all, tipX, tipY);
            }
            return;
        }
        g.fill(layout.panelX, layout.top, layout.panelX + layout.panelW, layout.bottom, PANEL);
        int x = layout.panelX + PAD;
        int y = layout.top + PAD;
        for (FormattedCharSequence line : layout.font.split(caption(option).copy().withStyle(ChatFormatting.BOLD), wrap)) {
            g.text(layout.font, line, x, y, ACCENT);
            y += 11;
        }
        y += 4;
        for (FormattedCharSequence line : lines) {
            g.text(layout.font, line, x, y, TEXT_DIM);
            y += 10;
        }
        if (impact != null) {
            y += 8;
            for (FormattedCharSequence line : layout.font.split(impactLine(impact), wrap)) {
                g.text(layout.font, line, x, y, TEXT);
                y += 10;
            }
        }
    }

    private static Component impactLine(OptionImpact impact) {
        return Component.translatable("options.radiante.impact",
            Component.translatable(impact.key).withColor(impact.color));
    }
}
